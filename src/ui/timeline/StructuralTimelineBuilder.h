#pragma once

#include <QVector>

#include "StructuralTimelineEvent.h"
#include "../model/StructuralResultRow.h"

struct StructuralTimelineSnapshotRows
{
    qint64 snapshotId = 0;
    QString timestamp;
    QVector<StructuralResultRow> rows;
};

namespace StructuralTimelineBuilder
{
QVector<StructuralTimelineEvent> build(const QVector<StructuralTimelineSnapshotRows>& snapshots);
}
