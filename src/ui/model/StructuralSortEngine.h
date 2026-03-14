#pragma once

#include <QVector>

#include "StructuralResultRow.h"

enum class StructuralSortField
{
    PrimaryPath,
    SecondaryPath,
    Category,
    Status,
    Timestamp,
    SnapshotId,
    Relationship,
    SizeBytes,
    Symbol,
    RankScore,
};

enum class StructuralSortDirection
{
    Ascending,
    Descending,
};

namespace StructuralSortEngine
{
bool defaultSortForCategory(StructuralResultCategory category,
                            StructuralSortField* fieldOut,
                            StructuralSortDirection* directionOut);
void sortRows(QVector<StructuralResultRow>* rows,
              StructuralSortField field,
              StructuralSortDirection direction);
}
