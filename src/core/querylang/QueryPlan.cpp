#include "QueryPlan.h"

#include <QDir>
#include <QFileInfo>

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

    if (QFileInfo(normalizedUnder).isAbsolute()) {
        return normalizedUnder;
    }

    return QDir::fromNativeSeparators(QDir::cleanPath(QDir(normalizedRuntime).filePath(normalizedUnder)));
}
