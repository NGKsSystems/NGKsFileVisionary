#pragma once

#include <QString>

#include "VisionIndexScheduler.h"

class MetaStore;

namespace VisionIndexEngine
{
struct VisionIndexWorkResult
{
    bool ok = false;
    bool canceled = false;
    QString errorText;
    qint64 sessionId = 0;
    qint64 scanVersion = 0;
    qint64 totalSeen = 0;
    qint64 totalInserted = 0;
    qint64 totalUpdated = 0;
};

class VisionIndexJournal;

class VisionIndexWorker
{
public:
    VisionIndexWorker(MetaStore& store, VisionIndexJournal& journal);

    bool run(const VisionIndexWorkItem& item,
             qint64 nextScanVersion,
             VisionIndexWorkResult* out,
             QString* errorText = nullptr);

private:
    MetaStore& m_store;
    VisionIndexJournal& m_journal;
};
}
