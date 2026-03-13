#pragma once

#include <QString>

#include "core/snapshot/SnapshotTypes.h"

enum class HistoryStatus
{
    Added,
    Removed,
    Changed,
    Unchanged,
    Absent
};

struct HistoryEntry
{
    qint64 snapshotId = 0;
    QString snapshotName;
    QString snapshotCreatedUtc;
    QString targetPath;
    HistoryStatus status = HistoryStatus::Absent;
    qint64 sizeBytes = 0;
    bool hasSizeBytes = false;
    QString modifiedUtc;
    QString note;
};

namespace HistoryEntryUtil
{
QString statusToString(HistoryStatus status);
bool snapshotEntriesEquivalent(const SnapshotEntryRecord& oldRow, const SnapshotEntryRecord& newRow);
}
