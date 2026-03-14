#include "StructuralResultAdapter.h"

#include <QFileInfo>

namespace StructuralResultAdapter
{
QVector<StructuralResultRow> fromHistoryRows(const QVector<HistoryEntry>& rows,
                                             const QString& selectedAbsolutePath)
{
    QVector<StructuralResultRow> out;
    out.reserve(rows.size());

    for (const HistoryEntry& history : rows) {
        StructuralResultRow row;
        row.category = StructuralResultCategory::History;
        row.primaryPath = selectedAbsolutePath;
        row.status = HistoryEntryUtil::statusToString(history.status);
        row.timestamp = history.snapshotCreatedUtc;
        row.secondaryPath = history.targetPath;
        row.hasSnapshotId = history.snapshotId > 0;
        row.snapshotId = history.snapshotId;
        row.hasSizeBytes = history.hasSizeBytes;
        row.sizeBytes = history.sizeBytes;
        row.note = history.note;
        out.push_back(row);
    }

    return out;
}

QVector<StructuralResultRow> fromSnapshotRows(const QVector<SnapshotRecord>& rows)
{
    QVector<StructuralResultRow> out;
    out.reserve(rows.size());

    for (const SnapshotRecord& snapshot : rows) {
        StructuralResultRow row;
        row.category = StructuralResultCategory::Snapshot;
        row.primaryPath = snapshot.rootPath;
        row.status = snapshot.snapshotType;
        row.timestamp = snapshot.createdUtc;
        row.secondaryPath = snapshot.snapshotName;
        row.hasSnapshotId = snapshot.id > 0;
        row.snapshotId = snapshot.id;
        row.hasSizeBytes = snapshot.itemCount >= 0;
        row.sizeBytes = snapshot.itemCount;
        row.note = snapshot.noteText;
        out.push_back(row);
    }

    return out;
}

QVector<StructuralResultRow> fromDiffRows(const QVector<SnapshotDiffRow>& rows)
{
    QVector<StructuralResultRow> out;
    out.reserve(rows.size());

    for (const SnapshotDiffRow& diff : rows) {
        StructuralResultRow row;
        row.category = StructuralResultCategory::Diff;
        row.primaryPath = diff.path;
        row.status = SnapshotDiffTypesUtil::statusToString(diff.status);
        row.timestamp = !diff.newModifiedUtc.isEmpty() ? diff.newModifiedUtc : diff.oldModifiedUtc;
        row.hasSizeBytes = diff.newHasSizeBytes || diff.oldHasSizeBytes;
        row.sizeBytes = diff.newHasSizeBytes ? diff.newSizeBytes : diff.oldSizeBytes;
        out.push_back(row);
    }

    return out;
}

QVector<StructuralResultRow> fromReferenceRows(const QVector<QueryRow>& rows)
{
    QVector<StructuralResultRow> out;
    out.reserve(rows.size());

    for (const QueryRow& query : rows) {
        StructuralResultRow row;
        row.category = StructuralResultCategory::Reference;
        row.primaryPath = query.path;
        row.status = query.graphReferenceType.isEmpty() ? QStringLiteral("reference") : query.graphReferenceType;
        row.timestamp = query.modifiedUtc;
        row.secondaryPath = query.graphTargetPath;
        row.relationship = row.status;
        row.sourceFile = query.graphSourcePath;
        row.symbol = query.name.isEmpty() ? QFileInfo(query.path).fileName() : query.name;
        row.hasSizeBytes = query.hasSizeBytes;
        row.sizeBytes = query.sizeBytes;
        row.note = query.graphResolvedFlag ? QStringLiteral("resolved") : QStringLiteral("unresolved");
        out.push_back(row);
    }

    return out;
}

QVector<FileEntry> toFileEntries(const QVector<StructuralResultRow>& rows)
{
    QVector<FileEntry> out;
    out.reserve(rows.size());

    for (const StructuralResultRow& row : rows) {
        out.push_back(StructuralResultRowUtil::toFileEntry(row));
    }

    return out;
}

QStringList toDebugStrings(const QVector<StructuralResultRow>& rows)
{
    QStringList out;
    out.reserve(rows.size());

    for (int i = 0; i < rows.size(); ++i) {
        const StructuralResultRow& row = rows.at(i);
        out.push_back(QStringLiteral("row[%1] category=%2 primaryPath=%3 secondaryPath=%4 relationship=%5 status=%6 snapshotId=%7 timestamp=%8 sizeBytes=%9 sourceFile=%10 symbol=%11 note=%12 dependencyFrequency=%13 changeFrequency=%14 hubScore=%15 rankScore=%16")
                          .arg(i)
                          .arg(StructuralResultRowUtil::categoryToString(row.category))
                          .arg(row.primaryPath)
                          .arg(row.secondaryPath)
                          .arg(row.relationship)
                          .arg(row.status)
                          .arg(row.hasSnapshotId ? QString::number(row.snapshotId) : QStringLiteral(""))
                          .arg(row.timestamp)
                          .arg(row.hasSizeBytes ? QString::number(row.sizeBytes) : QStringLiteral(""))
                          .arg(row.sourceFile)
                          .arg(row.symbol)
                          .arg(row.note)
                          .arg(row.dependencyFrequency)
                          .arg(row.changeFrequency)
                          .arg(row.hubScore)
                          .arg(QString::number(row.rankScore, 'f', 3)));
    }

    return out;
}
}
