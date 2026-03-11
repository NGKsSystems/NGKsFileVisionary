#include "WatchBridge.h"

#include <QDateTime>
#include <QMutexLocker>

namespace {
qint64 nowMsUtc()
{
    return QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
}

QString dedupeKey(const ChangeEvent& event)
{
    return QStringLiteral("%1|%2").arg(changeEventTypeToString(event.type), event.targetPath.toLower());
}
}

WatchBridge::WatchBridge()
    : m_hasPendingRenameOld(false)
{
}

WatchBridge::~WatchBridge()
{
    stop();
}

bool WatchBridge::start(const QString& rootPath, QString* errorText)
{
    QMutexLocker locker(&m_mutex);
    m_pending.clear();
    m_recentMsByKey.clear();
    m_knownPaths.clear();
    m_hasPendingRenameOld = false;
    locker.unlock();

    return m_session.start(rootPath,
                           [this](const ChangeEvent& event) {
                               onRawEvent(event);
                           },
                           errorText);
}

void WatchBridge::stop()
{
    m_session.stop();
}

bool WatchBridge::isRunning() const
{
    return m_session.isRunning();
}

QVector<ChangeEvent> WatchBridge::takePendingEvents()
{
    QMutexLocker locker(&m_mutex);
    QVector<ChangeEvent> out = m_pending;
    m_pending.clear();
    return out;
}

void WatchBridge::onRawEvent(const ChangeEvent& event)
{
    QMutexLocker locker(&m_mutex);

    ChangeEvent normalized = event;
    const QString normalizedPath = normalized.targetPath.toLower();

    if (normalized.type == ChangeEventType::Modified && !m_knownPaths.contains(normalizedPath)) {
        normalized.type = ChangeEventType::Created;
    }

    if (normalized.type == ChangeEventType::RenamedOld) {
        m_pendingRenameOld = normalized;
        m_hasPendingRenameOld = true;
        m_knownPaths.remove(normalizedPath);
        m_pending.push_back(normalized);
        return;
    }

    if (normalized.type == ChangeEventType::RenamedNew) {
        if (m_hasPendingRenameOld) {
            ChangeEvent pair;
            pair.type = ChangeEventType::RenamedPair;
            pair.watchedRoot = normalized.watchedRoot;
            pair.targetPath = normalized.targetPath;
            pair.oldPath = m_pendingRenameOld.targetPath;
            pair.timestampUtc = normalized.timestampUtc;
            m_pending.push_back(pair);
            m_hasPendingRenameOld = false;
        }
        m_knownPaths.insert(normalizedPath);
        m_pending.push_back(normalized);
        return;
    }

    if (normalized.type == ChangeEventType::Created) {
        m_knownPaths.insert(normalizedPath);
    } else if (normalized.type == ChangeEventType::Deleted) {
        m_knownPaths.remove(normalizedPath);
    }

    if (shouldSuppressDuplicate(normalized)) {
        return;
    }

    m_pending.push_back(normalized);
}

bool WatchBridge::shouldSuppressDuplicate(const ChangeEvent& event)
{
    static constexpr qint64 kDedupeWindowMs = 250;
    const QString key = dedupeKey(event);
    const qint64 now = nowMsUtc();
    const auto it = m_recentMsByKey.constFind(key);
    if (it != m_recentMsByKey.constEnd()) {
        if ((now - it.value()) <= kDedupeWindowMs) {
            return true;
        }
    }

    m_recentMsByKey.insert(key, now);
    return false;
}
