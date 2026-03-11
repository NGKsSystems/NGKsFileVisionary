#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#include <QString>
#include <QVector>

#include "RefreshPolicy.h"
#include "RefreshTypes.h"

class RefreshService
{
public:
    RefreshService();
    ~RefreshService();

    bool initialize(const QString& dbPath, const RefreshPolicy& policy, QString* errorText = nullptr);
    void shutdown();

    RefreshRequestResult requestRefresh(const QString& path, const RefreshContext& context);
    QVector<RefreshEvent> takeEvents();

    bool waitForIdle(int timeoutMs);
    bool hasPendingWork() const;

private:
    struct QueueItem
    {
        quint64 requestId = 0;
        QString path;
        QString mode;
        bool force = false;
        QString reason;
        QString queuedAtUtc;
    };

    struct ActiveRequest
    {
        quint64 requestId = 0;
        QString path;
    };

    void workerLoop();
    bool launchWorker(QString* errorText = nullptr);
    void emitEvent(const RefreshEvent& event);

    bool hasRecentFreshScan(const QString& path, int staleThresholdSeconds) const;
    bool hasOverlappingActivePath(const QString& path, QString* overlapWith = nullptr) const;
    bool hasRecentDuplicateWithinWindow(const QString& path) const;
    void removeActivePath(const QString& path);

    static QString canonicalPath(const QString& path);
    static bool isSameOrParentPath(const QString& parent, const QString& candidate);

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_worker;

    bool m_initialized = false;
    bool m_stopRequested = false;
    bool m_running = false;

    QString m_dbPath;
    RefreshPolicy m_policy;

    quint64 m_nextRequestId = 0;
    std::deque<QueueItem> m_queue;
    QVector<ActiveRequest> m_active;
    QVector<RefreshEvent> m_events;
    QVector<RefreshEvent> m_recentTerminalEvents;
};