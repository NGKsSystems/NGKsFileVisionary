#pragma once

#include <QString>
#include <QVector>

#include "VisionIndexScheduler.h"
#include "VisionIndexStats.h"
#include "core/db/DbTypes.h"
#include "core/query/QueryTypes.h"

class MetaStore;
class QueryCore;

namespace VisionIndexEngine
{
class VisionIndexJournal;
class VisionIndexWorker;

struct VisionIndexRunSummary
{
    bool ok = false;
    QString errorText;
    qint64 scanVersion = 0;
    qint64 sessionId = 0;
    qint64 inserted = 0;
    qint64 updated = 0;
    qint64 seen = 0;
    bool deduped = false;
    QString dedupeReason;
};

class VisionIndexService
{
public:
    VisionIndexService();
    ~VisionIndexService();

    bool openIndex(const QString& dbPath, QString* errorText = nullptr, QString* migrationLog = nullptr);
    void closeIndex();

    bool isReady() const;
    QString dbPath() const;

    bool ensureIndexRoot(const QString& rootPath, QString* errorText = nullptr);
    VisionIndexRunSummary scheduleScan(const QString& rootPath,
                                       bool visiblePriority,
                                       const QString& mode,
                                       bool force,
                                       QString* errorText = nullptr);
    VisionIndexRunSummary runScheduled(QString* errorText = nullptr);

    VisionIndexRunSummary runInitialIndex(const QString& rootPath, QString* errorText = nullptr);
    VisionIndexRunSummary runIncrementalIndex(const QString& rootPath, QString* errorText = nullptr);

    bool lookupPath(const QString& path, EntryRecord* out, QString* errorText = nullptr) const;
    QueryResult queryChildren(const QString& rootPath, const QueryOptions& options) const;

    bool listJournal(const QString& rootPath,
                     int limit,
                     QVector<IndexJournalRecord>* out,
                     QString* errorText = nullptr) const;

    bool collectStats(const QString& rootPath,
                      VisionIndexStatsSnapshot* out,
                      QString* errorText = nullptr) const;

private:
    qint64 nextScanVersion(const QString& rootPath, QString* errorText = nullptr);

private:
    MetaStore* m_store = nullptr;
    QueryCore* m_queryCore = nullptr;
    VisionIndexJournal* m_journal = nullptr;
    VisionIndexWorker* m_worker = nullptr;
    VisionIndexStats* m_stats = nullptr;
    VisionIndexScheduler m_scheduler;
    QString m_dbPath;
};
}
