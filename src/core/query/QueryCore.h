#pragma once

#include "QueryTypes.h"

class MetaStore;
class FolderQueryService;
class SearchQueryService;
class SnapshotQueryService;

class QueryCore
{
public:
    explicit QueryCore(MetaStore& store);
    ~QueryCore();

    QueryResult queryChildren(const QString& parentPath, const QueryOptions& options) const;
    QueryResult queryFlat(const QString& rootPath, const QueryOptions& options) const;
    QueryResult querySubtree(const QString& rootPath, const QueryOptions& options) const;
    QueryResult querySearch(const QString& rootPath, const QueryOptions& options) const;

private:
    FolderQueryService* m_folder;
    SearchQueryService* m_search;
    SnapshotQueryService* m_snapshot;
};
