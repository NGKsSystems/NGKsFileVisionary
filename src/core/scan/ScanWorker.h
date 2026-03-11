#pragma once

#include <functional>

#include "ScanTask.h"

class ScanWorker
{
public:
    struct Callbacks
    {
        std::function<void(const QVector<ScanIngestItem>&)> onBatch;
        std::function<void(const ScanWorkerProgress&)> onProgress;
        std::function<void(const QString&)> onLog;
    };

    struct Result
    {
        bool success = false;
        bool canceled = false;
        QString errorText;
        ScanWorkerProgress progress;
    };

    Result run(const ScanTask& task, const Callbacks& callbacks) const;
};
