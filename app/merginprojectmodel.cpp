#include "merginprojectmodel.h"

#include <QAbstractListModel>
#include <QString>

MerginProjectModel::MerginProjectModel(QObject* parent)
    : QAbstractListModel( parent )
{
}

QVariant MerginProjectModel::data( const QModelIndex& index, int role ) const
{
    int row = index.row();
    if (row < 0 || row >= mMerginProjects.count())
        return QVariant();

    const MerginProject* project = mMerginProjects.at(row).get();

    switch ( role )
    {
    case Name: return QVariant(project->name);
    case ProjectInfo: {
        if (!project->updated.isValid()) {
            return QVariant(project->serverUpdated).toString();
        } else {
            return QVariant(project->updated).toString();
        }
    }
    case Status: {
        if (project->status == ProjectStatus::OutOfDate) return QVariant("outOfDate");
        if (project->status == ProjectStatus::UpToDate) return QVariant("upToDate");
        if (project->status == ProjectStatus::NoVersion) return QVariant("noVersion");
        return QVariant("noVersion");
    }
    case Pending: return QVariant(project->pending);
    }

    return QVariant();
}

void MerginProjectModel::setPending(int row, bool pending)
{
    if (row < 0 || row > mMerginProjects.length() - 1) return;
    QModelIndex ix = index(row);
    mMerginProjects.at(row)->pending = pending;
    emit dataChanged(ix, ix);
}

QHash<int, QByteArray> MerginProjectModel::roleNames() const
{
    QHash<int, QByteArray> roleNames = QAbstractListModel::roleNames();
    roleNames[Name] = "name";
    roleNames[ProjectInfo] = "projectInfo";
    roleNames[Status] = "status";
    roleNames[Pending] = "pendingProject";
    return roleNames;
}

ProjectList MerginProjectModel::projects()
{
    return mMerginProjects;
}

int MerginProjectModel::rowCount(const QModelIndex &parent) const {
    Q_UNUSED(parent);
    return mMerginProjects.count();
}

void MerginProjectModel::resetProjects(const ProjectList &merginProjects)
{
    mMerginProjects.clear();
    beginResetModel();
    mMerginProjects = merginProjects;
    endResetModel();
}

void MerginProjectModel::downloadProjectFinished(QString projectFolder, QString projectName)
{
    Q_UNUSED(projectFolder);
    int row = 0;
    for (std::shared_ptr<MerginProject> project: mMerginProjects) {
        if (project->name == projectName) {
            project->status = ProjectStatus::UpToDate;
            setPending(row, false); // emits dataChanged
            return;
        }
        row++;
    }
}
