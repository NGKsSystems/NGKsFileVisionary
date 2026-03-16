#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

#include "core/scan/ScanTask.h"

enum class RefreshState
{
    Idle = 0,
    Queued = 1,
    Running = 2,
    Completed = 3,
    Failed = 4,
    Canceled = 5,
    SkippedDuplicate = 6,
    SkippedFresh = 7,
};

struct RefreshEvent
{
    quint64 requestId = 0;
    QString path;
    QString mode;
    RefreshState state = RefreshState::Idle;
    QString reason;
    QString errorText;
    QString timestampUtc;

    qint64 sessionId = 0;
    qint64 totalSeen = 0;
    qint64 totalInserted = 0;
    qint64 totalUpdated = 0;
    qint64 totalRemoved = 0;
    qint64 pendingDirectories = 0;
    int progressPercent = -1;

    bool success = false;
};

struct RefreshRequestResult
{
    bool accepted = false;
    RefreshState state = RefreshState::Idle;
    quint64 requestId = 0;
    QString path;
    QString reason;
    QString errorText;
};

namespace RefreshTypes
{
QString stateToString(RefreshState state);
QString utcNowIso();
bool isTerminalState(RefreshState state);
bool isSuccessfulState(RefreshState state);
}