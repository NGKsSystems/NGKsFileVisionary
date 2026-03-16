#pragma once

#include <QString>
#include <QVector>

#include <atomic>

#include "core/db/DbTypes.h"

struct ScanIngestItem
{
    EntryRecord record;
    QString parentPath;
};

struct ScanTask
{
    QString rootPath;
    QString mode;
    int batchSize = 500;
    qint64 volumeId = 0;
    qint64 scanSessionId = 0;
    std::atomic_bool* cancelRequested = nullptr;

    QString normalizedRootPath() const;
    int effectiveBatchSize() const;
};

struct ScanWorkerProgress
{
    qint64 totalSeen = 0;
    qint64 totalEmitted = 0;
    qint64 errorCount = 0;
    qint64 pendingDirectories = 0;
};

struct ScanCoordinatorResult
{
    bool success = false;
    bool canceled = false;
    QString errorText;

    qint64 sessionId = 0;
    qint64 volumeId = 0;
    qint64 totalSeen = 0;
    qint64 totalInserted = 0;
    qint64 totalUpdated = 0;
    qint64 totalRemoved = 0;
    qint64 errorCount = 0;
    qint64 pendingDirectories = 0;
};
