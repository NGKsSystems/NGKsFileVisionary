#pragma once

#include <functional>

#include "ScanTask.h"

class MetaStore;

class ScanCoordinator
{
public:
    explicit ScanCoordinator(MetaStore& store);

    struct Callbacks
    {
        std::function<void(const QString&)> onLog;
        std::function<void(const ScanCoordinatorResult&)> onProgress;
    };

    bool runScan(const ScanTask& task, const Callbacks& callbacks, ScanCoordinatorResult* outResult);

private:
    bool ingestBatch(const QVector<ScanIngestItem>& batch,
                     ScanCoordinatorResult* state,
                     QString* errorText);
    bool reconcileRemovedEntries(const QString& rootPath,
                                 qint64 scanSessionId,
                                 ScanCoordinatorResult* state,
                                 QString* errorText);
    QString buildVolumeKey(const QString& rootPath) const;

private:
    MetaStore& m_store;
};
