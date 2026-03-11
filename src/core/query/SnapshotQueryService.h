#pragma once

#include "QueryTypes.h"

class MetaStore;

class SnapshotQueryService
{
public:
    explicit SnapshotQueryService(MetaStore& store);

    QueryResult querySubtree(const QString& rootPath, const QueryOptions& options) const;

private:
    MetaStore& m_store;
};
