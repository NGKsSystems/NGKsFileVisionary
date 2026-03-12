#include "PathUtils.h"

#include <QDir>
#include <QFileInfo>

namespace
{
const QStringList kArchiveExtensions = {
    ".7z", ".zip", ".tar", ".tar.gz", ".tar.xz", ".tgz", ".gz", ".xz"
};

const QString kArchiveVirtualSeparator = QStringLiteral("::");
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

bool isArchiveVirtualPath(const QString& path)
{
    return path.contains(kArchiveVirtualSeparator);
}

QString buildArchiveVirtualPath(const QString& archivePath, const QString& internalPath)
{
    const QString archive = QDir::cleanPath(archivePath);
    const QString internal = normalizeInternalPath(internalPath);
    if (internal.isEmpty()) {
        return archive;
    }
    return archive + kArchiveVirtualSeparator + internal;
}

bool splitArchiveVirtualPath(const QString& path, QString* archivePath, QString* internalPath)
{
    const int sep = path.indexOf(kArchiveVirtualSeparator);
    if (sep <= 0) {
        return false;
    }

    const QString archive = QDir::cleanPath(path.left(sep));
    const QString internal = normalizeInternalPath(path.mid(sep + kArchiveVirtualSeparator.size()));
    if (archivePath) {
        *archivePath = archive;
    }
    if (internalPath) {
        *internalPath = internal;
    }
    return true;
}

QString archiveVirtualParentPath(const QString& path)
{
    QString archivePath;
    QString internalPath;
    if (!splitArchiveVirtualPath(path, &archivePath, &internalPath)) {
        return QString();
    }

    if (internalPath.isEmpty()) {
        return QFileInfo(archivePath).absolutePath();
    }

    const int slash = internalPath.lastIndexOf('/');
    if (slash < 0) {
        return archivePath;
    }

    return buildArchiveVirtualPath(archivePath, internalPath.left(slash));
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
