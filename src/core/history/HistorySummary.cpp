#include "HistorySummary.h"

#include <QStringList>

namespace HistorySummaryUtil
{
PathHistorySummary summarizePathEntries(const QString& rootPath,
                                       const QString& targetPath,
                                       const QVector<HistoryEntry>& entries)
{
    PathHistorySummary summary;
    summary.rootPath = rootPath;
    summary.targetPath = targetPath;
    summary.snapshotCount = entries.size();

    for (const HistoryEntry& entry : entries) {
        switch (entry.status) {
        case HistoryStatus::Added:
            ++summary.addedCount;
            ++summary.presentCount;
            break;
        case HistoryStatus::Removed:
            ++summary.removedCount;
            ++summary.absentCount;
            break;
        case HistoryStatus::Changed:
            ++summary.changedCount;
            ++summary.presentCount;
            break;
        case HistoryStatus::Unchanged:
            ++summary.unchangedCount;
            ++summary.presentCount;
            break;
        case HistoryStatus::Absent:
            ++summary.absentCount;
            break;
        }
    }

    summary.targetEverPresent = summary.presentCount > 0;
    return summary;
}

QString pathSummaryToText(const PathHistorySummary& summary)
{
    QStringList lines;
    lines << QStringLiteral("root_path=%1").arg(summary.rootPath);
    lines << QStringLiteral("target_path=%1").arg(summary.targetPath);
    lines << QStringLiteral("snapshot_count=%1").arg(summary.snapshotCount);
    lines << QStringLiteral("target_ever_present=%1").arg(summary.targetEverPresent ? QStringLiteral("true") : QStringLiteral("false"));
    lines << QStringLiteral("present_count=%1").arg(summary.presentCount);
    lines << QStringLiteral("absent_count=%1").arg(summary.absentCount);
    lines << QStringLiteral("added_count=%1").arg(summary.addedCount);
    lines << QStringLiteral("removed_count=%1").arg(summary.removedCount);
    lines << QStringLiteral("changed_count=%1").arg(summary.changedCount);
    lines << QStringLiteral("unchanged_count=%1").arg(summary.unchangedCount);
    return lines.join(QStringLiteral("\n"));
}

QString rootSummaryToText(const RootHistorySummary& summary)
{
    QStringList lines;
    lines << QStringLiteral("root_path=%1").arg(summary.rootPath);
    lines << QStringLiteral("snapshot_count=%1").arg(summary.snapshotCount);
    lines << QStringLiteral("pair_count=%1").arg(summary.pairs.size());
    lines << QStringLiteral("total_added=%1").arg(summary.totalAdded);
    lines << QStringLiteral("total_removed=%1").arg(summary.totalRemoved);
    lines << QStringLiteral("total_changed=%1").arg(summary.totalChanged);
    lines << QStringLiteral("total_unchanged=%1").arg(summary.totalUnchanged);
    lines << QStringLiteral("total_rows=%1").arg(summary.totalRows);

    for (const RootHistoryPairSummary& pair : summary.pairs) {
        lines << QStringLiteral("pair old_id=%1 old_name=%2 new_id=%3 new_name=%4 added=%5 removed=%6 changed=%7 unchanged=%8 total=%9")
                     .arg(pair.oldSnapshotId)
                     .arg(pair.oldSnapshotName)
                     .arg(pair.newSnapshotId)
                     .arg(pair.newSnapshotName)
                     .arg(pair.added)
                     .arg(pair.removed)
                     .arg(pair.changed)
                     .arg(pair.unchanged)
                     .arg(pair.totalRows);
    }

    return lines.join(QStringLiteral("\n"));
}
}
