#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

enum class QuerySortField
{
    Name = 0,
    Modified = 1,
    Size = 2,
    Path = 3,
};

enum class QueryComparator
{
    None = 0,
    Less = 1,
    LessEqual = 2,
    Greater = 3,
    GreaterEqual = 4,
};

enum class QueryGraphMode
{
    None = 0,
    References = 1,
    UsedBy = 2,
};

struct QueryOptions
{
    bool includeHidden = false;
    bool includeSystem = false;
    bool foldersFirst = true;

    QString extensionFilter;
    QString substringFilter;

    QuerySortField sortField = QuerySortField::Name;
    bool ascending = true;
    int maxDepth = -1;
    int pageSize = 0;
    int pageOffset = 0;

    bool filesOnly = false;
    bool directoriesOnly = false;

    QueryComparator sizeComparator = QueryComparator::None;
    qint64 sizeBytes = 0;

    QueryComparator modifiedAgeComparator = QueryComparator::None;
    qint64 modifiedAgeSeconds = 0;

    QStringList excludedExtensions;
    QStringList excludedPathPrefixes;
    QStringList excludedSubstrings;
    QStringList substringAlternatives;
};

struct QueryRow
{
    qint64 id = 0;
    qint64 parentId = 0;
    bool hasParentId = false;

    QString path;
    QString name;
    QString normalizedName;
    QString extension;

    bool isDir = false;
    qint64 sizeBytes = 0;
    bool hasSizeBytes = false;
    QString modifiedUtc;

    bool hiddenFlag = false;
    bool systemFlag = false;
    bool archiveFlag = false;
    bool existsFlag = true;

    bool hasGraphEdge = false;
    QString graphSourcePath;
    QString graphTargetPath;
    QString graphReferenceType;
    bool graphResolvedFlag = false;
    QString graphConfidence;
    int graphSourceLine = 0;

    int depth = -1;
};

struct QueryResult
{
    bool ok = false;
    QString errorText;
    QVector<QueryRow> rows;
    qint64 totalCount = 0;
};

namespace QueryTypesUtil
{
bool parseSortField(const QString& value, QuerySortField* out);
bool parseComparator(const QString& value, QueryComparator* out);
QString sortFieldToString(QuerySortField value);
QString comparatorToString(QueryComparator value);
QStringList normalizedExtensionSet(const QString& extensionFilter);
bool applyFiltersAndSort(const QueryOptions& options, QVector<QueryRow>* rows, QString* errorText = nullptr);
}
