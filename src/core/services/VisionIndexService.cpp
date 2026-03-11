#include "VisionIndexService.h"

#include <QDir>

#include "core/db/MetaStore.h"
#include "core/query/QueryCore.h"

VisionIndexService::VisionIndexService()
    : m_store(new MetaStore())
{
}

VisionIndexService::~VisionIndexService()
{
    shutdown();
    delete m_store;
    m_store = nullptr;
}

bool VisionIndexService::initialize(const QString& dbPath,
                                    const RefreshPolicy& policy,
                                    QString* errorText)
{
    shutdown();

    if (!m_store) {
        if (errorText) {
            *errorText = QStringLiteral("vision_store_missing");
        }
        return false;
    }

    const QString normalized = QDir::cleanPath(dbPath);
    QString migrationLog;
    if (!m_store->initialize(normalized, errorText, &migrationLog)) {
        return false;
    }

    m_queryCore = new QueryCore(*m_store);
    m_policy = policy;
    m_dbPath = normalized;

    QString refreshError;
    if (!m_refreshService.initialize(normalized, m_policy, &refreshError)) {
        if (errorText) {
            *errorText = refreshError;
        }
        delete m_queryCore;
        m_queryCore = nullptr;
        m_store->shutdown();
        return false;
    }

    return true;
}

void VisionIndexService::shutdown()
{
    m_refreshService.shutdown();

    delete m_queryCore;
    m_queryCore = nullptr;

    if (m_store) {
        m_store->shutdown();
    }

    m_dbPath.clear();
}

bool VisionIndexService::isReady() const
{
    return m_store && m_store->isReady() && m_queryCore;
}

QString VisionIndexService::dbPath() const
{
    return m_dbPath;
}

QueryResult VisionIndexService::queryChildren(const QString& path, const QueryOptions& options)
{
    QueryResult result;
    if (!isReady()) {
        result.ok = false;
        result.errorText = QStringLiteral("vision_index_not_ready");
        return result;
    }

    result = m_queryCore->queryChildren(path, options);
    if (result.ok) {
        scheduleVisibleRefresh(path, QStringLiteral("query_children"));
    }
    return result;
}

QueryResult VisionIndexService::queryFlat(const QString& path, const QueryOptions& options)
{
    QueryResult result;
    if (!isReady()) {
        result.ok = false;
        result.errorText = QStringLiteral("vision_index_not_ready");
        return result;
    }

    result = m_queryCore->queryFlat(path, options);
    if (result.ok) {
        scheduleVisibleRefresh(path, QStringLiteral("query_flat"));
    }
    return result;
}

QueryResult VisionIndexService::queryHierarchy(const QString& path, const QueryOptions& options)
{
    QueryResult result;
    if (!isReady()) {
        result.ok = false;
        result.errorText = QStringLiteral("vision_index_not_ready");
        return result;
    }

    result = m_queryCore->querySubtree(path, options);
    if (result.ok) {
        scheduleVisibleRefresh(path, QStringLiteral("query_hierarchy"));
    }
    return result;
}

RefreshRequestResult VisionIndexService::requestRefresh(const QString& path,
                                                        const QString& mode,
                                                        bool force,
                                                        const QString& reason)
{
    RefreshContext context;
    context.force = force;
    context.mode = mode;
    context.reason = reason;
    return m_refreshService.requestRefresh(path, context);
}

RefreshRequestResult VisionIndexService::maybeRefresh(const QString& path, const RefreshContext& context)
{
    return m_refreshService.requestRefresh(path, context);
}

QVector<RefreshEvent> VisionIndexService::takeRefreshEvents()
{
    return m_refreshService.takeEvents();
}

void VisionIndexService::notifyRefreshCompleted(const RefreshEvent& event)
{
    Q_UNUSED(event);
}

bool VisionIndexService::waitForRefreshIdle(int timeoutMs)
{
    return m_refreshService.waitForIdle(timeoutMs);
}

void VisionIndexService::scheduleVisibleRefresh(const QString& path, const QString& reason)
{
    RefreshContext context;
    context.force = false;
    context.mode = m_policy.defaultMode;
    context.reason = reason;
    m_refreshService.requestRefresh(path, context);
}
