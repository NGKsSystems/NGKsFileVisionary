#include "StructuralExportFormat.h"

namespace StructuralExportFormatUtil
{
QString toToken(StructuralExportFormat format)
{
    switch (format) {
    case StructuralExportFormat::Json:
        return QStringLiteral("json");
    case StructuralExportFormat::Csv:
        return QStringLiteral("csv");
    case StructuralExportFormat::Markdown:
        return QStringLiteral("markdown");
    case StructuralExportFormat::GraphViz:
        return QStringLiteral("dot");
    }
    return QStringLiteral("json");
}

bool fromToken(const QString& token, StructuralExportFormat* outFormat)
{
    const QString normalized = token.trimmed().toLower();
    if (normalized == QStringLiteral("json")) {
        if (outFormat) {
            *outFormat = StructuralExportFormat::Json;
        }
        return true;
    }
    if (normalized == QStringLiteral("csv")) {
        if (outFormat) {
            *outFormat = StructuralExportFormat::Csv;
        }
        return true;
    }
    if (normalized == QStringLiteral("markdown") || normalized == QStringLiteral("md")) {
        if (outFormat) {
            *outFormat = StructuralExportFormat::Markdown;
        }
        return true;
    }
    if (normalized == QStringLiteral("dot") || normalized == QStringLiteral("graphviz")) {
        if (outFormat) {
            *outFormat = StructuralExportFormat::GraphViz;
        }
        return true;
    }
    return false;
}

QString defaultExtension(StructuralExportFormat format)
{
    switch (format) {
    case StructuralExportFormat::Json:
        return QStringLiteral(".json");
    case StructuralExportFormat::Csv:
        return QStringLiteral(".csv");
    case StructuralExportFormat::Markdown:
        return QStringLiteral(".md");
    case StructuralExportFormat::GraphViz:
        return QStringLiteral(".dot");
    }
    return QStringLiteral(".txt");
}

QString defaultFilter(StructuralExportFormat format)
{
    switch (format) {
    case StructuralExportFormat::Json:
        return QStringLiteral("JSON files (*.json)");
    case StructuralExportFormat::Csv:
        return QStringLiteral("CSV files (*.csv)");
    case StructuralExportFormat::Markdown:
        return QStringLiteral("Markdown files (*.md)");
    case StructuralExportFormat::GraphViz:
        return QStringLiteral("GraphViz files (*.dot)");
    }
    return QStringLiteral("All files (*.*)");
}
}
