#pragma once

#include <QVector>

#include "StructuralFilterState.h"
#include "StructuralResultRow.h"

namespace StructuralFilterEngine
{
QVector<StructuralResultRow> apply(const QVector<StructuralResultRow>& rows, const StructuralFilterState& state);
QString extensionForPath(const QString& path);
}
