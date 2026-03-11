#include "SnapshotQueryService.h"

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

SnapshotQueryService::SnapshotQueryService(MetaStore& store)
    : m_store(store)
{
}

QueryResult SnapshotQueryService::querySubtree(const QString& rootPath, const QueryOptions& options) const
{
    QVector<EntryRecord> records;
    QString errorText;
    if (!m_store.findSubtreeByRootPath(rootPath, options.maxDepth, &records, &errorText)) {
        QueryResult failed;
        failed.ok = false;
        failed.errorText = errorText;
        return failed;
    }

    QueryResult result;
    result.ok = true;
    result.rows.reserve(records.size());
    for (const EntryRecord& r : records) {
        result.rows.push_back(toRow(r));
    }

    if (!QueryTypesUtil::applyFiltersAndSort(options, &result.rows, &errorText)) {
        QueryResult failed;
        failed.ok = false;
        failed.errorText = errorText;
        return failed;
    }

    result.totalCount = result.rows.size();
    return result;
}
