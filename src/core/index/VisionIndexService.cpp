#include "VisionIndexService.h"

#include <QDir>
#include <QFileInfo>

#include "VisionIndexJournal.h"
#include "VisionIndexWorker.h"
#include "core/db/MetaStore.h"
#include "core/db/SqlHelpers.h"
#include "core/query/QueryCore.h"

namespace VisionIndexEngine
{
VisionIndexService::VisionIndexService()
    : m_store(new MetaStore())
{
}

VisionIndexService::~VisionIndexService()
{
    closeIndex();
    delete m_store;
    m_store = nullptr;
}

bool VisionIndexService::openIndex(const QString& dbPath, QString* errorText, QString* migrationLog)
{
    closeIndex();

    if (!m_store) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_missing");
        }
        return false;
    }

    const QString normalized = QDir::cleanPath(dbPath);
    if (!m_store->initialize(normalized, errorText, migrationLog)) {
        return false;
    }

    m_queryCore = new QueryCore(*m_store);
    m_journal = new VisionIndexJournal(*m_store);
    m_worker = new VisionIndexWorker(*m_store, *m_journal);
    m_stats = new VisionIndexStats(*m_store);
    m_dbPath = normalized;
    return true;
}

void VisionIndexService::closeIndex()
{
    delete m_stats;
    m_stats = nullptr;

    delete m_worker;
    m_worker = nullptr;

    delete m_journal;
    m_journal = nullptr;

    delete m_queryCore;
    m_queryCore = nullptr;

    if (m_store) {
        m_store->shutdown();
    }
    m_dbPath.clear();
}

bool VisionIndexService::isReady() const
{
    return m_store && m_store->isReady() && m_queryCore && m_journal && m_worker && m_stats;
}

QString VisionIndexService::dbPath() const
{
    return m_dbPath;
}

bool VisionIndexService::ensureIndexRoot(const QString& rootPath, QString* errorText)
{
    if (!isReady()) {
        if (errorText) {
            *errorText = QStringLiteral("index_service_not_ready");
        }
        return false;
    }

    const QString cleaned = QDir::cleanPath(rootPath);
    const QString normalizedRoot = QDir::fromNativeSeparators(cleaned);

    bool exists = false;
    QDir nativeDir(cleaned);
    if (nativeDir.exists()) {
        exists = true;
    } else {
        QDir normalizedDir(normalizedRoot);
        exists = normalizedDir.exists();
    }

    IndexRootRecord root;
    root.rootPath = normalizedRoot;
    root.status = exists ? QStringLiteral("active") : QStringLiteral("pending_validation");
    root.createdUtc = SqlHelpers::utcNowIso();
    root.updatedUtc = root.createdUtc;
    root.lastIndexedUtc = QString();
    root.lastScanVersion = 0;
    return m_store->upsertIndexRoot(root, nullptr, errorText);
}

VisionIndexRunSummary VisionIndexService::scheduleScan(const QString& rootPath,
                                                       bool visiblePriority,
                                                       const QString& mode,
                                                       bool force,
                                                       QString* errorText)
{
    VisionIndexRunSummary summary;
    if (!isReady()) {
        summary.errorText = QStringLiteral("index_service_not_ready");
        if (errorText) {
            *errorText = summary.errorText;
        }
        return summary;
    }

    VisionIndexWorkItem item;
    item.rootPath = rootPath;
    item.mode = mode;
    item.force = force;
    item.visiblePriority = visiblePriority;

    QString reason;
    const bool queued = m_scheduler.enqueue(item, &reason);
    if (!queued) {
        summary.ok = true;
        summary.deduped = true;
        summary.dedupeReason = reason;
        return summary;
    }

    return runScheduled(errorText);
}

VisionIndexRunSummary VisionIndexService::runScheduled(QString* errorText)
{
    VisionIndexRunSummary summary;
    if (!isReady()) {
        summary.errorText = QStringLiteral("index_service_not_ready");
        if (errorText) {
            *errorText = summary.errorText;
        }
        return summary;
    }

    VisionIndexWorkItem item;
    if (!m_scheduler.dequeueNext(&item)) {
        summary.ok = true;
        summary.deduped = true;
        summary.dedupeReason = QStringLiteral("queue_empty");
        return summary;
    }

    if (!ensureIndexRoot(item.rootPath, errorText)) {
        summary.errorText = errorText ? *errorText : QStringLiteral("ensure_index_root_failed");
        return summary;
    }

    qint64 scanVersion = nextScanVersion(item.rootPath, errorText);
    if (scanVersion <= 0) {
        summary.errorText = errorText ? *errorText : QStringLiteral("next_scan_version_failed");
        return summary;
    }

    VisionIndexWorkResult work;
    if (!m_worker->run(item, scanVersion, &work, errorText)) {
        summary.ok = false;
        summary.errorText = work.errorText;
        summary.scanVersion = scanVersion;
        summary.sessionId = work.sessionId;
        summary.inserted = work.totalInserted;
        summary.updated = work.totalUpdated;
        summary.seen = work.totalSeen;
        return summary;
    }

    IndexRootRecord root;
    QString lookupError;
    if (m_store->getIndexRoot(QDir::fromNativeSeparators(QDir::cleanPath(item.rootPath)), &root, &lookupError)) {
        root.status = QStringLiteral("active");
        root.lastScanVersion = scanVersion;
        root.lastIndexedUtc = SqlHelpers::utcNowIso();
        root.updatedUtc = root.lastIndexedUtc;
        m_store->upsertIndexRoot(root, nullptr, nullptr);
    }

    summary.ok = true;
    summary.scanVersion = scanVersion;
    summary.sessionId = work.sessionId;
    summary.inserted = work.totalInserted;
    summary.updated = work.totalUpdated;
    summary.seen = work.totalSeen;

    IndexStatRecord stat;
    stat.key = QStringLiteral("last_refresh");
    stat.value = QStringLiteral("{\"root\":\"%1\",\"scan_version\":%2,\"session_id\":%3}")
                     .arg(item.rootPath)
                     .arg(scanVersion)
                     .arg(work.sessionId);
    stat.updatedUtc = SqlHelpers::utcNowIso();
    m_store->upsertIndexStat(stat, nullptr);
    return summary;
}

VisionIndexRunSummary VisionIndexService::runInitialIndex(const QString& rootPath, QString* errorText)
{
    IndexRootRecord root;
    const QString normalizedRoot = QDir::fromNativeSeparators(QDir::cleanPath(rootPath));
    if (m_store->getIndexRoot(normalizedRoot, &root, nullptr) && root.lastScanVersion > 0) {
        VisionIndexRunSummary summary;
        summary.ok = true;
        summary.deduped = true;
        summary.dedupeReason = QStringLiteral("initial_scan_already_completed");
        return summary;
    }

    return scheduleScan(normalizedRoot, true, QStringLiteral("full"), true, errorText);
}

VisionIndexRunSummary VisionIndexService::runIncrementalIndex(const QString& rootPath, QString* errorText)
{
    return scheduleScan(QDir::fromNativeSeparators(QDir::cleanPath(rootPath)),
                        true,
                        QStringLiteral("incremental"),
                        true,
                        errorText);
}

bool VisionIndexService::lookupPath(const QString& path, EntryRecord* out, QString* errorText) const
{
    if (!isReady()) {
        if (errorText) {
            *errorText = QStringLiteral("index_service_not_ready");
        }
        return false;
    }
    return m_store->getEntryByPath(QDir::fromNativeSeparators(QDir::cleanPath(path)), out, errorText);
}

QueryResult VisionIndexService::queryChildren(const QString& rootPath, const QueryOptions& options) const
{
    if (!isReady()) {
        QueryResult result;
        result.ok = false;
        result.errorText = QStringLiteral("index_service_not_ready");
        return result;
    }
    return m_queryCore->queryChildren(QDir::fromNativeSeparators(QDir::cleanPath(rootPath)), options);
}

bool VisionIndexService::listJournal(const QString& rootPath,
                                     int limit,
                                     QVector<IndexJournalRecord>* out,
                                     QString* errorText) const
{
    if (!isReady()) {
        if (errorText) {
            *errorText = QStringLiteral("index_service_not_ready");
        }
        return false;
    }
    return m_journal->list(QDir::fromNativeSeparators(QDir::cleanPath(rootPath)), limit, out, errorText);
}

bool VisionIndexService::collectStats(const QString& rootPath,
                                      VisionIndexStatsSnapshot* out,
                                      QString* errorText) const
{
    if (!isReady()) {
        if (errorText) {
            *errorText = QStringLiteral("index_service_not_ready");
        }
        return false;
    }
    return m_stats->collect(QDir::fromNativeSeparators(QDir::cleanPath(rootPath)), out, errorText);
}

qint64 VisionIndexService::nextScanVersion(const QString& rootPath, QString* errorText)
{
    IndexRootRecord root;
    const QString normalizedRoot = QDir::fromNativeSeparators(QDir::cleanPath(rootPath));
    if (!m_store->getIndexRoot(normalizedRoot, &root, errorText)) {
        return -1;
    }
    return root.lastScanVersion + 1;
}
}
