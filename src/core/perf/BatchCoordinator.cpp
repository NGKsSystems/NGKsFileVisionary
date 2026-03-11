#include "BatchCoordinator.h"

BatchCoordinator::BatchCoordinator()
    : m_directoryBatchSize(500)
    , m_queryPageSize(500)
    , m_scanBatchSize(1000)
{
}

int BatchCoordinator::directoryBatchSize() const
{
    return m_directoryBatchSize;
}

int BatchCoordinator::queryPageSize() const
{
    return m_queryPageSize;
}

int BatchCoordinator::scanBatchSize() const
{
    return m_scanBatchSize;
}
