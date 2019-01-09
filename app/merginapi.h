#ifndef MERGINAPI_H
#define MERGINAPI_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QEventLoop>
#include <memory>
#include <QFile>

struct MerginProject {
    QString name;
    QStringList tags;
    QString info;
    bool pending = false;
};

struct MerginFile {
    QString path;
    QString checksum;
    QString location;
    QString mtime;
    int size;
};


typedef QList<std::shared_ptr<MerginProject>> ProjectList;

class MerginApi: public QObject {
    Q_OBJECT
public:
    explicit MerginApi(const QString& root, const QString& dataDir, QByteArray token, QObject* parent = nullptr );
    ~MerginApi() = default;
    Q_INVOKABLE void listProjects();
    Q_INVOKABLE void downloadProject(QString projectName);
    void projectInfo(QString projectName);
    void fetchProject(QString projectName, QByteArray json);
    ProjectList projects();

signals:
    void listProjectsFinished();
    void downloadProjectFinished(QString projectDir, QString projectName);
    // TODO merge with downlaodProject??
    void fetchProjectFinished(QString projectDir, QString projectName);
    void networkErrorOccurred(QString message, QString additionalInfo);
    void notify(QString message);
    // TODO needed?
    void projectInfoFinished();

private slots:
    void listProjectsReplyFinished();
    void downloadProjectReplyFinished();
    void projectInfoReplyFinished();
    void fetchProjectReplyFinished();

private:
    ProjectList parseProjectsData( const QByteArray &data );
    void makeToast(const QString &errorMessage, const QString &additionalInfo);
    bool cacheProjectsData(const QByteArray &data);
    void handleDataStream(QNetworkReply* r, QString projectDir);
    bool saveFile(const QByteArray &data, QString fileName, bool append);
    void createPathIfNotExists(QString filePath);
    QByteArray getChecksum(QString filePath);
    QSet<QString> listFiles(QString projectPath);

    QNetworkAccessManager mManager;
    QString mApiRoot;
    ProjectList mMerginProjects;
    QString mDataDir;
    QByteArray mToken;
    QHash<QUrl, QString>mPendingRequests;

    const int CHUNK_SIZE = 65536;
};

#endif // MERGINAPI_H
