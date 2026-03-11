#pragma once

#include <QString>
#include <QVector>

#include <QHash>
#include <QMutex>
#include <QSet>

#include "ChangeEvent.h"
#include "WatchSession.h"

class WatchBridge
{
public:
    WatchBridge();
    ~WatchBridge();

    bool start(const QString& rootPath, QString* errorText = nullptr);
    void stop();
    bool isRunning() const;

    QVector<ChangeEvent> takePendingEvents();

private:
    void onRawEvent(const ChangeEvent& event);
    bool shouldSuppressDuplicate(const ChangeEvent& event);

    WatchSession m_session;
    QMutex m_mutex;
    QVector<ChangeEvent> m_pending;
    QHash<QString, qint64> m_recentMsByKey;
    QSet<QString> m_knownPaths;

    bool m_hasPendingRenameOld;
    ChangeEvent m_pendingRenameOld;
};
