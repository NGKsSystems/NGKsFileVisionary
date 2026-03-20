#pragma once

#include <QString>
#include <QVector>

#include "ViewModeController.h"
#include "core/services/RefreshTypes.h"
#include "core/query/QueryTypes.h"
#include "core/querylang/QueryPlan.h"

class VisionIndexService;
namespace ArchiveNav {
class ArchiveQueryAdapter;
}

class DirectoryModel
{
public:
    struct Request
    {
        QString rootPath;
        ViewModeController::UiViewMode mode = ViewModeController::UiViewMode::Standard;
        bool authoritativeRebuild = false;
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
    QueryResult queryGraph(const QString& rootPath,
                           QueryGraphMode mode,
                           const QString& graphTarget,
                           const QueryOptions& options);
    RefreshRequestResult requestRefresh(const QString& path,
                                        bool force,
                                        const QString& mode = QStringLiteral("visible_refresh"),
                                        const QString& reason = QStringLiteral("ui_explicit_refresh"));
    QVector<RefreshEvent> takeRefreshEvents();
    bool waitForRefreshIdle(int timeoutMs);
    QString dbPath() const;

private:
    VisionIndexService* m_visionService;
    ArchiveNav::ArchiveQueryAdapter* m_archiveQueryAdapter;
    QString m_dbPath;
};
