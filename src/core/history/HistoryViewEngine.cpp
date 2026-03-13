#include "HistoryViewEngine.h"

#include <algorithm>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QStringList>

namespace
{
QString normalizePath(const QString& path)
{
    return QDir::fromNativeSeparators(QDir::cleanPath(path));
}

void sortSnapshotsChronological(QVector<SnapshotRecord>* snapshots)
{
    std::sort(snapshots->begin(), snapshots->end(), [](const SnapshotRecord& a, const SnapshotRecord& b) {
        const int byTime = QString::compare(a.createdUtc, b.createdUtc, Qt::CaseInsensitive);
        if (byTime != 0) {
            return byTime < 0;
        }
        return a.id < b.id;
    });
}

bool resolveTargetPath(const QVector<SnapshotEntryRecord>& allEntries,
                       const QString& rootPath,
                       const QString& targetHint,
                       QString* resolvedTarget)
{
    QStringList candidates;
    candidates.reserve(allEntries.size());
    for (const SnapshotEntryRecord& entry : allEntries) {
        const QString candidate = normalizePath(entry.entryPath);
        if (!candidate.isEmpty() && !candidates.contains(candidate, Qt::CaseInsensitive)) {
            candidates.push_back(candidate);
        }
    }

    const QString hint = normalizePath(targetHint);
    if (hint.isEmpty()) {
        if (resolvedTarget) {
            *resolvedTarget = QString();
        }
        return false;
    }

    QStringList exactCandidates;
    exactCandidates.push_back(hint);
    if (!QFileInfo(hint).isAbsolute()) {
        exactCandidates.push_back(normalizePath(QDir(rootPath).filePath(hint)));
    }

    for (const QString& candidate : candidates) {
        for (const QString& exact : exactCandidates) {
            if (candidate.compare(exact, Qt::CaseInsensitive) == 0) {
                if (resolvedTarget) {
                    *resolvedTarget = candidate;
                }
                return true;
            }
        }
    }

    if (hint.contains('/')) {
        const QString suffixToken = QStringLiteral("/") + hint;
        for (const QString& candidate : candidates) {
            if (candidate.endsWith(hint, Qt::CaseInsensitive)
                || candidate.endsWith(suffixToken, Qt::CaseInsensitive)) {
                if (resolvedTarget) {
                    *resolvedTarget = candidate;
                }
                return true;
            }
        }
    }

    const QString basename = QFileInfo(hint).fileName().toLower();
    if (!basename.isEmpty()) {
        for (const QString& candidate : candidates) {
            if (QFileInfo(candidate).fileName().toLower() == basename) {
                if (resolvedTarget) {
                    *resolvedTarget = candidate;
                }
                return true;
            }
        }
    }

    if (resolvedTarget) {
        *resolvedTarget = QString();
    }
    return false;
}
}

HistoryViewEngine::HistoryViewEngine(SnapshotRepository& repository, const SnapshotDiffEngine& diffEngine)
    : m_repository(repository)
    , m_diffEngine(diffEngine)
{
}

bool HistoryViewEngine::listSnapshotsForRoot(const QString& rootPath,
                                             QVector<SnapshotRecord>* out,
                                             QString* errorText) const
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("null_output_vector");
        }
        return false;
    }

    QVector<SnapshotRecord> snapshots;
    if (!m_repository.listSnapshots(normalizePath(rootPath), &snapshots, errorText)) {
        return false;
    }

    sortSnapshotsChronological(&snapshots);
    *out = snapshots;
    return true;
}

bool HistoryViewEngine::getPathHistory(const QString& rootPath,
                                       const QString& targetPath,
                                       QVector<HistoryEntry>* out,
                                       QString* errorText) const
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("null_output_vector");
        }
        return false;
    }

    out->clear();

    QVector<SnapshotRecord> snapshots;
    if (!listSnapshotsForRoot(rootPath, &snapshots, errorText)) {
        return false;
    }
    if (snapshots.isEmpty()) {
        if (errorText) {
            *errorText = QStringLiteral("no_snapshots_for_root");
        }
        return false;
    }

    const QString normalizedRoot = normalizePath(rootPath);
    const QString normalizedHint = normalizePath(targetPath);

    QVector<QVector<SnapshotEntryRecord>> entriesBySnapshot;
    entriesBySnapshot.reserve(snapshots.size());

    QVector<SnapshotEntryRecord> allEntries;
    for (const SnapshotRecord& snapshot : snapshots) {
        QVector<SnapshotEntryRecord> entries;
        QString loadError;
        if (!m_repository.listSnapshotEntries(snapshot.id, &entries, &loadError)) {
            if (errorText) {
                *errorText = QStringLiteral("snapshot_entries_load_failed:%1").arg(loadError);
            }
            return false;
        }
        entriesBySnapshot.push_back(entries);
        for (const SnapshotEntryRecord& entry : entries) {
            allEntries.push_back(entry);
        }
    }

    QString resolvedTarget;
    const bool resolved = resolveTargetPath(allEntries, normalizedRoot, normalizedHint, &resolvedTarget);

    bool prevPresent = false;
    SnapshotEntryRecord prevRow;

    for (int i = 0; i < snapshots.size(); ++i) {
        const SnapshotRecord& snapshot = snapshots.at(i);
        const QVector<SnapshotEntryRecord>& entries = entriesBySnapshot.at(i);

        bool present = false;
        SnapshotEntryRecord current;

        for (const SnapshotEntryRecord& row : entries) {
            const QString rowPath = normalizePath(row.entryPath);
            if (resolved) {
                if (rowPath.compare(resolvedTarget, Qt::CaseInsensitive) == 0) {
                    present = true;
                    current = row;
                    break;
                }
            }
        }

        HistoryEntry history;
        history.snapshotId = snapshot.id;
        history.snapshotName = snapshot.snapshotName;
        history.snapshotCreatedUtc = snapshot.createdUtc;
        history.targetPath = resolved ? resolvedTarget : normalizedHint;

        if (i == 0) {
            if (present) {
                history.status = HistoryStatus::Added;
                history.note = QStringLiteral("first_snapshot_present");
            } else {
                history.status = HistoryStatus::Absent;
                history.note = resolved ? QStringLiteral("first_snapshot_absent") : QStringLiteral("target_unresolved_in_all_snapshots");
            }
        } else {
            if (!prevPresent && !present) {
                history.status = HistoryStatus::Absent;
                history.note = QStringLiteral("still_absent");
            } else if (!prevPresent && present) {
                history.status = HistoryStatus::Added;
                history.note = QStringLiteral("became_present");
            } else if (prevPresent && !present) {
                history.status = HistoryStatus::Removed;
                history.note = QStringLiteral("became_absent");
            } else {
                const bool same = HistoryEntryUtil::snapshotEntriesEquivalent(prevRow, current);
                history.status = same ? HistoryStatus::Unchanged : HistoryStatus::Changed;
                history.note = same ? QStringLiteral("present_unchanged") : QStringLiteral("present_changed");
            }
        }

        if (present) {
            history.hasSizeBytes = current.hasSizeBytes;
            history.sizeBytes = current.sizeBytes;
            history.modifiedUtc = current.modifiedUtc;
        }

        out->push_back(history);
        prevPresent = present;
        if (present) {
            prevRow = current;
        }
    }

    return true;
}

bool HistoryViewEngine::summarizePathHistory(const QString& rootPath,
                                             const QString& targetPath,
                                             PathHistorySummary* out,
                                             QString* errorText) const
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("null_output_summary");
        }
        return false;
    }

    QVector<HistoryEntry> history;
    if (!getPathHistory(rootPath, targetPath, &history, errorText)) {
        return false;
    }

    *out = HistorySummaryUtil::summarizePathEntries(normalizePath(rootPath), normalizePath(targetPath), history);
    return true;
}

bool HistoryViewEngine::getRootHistorySummary(const QString& rootPath,
                                              RootHistorySummary* out,
                                              QString* errorText) const
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("null_output_summary");
        }
        return false;
    }

    RootHistorySummary summary;
    summary.ok = false;
    summary.rootPath = normalizePath(rootPath);

    QVector<SnapshotRecord> snapshots;
    if (!listSnapshotsForRoot(summary.rootPath, &snapshots, errorText)) {
        return false;
    }
    summary.snapshotCount = snapshots.size();
    if (snapshots.size() < 2) {
        if (errorText) {
            *errorText = QStringLiteral("insufficient_snapshots_for_root_summary");
        }
        return false;
    }

    SnapshotDiffOptions options;
    options.includeUnchanged = true;

    for (int i = 1; i < snapshots.size(); ++i) {
        const SnapshotRecord& oldSnapshot = snapshots.at(i - 1);
        const SnapshotRecord& newSnapshot = snapshots.at(i);

        const SnapshotDiffResult diff = m_diffEngine.compareSnapshots(oldSnapshot.id, newSnapshot.id, options);
        if (!diff.ok) {
            if (errorText) {
                *errorText = QStringLiteral("diff_failed:%1").arg(diff.errorText);
            }
            return false;
        }

        RootHistoryPairSummary pair;
        pair.oldSnapshotId = oldSnapshot.id;
        pair.newSnapshotId = newSnapshot.id;
        pair.oldSnapshotName = oldSnapshot.snapshotName;
        pair.newSnapshotName = newSnapshot.snapshotName;
        pair.oldSnapshotCreatedUtc = oldSnapshot.createdUtc;
        pair.newSnapshotCreatedUtc = newSnapshot.createdUtc;
        pair.added = diff.summary.added;
        pair.removed = diff.summary.removed;
        pair.changed = diff.summary.changed;
        pair.unchanged = diff.summary.unchanged;
        pair.totalRows = diff.summary.totalRows;

        summary.pairs.push_back(pair);
        summary.totalAdded += pair.added;
        summary.totalRemoved += pair.removed;
        summary.totalChanged += pair.changed;
        summary.totalUnchanged += pair.unchanged;
        summary.totalRows += pair.totalRows;
    }

    summary.ok = true;
    *out = summary;
    return true;
}
