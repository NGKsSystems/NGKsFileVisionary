#include "PathUtils.h"

#include <QFileInfo>

namespace
{
const QStringList kArchiveExtensions = {
    ".7z", ".zip", ".tar", ".tar.gz", ".tar.xz", ".tgz", ".gz", ".xz"
};
}

namespace PathUtils
{
QString normalizeInternalPath(const QString& path)
{
    QString normalized = path;
    normalized.replace('\\', '/');
    while (normalized.contains("//")) {
        normalized.replace("//", "/");
    }
    if (normalized.startsWith('/')) {
        normalized.remove(0, 1);
    }
    return normalized;
}

bool isArchivePath(const QString& path)
{
    const QString lower = QFileInfo(path).fileName().toLower();
    for (const QString& ext : kArchiveExtensions) {
        if (lower.endsWith(ext)) {
            return true;
        }
    }
    return false;
}

QStringList splitExtensionsFilter(const QString& rawFilter)
{
    QStringList values = rawFilter.split(';', Qt::SkipEmptyParts);
    for (QString& value : values) {
        value = value.trimmed().toLower();
        if (!value.isEmpty() && !value.startsWith('.')) {
            value.prepend('.');
        }
    }
    values.removeAll(QString());
    return values;
}
}
