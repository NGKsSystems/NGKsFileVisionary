#include "QueryTypes.h"

#include <algorithm>
#include <QDateTime>
#include <QDir>

namespace {
bool compareNumeric(QueryComparator comparator, qint64 left, qint64 right)
{
    switch (comparator) {
    case QueryComparator::None:
        return true;
    case QueryComparator::Less:
        return left < right;
    case QueryComparator::LessEqual:
        return left <= right;
    case QueryComparator::Greater:
        return left > right;
    case QueryComparator::GreaterEqual:
        return left >= right;
    }
    return false;
}

QString normalizedPathLike(const QString& value)
{
    return QDir::fromNativeSeparators(value).toLower();
}

bool containsAnySubstring(const QString& haystackName,
                          const QString& haystackPath,
                          const QStringList& needles)
{
    for (const QString& needleRaw : needles) {
        const QString needle = needleRaw.trimmed().toLower();
        if (needle.isEmpty()) {
            continue;
        }
        if (haystackName.contains(needle) || haystackPath.contains(needle)) {
            return true;
        }
    }
    return false;
}
}

namespace {
QString normalizedSubstring(const QString& value)
{
    return value.trimmed().toLower();
}

bool rowPassesFilters(const QueryRow& row,
                      const QueryOptions& options,
                      const QString& substring,
                      const QStringList& extSet,
                      const QStringList& excludedExtSet)
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

    if (!options.substringAlternatives.isEmpty()) {
        const QString hayName = row.normalizedName.toLower();
        const QString hayPath = row.path.toLower();
        if (!containsAnySubstring(hayName, hayPath, options.substringAlternatives)) {
            return false;
        }
    }

    if (!extSet.isEmpty() && !row.isDir) {
        if (!extSet.contains(row.extension.toLower())) {
            return false;
        }
    }

    if (!excludedExtSet.isEmpty() && !row.isDir) {
        if (excludedExtSet.contains(row.extension.toLower())) {
            return false;
        }
    }

    if (!options.excludedSubstrings.isEmpty()) {
        const QString hayName = row.normalizedName.toLower();
        const QString hayPath = row.path.toLower();
        if (containsAnySubstring(hayName, hayPath, options.excludedSubstrings)) {
            return false;
        }
    }

    if (!options.excludedPathPrefixes.isEmpty()) {
        const QString rowPath = normalizedPathLike(row.path);
        for (QString prefix : options.excludedPathPrefixes) {
            prefix = normalizedPathLike(prefix);
            if (prefix.isEmpty()) {
                continue;
            }
            if (rowPath == prefix || rowPath.startsWith(prefix + QStringLiteral("/"))) {
                return false;
            }
        }
    }

    if (options.sizeComparator != QueryComparator::None) {
        if (!row.hasSizeBytes || row.isDir) {
            return false;
        }
        if (!compareNumeric(options.sizeComparator, row.sizeBytes, options.sizeBytes)) {
            return false;
        }
    }

    if (options.modifiedAgeComparator != QueryComparator::None) {
        const QDateTime modified = QDateTime::fromString(row.modifiedUtc, Qt::ISODate);
        if (!modified.isValid()) {
            return false;
        }
        const qint64 ageSeconds = modified.secsTo(QDateTime::currentDateTimeUtc());
        if (!compareNumeric(options.modifiedAgeComparator, ageSeconds, options.modifiedAgeSeconds)) {
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

bool parseComparator(const QString& value, QueryComparator* out)
{
    if (!out) {
        return false;
    }

    const QString normalized = value.trimmed();
    if (normalized == QStringLiteral("<")) {
        *out = QueryComparator::Less;
        return true;
    }
    if (normalized == QStringLiteral("<=")) {
        *out = QueryComparator::LessEqual;
        return true;
    }
    if (normalized == QStringLiteral(">")) {
        *out = QueryComparator::Greater;
        return true;
    }
    if (normalized == QStringLiteral(">=")) {
        *out = QueryComparator::GreaterEqual;
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

QString comparatorToString(QueryComparator value)
{
    switch (value) {
    case QueryComparator::None:
        return QStringLiteral("none");
    case QueryComparator::Less:
        return QStringLiteral("<");
    case QueryComparator::LessEqual:
        return QStringLiteral("<=");
    case QueryComparator::Greater:
        return QStringLiteral(">");
    case QueryComparator::GreaterEqual:
        return QStringLiteral(">=");
    }
    return QStringLiteral("none");
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
    const QStringList excludedExtSet = normalizedExtensionSet(options.excludedExtensions.join(QStringLiteral(";")));

    QVector<QueryRow> filtered;
    filtered.reserve(rows->size());
    for (const QueryRow& row : *rows) {
        if (rowPassesFilters(row, options, substring, extSet, excludedExtSet)) {
            filtered.push_back(row);
        }
    }

    std::stable_sort(filtered.begin(), filtered.end(), [&](const QueryRow& a, const QueryRow& b) {
        return compareRows(a, b, options) < 0;
    });

    if (options.pageOffset > 0 || options.pageSize > 0) {
        const int start = qBound(0, options.pageOffset, filtered.size());
        int end = filtered.size();
        if (options.pageSize > 0) {
            end = qMin(filtered.size(), start + options.pageSize);
        }

        QVector<QueryRow> paged;
        paged.reserve(qMax(0, end - start));
        for (int i = start; i < end; ++i) {
            paged.push_back(filtered.at(i));
        }
        filtered = paged;
    }

    *rows = filtered;
    return true;
}
}
