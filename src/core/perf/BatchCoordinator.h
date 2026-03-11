#pragma once

class BatchCoordinator
{
public:
    BatchCoordinator();

    int directoryBatchSize() const;
    int queryPageSize() const;
    int scanBatchSize() const;

private:
    int m_directoryBatchSize;
    int m_queryPageSize;
    int m_scanBatchSize;
};
