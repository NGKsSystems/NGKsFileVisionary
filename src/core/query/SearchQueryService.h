#pragma once

#include "QueryTypes.h"

class MetaStore;

class SearchQueryService
{
public:
    explicit SearchQueryService(MetaStore& store);

    QueryResult queryByFilter(const QString& rootPath, const QueryOptions& options) const;

private:
    MetaStore& m_store;
};
