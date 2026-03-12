#include "SnapshotTypes.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace SnapshotTypesUtil
{
QString optionsToJson(const SnapshotCreateOptions& options)
{
    QJsonObject o;
    o.insert(QStringLiteral("includeHidden"), options.includeHidden);
    o.insert(QStringLiteral("includeSystem"), options.includeSystem);
    o.insert(QStringLiteral("maxDepth"), options.maxDepth);
    o.insert(QStringLiteral("filesOnly"), options.filesOnly);
    o.insert(QStringLiteral("directoriesOnly"), options.directoriesOnly);
    o.insert(QStringLiteral("snapshotType"), normalizedSnapshotType(options.snapshotType));
    if (!options.noteText.trimmed().isEmpty()) {
        o.insert(QStringLiteral("noteText"), options.noteText.trimmed());
    }
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString normalizedSnapshotType(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized.isEmpty()) {
        return QStringLiteral("structural_full");
    }
    return normalized;
}

bool isSupportedSnapshotType(const QString& value)
{
    const QString normalized = normalizedSnapshotType(value);
    return normalized == QStringLiteral("structural_full")
        || normalized == QStringLiteral("structural_filtered");
}
}
