#pragma once

#include "QueryTypes.h"

class MetaStore;

class FolderQueryService
{
public:
    explicit FolderQueryService(MetaStore& store);

    QueryResult queryChildren(const QString& parentPath, const QueryOptions& options) const;
    QueryResult queryFlatDescendants(const QString& rootPath, const QueryOptions& options) const;

private:
    QueryResult makeError(const QString& error) const;

private:
    MetaStore& m_store;
};
