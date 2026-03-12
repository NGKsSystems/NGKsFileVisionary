#include "SnapshotDiffEngine.h"

#include <algorithm>
#include <QHash>
#include <QSet>
#include <QStringList>

#include "SnapshotRepository.h"
#include "SnapshotTypes.h"

namespace {
bool rowPassesFilters(const SnapshotEntryRecord& row, const SnapshotDiffOptions& options)
{
    if (!options.includeHidden && row.hiddenFlag) {
        return false;
    }
    if (!options.includeSystem && row.systemFlag) {
        return false;
    }
    if (options.filesOnly && row.isDir) {
        return false;
    }
    if (options.directoriesOnly && !row.isDir) {
        return false;
    }
    return true;
}

bool rowsEquivalent(const SnapshotEntryRecord& oldRow,
                    const SnapshotEntryRecord& newRow,
                    const SnapshotDiffOptions& options)
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

    if (options.includeHidden || options.includeSystem) {
        if (oldRow.existsFlag != newRow.existsFlag) {
            return false;
        }
    }

    return true;
}

SnapshotDiffRow makeAddedRow(const SnapshotEntryRecord& newRow)
{
    SnapshotDiffRow row;
    row.path = newRow.entryPath;
    row.status = SnapshotDiffStatus::Added;
    row.newSizeBytes = newRow.sizeBytes;
    row.newHasSizeBytes = newRow.hasSizeBytes;
    row.newModifiedUtc = newRow.modifiedUtc;
    row.newIsDir = newRow.isDir;
    row.newHiddenFlag = newRow.hiddenFlag;
    row.newSystemFlag = newRow.systemFlag;
    row.newArchiveFlag = newRow.archiveFlag;
    return row;
}

SnapshotDiffRow makeRemovedRow(const SnapshotEntryRecord& oldRow)
{
    SnapshotDiffRow row;
    row.path = oldRow.entryPath;
    row.status = SnapshotDiffStatus::Removed;
    row.oldSizeBytes = oldRow.sizeBytes;
    row.oldHasSizeBytes = oldRow.hasSizeBytes;
    row.oldModifiedUtc = oldRow.modifiedUtc;
    row.oldIsDir = oldRow.isDir;
    row.oldHiddenFlag = oldRow.hiddenFlag;
    row.oldSystemFlag = oldRow.systemFlag;
    row.oldArchiveFlag = oldRow.archiveFlag;
    return row;
}

SnapshotDiffRow makeCompareRow(const SnapshotEntryRecord& oldRow,
                               const SnapshotEntryRecord& newRow,
                               SnapshotDiffStatus status)
{
    SnapshotDiffRow row;
    row.path = oldRow.entryPath;
    row.status = status;

    row.oldSizeBytes = oldRow.sizeBytes;
    row.oldHasSizeBytes = oldRow.hasSizeBytes;
    row.newSizeBytes = newRow.sizeBytes;
    row.newHasSizeBytes = newRow.hasSizeBytes;

    row.oldModifiedUtc = oldRow.modifiedUtc;
    row.newModifiedUtc = newRow.modifiedUtc;

    row.oldIsDir = oldRow.isDir;
    row.newIsDir = newRow.isDir;

    row.oldHiddenFlag = oldRow.hiddenFlag;
    row.newHiddenFlag = newRow.hiddenFlag;
    row.oldSystemFlag = oldRow.systemFlag;
    row.newSystemFlag = newRow.systemFlag;
    row.oldArchiveFlag = oldRow.archiveFlag;
    row.newArchiveFlag = newRow.archiveFlag;
    return row;
}
}

SnapshotDiffEngine::SnapshotDiffEngine(SnapshotRepository& repository)
    : m_repository(repository)
{
}

SnapshotDiffResult SnapshotDiffEngine::compareSnapshots(qint64 oldSnapshotId,
                                                        qint64 newSnapshotId,
                                                        const SnapshotDiffOptions& options) const
{
    SnapshotDiffResult result;
    result.oldSnapshotId = oldSnapshotId;
    result.newSnapshotId = newSnapshotId;

    if (options.filesOnly && options.directoriesOnly) {
        result.errorText = QStringLiteral("conflicting_file_dir_filters");
        return result;
    }

    SnapshotRecord oldSnapshot;
    SnapshotRecord newSnapshot;
    QString errorText;
    if (!m_repository.getSnapshotById(oldSnapshotId, &oldSnapshot, &errorText)) {
        result.errorText = QStringLiteral("old_snapshot_not_found:%1").arg(errorText);
        return result;
    }
    if (!m_repository.getSnapshotById(newSnapshotId, &newSnapshot, &errorText)) {
        result.errorText = QStringLiteral("new_snapshot_not_found:%1").arg(errorText);
        return result;
    }

    if (oldSnapshot.rootPath.compare(newSnapshot.rootPath, Qt::CaseInsensitive) != 0) {
        result.errorText = QStringLiteral("snapshot_root_mismatch");
        return result;
    }

    QVector<SnapshotEntryRecord> oldRows;
    QVector<SnapshotEntryRecord> newRows;
    if (!m_repository.listSnapshotEntries(oldSnapshotId, &oldRows, &errorText)) {
        result.errorText = QStringLiteral("old_snapshot_entries_load_failed:%1").arg(errorText);
        return result;
    }
    if (!m_repository.listSnapshotEntries(newSnapshotId, &newRows, &errorText)) {
        result.errorText = QStringLiteral("new_snapshot_entries_load_failed:%1").arg(errorText);
        return result;
    }

    QHash<QString, SnapshotEntryRecord> oldByPath;
    QHash<QString, SnapshotEntryRecord> newByPath;
    for (const SnapshotEntryRecord& row : oldRows) {
        if (!rowPassesFilters(row, options)) {
            continue;
        }
        oldByPath.insert(row.entryPath, row);
    }
    for (const SnapshotEntryRecord& row : newRows) {
        if (!rowPassesFilters(row, options)) {
            continue;
        }
        newByPath.insert(row.entryPath, row);
    }

    QSet<QString> allPaths;
    for (auto it = oldByPath.constBegin(); it != oldByPath.constEnd(); ++it) {
        allPaths.insert(it.key());
    }
    for (auto it = newByPath.constBegin(); it != newByPath.constEnd(); ++it) {
        allPaths.insert(it.key());
    }

    QList<QString> sortedPaths = allPaths.values();
    std::sort(sortedPaths.begin(), sortedPaths.end(), [](const QString& a, const QString& b) {
        return QString::compare(a, b, Qt::CaseInsensitive) < 0;
    });

    result.rows.reserve(sortedPaths.size());
    for (const QString& path : sortedPaths) {
        const bool hasOld = oldByPath.contains(path);
        const bool hasNew = newByPath.contains(path);

        if (!hasOld && hasNew) {
            result.rows.push_back(makeAddedRow(newByPath.value(path)));
            continue;
        }
        if (hasOld && !hasNew) {
            result.rows.push_back(makeRemovedRow(oldByPath.value(path)));
            continue;
        }

        const SnapshotEntryRecord oldRow = oldByPath.value(path);
        const SnapshotEntryRecord newRow = newByPath.value(path);
        const bool same = rowsEquivalent(oldRow, newRow, options);
        if (!same) {
            result.rows.push_back(makeCompareRow(oldRow, newRow, SnapshotDiffStatus::Changed));
        } else if (options.includeUnchanged) {
            result.rows.push_back(makeCompareRow(oldRow, newRow, SnapshotDiffStatus::Unchanged));
        }
    }

    result.ok = true;
    result.summary = summarizeDiff(result);
    return result;
}

SnapshotDiffSummary SnapshotDiffEngine::summarizeDiff(const SnapshotDiffResult& result) const
{
    SnapshotDiffSummary summary;
    for (const SnapshotDiffRow& row : result.rows) {
        switch (row.status) {
        case SnapshotDiffStatus::Added:
            ++summary.added;
            break;
        case SnapshotDiffStatus::Removed:
            ++summary.removed;
            break;
        case SnapshotDiffStatus::Changed:
            ++summary.changed;
            break;
        case SnapshotDiffStatus::Unchanged:
            ++summary.unchanged;
            break;
        }
    }
    summary.totalRows = result.rows.size();
    return summary;
}

bool SnapshotDiffEngine::exportDiffSummary(const SnapshotDiffResult& result,
                                           QString* summaryOut,
                                           QString* errorText) const
{
    if (!summaryOut) {
        if (errorText) {
            *errorText = QStringLiteral("null_summary_output");
        }
        return false;
    }

    QStringList lines;
    lines << QStringLiteral("old_snapshot_id=%1").arg(result.oldSnapshotId);
    lines << QStringLiteral("new_snapshot_id=%1").arg(result.newSnapshotId);
    lines << QStringLiteral("added=%1").arg(result.summary.added);
    lines << QStringLiteral("removed=%1").arg(result.summary.removed);
    lines << QStringLiteral("changed=%1").arg(result.summary.changed);
    lines << QStringLiteral("unchanged=%1").arg(result.summary.unchanged);
    lines << QStringLiteral("total_rows=%1").arg(result.summary.totalRows);
    *summaryOut = lines.join(QStringLiteral("\n"));
    return true;
}
