#pragma once

#include <QString>
#include <QVector>

#include "HistoryEntry.h"

struct PathHistorySummary
{
    QString rootPath;
    QString targetPath;
    qint64 snapshotCount = 0;
    qint64 presentCount = 0;
    qint64 absentCount = 0;
    qint64 addedCount = 0;
    qint64 removedCount = 0;
    qint64 changedCount = 0;
    qint64 unchangedCount = 0;
    bool targetEverPresent = false;
};

struct RootHistoryPairSummary
{
    qint64 oldSnapshotId = 0;
    qint64 newSnapshotId = 0;
    QString oldSnapshotName;
    QString newSnapshotName;
    QString oldSnapshotCreatedUtc;
    QString newSnapshotCreatedUtc;
    qint64 added = 0;
    qint64 removed = 0;
    qint64 changed = 0;
    qint64 unchanged = 0;
    qint64 totalRows = 0;
};

struct RootHistorySummary
{
    bool ok = false;
    QString errorText;
    QString rootPath;
    qint64 snapshotCount = 0;
    QVector<RootHistoryPairSummary> pairs;
    qint64 totalAdded = 0;
    qint64 totalRemoved = 0;
    qint64 totalChanged = 0;
    qint64 totalUnchanged = 0;
    qint64 totalRows = 0;
};

namespace HistorySummaryUtil
{
PathHistorySummary summarizePathEntries(const QString& rootPath,
                                       const QString& targetPath,
                                       const QVector<HistoryEntry>& entries);
QString pathSummaryToText(const PathHistorySummary& summary);
QString rootSummaryToText(const RootHistorySummary& summary);
}
