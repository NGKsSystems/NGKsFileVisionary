#include "VisionIndexService.h"

#include <chrono>
#include <thread>

#include <QDir>
#include <QFileInfo>

#include "core/db/MetaStore.h"
#include "core/query/QueryCore.h"

namespace {
QString canonicalPath(const QString& path)
{
    return QDir::fromNativeSeparators(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
}

bool isSameOrParentPath(const QString& parent, const QString& candidate)
{
    if (parent.isEmpty() || candidate.isEmpty()) {
        return false;
    }

    const QString p = canonicalPath(parent).toLower();
    const QString c = canonicalPath(candidate).toLower();
    if (p == c) {
        return true;
    }

    const QString prefix = p.endsWith('/') ? p : p + '/';
    return c.startsWith(prefix);
}

bool pathsOverlap(const QString& lhs, const QString& rhs)
{
    return isSameOrParentPath(lhs, rhs) || isSameOrParentPath(rhs, lhs);
}
}

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

    QString gateError;
    if (!allowPublishForPath(path, &gateError)) {
        result.ok = false;
        result.errorText = gateError;
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

    QString gateError;
    if (!allowPublishForPath(path, &gateError)) {
        result.ok = false;
        result.errorText = gateError;
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

    QString gateError;
    if (!allowPublishForPath(path, &gateError)) {
        result.ok = false;
        result.errorText = gateError;
        return result;
    }

    result = m_queryCore->querySubtree(path, options);
    if (result.ok) {
        scheduleVisibleRefresh(path, QStringLiteral("query_hierarchy"));
    }
    return result;
}

QueryResult VisionIndexService::queryGraph(const QString& rootPath,
                                           QueryGraphMode mode,
                                           const QString& graphTarget,
                                           const QueryOptions& options)
{
    QueryResult result;
    if (!isReady()) {
        result.ok = false;
        result.errorText = QStringLiteral("vision_index_not_ready");
        return result;
    }

    return m_queryCore->queryGraph(QDir::fromNativeSeparators(QDir::cleanPath(rootPath)),
                                   mode,
                                   graphTarget,
                                   options);
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

bool VisionIndexService::waitForPublishReady(const QString& path,
                                             int timeoutMs,
                                             bool* sawActiveSession,
                                             ScanSessionRecord* terminalSession,
                                             QString* errorText) const
{
    if (sawActiveSession) {
        *sawActiveSession = false;
    }
    if (terminalSession) {
        *terminalSession = ScanSessionRecord();
    }

    const QString canonical = canonicalPath(path);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(qMax(0, timeoutMs));

    while (true) {
        QVector<ScanSessionRecord> sessions;
        QString listError;
        if (!m_store->listScanSessions(&sessions, &listError)) {
            if (errorText) {
                *errorText = QStringLiteral("publish_gate_list_scan_sessions_failed: %1").arg(listError);
            }
            return false;
        }

        ScanSessionRecord overlapping;
        bool hasOverlapping = false;
        for (const ScanSessionRecord& session : sessions) {
            if (!pathsOverlap(canonical, session.rootPath)) {
                continue;
            }
            overlapping = session;
            hasOverlapping = true;
            break;
        }

        if (!hasOverlapping) {
            if (terminalSession && sawActiveSession && *sawActiveSession) {
                terminalSession->status = QStringLiteral("complete");
            }
            return true;
        }

        if (terminalSession) {
            *terminalSession = overlapping;
        }

        const QString status = overlapping.status.trimmed().toLower();
        if (status == QStringLiteral("complete")) {
            return true;
        }

        if (status == QStringLiteral("running")) {
            if (sawActiveSession) {
                *sawActiveSession = true;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        if (errorText) {
            *errorText = QStringLiteral("publish_gate_terminal_status_not_complete status=%1 session=%2 root=%3 error=%4")
                             .arg(overlapping.status)
                             .arg(overlapping.id)
                             .arg(overlapping.rootPath)
                             .arg(overlapping.errorText);
        }
        return false;
    }
}

bool VisionIndexService::allowPublishForPath(const QString& path, QString* errorText)
{
    constexpr int kPublishGateTimeoutMs = 1200;

    bool sawActiveSession = false;
    ScanSessionRecord terminal;
    QString gateError;
    if (!waitForPublishReady(path, kPublishGateTimeoutMs, &sawActiveSession, &terminal, &gateError)) {
        if (errorText) {
            *errorText = gateError;
        }
        return false;
    }

    if (!sawActiveSession) {
        return true;
    }

    const QString terminalStatus = terminal.status.trimmed().toLower();
    if (terminalStatus == QStringLiteral("running")) {
        if (errorText) {
            *errorText = QStringLiteral("publish_deferred_active_scan root=%1 session=%2 timeout_ms=%3")
                             .arg(path)
                             .arg(terminal.id)
                             .arg(kPublishGateTimeoutMs);
        }
        return false;
    }

    if (terminalStatus != QStringLiteral("complete")) {
        if (errorText) {
            *errorText = QStringLiteral("publish_gate_failed_closed status=%1 session=%2 root=%3 error=%4")
                             .arg(terminal.status)
                             .arg(terminal.id)
                             .arg(terminal.rootPath)
                             .arg(terminal.errorText);
        }
        return false;
    }

    return true;
}

void VisionIndexService::scheduleVisibleRefresh(const QString& path, const QString& reason)
{
    RefreshContext context;
    context.force = false;
    context.mode = m_policy.defaultMode;
    context.reason = reason;
    m_refreshService.requestRefresh(path, context);
}
