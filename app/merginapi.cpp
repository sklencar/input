#include "merginapi.h"
#include "qgsziputils.h"

#include <QtNetwork>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDate>

#ifdef ANDROID
#include <QtAndroid>
#include <QAndroidJniObject>
#endif

MerginApi::MerginApi(const QString &root, const QString& dataDir, QByteArray token, QObject *parent)
  : QObject (parent)
  , mApiRoot(root)
  , mDataDir(dataDir + "/downloads/")
  , mToken(token)
{
}

void MerginApi::listProjects()
{
    if (mToken.isEmpty()) {
        emit networkErrorOccurred( "Auth token is invalid", "Mergin API error: listProjects" );
        return;
    }

    mMerginProjects.clear();
    QNetworkRequest request;
    // projects filtered by tag "input_use"
    QUrl url(mApiRoot + "/v1/project"); //?tags=input_use"
    request.setUrl(url);
    request.setRawHeader("Authorization", QByteArray("Basic " + mToken));

    QNetworkReply *reply = mManager.get(request);
    connect(reply, &QNetworkReply::finished, this, &MerginApi::listProjectsReplyFinished);
}

void MerginApi::downloadProject(QString projectName)
{
    if (mToken.isEmpty()) {
        emit networkErrorOccurred( "Auth token is invalid", "Mergin API error: downloadProject" );
    }

    QNetworkRequest request;
    QUrl url(mApiRoot + "/v1/project/download/" + projectName);
    qDebug() << "Requested " << url.toString();

    if (mPendingRequests.contains(url)) {
        QString errorMsg = QStringLiteral("Download request for %1 is already pending.").arg(projectName);
        qDebug() << errorMsg;
        emit networkErrorOccurred( errorMsg, "Mergin API error: downloadProject" );
        return;
    }

    request.setUrl(url);
    request.setRawHeader("Authorization", QByteArray("Basic " + mToken));

    QNetworkReply *reply = mManager.get(request);
    mPendingRequests.insert(url, projectName);
    connect(reply, &QNetworkReply::finished, this, &MerginApi::downloadProjectReplyFinished);
}

ProjectList MerginApi::projects()
{
    return mMerginProjects;
}

void MerginApi::listProjectsReplyFinished()
{

    QNetworkReply* r = qobject_cast<QNetworkReply*>(sender());
    Q_ASSERT(r);

    if (r->error() == QNetworkReply::NoError)
    {
      QByteArray data = r->readAll();
      cacheProjectsData(data);
      mMerginProjects = parseProjectsData(data);
    }
    else {
        QString message = QStringLiteral("Network API error: %1(): %2").arg("listProjects", r->errorString());
        qDebug("%s", message.toStdString().c_str());
        emit networkErrorOccurred( r->errorString(), "Mergin API error: listProjects" );
    }

    r->deleteLater();
    emit listProjectsFinished();
}

QString MerginApi::createProjectFile(const QByteArray &data, QString projectName)
{
    QDir dir;
    if (!dir.exists(mDataDir))
        dir.mkpath(mDataDir);
    QFile file(mDataDir + projectName);
    if (file.exists()) {
        qDebug("Zip file already exists!");
        // TODO overwrite projects / handle data-sync
    }

    bool isOpen = file.open(QIODevice::WriteOnly);
    QString path;
    if (isOpen) {
        file.write(data);
        file.close();
        path = QFileInfo(file).absoluteFilePath();
    } else {
        notify(QString("Project %1 cannot be open").arg(projectName));
    }

    return path;
}

void MerginApi::downloadProjectReplyFinished()
{

    QNetworkReply* r = qobject_cast<QNetworkReply*>(sender());
    if (!r) {
        emit networkErrorOccurred( "No reply received.", "Mergin API error: downloadProject" );
        return;
    }

    if (r->error() == QNetworkReply::NoError)
    {
        handleDataStream(r);
        emit notify("Download successful");
    }
    else {
        emit networkErrorOccurred( r->errorString(), "Mergin API error: downloadProject" );
    }
    mPendingRequests.remove(r->url());
    r->deleteLater();
}

ProjectList MerginApi::parseProjectsData(const QByteArray &data)
{
    ProjectList result;

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isArray()) {
        QJsonArray vArray = doc.array();

        for ( auto it = vArray.constBegin(); it != vArray.constEnd(); ++it)
        {
            QJsonObject designMap = it->toObject();
            MerginProject p;
            p.name = designMap.value("name").toString();
            QString created = designMap.value("created").toString();
            p.info = QDateTime::fromString(created, Qt::ISODateWithMs).toString();
            result << std::make_shared<MerginProject>(p);
        }
    }
    return result;
}

QString MerginApi::saveFileName(const QUrl &url)
{
    QString path = url.path();
    QString basename = QFileInfo(path).fileName();

    if (basename.isEmpty())
        basename = "download";

    if (QFile::exists(basename)) {
        // already exists, don't overwrite
        int i = 0;
        basename += '.';
        while (QFile::exists(basename + QString::number(i)))
            ++i;

        basename += QString::number(i);
    }

    return basename;
}

void MerginApi::unzipProject(QString path, QString dir)
{
    QDir d;
    if (!d.exists(dir))
        d.mkpath(dir);

    QStringList files;
    QgsZipUtils::unzip(path, dir, files);
}

// TODO refactor with saveFile
bool MerginApi::cacheProjectsData(const QByteArray &data)
{
    QDir dir;
    if (!dir.exists(mDataDir))
        dir.mkpath(mDataDir);

    QFile file(mDataDir + "projectsCache.txt");
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    QDataStream stream(&file);
    stream << data;
    file.close();

    return true;
}

void MerginApi::handleDataStream(QNetworkReply* r)
{

    QString projectName("temp");
    if (mPendingRequests.contains(r->url())) {
        projectName = mPendingRequests.value(r->url());
    }
    QDir dir;
    if (!dir.exists(mDataDir))
        dir.mkpath(mDataDir);
    QFile file(mDataDir + projectName);
    if (file.exists()) {
        qDebug("Zip file already exists!");
        // TODO overwrite projects / handle data-sync
    }
    QString projectDir = mDataDir + projectName;

    QByteArray contentType;
    QList<QByteArray> headerList = r->rawHeaderList();
    foreach(QByteArray head, headerList) {
        if (head.compare("Content-Type") == 0) {
            contentType = r->rawHeader(head);
            qDebug() << head << ":" << r->rawHeader(head);
        }
    }

    QString content = QString::fromStdString(contentType.toStdString());
    int CHUNK_SIZE = 65536;
    QString boundary;
    QRegularExpression re;
    re.setPattern("[^;]+; boundary=(?<boundary>.+)");

    QRegularExpressionMatch match = re.match(content);
    if (match.hasMatch()) {
        boundary = match.captured("boundary");
    }

    int boundarySize = boundary.length() + 8; // plus some trashold
    QRegularExpression boundaryPattern("(\r\n)?--" + boundary + "\r\n");
    QRegularExpression headerPattern("Content-Disposition: form-data; name=\"(?P<name>[^'\"]+)\"(; filename=\"(?P<filename>[^\"]+)\")?\r\n(Content-Type: (?P<content_type>.+))?\r\n");
    QRegularExpression endPattern("(\r\n)?--" + boundary +  "--\r\n");

    QByteArray fileData; // obj
    QByteArray data;
    QString dataString;
    QString activeFilePath;

    // read and write data
    while (true) {
        QByteArray chunk = r->read(CHUNK_SIZE);
        if (chunk.isEmpty()) {
            // END OF STREAM
            if (!activeFilePath.isEmpty()) {
                // write rest of data to active file
                QRegularExpressionMatch endMatch = endPattern.match(data);
                int tillIndex = data.indexOf(endMatch.captured(0));
                bool finished = saveFile(data.left(tillIndex), activeFilePath, true);
                activeFilePath = "";
            }
            return;
        }

        data = data.append(chunk);
        dataString = QString::fromStdString(data.toStdString());
        QRegularExpressionMatch boundaryMatch = boundaryPattern.match(dataString);

        while (boundaryMatch.hasMatch()) {
            // file data have been splited.
            if (!activeFilePath.isEmpty()) {
                int tillIndex = data.indexOf(boundaryMatch.captured(0));
                bool finished = saveFile(data.left(tillIndex), activeFilePath, true); // -1 eleted
                activeFilePath = ""; // writing to one file is finished
            }

            // delete previously written data with next boundary part
            int tillIndex = data.indexOf(boundaryMatch.captured(0)) + boundaryMatch.captured(0).length();
            data = data.remove(0, tillIndex); // String != QByteArray
            dataString = QString::fromStdString(data.toStdString());

            QRegularExpressionMatch headerMatch = headerPattern.match(dataString);
            if (!headerMatch.hasMatch()) {
                qDebug() << "-- CORRUPTED HEADER --";
                data = data + r->read(CHUNK_SIZE);
                dataString = QString::fromStdString(data.toStdString());
            }
            headerMatch = headerPattern.match(dataString);
            data = data.remove(0, headerMatch.captured(0).length());
            dataString = QString::fromStdString(data.toStdString());
            QString name = headerMatch.captured("name");
            QString filename = headerMatch.captured("filename"); // TODO same s name ????
            QString fileContentType = (headerMatch.captured("content_type"));

            // just CREATE file
            QDir dir;
            if (!dir.exists(mDataDir))
                dir.mkpath(mDataDir);

             QFileInfo newFile( projectDir + "/" + filename );
            // Create path for a new file if it does not exist.
            if ( !newFile.absoluteDir().exists() )
            {
              if ( !QDir( dir ).mkpath( newFile.absolutePath() ) )
                qDebug() << "Creating folder failed";
            }
            activeFilePath = projectDir + "/" + filename;
            boundaryMatch = boundaryPattern.match(dataString);
        }


        // Write rest of chunk to file
        if (!activeFilePath.isEmpty()) {
            saveFile(data.left(data.size() - boundarySize), activeFilePath, true);
        }
        data = data.remove(0, data.size() - boundarySize);
    }

}

bool MerginApi::saveFile(const QByteArray &data, QString filePath, bool append)
{

    QDir dir;
    if (!dir.exists(mDataDir))
        dir.mkpath(mDataDir);

     QFileInfo newFile( filePath );
    // Create path for a new file if it does not exist.
    if ( !newFile.absoluteDir().exists() )
    {
      if ( !QDir( dir ).mkpath( newFile.absolutePath() ) )
        qDebug() << "Creating folder failed. tralala!!!!!";
    }

    QFile file(filePath);
    if (append) {
        if (!file.open(QIODevice::Append)) {
            return false; // TODO
        }
    } else {
        if (!file.open(QIODevice::WriteOnly)) {
            return false; // TODO
        }
    }

    file.write(data);
    file.close();

    qDebug() << "File has size " << file.size() << " " << filePath;

    return true;
}
