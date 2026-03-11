#pragma once

#include <QString>
#include <QVector>

#include "RefreshPolicy.h"
#include "RefreshService.h"
#include "core/query/QueryTypes.h"

class MetaStore;
class QueryCore;

class VisionIndexService
{
public:
    VisionIndexService();
    ~VisionIndexService();

    bool initialize(const QString& dbPath,
                    const RefreshPolicy& policy = RefreshPolicy(),
                    QString* errorText = nullptr);
    void shutdown();

    bool isReady() const;
    QString dbPath() const;

    QueryResult queryChildren(const QString& path, const QueryOptions& options);
    QueryResult queryFlat(const QString& path, const QueryOptions& options);
    QueryResult queryHierarchy(const QString& path, const QueryOptions& options);

    RefreshRequestResult requestRefresh(const QString& path,
                                        const QString& mode,
                                        bool force,
                                        const QString& reason = QString());
    RefreshRequestResult maybeRefresh(const QString& path, const RefreshContext& context);

    QVector<RefreshEvent> takeRefreshEvents();
    void notifyRefreshCompleted(const RefreshEvent& event);
    bool waitForRefreshIdle(int timeoutMs);

private:
    void scheduleVisibleRefresh(const QString& path, const QString& reason);

private:
    MetaStore* m_store = nullptr;
    QueryCore* m_queryCore = nullptr;
    RefreshService m_refreshService;
    RefreshPolicy m_policy;
    QString m_dbPath;
};