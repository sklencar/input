#include "merginapi.h"
#include "qgsziputils.h"

#include <QtNetwork>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDate>
#include <QByteArray>
#include <QSet>

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
    QUrl url(mApiRoot + "/v1/project?tags=input_use");
    request.setUrl(url);
    request.setRawHeader("Authorization", QByteArray("Basic " + mToken));

    QNetworkReply *reply = mManager.get(request);
    connect(reply, &QNetworkReply::finished, this, &MerginApi::listProjectsReplyFinished);
}

void MerginApi::downloadProject(QString projectName)
{
    // Redirect to get info and fetch data if project exists
    QDir projectDir(mDataDir + projectName);
    if (projectDir.exists()) {
        projectInfo(projectName);
        return;
    }

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

void MerginApi::projectInfo(QString projectName)
{

    if (mToken.isEmpty()) {
        emit networkErrorOccurred( "Auth token is invalid", "Mergin API error: projectInfo" );
    }

    QNetworkRequest request;
    QUrl url(mApiRoot + "/v1/project/" + projectName);
    qDebug() << "Requested " << url.toString();

    request.setUrl(url);
    request.setRawHeader("Authorization", QByteArray("Basic " + mToken));

    QNetworkReply *reply = mManager.get(request);
    connect(reply, &QNetworkReply::finished, this, &MerginApi::projectInfoReplyFinished);

}

void MerginApi::fetchProject(QString projectName, QByteArray json)
{
    if (mToken.isEmpty()) {
        emit networkErrorOccurred( "Auth token is invalid", "Mergin API error: fetchProject" );
        return;
    }

    QNetworkRequest request;
    QUrl url(mApiRoot + "/v1/project/fetch/" + projectName);
    request.setUrl(url);
    request.setRawHeader("Authorization", QByteArray("Basic " + mToken));
    request.setRawHeader("Content-Type", "application/json");
    request.setRawHeader("Accept", "application/json");
    mPendingRequests.insert(url, projectName);

    QNetworkReply *reply = mManager.post(request, json);
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

void MerginApi::downloadProjectReplyFinished()
{

    QNetworkReply* r = qobject_cast<QNetworkReply*>(sender());
    if (!r) {
        emit networkErrorOccurred( "No reply received.", "Mergin API error: downloadProject" );
        qDebug() << r->errorString();
        mPendingRequests.remove(r->url());
        return;
    }

    if (r->error() == QNetworkReply::NoError)
    {
        QString projectName("temp");
        if (mPendingRequests.contains(r->url())) {
            projectName = mPendingRequests.value(r->url());
        }
        QString projectDir = mDataDir + projectName;
        QFile file(mDataDir + projectName);

        handleDataStream(r, projectDir);
        emit downloadProjectFinished(projectDir, projectName);
        emit notify("Download successful");
    }
    else {
        qDebug() << r->errorString();
        emit networkErrorOccurred( r->errorString(), "Mergin API error: downloadProject" );
    }
    mPendingRequests.remove(r->url());
    r->deleteLater();
}

void MerginApi::projectInfoReplyFinished()
{
    QNetworkReply* r = qobject_cast<QNetworkReply*>(sender());
    if (!r) {
        emit networkErrorOccurred( "No reply received.", "Mergin API error: projectInfo" );
        return;
    }

    QList<MerginFile> files;
    QUrl url = r->url();
    QStringList res = url.path().split("/");
    QString projectName;
    projectName = res.last();
    qDebug() << "project updated: " << projectName;
    QString projectPath = QString(mDataDir + projectName + "/");

    if (r->error() == QNetworkReply::NoError)
    {
      QByteArray data = r->readAll();
      QJsonDocument doc = QJsonDocument::fromJson(data);
      if (doc.isObject()) {
          QJsonObject docObj = doc.object();
          auto it = docObj.constFind("files");
          QJsonValue v = *it;
          Q_ASSERT( v.isArray() );
          QJsonArray vArray = v.toArray();

          QSet<QString> localFiles = listFiles(projectPath);
          for ( auto it = vArray.constBegin(); it != vArray.constEnd(); ++it)
          {
            QJsonObject projectInfoMap = it->toObject();
            QString serverChecksum = projectInfoMap.value("checksum").toString();
            QString path = projectInfoMap.value("path").toString();
            QString localChecksum = QString::fromStdString(getChecksum(projectPath + path).toStdString());

            localFiles.remove(path);
            MerginFile file;
            file.checksum = serverChecksum;
            file.path = path;
            files.append(file);
            qDebug() << path << " : " << serverChecksum << "|" << localChecksum;
          }

          // abundant files
          if (!localFiles.isEmpty()) {
              for (QString filename: localFiles) {
                  QFile file (projectPath + filename);
                  file.remove();
              }
          }
      }
      QJsonDocument jsonDoc;
      QJsonArray fileArray;

      for(MerginFile file: files) {
        QJsonObject fileObject;
        fileObject.insert("path", file.path);
        fileObject.insert("checksum", file.checksum);
        fileArray.append(fileObject);
      }

      jsonDoc.setArray(fileArray);
      fetchProject(projectName, jsonDoc.toJson(QJsonDocument::Compact));
    }
    else {
        QString message = QStringLiteral("Network API error: %1(): %2").arg("listProjects", r->errorString());
        qDebug("%s", message.toStdString().c_str());
        emit networkErrorOccurred( r->errorString(), "Mergin API error: projectInfo" );
    }

    r->deleteLater();
    emit projectInfoFinished();
}

void MerginApi::fetchProjectReplyFinished()
{
    QNetworkReply* r = qobject_cast<QNetworkReply*>(sender());
    if (!r) {
        emit networkErrorOccurred( "No reply received.", "Mergin API error: fetchProject" );
        return;
    }

    if (r->error() == QNetworkReply::NoError)
    {
      QByteArray data = r->readAll();
    }
    else {
        QString message = QStringLiteral("Network API error: %1(): %2").arg("fetchProject", r->errorString());
        qDebug("%s", message.toStdString().c_str());
        emit networkErrorOccurred( r->errorString(), "Mergin API error: fetchProject" );
    }

    r->deleteLater();
    //emit fetchProjectFinished(projectDir, projectName);
}

ProjectList MerginApi::parseProjectsData(const QByteArray &data)
{
    ProjectList result;

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isArray()) {
        QJsonArray vArray = doc.array();

        for ( auto it = vArray.constBegin(); it != vArray.constEnd(); ++it)
        {
            QJsonObject projectMap = it->toObject();
            MerginProject p;
            QJsonValue meta = projectMap.value("meta");
            p.name = projectMap.value("name").toString();
            if (meta.isObject()) {
                QJsonObject metaObject = meta.toObject();
                int size = metaObject.value("size").toInt(0);
                int filesCount = metaObject.value("files_count").toInt(0);
                p.status = getProjectStatus(mDataDir + p.name, size, filesCount);
            }

            QString created = projectMap.value("created").toString();
            p.info = QDateTime::fromString(created, Qt::ISODateWithMs).toString();
            result << std::make_shared<MerginProject>(p);
        }
    }
    return result;
}

bool MerginApi::cacheProjectsData(const QByteArray &data)
{
    QFile file(mDataDir + "projectsCache.txt");
    createPathIfNotExists(mDataDir + "projectsCache.txt");
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    QTextStream stream(&file);
    stream << data;
    file.close();

    return true;
}

void MerginApi::handleDataStream(QNetworkReply* r, QString projectDir)
{
    // Read content type from reply's header
    QByteArray contentType;
    QString contentTypeString;
    QList<QByteArray> headerList = r->rawHeaderList();
    // QByteArray.compare was introduced in Qt 5.12; String conversion needed for Android build
    QString headString;
    foreach(QByteArray head, headerList) {
        headString = QString::fromStdString(head.toStdString());
        if (headString.compare("Content-Type") == 0) {
            contentType = r->rawHeader(head);
            contentTypeString = QString::fromStdString(contentType.toStdString());
        }
    }

    // Read boundary hash from content types
    QString boundary;
    QRegularExpression re;
    re.setPattern("[^;]+; boundary=(?<boundary>.+)");
    QRegularExpressionMatch match = re.match(contentTypeString);
    if (match.hasMatch()) {
        boundary = match.captured("boundary");
    }

    // plus some safety trashold
    int boundarySize = boundary.length() + 8;
    QRegularExpression boundaryPattern("(\r\n)?--" + boundary + "\r\n");
    QRegularExpression headerPattern("Content-Disposition: form-data; name=\"(?P<name>[^'\"]+)\"(; filename=\"(?P<filename>[^\"]+)\")?\r\n(Content-Type: (?P<content_type>.+))?\r\n");
    QRegularExpression endPattern("(\r\n)?--" + boundary +  "--\r\n");

    QByteArray data;
    QString dataString;
    QString activeFilePath;

    while (true) {
        QByteArray chunk = r->read(CHUNK_SIZE);
        if (chunk.isEmpty()) {
            // End of stream - write rest of data to active file
            if (!activeFilePath.isEmpty()) {
                QRegularExpressionMatch endMatch = endPattern.match(data);
                int tillIndex = data.indexOf(endMatch.captured(0));
                saveFile(data.left(tillIndex), activeFilePath, true);
                activeFilePath = "";
            }
            return;
        }

        data = data.append(chunk);
        dataString = QString::fromStdString(data.toStdString());
        QRegularExpressionMatch boundaryMatch = boundaryPattern.match(dataString);

        while (boundaryMatch.hasMatch()) {
            if (!activeFilePath.isEmpty()) {
                int tillIndex = data.indexOf(boundaryMatch.captured(0));
                saveFile(data.left(tillIndex), activeFilePath, true);
                activeFilePath = "";
            }

            // delete previously written data with next boundary part
            int tillIndex = data.indexOf(boundaryMatch.captured(0)) + boundaryMatch.captured(0).length();
            data = data.remove(0, tillIndex); // String != QByteArray
            dataString = QString::fromStdString(data.toStdString());

            QRegularExpressionMatch headerMatch = headerPattern.match(dataString);
            if (!headerMatch.hasMatch()) {
                qDebug() << "Received corrupted header";
                data = data + r->read(CHUNK_SIZE);
                dataString = QString::fromStdString(data.toStdString());
            }
            headerMatch = headerPattern.match(dataString);
            data = data.remove(0, headerMatch.captured(0).length());
            dataString = QString::fromStdString(data.toStdString());
            QString filename = headerMatch.captured("filename");

            activeFilePath = projectDir + "/" + filename;
            createPathIfNotExists(activeFilePath);
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
    createPathIfNotExists(filePath);
    QFile file(filePath);
    if (append) {
        if (!file.open(QIODevice::Append)) {
            return false;
        }
    } else {
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }
    }

    file.write(data);
    file.close();

    return true;
}

void MerginApi::createPathIfNotExists(QString filePath)
{
    QDir dir;
    if (!dir.exists(mDataDir))
        dir.mkpath(mDataDir);

    QFileInfo newFile( filePath );
    if ( !newFile.absoluteDir().exists() )
    {
      if ( !QDir( dir ).mkpath( newFile.absolutePath() ) )
        qDebug() << "Creating folder failed";
    }
}

// TODO fix when a project is open - could have different size and files number
ProjectStatus MerginApi::getProjectStatus(QString projectPath, int size, int filesCount)
{
    QFileInfo info(projectPath);
    if (!info.exists()) {
        return ProjectStatus::NoVersion;
    }

    int filesOnDisk = listFiles(projectPath).size();
    int totalSize = sizeOfProject(projectPath);
    if (size != totalSize || filesOnDisk != filesCount) {
        return ProjectStatus::OutOfDate;
    }
    return ProjectStatus::UpToDate;
}

QByteArray MerginApi::getChecksum(QString filePath) {
    QFile f(filePath);
    if (f.open(QFile::ReadOnly)) {
        QCryptographicHash hash(QCryptographicHash::Sha1);
        QByteArray chunk = f.read(CHUNK_SIZE);
        while (!chunk.isEmpty()) {
           hash.addData(chunk);
           chunk = f.read(CHUNK_SIZE);
           hash.addData(chunk);
        }
        return hash.result().toHex();
    }
    f.close();
    return QByteArray();
}

QSet<QString> MerginApi::listFiles(QString path)
{
    QSet<QString> files;
    QDirIterator it(path, QStringList() << QStringLiteral("*"), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        it.next();
        files << it.filePath().replace(path, "");
    }
    return files;
}

int MerginApi::sizeOfProject(QString path) {
    QDirIterator it(path, QStringList() << QStringLiteral("*"), QDir::Files, QDirIterator::Subdirectories);
    int totalSize = 0;
    while (it.hasNext())
    {
        it.next();
        totalSize += it.fileInfo().size();
    }
    return totalSize;
}
