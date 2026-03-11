#include "FolderQueryService.h"

#include "core/db/MetaStore.h"

namespace {
QueryRow toRow(const EntryRecord& e)
{
    QueryRow row;
    row.id = e.id;
    row.parentId = e.parentId;
    row.hasParentId = e.hasParentId;
    row.path = e.path;
    row.name = e.name;
    row.normalizedName = e.normalizedName;
    row.extension = e.extension;
    row.isDir = e.isDir;
    row.sizeBytes = e.sizeBytes;
    row.hasSizeBytes = e.hasSizeBytes;
    row.modifiedUtc = e.modifiedUtc;
    row.hiddenFlag = e.hiddenFlag;
    row.systemFlag = e.systemFlag;
    row.archiveFlag = e.archiveFlag;
    row.existsFlag = e.existsFlag;
    return row;
}
}

FolderQueryService::FolderQueryService(MetaStore& store)
    : m_store(store)
{
}

QueryResult FolderQueryService::queryChildren(const QString& parentPath, const QueryOptions& options) const
{
    QVector<EntryRecord> records;
    QString errorText;
    if (!m_store.findChildrenByParentPath(parentPath, &records, &errorText)) {
        return makeError(errorText);
    }

    QueryResult result;
    result.ok = true;
    result.rows.reserve(records.size());
    for (const EntryRecord& r : records) {
        result.rows.push_back(toRow(r));
    }

    if (!QueryTypesUtil::applyFiltersAndSort(options, &result.rows, &errorText)) {
        return makeError(errorText);
    }

    result.totalCount = result.rows.size();
    return result;
}

QueryResult FolderQueryService::queryFlatDescendants(const QString& rootPath, const QueryOptions& options) const
{
    QVector<EntryRecord> records;
    QString errorText;
    if (!m_store.findFlatDescendantsByRootPath(rootPath, options.maxDepth, &records, &errorText)) {
        return makeError(errorText);
    }

    QueryResult result;
    result.ok = true;
    result.rows.reserve(records.size());
    for (const EntryRecord& r : records) {
        result.rows.push_back(toRow(r));
    }

    if (!QueryTypesUtil::applyFiltersAndSort(options, &result.rows, &errorText)) {
        return makeError(errorText);
    }

    result.totalCount = result.rows.size();
    return result;
}

QueryResult FolderQueryService::makeError(const QString& error) const
{
    QueryResult result;
    result.ok = false;
    result.errorText = error;
    return result;
}
