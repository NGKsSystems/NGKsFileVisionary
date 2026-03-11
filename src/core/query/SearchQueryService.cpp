#include "SearchQueryService.h"

#include "FolderQueryService.h"

SearchQueryService::SearchQueryService(MetaStore& store)
    : m_store(store)
{
}

QueryResult SearchQueryService::queryByFilter(const QString& rootPath, const QueryOptions& options) const
{
    FolderQueryService folder(m_store);
    return folder.queryFlatDescendants(rootPath, options);
}
