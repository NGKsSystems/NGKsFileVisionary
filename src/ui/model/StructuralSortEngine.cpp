#include "StructuralSortEngine.h"

#include <algorithm>

namespace {
int statusPriority(const QString& status)
{
    const QString normalized = status.trimmed().toLower();
    if (normalized == QStringLiteral("added")) {
        return 0;
    }
    if (normalized == QStringLiteral("changed")) {
        return 1;
    }
    if (normalized == QStringLiteral("removed")) {
        return 2;
    }
    if (normalized == QStringLiteral("unchanged")) {
        return 3;
    }
    if (normalized == QStringLiteral("absent")) {
        return 4;
    }
    return 5;
}

int compareByField(const StructuralResultRow& a,
                   const StructuralResultRow& b,
                   StructuralSortField field)
{
    switch (field) {
    case StructuralSortField::PrimaryPath:
        return QString::compare(a.primaryPath, b.primaryPath, Qt::CaseInsensitive);
    case StructuralSortField::SecondaryPath:
        return QString::compare(a.secondaryPath, b.secondaryPath, Qt::CaseInsensitive);
    case StructuralSortField::Category:
        return QString::compare(StructuralResultRowUtil::categoryToString(a.category),
                                StructuralResultRowUtil::categoryToString(b.category),
                                Qt::CaseInsensitive);
    case StructuralSortField::Status: {
        const int pa = statusPriority(a.status);
        const int pb = statusPriority(b.status);
        if (pa != pb) {
            return pa < pb ? -1 : 1;
        }
        return QString::compare(a.status, b.status, Qt::CaseInsensitive);
    }
    case StructuralSortField::Timestamp:
        return QString::compare(a.timestamp, b.timestamp, Qt::CaseInsensitive);
    case StructuralSortField::SnapshotId: {
        const qint64 va = a.hasSnapshotId ? a.snapshotId : -1;
        const qint64 vb = b.hasSnapshotId ? b.snapshotId : -1;
        if (va == vb) {
            return 0;
        }
        return va < vb ? -1 : 1;
    }
    case StructuralSortField::Relationship:
        return QString::compare(a.relationship, b.relationship, Qt::CaseInsensitive);
    case StructuralSortField::SizeBytes: {
        const qint64 va = a.hasSizeBytes ? a.sizeBytes : -1;
        const qint64 vb = b.hasSizeBytes ? b.sizeBytes : -1;
        if (va == vb) {
            return 0;
        }
        return va < vb ? -1 : 1;
    }
    case StructuralSortField::Symbol:
        return QString::compare(a.symbol, b.symbol, Qt::CaseInsensitive);
    case StructuralSortField::RankScore:
        if (a.rankScore == b.rankScore) {
            return 0;
        }
        return a.rankScore < b.rankScore ? -1 : 1;
    }

    return 0;
}

bool lessForDirection(const StructuralResultRow& a,
                      const StructuralResultRow& b,
                      StructuralSortField field,
                      StructuralSortDirection direction)
{
    const int cmp = compareByField(a, b, field);
    if (cmp != 0) {
        return direction == StructuralSortDirection::Ascending ? (cmp < 0) : (cmp > 0);
    }

    // Stable deterministic tie-breakers.
    const int tiePath = QString::compare(a.primaryPath, b.primaryPath, Qt::CaseInsensitive);
    if (tiePath != 0) {
        return tiePath < 0;
    }
    const int tieSecondary = QString::compare(a.secondaryPath, b.secondaryPath, Qt::CaseInsensitive);
    if (tieSecondary != 0) {
        return tieSecondary < 0;
    }
    const int tieTimestamp = QString::compare(a.timestamp, b.timestamp, Qt::CaseInsensitive);
    if (tieTimestamp != 0) {
        return tieTimestamp < 0;
    }
    return QString::compare(a.symbol, b.symbol, Qt::CaseInsensitive) < 0;
}
}

namespace StructuralSortEngine
{
bool defaultSortForCategory(StructuralResultCategory category,
                            StructuralSortField* fieldOut,
                            StructuralSortDirection* directionOut)
{
    if (!fieldOut || !directionOut) {
        return false;
    }

    switch (category) {
    case StructuralResultCategory::History:
        *fieldOut = StructuralSortField::Timestamp;
        *directionOut = StructuralSortDirection::Descending;
        return true;
    case StructuralResultCategory::Snapshot:
        *fieldOut = StructuralSortField::SnapshotId;
        *directionOut = StructuralSortDirection::Descending;
        return true;
    case StructuralResultCategory::Diff:
        *fieldOut = StructuralSortField::Status;
        *directionOut = StructuralSortDirection::Ascending;
        return true;
    case StructuralResultCategory::Reference:
        *fieldOut = StructuralSortField::PrimaryPath;
        *directionOut = StructuralSortDirection::Ascending;
        return true;
    }

    return false;
}

void sortRows(QVector<StructuralResultRow>* rows,
              StructuralSortField field,
              StructuralSortDirection direction)
{
    if (!rows || rows->size() <= 1) {
        return;
    }

    std::stable_sort(rows->begin(),
                     rows->end(),
                     [field, direction](const StructuralResultRow& a, const StructuralResultRow& b) {
                         return lessForDirection(a, b, field, direction);
                     });
}
}
