#include "RefreshService.h"

#include <chrono>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>

#include "core/db/MetaStore.h"
#include "core/scan/ScanCoordinator.h"

namespace {
QString nowIso()
{
    return RefreshTypes::utcNowIso();
}
}

RefreshService::RefreshService() = default;

RefreshService::~RefreshService()
{
    shutdown();
}

bool RefreshService::initialize(const QString& dbPath, const RefreshPolicy& policy, QString* errorText)
{
    shutdown();

    const QString normalizedDbPath = QDir::cleanPath(dbPath);
    if (normalizedDbPath.isEmpty()) {
        if (errorText) {
            *errorText = QStringLiteral("refresh_db_path_empty");
        }
        return false;
    }

    m_dbPath = normalizedDbPath;
    m_policy = policy;
    m_stopRequested = false;
    m_initialized = true;
    return launchWorker(errorText);
}

void RefreshService::shutdown()
{
    QVector<RefreshEvent> canceledEvents;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (!m_initialized && !m_running) {
            return;
        }
        m_stopRequested = true;
        for (const QueueItem& queued : m_queue) {
            RefreshEvent event;
            event.requestId = queued.requestId;
            event.path = queued.path;
            event.mode = queued.mode;
            event.state = RefreshState::Canceled;
            event.reason = QStringLiteral("service_shutdown");
            event.timestampUtc = nowIso();
            canceledEvents.push_back(event);
        }
        m_queue.clear();
    }

    for (const RefreshEvent& event : canceledEvents) {
        emitEvent(event);
    }

    m_cv.notify_all();
    if (m_worker.joinable()) {
        m_worker.join();
    }

    std::lock_guard<std::mutex> guard(m_mutex);
    m_active.clear();
    m_running = false;
    m_initialized = false;
}

bool RefreshService::launchWorker(QString* errorText)
{
    try {
        m_worker = std::thread([this]() { workerLoop(); });
    } catch (...) {
        if (errorText) {
            *errorText = QStringLiteral("refresh_worker_launch_failed");
        }
        return false;
    }
    return true;
}

RefreshRequestResult RefreshService::requestRefresh(const QString& path, const RefreshContext& context)
{
    RefreshRequestResult response;
    response.path = canonicalPath(path);
    response.state = RefreshState::Failed;

    if (!m_initialized) {
        response.errorText = QStringLiteral("refresh_service_not_initialized");
        return response;
    }

    if (response.path.isEmpty()) {
        response.errorText = QStringLiteral("refresh_path_empty");
        return response;
    }

    QFileInfo info(response.path);
    if (!info.exists() || !info.isDir()) {
        response.errorText = QStringLiteral("refresh_path_not_usable");

        RefreshEvent failedEvent;
        failedEvent.path = response.path;
        failedEvent.mode = context.mode.isEmpty() ? m_policy.defaultMode : context.mode;
        failedEvent.state = RefreshState::Failed;
        failedEvent.reason = QStringLiteral("invalid_path");
        failedEvent.errorText = response.errorText;
        failedEvent.timestampUtc = nowIso();
        emitEvent(failedEvent);
        return response;
    }

    const int staleSeconds = (context.staleThresholdSeconds >= 0)
        ? context.staleThresholdSeconds
        : m_policy.staleThresholdSeconds;
    const bool force = context.force;

    if (!force && !m_policy.alwaysRefreshVisiblePath && hasRecentFreshScan(response.path, staleSeconds)) {
        response.state = RefreshState::SkippedFresh;
        response.reason = QStringLiteral("recent_scan_within_threshold");

        RefreshEvent freshEvent;
        freshEvent.path = response.path;
        freshEvent.mode = context.mode.isEmpty() ? m_policy.defaultMode : context.mode;
        freshEvent.state = RefreshState::SkippedFresh;
        freshEvent.reason = response.reason;
        freshEvent.timestampUtc = nowIso();
        emitEvent(freshEvent);
        return response;
    }

    RefreshEvent deferredEvent;
    bool hasDeferredEvent = false;

    {
        std::lock_guard<std::mutex> guard(m_mutex);

        QString overlapPath;
        if (hasOverlappingActivePath(response.path, &overlapPath)) {
            response.state = RefreshState::SkippedDuplicate;
            response.reason = QStringLiteral("overlap_with_active_path:%1").arg(overlapPath);

            deferredEvent.path = response.path;
            deferredEvent.mode = context.mode.isEmpty() ? m_policy.defaultMode : context.mode;
            deferredEvent.state = RefreshState::SkippedDuplicate;
            deferredEvent.reason = response.reason;
            deferredEvent.timestampUtc = nowIso();
            hasDeferredEvent = true;
        } else if (!force && hasRecentDuplicateWithinWindow(response.path)) {
            response.state = RefreshState::SkippedDuplicate;
            response.reason = QStringLiteral("dedupe_window");

            deferredEvent.path = response.path;
            deferredEvent.mode = context.mode.isEmpty() ? m_policy.defaultMode : context.mode;
            deferredEvent.state = RefreshState::SkippedDuplicate;
            deferredEvent.reason = response.reason;
            deferredEvent.timestampUtc = nowIso();
            hasDeferredEvent = true;
        } else {
            QueueItem item;
            item.requestId = ++m_nextRequestId;
            item.path = response.path;
            item.mode = context.mode.isEmpty() ? m_policy.defaultMode : context.mode;
            item.force = force;
            item.reason = context.reason;
            item.queuedAtUtc = nowIso();
            m_queue.push_back(item);

            ActiveRequest active;
            active.requestId = item.requestId;
            active.path = item.path;
            m_active.push_back(active);

            response.accepted = true;
            response.requestId = item.requestId;
            response.state = RefreshState::Queued;
            response.reason = context.reason;

            RefreshEvent queuedEvent;
            queuedEvent.requestId = item.requestId;
            queuedEvent.path = item.path;
            queuedEvent.mode = item.mode;
            queuedEvent.state = RefreshState::Queued;
            queuedEvent.reason = context.reason;
            queuedEvent.timestampUtc = item.queuedAtUtc;
            deferredEvent = queuedEvent;
            hasDeferredEvent = true;
        }
    }

    if (hasDeferredEvent) {
        emitEvent(deferredEvent);
    }

    if (response.accepted) {
        m_cv.notify_one();
    }
    return response;
}

QVector<RefreshEvent> RefreshService::takeEvents()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    QVector<RefreshEvent> events = m_events;
    m_events.clear();
    return events;
}

bool RefreshService::waitForIdle(int timeoutMs)
{
    const auto start = std::chrono::steady_clock::now();
    while (true) {
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            if (m_queue.empty() && m_active.isEmpty()) {
                return true;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (timeoutMs >= 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - start)
                                     .count();
            if (elapsed > timeoutMs) {
                return false;
            }
        }
    }
}

bool RefreshService::hasPendingWork() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return !m_queue.empty() || !m_active.isEmpty();
}

void RefreshService::workerLoop()
{
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_running = true;
    }

    while (true) {
        QueueItem item;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this]() { return m_stopRequested || !m_queue.empty(); });
            if (m_stopRequested && m_queue.empty()) {
                break;
            }
            item = m_queue.front();
            m_queue.pop_front();
        }

        RefreshEvent runningEvent;
        runningEvent.requestId = item.requestId;
        runningEvent.path = item.path;
        runningEvent.mode = item.mode;
        runningEvent.state = RefreshState::Running;
        runningEvent.reason = item.reason;
        runningEvent.timestampUtc = nowIso();
        emitEvent(runningEvent);

        RefreshEvent doneEvent;
        doneEvent.requestId = item.requestId;
        doneEvent.path = item.path;
        doneEvent.mode = item.mode;
        doneEvent.timestampUtc = nowIso();

        MetaStore refreshStore;
        QString dbError;
        QString migrationLog;
        if (!refreshStore.initialize(m_dbPath, &dbError, &migrationLog)) {
            doneEvent.state = RefreshState::Failed;
            doneEvent.errorText = QStringLiteral("refresh_store_init_failed:%1").arg(dbError);
            emitEvent(doneEvent);
            removeActivePath(item.path);
            continue;
        }

        ScanCoordinator coordinator(refreshStore);
        ScanTask task;
        task.rootPath = item.path;
        task.mode = item.mode;
        task.batchSize = 500;
        ScanCoordinatorResult runResult;
        auto lastProgressEmitAt = std::chrono::steady_clock::now();
        qint64 lastProgressSeen = -1;

        ScanCoordinator::Callbacks callbacks;
        callbacks.onLog = nullptr;
        callbacks.onProgress = [&](const ScanCoordinatorResult& progress) {
            const auto now = std::chrono::steady_clock::now();
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastProgressEmitAt).count();
            const bool seenAdvanced = (progress.totalSeen - lastProgressSeen) >= 500;
            if (!seenAdvanced && elapsedMs < 300) {
                return;
            }
            lastProgressEmitAt = now;
            lastProgressSeen = progress.totalSeen;

            const qint64 denominator = progress.totalSeen + qMax<qint64>(0, progress.pendingDirectories);
            int percent = 0;
            if (denominator > 0) {
                const double ratio = static_cast<double>(progress.totalSeen) / static_cast<double>(denominator);
                percent = qBound(0, static_cast<int>(ratio * 100.0), 99);
            }

            RefreshEvent progressEvent;
            progressEvent.requestId = item.requestId;
            progressEvent.path = item.path;
            progressEvent.mode = item.mode;
            progressEvent.state = RefreshState::Running;
            progressEvent.reason = item.reason;
            progressEvent.timestampUtc = nowIso();
            progressEvent.sessionId = progress.sessionId;
            progressEvent.totalSeen = progress.totalSeen;
            progressEvent.totalInserted = progress.totalInserted;
            progressEvent.totalUpdated = progress.totalUpdated;
            progressEvent.totalRemoved = progress.totalRemoved;
            progressEvent.pendingDirectories = progress.pendingDirectories;
            progressEvent.progressPercent = percent;
            emitEvent(progressEvent);
        };

        const bool runOk = coordinator.runScan(task, callbacks, &runResult);
        doneEvent.sessionId = runResult.sessionId;
        doneEvent.totalSeen = runResult.totalSeen;
        doneEvent.totalInserted = runResult.totalInserted;
        doneEvent.totalUpdated = runResult.totalUpdated;
        doneEvent.totalRemoved = runResult.totalRemoved;
        doneEvent.pendingDirectories = 0;
        doneEvent.progressPercent = 100;

        if (runResult.canceled) {
            doneEvent.state = RefreshState::Canceled;
            doneEvent.success = false;
        } else if (!runOk || !runResult.errorText.isEmpty()) {
            doneEvent.state = RefreshState::Failed;
            doneEvent.success = false;
            doneEvent.errorText = runResult.errorText;
            if (doneEvent.errorText.isEmpty() && !runOk) {
                doneEvent.errorText = QStringLiteral("refresh_scan_failed");
            }
        } else {
            doneEvent.state = RefreshState::Completed;
            doneEvent.success = true;
        }

        emitEvent(doneEvent);
        removeActivePath(item.path);
    }

    std::lock_guard<std::mutex> guard(m_mutex);
    m_running = false;
}

void RefreshService::emitEvent(const RefreshEvent& event)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_events.push_back(event);
    if (RefreshTypes::isTerminalState(event.state)) {
        m_recentTerminalEvents.push_back(event);
        if (m_recentTerminalEvents.size() > 128) {
            m_recentTerminalEvents.remove(0, m_recentTerminalEvents.size() - 128);
        }
    }
}

bool RefreshService::hasRecentFreshScan(const QString& path, int staleThresholdSeconds) const
{
    if (staleThresholdSeconds < 0) {
        return false;
    }

    MetaStore tempStore;
    QString errorText;
    QString migrationLog;
    if (!tempStore.initialize(m_dbPath, &errorText, &migrationLog)) {
        return false;
    }

    QVector<ScanSessionRecord> sessions;
    if (!tempStore.listScanSessions(&sessions, &errorText)) {
        tempStore.shutdown();
        return false;
    }

    const QString canonical = canonicalPath(path);
    const QDateTime now = QDateTime::currentDateTimeUtc();

    for (const ScanSessionRecord& s : sessions) {
        if (s.status.compare(QStringLiteral("complete"), Qt::CaseInsensitive) != 0) {
            continue;
        }

        const QString scanRoot = canonicalPath(s.rootPath);
        if (!isSameOrParentPath(scanRoot, canonical)) {
            continue;
        }

        const QDateTime completed = QDateTime::fromString(s.completedUtc, Qt::ISODate);
        if (!completed.isValid()) {
            continue;
        }

        if (completed.secsTo(now) <= staleThresholdSeconds) {
            tempStore.shutdown();
            return true;
        }
    }

    tempStore.shutdown();
    return false;
}

bool RefreshService::hasOverlappingActivePath(const QString& path, QString* overlapWith) const
{
    for (const ActiveRequest& active : m_active) {
        if (isSameOrParentPath(active.path, path) || isSameOrParentPath(path, active.path)) {
            if (overlapWith) {
                *overlapWith = active.path;
            }
            return true;
        }
    }
    return false;
}

bool RefreshService::hasRecentDuplicateWithinWindow(const QString& path) const
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (auto it = m_recentTerminalEvents.crbegin(); it != m_recentTerminalEvents.crend(); ++it) {
        if (canonicalPath(it->path) != canonicalPath(path)) {
            continue;
        }
        const QDateTime eventTime = QDateTime::fromString(it->timestampUtc, Qt::ISODate);
        if (!eventTime.isValid()) {
            continue;
        }
        if (eventTime.secsTo(now) <= m_policy.dedupeWindowSeconds) {
            return true;
        }
        break;
    }
    return false;
}

void RefreshService::removeActivePath(const QString& path)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    for (qsizetype i = 0; i < m_active.size(); ++i) {
        if (canonicalPath(m_active[i].path) == canonicalPath(path)) {
            m_active.removeAt(i);
            break;
        }
    }
}

QString RefreshService::canonicalPath(const QString& path)
{
    return QDir::fromNativeSeparators(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
}

bool RefreshService::isSameOrParentPath(const QString& parent, const QString& candidate)
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
