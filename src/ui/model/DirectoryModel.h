#pragma once

#include <QString>
#include <QVector>

#include "ViewModeController.h"
#include "core/services/RefreshTypes.h"
#include "core/query/QueryTypes.h"

class VisionIndexService;
namespace ArchiveNav {
class ArchiveProvider;
}

class DirectoryModel
{
public:
    struct Request
    {
        QString rootPath;
        ViewModeController::UiViewMode mode = ViewModeController::UiViewMode::Standard;
        bool includeHidden = false;
        bool includeSystem = false;
        bool foldersFirst = true;
        QString extensionFilter;
        QString substringFilter;
        QuerySortField sortField = QuerySortField::Name;
        bool ascending = true;
        int maxDepth = -1;
        int pageSize = 500;
        int pageOffset = 0;
        bool filesOnly = false;
        bool directoriesOnly = false;
    };

    DirectoryModel();
    ~DirectoryModel();

    bool initialize(const QString& dbPath, QString* errorText = nullptr);
    bool isReady() const;

    QueryResult query(const Request& request);
    RefreshRequestResult requestRefresh(const QString& path,
                                        bool force,
                                        const QString& mode = QStringLiteral("visible_refresh"),
                                        const QString& reason = QStringLiteral("ui_explicit_refresh"));
    QVector<RefreshEvent> takeRefreshEvents();
    bool waitForRefreshIdle(int timeoutMs);
    QString dbPath() const;

private:
    VisionIndexService* m_visionService;
    ArchiveNav::ArchiveProvider* m_archiveProvider;
    QString m_dbPath;
};
