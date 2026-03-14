#pragma once

#include <QVector>
#include <QString>

#include "../model/StructuralResultRow.h"
#include "StructuralExportFormat.h"

namespace StructuralExportEngine
{
QString renderRows(const QVector<StructuralResultRow>& rows,
                   StructuralExportFormat format,
                   bool* hasGraphEdges = nullptr);

bool writeRowsToFile(const QVector<StructuralResultRow>& rows,
                     StructuralExportFormat format,
                     const QString& outputPath,
                     QString* errorText = nullptr,
                     bool* hasGraphEdges = nullptr);
}
