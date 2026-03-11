#include "QueryCore.h"

#include "FolderQueryService.h"
#include "SearchQueryService.h"
#include "SnapshotQueryService.h"
#include "core/db/MetaStore.h"

QueryCore::QueryCore(MetaStore& store)
    : m_folder(new FolderQueryService(store))
    , m_search(new SearchQueryService(store))
    , m_snapshot(new SnapshotQueryService(store))
{
}

QueryCore::~QueryCore()
{
    delete m_snapshot;
    m_snapshot = nullptr;

    delete m_search;
    m_search = nullptr;

    delete m_folder;
    m_folder = nullptr;
}

QueryResult QueryCore::queryChildren(const QString& parentPath, const QueryOptions& options) const
{
    return m_folder->queryChildren(parentPath, options);
}

QueryResult QueryCore::queryFlat(const QString& rootPath, const QueryOptions& options) const
{
    return m_folder->queryFlatDescendants(rootPath, options);
}

QueryResult QueryCore::querySubtree(const QString& rootPath, const QueryOptions& options) const
{
    return m_snapshot->querySubtree(rootPath, options);
}

QueryResult QueryCore::querySearch(const QString& rootPath, const QueryOptions& options) const
{
    return m_search->queryByFilter(rootPath, options);
}
