#include "QueryTypes.h"

#include <algorithm>

namespace {
QString normalizedSubstring(const QString& value)
{
    return value.trimmed().toLower();
}

bool rowPassesFilters(const QueryRow& row,
                      const QueryOptions& options,
                      const QString& substring,
                      const QStringList& extSet)
{
    if (!options.includeHidden && row.hiddenFlag) {
        return false;
    }
    if (!options.includeSystem && row.systemFlag) {
        return false;
    }
    if (options.filesOnly && row.isDir) {
        return false;
    }
    if (options.directoriesOnly && !row.isDir) {
        return false;
    }

    if (!substring.isEmpty()) {
        const QString hayName = row.normalizedName.toLower();
        const QString hayPath = row.path.toLower();
        if (!hayName.contains(substring) && !hayPath.contains(substring)) {
            return false;
        }
    }

    if (!extSet.isEmpty() && !row.isDir) {
        if (!extSet.contains(row.extension.toLower())) {
            return false;
        }
    }

    return true;
}

int compareRows(const QueryRow& a, const QueryRow& b, const QueryOptions& options)
{
    if (options.foldersFirst && a.isDir != b.isDir) {
        return a.isDir ? -1 : 1;
    }

    int primary = 0;
    switch (options.sortField) {
    case QuerySortField::Name:
        primary = QString::compare(a.normalizedName, b.normalizedName, Qt::CaseInsensitive);
        break;
    case QuerySortField::Modified:
        primary = QString::compare(a.modifiedUtc, b.modifiedUtc, Qt::CaseInsensitive);
        break;
    case QuerySortField::Size: {
        const qint64 as = a.hasSizeBytes ? a.sizeBytes : -1;
        const qint64 bs = b.hasSizeBytes ? b.sizeBytes : -1;
        if (as < bs) {
            primary = -1;
        } else if (as > bs) {
            primary = 1;
        } else {
            primary = 0;
        }
        break;
    }
    case QuerySortField::Path:
        primary = QString::compare(a.path, b.path, Qt::CaseInsensitive);
        break;
    }

    if (!options.ascending) {
        primary = -primary;
    }

    if (primary != 0) {
        return primary;
    }

    const int secondaryName = QString::compare(a.normalizedName, b.normalizedName, Qt::CaseInsensitive);
    if (secondaryName != 0) {
        return secondaryName;
    }
    return QString::compare(a.path, b.path, Qt::CaseInsensitive);
}
}

namespace QueryTypesUtil
{
bool parseSortField(const QString& value, QuerySortField* out)
{
    if (!out) {
        return false;
    }

    const QString normalized = value.trimmed().toLower();
    if (normalized.isEmpty() || normalized == QStringLiteral("name")) {
        *out = QuerySortField::Name;
        return true;
    }
    if (normalized == QStringLiteral("modified")) {
        *out = QuerySortField::Modified;
        return true;
    }
    if (normalized == QStringLiteral("size")) {
        *out = QuerySortField::Size;
        return true;
    }
    if (normalized == QStringLiteral("path")) {
        *out = QuerySortField::Path;
        return true;
    }
    return false;
}

QString sortFieldToString(QuerySortField value)
{
    switch (value) {
    case QuerySortField::Name:
        return QStringLiteral("name");
    case QuerySortField::Modified:
        return QStringLiteral("modified");
    case QuerySortField::Size:
        return QStringLiteral("size");
    case QuerySortField::Path:
        return QStringLiteral("path");
    }
    return QStringLiteral("name");
}

QStringList normalizedExtensionSet(const QString& extensionFilter)
{
    QStringList out;
    const QStringList pieces = extensionFilter.split(';', Qt::SkipEmptyParts);
    for (QString ext : pieces) {
        ext = ext.trimmed().toLower();
        if (ext.isEmpty()) {
            continue;
        }
        if (!ext.startsWith('.')) {
            ext.prepend('.');
        }
        if (!out.contains(ext)) {
            out.push_back(ext);
        }
    }
    return out;
}

bool applyFiltersAndSort(const QueryOptions& options, QVector<QueryRow>* rows, QString* errorText)
{
    if (!rows) {
        if (errorText) {
            *errorText = QStringLiteral("null_rows");
        }
        return false;
    }

    if (options.filesOnly && options.directoriesOnly) {
        if (errorText) {
            *errorText = QStringLiteral("conflicting_file_dir_filters");
        }
        return false;
    }

    const QString substring = normalizedSubstring(options.substringFilter);
    const QStringList extSet = normalizedExtensionSet(options.extensionFilter);

    QVector<QueryRow> filtered;
    filtered.reserve(rows->size());
    for (const QueryRow& row : *rows) {
        if (rowPassesFilters(row, options, substring, extSet)) {
            filtered.push_back(row);
        }
    }

    std::stable_sort(filtered.begin(), filtered.end(), [&](const QueryRow& a, const QueryRow& b) {
        return compareRows(a, b, options) < 0;
    });

    *rows = filtered;
    return true;
}
}
