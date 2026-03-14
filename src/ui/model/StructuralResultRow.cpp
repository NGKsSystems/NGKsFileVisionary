#include "StructuralResultRow.h"

namespace StructuralResultRowUtil
{
QString categoryToString(StructuralResultCategory category)
{
    switch (category) {
    case StructuralResultCategory::History:
        return QStringLiteral("history");
    case StructuralResultCategory::Snapshot:
        return QStringLiteral("snapshot");
    case StructuralResultCategory::Diff:
        return QStringLiteral("diff");
    case StructuralResultCategory::Reference:
        return QStringLiteral("reference");
    }
    return QStringLiteral("history");
}

FileEntry toFileEntry(const StructuralResultRow& row)
{
    FileEntry entry;
    entry.name = QStringLiteral("category=%1 path=%2 status=%3 ts=%4")
                     .arg(categoryToString(row.category),
                          row.primaryPath,
                          row.status,
                          row.timestamp.isEmpty() ? QStringLiteral("null") : row.timestamp);
    entry.isDir = false;
    entry.size = row.hasSizeBytes && row.sizeBytes > 0 ? static_cast<quint64>(row.sizeBytes) : 0;
    entry.modified = QDateTime::fromString(row.timestamp, Qt::ISODate);
    entry.absolutePath = row.primaryPath;
    return entry;
}
}
