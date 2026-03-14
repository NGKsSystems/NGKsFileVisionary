#pragma once

#include <QString>

enum class StructuralExportFormat
{
    Json,
    Csv,
    Markdown,
    GraphViz,
};

namespace StructuralExportFormatUtil
{
QString toToken(StructuralExportFormat format);
bool fromToken(const QString& token, StructuralExportFormat* outFormat);
QString defaultExtension(StructuralExportFormat format);
QString defaultFilter(StructuralExportFormat format);
}
