#pragma once

#include <QStringList>
#include <QVector>

#include "core/history/HistoryEntry.h"
#include "core/query/QueryTypes.h"
#include "core/snapshot/SnapshotDiffTypes.h"
#include "core/snapshot/SnapshotTypes.h"
#include "StructuralResultRow.h"

namespace StructuralResultAdapter
{
QVector<StructuralResultRow> fromHistoryRows(const QVector<HistoryEntry>& rows,
                                             const QString& selectedAbsolutePath);
QVector<StructuralResultRow> fromSnapshotRows(const QVector<SnapshotRecord>& rows);
QVector<StructuralResultRow> fromDiffRows(const QVector<SnapshotDiffRow>& rows);
QVector<StructuralResultRow> fromReferenceRows(const QVector<QueryRow>& rows);
QVector<FileEntry> toFileEntries(const QVector<StructuralResultRow>& rows);
QStringList toDebugStrings(const QVector<StructuralResultRow>& rows);
}
