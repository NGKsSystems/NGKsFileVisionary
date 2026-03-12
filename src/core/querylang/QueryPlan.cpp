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

QueryOptions QueryPlan::toQueryOptions(const QString& runtimeRoot) const
{
    QueryOptions options;
    options.includeHidden = includeHidden;
    options.includeSystem = includeSystem;
    options.filesOnly = filesOnly;
    options.directoriesOnly = directoriesOnly;
    options.sortField = sortField;
    options.ascending = ascending;
    options.substringFilter = nameContains.trimmed();
    options.substringAlternatives = nameContainsAny;
    options.excludedSubstrings = excludedNameContains;
    options.excludedExtensions = excludedExtensions;
    options.sizeComparator = sizeComparator;
    options.sizeBytes = sizeBytes;
    options.modifiedAgeComparator = modifiedAgeComparator;
    options.modifiedAgeSeconds = modifiedAgeSeconds;
    if (!extensions.isEmpty()) {
        options.extensionFilter = extensions.join(QStringLiteral(";"));
    }

    const QString normalizedRuntime = QDir::fromNativeSeparators(QDir::cleanPath(runtimeRoot));
    for (const QString& rawUnder : excludedUnderPaths) {
        const QString normalizedUnder = QDir::fromNativeSeparators(QDir::cleanPath(rawUnder));
        if (normalizedUnder.trimmed().isEmpty()) {
            continue;
        }

        QString resolvedPath = normalizedUnder;
        if (!QFileInfo(normalizedUnder).isAbsolute()) {
            resolvedPath = QDir::fromNativeSeparators(QDir::cleanPath(QDir(normalizedRuntime).filePath(normalizedUnder)));
        }

        QString archivePath;
        QString internalPath;
        if (splitArchiveScopedPath(resolvedPath, &archivePath, &internalPath)) {
            options.excludedPathPrefixes.push_back(PathUtils::buildArchiveVirtualPath(archivePath, internalPath));
        } else {
            options.excludedPathPrefixes.push_back(resolvedPath);
        }
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
