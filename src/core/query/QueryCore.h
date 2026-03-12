#pragma once

#include "QueryTypes.h"

class MetaStore;
class FolderQueryService;
class SearchQueryService;
class SnapshotQueryService;
namespace ReferenceGraph {
class ReferenceGraphRepository;
}

class QueryCore
{
public:
    explicit QueryCore(MetaStore& store);
    ~QueryCore();

    QueryResult queryChildren(const QString& parentPath, const QueryOptions& options) const;
    QueryResult queryFlat(const QString& rootPath, const QueryOptions& options) const;
    QueryResult querySubtree(const QString& rootPath, const QueryOptions& options) const;
    QueryResult querySearch(const QString& rootPath, const QueryOptions& options) const;
    QueryResult queryGraph(const QString& rootPath,
                           QueryGraphMode mode,
                           const QString& graphTarget,
                           const QueryOptions& options) const;

private:
    MetaStore& m_store;
    FolderQueryService* m_folder;
    SearchQueryService* m_search;
    SnapshotQueryService* m_snapshot;
    ReferenceGraph::ReferenceGraphRepository* m_reference;
};
