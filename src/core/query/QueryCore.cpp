#include "QueryCore.h"

#include "FolderQueryService.h"
#include "SearchQueryService.h"
#include "SnapshotQueryService.h"
#include "core/db/MetaStore.h"
#include "core/perf/ResultLimiter.h"

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
    const QueryResult raw = m_folder->queryChildren(parentPath, options);
    ResultLimiter limiter;
    return limiter.limit(raw);
}

QueryResult QueryCore::queryFlat(const QString& rootPath, const QueryOptions& options) const
{
    const QueryResult raw = m_folder->queryFlatDescendants(rootPath, options);
    ResultLimiter limiter;
    return limiter.limit(raw);
}

QueryResult QueryCore::querySubtree(const QString& rootPath, const QueryOptions& options) const
{
    const QueryResult raw = m_snapshot->querySubtree(rootPath, options);
    ResultLimiter limiter;
    return limiter.limit(raw);
}

QueryResult QueryCore::querySearch(const QString& rootPath, const QueryOptions& options) const
{
    const QueryResult raw = m_search->queryByFilter(rootPath, options);
    ResultLimiter limiter;
    return limiter.limit(raw);
}
