#pragma once

#include <QString>
#include <QVector>

enum class SnapshotDiffStatus
{
    Added,
    Removed,
    Changed,
    Unchanged
};

struct SnapshotDiffOptions
{
    bool includeUnchanged = false;
    bool includeHidden = false;
    bool includeSystem = false;
    bool filesOnly = false;
    bool directoriesOnly = false;
};

struct SnapshotDiffRow
{
    QString path;
    SnapshotDiffStatus status = SnapshotDiffStatus::Unchanged;

    qint64 oldSizeBytes = 0;
    bool oldHasSizeBytes = false;
    qint64 newSizeBytes = 0;
    bool newHasSizeBytes = false;

    QString oldModifiedUtc;
    QString newModifiedUtc;

    bool oldIsDir = false;
    bool newIsDir = false;

    bool oldHiddenFlag = false;
    bool newHiddenFlag = false;
    bool oldSystemFlag = false;
    bool newSystemFlag = false;
    bool oldArchiveFlag = false;
    bool newArchiveFlag = false;
};

struct SnapshotDiffSummary
{
    qint64 added = 0;
    qint64 removed = 0;
    qint64 changed = 0;
    qint64 unchanged = 0;
    qint64 totalRows = 0;
};

struct SnapshotDiffResult
{
    bool ok = false;
    QString errorText;
    qint64 oldSnapshotId = 0;
    qint64 newSnapshotId = 0;
    QVector<SnapshotDiffRow> rows;
    SnapshotDiffSummary summary;
};

namespace SnapshotDiffTypesUtil
{
QString statusToString(SnapshotDiffStatus status);
}
