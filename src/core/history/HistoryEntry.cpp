#include "HistoryEntry.h"

namespace HistoryEntryUtil
{
QString statusToString(HistoryStatus status)
{
    switch (status) {
    case HistoryStatus::Added:
        return QStringLiteral("added");
    case HistoryStatus::Removed:
        return QStringLiteral("removed");
    case HistoryStatus::Changed:
        return QStringLiteral("changed");
    case HistoryStatus::Unchanged:
        return QStringLiteral("unchanged");
    case HistoryStatus::Absent:
        return QStringLiteral("absent");
    }
    return QStringLiteral("unknown");
}

bool snapshotEntriesEquivalent(const SnapshotEntryRecord& oldRow, const SnapshotEntryRecord& newRow)
{
    if (oldRow.isDir != newRow.isDir) {
        return false;
    }
    if (oldRow.hasSizeBytes != newRow.hasSizeBytes) {
        return false;
    }
    if (oldRow.hasSizeBytes && oldRow.sizeBytes != newRow.sizeBytes) {
        return false;
    }
    if (oldRow.modifiedUtc != newRow.modifiedUtc) {
        return false;
    }
    if (oldRow.hiddenFlag != newRow.hiddenFlag) {
        return false;
    }
    if (oldRow.systemFlag != newRow.systemFlag) {
        return false;
    }
    if (oldRow.archiveFlag != newRow.archiveFlag) {
        return false;
    }
    if (oldRow.existsFlag != newRow.existsFlag) {
        return false;
    }
    return true;
}
}
