#include "QueryPlan.h"

#include <QDir>
#include <QFileInfo>

#include "util/PathUtils.h"

namespace {
bool splitArchiveScopedPath(const QString& normalizedPath, QString* archivePath, QString* internalPath)
{
    if (PathUtils::isArchiveVirtualPath(normalizedPath)) {
        return PathUtils::splitArchiveVirtualPath(normalizedPath, archivePath, internalPath);
    }

    const QString path = QDir::fromNativeSeparators(normalizedPath);
    if (PathUtils::isArchivePath(path)) {
        if (archivePath) {
            *archivePath = QDir::cleanPath(path);
        }
        if (internalPath) {
            internalPath->clear();
        }
        return true;
    }

    for (int i = 0; i < path.size(); ++i) {
        if (path.at(i) != QLatin1Char('/')) {
            continue;
        }

        const QString prefix = path.left(i);
        if (!PathUtils::isArchivePath(prefix)) {
            continue;
        }

        if (archivePath) {
            *archivePath = QDir::cleanPath(prefix);
        }
        if (internalPath) {
            *internalPath = PathUtils::normalizeInternalPath(path.mid(i + 1));
        }
        return true;
    }

    return false;
}
}

QueryOptions QueryPlan::toQueryOptions() const
{
    QueryOptions options;
    options.includeHidden = includeHidden;
    options.includeSystem = includeSystem;
    options.filesOnly = filesOnly;
    options.directoriesOnly = directoriesOnly;
    options.sortField = sortField;
    options.ascending = ascending;
    options.substringFilter = nameContains.trimmed();
    if (!extensions.isEmpty()) {
        options.extensionFilter = extensions.join(QStringLiteral(";"));
    }
    return options;
}

QString QueryPlan::resolveRootPath(const QString& runtimeRoot) const
{
    const QString normalizedRuntime = QDir::fromNativeSeparators(QDir::cleanPath(runtimeRoot));
    const QString normalizedUnder = QDir::fromNativeSeparators(QDir::cleanPath(underPath));
    if (normalizedUnder.trimmed().isEmpty()) {
        return normalizedRuntime;
    }

    QString resolvedPath = normalizedUnder;
    if (!QFileInfo(normalizedUnder).isAbsolute()) {
        resolvedPath = QDir::fromNativeSeparators(QDir::cleanPath(QDir(normalizedRuntime).filePath(normalizedUnder)));
    }

    QString archivePath;
    QString internalPath;
    if (splitArchiveScopedPath(resolvedPath, &archivePath, &internalPath)) {
        return PathUtils::buildArchiveVirtualPath(archivePath, internalPath);
    }

    return resolvedPath;
}
