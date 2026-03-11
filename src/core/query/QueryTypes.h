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

    bool filesOnly = false;
    bool directoriesOnly = false;
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
QString sortFieldToString(QuerySortField value);
QStringList normalizedExtensionSet(const QString& extensionFilter);
bool applyFiltersAndSort(const QueryOptions& options, QVector<QueryRow>* rows, QString* errorText = nullptr);
}
