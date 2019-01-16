#include "merginapi.h"

#include <QtNetwork>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDate>
#include <QByteArray>
#include <QSet>

MerginApi::MerginApi(const QString &root, const QString& dataDir, QByteArray token, QObject *parent)
  : QObject (parent)
  , mApiRoot(root)
  , mDataDir(dataDir + "/downloads/")
  , mCacheFile(".projectsCache.txt")
  , mToken(token)
{
    QObject::connect(this, &MerginApi::merginProjectsChanged,this, &MerginApi::cacheProjects);
}

void MerginApi::listProjects()
{
    if (mToken.isEmpty()) {
        emit networkErrorOccurred( "Auth token is invalid", "Mergin API error: listProjects" );
        return;
    }

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
    if (mToken.isEmpty()) {
        emit networkErrorOccurred( "Auth token is invalid", "Mergin API error: downloadProject" );
    }

    QNetworkRequest request;
    QUrl url(mApiRoot + "/v1/project/download/" + projectName);

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

void MerginApi::updateProject(QString projectName)
{

    if (mToken.isEmpty()) {
        emit networkErrorOccurred( "Auth token is invalid", "Mergin API error: projectInfo" );
    }

    QNetworkRequest request;
    QUrl url(mApiRoot + "/v1/project/" + projectName);

    request.setUrl(url);
    request.setRawHeader("Authorization", QByteArray("Basic " + mToken));

    QNetworkReply *reply = mManager.get(request);
    connect(reply, &QNetworkReply::finished, this, &MerginApi::updateInfoReplyFinished);

}

void MerginApi::downloadProjectFiles(QString projectName, QByteArray json)
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

ProjectList MerginApi::updateMerginProjectList(ProjectList serverProjects)
{
    QHash<QString, QDateTime> projectUpdates;
    for (std::shared_ptr<MerginProject> project: mMerginProjects) {
        projectUpdates.insert(project->name, project->updated);
    }

    for (std::shared_ptr<MerginProject> project: serverProjects) {
        if (projectUpdates.contains(project->name)) {
            QDateTime localUpdate = projectUpdates.value(project->name);
            project->updated = localUpdate;
            project->status = getProjectStatus(project->updated, project->serverUpdated);
        }
    }
    return serverProjects;
}

void MerginApi::setUpdateToProject(QString projectName)
{
    for (std::shared_ptr<MerginProject> project: mMerginProjects) {
        if (projectName == project->name) {
            project->updated = project->serverUpdated;
            emit merginProjectsChanged();
            return;
        }
    }
}

void MerginApi::deleteObsoleteFiles(QString projectPath)
{
    if (!mObsoleteFiles.value(projectPath).isEmpty()) {
        for (QString filename: mObsoleteFiles.value(projectPath)) {
            QFile file (projectPath + filename);
            file.remove();
        }
       mObsoleteFiles.remove(projectPath);
    }
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

        if (mMerginProjects.isEmpty()) {
            QFile file(mDataDir + mCacheFile);
            if (file.open(QIODevice::ReadOnly)) {
                QByteArray cachedData = file.readAll();
                file.close();

                mMerginProjects = parseProjectsData(cachedData);
            }
        }


      QByteArray data = r->readAll();
      ProjectList serverProjects = parseProjectsData(data, true);
      mMerginProjects = updateMerginProjectList(serverProjects);
      emit merginProjectsChanged();
    }
    else {
        QString message = QStringLiteral("Network API error: %1(): %2").arg("listProjects", r->errorString());
        qDebug("%s", message.toStdString().c_str());
        emit networkErrorOccurred( r->errorString(), "Mergin API error: listProjects" );
    }

    r->deleteLater();
    emit listProjectsFinished(mMerginProjects);
}

void MerginApi::downloadProjectReplyFinished()
{

    QNetworkReply* r = qobject_cast<QNetworkReply*>(sender());
    Q_ASSERT(r);

    if (r->error() == QNetworkReply::NoError)
    {
        QString projectName = mPendingRequests.value(r->url());
        QString projectDir = mDataDir + projectName;

        deleteObsoleteFiles(projectDir + "/");
        handleDataStream(r, projectDir);
        setUpdateToProject(projectName);

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

void MerginApi::updateInfoReplyFinished()
{
    QNetworkReply* r = qobject_cast<QNetworkReply*>(sender());
    Q_ASSERT(r);

    QList<MerginFile> files;
    QUrl url = r->url();
    QStringList res = url.path().split("/");
    QString projectName;
    projectName = res.last();
    QString projectPath = QString(mDataDir + projectName + "/");

    if (r->error() == QNetworkReply::NoError)
    {
      QByteArray data = r->readAll();
      QJsonDocument doc = QJsonDocument::fromJson(data);
      if (doc.isObject()) {
          QJsonObject docObj = doc.object();
          QString updated = docObj.value("updated").toString();
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
            QByteArray localChecksumBytes = getChecksum(projectPath + path);
            QString localChecksum = QString::fromLatin1(localChecksumBytes.data(), localChecksumBytes.size());
            if (serverChecksum != localChecksum) {
                MerginFile file;
                file.checksum = serverChecksum;
                file.path = path;
                files.append(file);
            } else {
                localFiles.remove(path);
            }
          }

          // will be removed after successful download
          mObsoleteFiles.insert(projectPath, localFiles);
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
      downloadProjectFiles(projectName, jsonDoc.toJson(QJsonDocument::Compact));
    }
    else {
        QString message = QStringLiteral("Network API error: %1(): %2").arg("listProjects", r->errorString());
        qDebug("%s", message.toStdString().c_str());
        emit networkErrorOccurred( r->errorString(), "Mergin API error: projectInfo" );
    }

    r->deleteLater();
}

ProjectList MerginApi::parseProjectsData(const QByteArray &data, bool dataFromServer)
{
    ProjectList result;

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isArray()) {
        QJsonArray vArray = doc.array();

        for ( auto it = vArray.constBegin(); it != vArray.constEnd(); ++it)
        {
            QJsonObject projectMap = it->toObject();
            MerginProject p;
            p.name = projectMap.value("name").toString();
            QJsonValue tags = projectMap.value("tags");
            if (tags.isArray()) {
                for (QJsonValueRef tag: tags.toArray()) {
                    p.tags.append(tag.toString());
                }
                tags.toArray().toVariantList();
            }
            p.created = QDateTime::fromString(projectMap.value("created").toString(), Qt::ISODateWithMs);
            QDateTime updated = QDateTime::fromString(projectMap.value("updated").toString(), Qt::ISODateWithMs);

            if (dataFromServer) {
                if (!updated.isValid()) {
                    updated = p.created;
                }
                p.serverUpdated = updated;
            } else {
                p.updated = updated;
            }
            result << std::make_shared<MerginProject>(p);
        }
    }
    return result;
}

bool MerginApi::cacheProjectsData(const QByteArray &data)
{
    QFile file(mDataDir + mCacheFile);
    createPathIfNotExists(mDataDir + mCacheFile);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    file.write(data);
    file.close();

    return true;
}

void MerginApi::cacheProjects()
{
    QJsonDocument doc;
    QJsonArray array;
    for (std::shared_ptr<MerginProject> p: mMerginProjects) {
        QJsonObject projectMap;
        projectMap.insert("created", p->created.toString(Qt::ISODateWithMs));
        projectMap.insert("updated", p->updated.toString(Qt::ISODateWithMs));
        projectMap.insert("name", p->name);
        QJsonArray tags;
        projectMap.insert("tags", tags.fromStringList(p->tags));
        array.append(projectMap);
    }
    doc.setArray(array);
    cacheProjectsData(doc.toJson());
}

void MerginApi::handleDataStream(QNetworkReply* r, QString projectDir)
{
    // Read content type from reply's header
    QByteArray contentType;
    QString contentTypeString;
    QList<QByteArray> headerList = r->rawHeaderList();
    QString headString;
    foreach(QByteArray head, headerList) {
        headString = QString::fromLatin1(head.data(), head.size());
        if (headString == QStringLiteral("Content-Type")) {
            contentType = r->rawHeader(head);
            contentTypeString = QString::fromLatin1(contentType.data(), contentType.size());
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

    // Depends on boundary size itself + special chars which number may vary
    int boundarySize = boundary.length() + 8;
    QRegularExpression boundaryPattern("(\r\n)?--" + boundary + "\r\n");
    QRegularExpression headerPattern("Content-Disposition: form-data; name=\"(?P<name>[^'\"]+)\"(; filename=\"(?P<filename>[^\"]+)\")?\r\n(Content-Type: (?P<content_type>.+))?\r\n");
    QRegularExpression endPattern("(\r\n)?--" + boundary +  "--\r\n");

    QByteArray data;
    QString dataString;
    QString activeFilePath;
    QFile activeFile;

    while (true) {
        QByteArray chunk = r->read(CHUNK_SIZE);
        if (chunk.isEmpty()) {
            // End of stream - write rest of data to active file
            if (!activeFile.fileName().isEmpty()) {
                QRegularExpressionMatch endMatch = endPattern.match(data);
                int tillIndex = data.indexOf(endMatch.captured(0));
                saveFile(data.left(tillIndex), activeFile, true);
            }
            return;
        }

        data = data.append(chunk);
        dataString = QString::fromLatin1(data.data(), data.size());
        QRegularExpressionMatch boundaryMatch = boundaryPattern.match(dataString);

        while (boundaryMatch.hasMatch()) {
            if (!activeFile.fileName().isEmpty()) {
                int tillIndex = data.indexOf(boundaryMatch.captured(0));
                saveFile(data.left(tillIndex), activeFile, true);
            }

            // delete previously written data with next boundary part
            int tillIndex = data.indexOf(boundaryMatch.captured(0)) + boundaryMatch.captured(0).length();
            data = data.remove(0, tillIndex);
            dataString = QString::fromLatin1(data.data(), data.size());

            QRegularExpressionMatch headerMatch = headerPattern.match(dataString);
            if (!headerMatch.hasMatch()) {
                qDebug() << "Received corrupted header";
                data = data + r->read(CHUNK_SIZE);
                dataString = QString::fromLatin1(data.data(), data.size());
            }
            headerMatch = headerPattern.match(dataString);
            data = data.remove(0, headerMatch.captured(0).length());
            dataString = QString::fromLatin1(data.data(), data.size());
            QString filename = headerMatch.captured("filename");

            activeFilePath = projectDir + "/" + filename;
            activeFile.setFileName(activeFilePath);
            if (activeFile.exists()) {
                // Remove file if want to override
                activeFile.remove();
            } else {
                createPathIfNotExists(activeFilePath);
            }

            boundaryMatch = boundaryPattern.match(dataString);
        }

        // Write rest of chunk to file
        if (!activeFile.fileName().isEmpty()) {
            saveFile(data.left(data.size() - boundarySize), activeFile, false);
        }
        data = data.remove(0, data.size() - boundarySize);
    }
}

bool MerginApi::saveFile(const QByteArray &data, QFile &file, bool closeFile)
{
    if (!file.isOpen()) {
        if (!file.open(QIODevice::Append)) {
            return false;
        }
    }

    file.write(data);
    if (closeFile)
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

ProjectStatus MerginApi::getProjectStatus(QDateTime localUpdated, QDateTime updated)
{
    if (!localUpdated.isValid()) {
        return ProjectStatus::NoVersion;
    }

    if (localUpdated < updated) {
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
