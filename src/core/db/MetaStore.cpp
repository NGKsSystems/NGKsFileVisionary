#include "MetaStore.h"

#include "DbConnection.h"
#include "DbMigrations.h"
#include "EntryRepository.h"
#include "FavoriteRepository.h"
#include "VolumeRepository.h"

#include <QSqlError>
#include <QSqlQuery>

#include "SqlHelpers.h"

MetaStore::MetaStore()
    : m_connection(nullptr)
    , m_volumes(nullptr)
    , m_entries(nullptr)
    , m_favorites(nullptr)
{
}

MetaStore::~MetaStore()
{
    shutdown();
}

bool MetaStore::initialize(const QString& dbPath, QString* errorText, QString* migrationLog)
{
    shutdown();

    m_connection = new DbConnection();
    if (!m_connection->open(dbPath)) {
        if (errorText) {
            *errorText = m_connection->lastError();
        }
        shutdown();
        return false;
    }

    if (!DbMigrations::migrate(*m_connection, migrationLog)) {
        if (errorText) {
            *errorText = m_connection->lastError();
            if (errorText->isEmpty() && migrationLog) {
                *errorText = *migrationLog;
            }
        }
        shutdown();
        return false;
    }

    m_volumes = new VolumeRepository(*m_connection);
    m_entries = new EntryRepository(*m_connection);
    m_favorites = new FavoriteRepository(*m_connection);
    return true;
}

void MetaStore::shutdown()
{
    delete m_favorites;
    m_favorites = nullptr;

    delete m_entries;
    m_entries = nullptr;

    delete m_volumes;
    m_volumes = nullptr;

    if (m_connection) {
        m_connection->close();
        delete m_connection;
        m_connection = nullptr;
    }
}

bool MetaStore::isReady() const
{
    return m_connection && m_connection->isOpen() && m_volumes && m_entries && m_favorites;
}

int MetaStore::schemaVersion(QString* errorText)
{
    if (!m_connection || !m_connection->isOpen()) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return -1;
    }
    return DbMigrations::currentVersion(*m_connection, errorText);
}

bool MetaStore::upsertVolume(const VolumeRecord& record, qint64* volumeId, QString* errorText)
{
    if (!m_volumes) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }
    return m_volumes->upsertVolume(record, volumeId, errorText);
}

bool MetaStore::upsertEntry(const EntryRecord& record, qint64* entryId, QString* errorText)
{
    if (!m_entries) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }
    return m_entries->upsertEntry(record, entryId, errorText);
}

bool MetaStore::upsertEntry(const EntryRecord& record,
                            qint64* entryId,
                            bool* inserted,
                            bool* updated,
                            QString* errorText)
{
    if (!m_entries) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }
    return m_entries->upsertEntry(record, entryId, inserted, updated, errorText);
}

bool MetaStore::upsertFavorite(const FavoriteRecord& record, qint64* favoriteId, QString* errorText)
{
    if (!m_favorites) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }
    return m_favorites->upsertFavorite(record, favoriteId, errorText);
}

bool MetaStore::getEntryByPath(const QString& path, EntryRecord* out, QString* errorText)
{
    if (!m_entries) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }
    return m_entries->getByPath(path, out, errorText);
}

bool MetaStore::findChildrenByParentId(qint64 parentId, QVector<EntryRecord>* out, QString* errorText)
{
    if (!m_entries) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }
    return m_entries->findChildrenByParentId(parentId, out, errorText);
}

bool MetaStore::findChildrenByParentPath(const QString& parentPath, QVector<EntryRecord>* out, QString* errorText)
{
    if (!m_entries) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }
    return m_entries->findChildrenByParentPath(parentPath, out, errorText);
}

bool MetaStore::findFlatDescendantsByRootPath(const QString& rootPath,
                                              int maxDepth,
                                              QVector<EntryRecord>* out,
                                              QString* errorText)
{
    if (!m_entries) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }
    return m_entries->findFlatDescendantsByRootPath(rootPath, maxDepth, out, errorText);
}

bool MetaStore::findSubtreeByRootPath(const QString& rootPath,
                                      int maxDepth,
                                      QVector<EntryRecord>* out,
                                      QString* errorText)
{
    if (!m_entries) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }
    return m_entries->findSubtreeByRootPath(rootPath, maxDepth, out, errorText);
}

qint64 MetaStore::countEntriesUnderRoot(const QString& rootPath, int maxDepth, QString* errorText)
{
    if (!m_entries) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return -1;
    }
    return m_entries->countEntriesUnderRoot(rootPath, maxDepth, errorText);
}

bool MetaStore::listSampleRows(int limit, QVector<EntryRecord>* out, QString* errorText)
{
    if (!m_entries) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }
    return m_entries->listSampleRows(limit, out, errorText);
}

bool MetaStore::resolveParentIdByPath(const QString& parentPath,
                                      qint64* parentId,
                                      bool* found,
                                      QString* errorText)
{
    if (!m_entries) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }
    return m_entries->resolveParentIdByPath(parentPath, parentId, found, errorText);
}

qint64 MetaStore::countEntries(QString* errorText)
{
    if (!m_entries) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return -1;
    }
    return m_entries->countEntries(errorText);
}

qint64 MetaStore::countDirectories(QString* errorText)
{
    if (!m_entries) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return -1;
    }
    return m_entries->countDirectories(errorText);
}

qint64 MetaStore::countFiles(QString* errorText)
{
    if (!m_entries) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return -1;
    }
    return m_entries->countFiles(errorText);
}

bool MetaStore::listSomeEntries(int limit, QVector<EntryRecord>* out, QString* errorText)
{
    if (!m_entries) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }
    return m_entries->listSomeEntries(limit, out, errorText);
}

bool MetaStore::beginTransaction(QString* errorText)
{
    if (!m_connection || !m_connection->isOpen()) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }
    if (!m_connection->beginTransaction()) {
        if (errorText) {
            *errorText = m_connection->lastError();
        }
        return false;
    }
    return true;
}

bool MetaStore::commitTransaction(QString* errorText)
{
    if (!m_connection || !m_connection->isOpen()) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }
    if (!m_connection->commit()) {
        if (errorText) {
            *errorText = m_connection->lastError();
        }
        return false;
    }
    return true;
}

bool MetaStore::rollbackTransaction(QString* errorText)
{
    if (!m_connection || !m_connection->isOpen()) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }
    if (!m_connection->rollback()) {
        if (errorText) {
            *errorText = m_connection->lastError();
        }
        return false;
    }
    return true;
}

bool MetaStore::createScanSession(const ScanSessionRecord& record, qint64* sessionId, QString* errorText)
{
    if (!m_connection || !m_connection->isOpen()) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }

    QSqlQuery q(m_connection->database());
    q.prepare(QStringLiteral(
        "INSERT INTO scan_sessions(root_path, mode, started_utc, completed_utc, status, total_seen, total_inserted, total_updated, total_removed, error_text) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?);"));
    q.addBindValue(record.rootPath);
    q.addBindValue(record.mode);
    q.addBindValue(record.startedUtc.isEmpty() ? SqlHelpers::utcNowIso() : record.startedUtc);
    q.addBindValue(record.completedUtc);
    q.addBindValue(record.status.isEmpty() ? QStringLiteral("running") : record.status);
    q.addBindValue(record.totalSeen);
    q.addBindValue(record.totalInserted);
    q.addBindValue(record.totalUpdated);
    q.addBindValue(record.totalRemoved);
    q.addBindValue(record.errorText);
    if (!q.exec()) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }

    if (sessionId) {
        *sessionId = q.lastInsertId().toLongLong();
    }
    return true;
}

bool MetaStore::updateScanSessionProgress(qint64 sessionId,
                                          qint64 totalSeen,
                                          qint64 totalInserted,
                                          qint64 totalUpdated,
                                          qint64 totalRemoved,
                                          QString* errorText)
{
    if (!m_connection || !m_connection->isOpen()) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }

    QSqlQuery q(m_connection->database());
    q.prepare(QStringLiteral(
        "UPDATE scan_sessions SET total_seen=?, total_inserted=?, total_updated=?, total_removed=? WHERE id=?;"));
    q.addBindValue(totalSeen);
    q.addBindValue(totalInserted);
    q.addBindValue(totalUpdated);
    q.addBindValue(totalRemoved);
    q.addBindValue(sessionId);
    if (!q.exec()) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }
    return true;
}

bool MetaStore::completeScanSession(qint64 sessionId,
                                    qint64 totalSeen,
                                    qint64 totalInserted,
                                    qint64 totalUpdated,
                                    qint64 totalRemoved,
                                    QString* errorText)
{
    if (!m_connection || !m_connection->isOpen()) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }

    QSqlQuery q(m_connection->database());
    q.prepare(QStringLiteral(
        "UPDATE scan_sessions SET status='complete', completed_utc=?, total_seen=?, total_inserted=?, total_updated=?, total_removed=?, error_text=NULL WHERE id=?;"));
    q.addBindValue(SqlHelpers::utcNowIso());
    q.addBindValue(totalSeen);
    q.addBindValue(totalInserted);
    q.addBindValue(totalUpdated);
    q.addBindValue(totalRemoved);
    q.addBindValue(sessionId);
    if (!q.exec()) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }
    return true;
}

bool MetaStore::failScanSession(qint64 sessionId,
                                qint64 totalSeen,
                                qint64 totalInserted,
                                qint64 totalUpdated,
                                qint64 totalRemoved,
                                const QString& error,
                                QString* errorText)
{
    if (!m_connection || !m_connection->isOpen()) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }

    QSqlQuery q(m_connection->database());
    q.prepare(QStringLiteral(
        "UPDATE scan_sessions SET status='failed', completed_utc=?, total_seen=?, total_inserted=?, total_updated=?, total_removed=?, error_text=? WHERE id=?;"));
    q.addBindValue(SqlHelpers::utcNowIso());
    q.addBindValue(totalSeen);
    q.addBindValue(totalInserted);
    q.addBindValue(totalUpdated);
    q.addBindValue(totalRemoved);
    q.addBindValue(error);
    q.addBindValue(sessionId);
    if (!q.exec()) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }
    return true;
}

bool MetaStore::cancelScanSession(qint64 sessionId,
                                  qint64 totalSeen,
                                  qint64 totalInserted,
                                  qint64 totalUpdated,
                                  qint64 totalRemoved,
                                  QString* errorText)
{
    if (!m_connection || !m_connection->isOpen()) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }

    QSqlQuery q(m_connection->database());
    q.prepare(QStringLiteral(
        "UPDATE scan_sessions SET status='canceled', completed_utc=?, total_seen=?, total_inserted=?, total_updated=?, total_removed=? WHERE id=?;"));
    q.addBindValue(SqlHelpers::utcNowIso());
    q.addBindValue(totalSeen);
    q.addBindValue(totalInserted);
    q.addBindValue(totalUpdated);
    q.addBindValue(totalRemoved);
    q.addBindValue(sessionId);
    if (!q.exec()) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }
    return true;
}

bool MetaStore::listScanSessions(QVector<ScanSessionRecord>* out, QString* errorText)
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("null_output_vector");
        }
        return false;
    }
    if (!m_connection || !m_connection->isOpen()) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }

    out->clear();
    QSqlQuery q(m_connection->database());
    if (!q.exec(QStringLiteral(
            "SELECT id, root_path, mode, started_utc, completed_utc, status, total_seen, total_inserted, total_updated, total_removed, error_text "
            "FROM scan_sessions ORDER BY id DESC;"))) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }

    while (q.next()) {
        ScanSessionRecord r;
        r.id = q.value(0).toLongLong();
        r.rootPath = q.value(1).toString();
        r.mode = q.value(2).toString();
        r.startedUtc = q.value(3).toString();
        r.completedUtc = q.value(4).toString();
        r.status = q.value(5).toString();
        r.totalSeen = q.value(6).toLongLong();
        r.totalInserted = q.value(7).toLongLong();
        r.totalUpdated = q.value(8).toLongLong();
        r.totalRemoved = q.value(9).toLongLong();
        r.errorText = q.value(10).toString();
        out->push_back(r);
    }
    return true;
}

bool MetaStore::upsertIndexRoot(const IndexRootRecord& record, qint64* rootId, QString* errorText)
{
    if (!m_connection || !m_connection->isOpen()) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }

    QSqlQuery q(m_connection->database());
    q.prepare(QStringLiteral(
        "INSERT INTO index_roots(root_path, status, last_scan_version, last_indexed_utc, created_utc, updated_utc) "
        "VALUES(?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(root_path) DO UPDATE SET "
        "status=excluded.status, "
        "last_scan_version=excluded.last_scan_version, "
        "last_indexed_utc=excluded.last_indexed_utc, "
        "updated_utc=excluded.updated_utc;"));

    const QString now = SqlHelpers::utcNowIso();
    q.addBindValue(record.rootPath);
    q.addBindValue(record.status);
    q.addBindValue(record.lastScanVersion);
    q.addBindValue(record.lastIndexedUtc);
    q.addBindValue(record.createdUtc.isEmpty() ? now : record.createdUtc);
    q.addBindValue(record.updatedUtc.isEmpty() ? now : record.updatedUtc);
    if (!q.exec()) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }

    if (rootId) {
        QSqlQuery idQ(m_connection->database());
        idQ.prepare(QStringLiteral("SELECT id FROM index_roots WHERE root_path=? LIMIT 1;"));
        idQ.addBindValue(record.rootPath);
        if (!idQ.exec() || !idQ.next()) {
            if (errorText) {
                *errorText = idQ.lastError().isValid() ? idQ.lastError().text() : QStringLiteral("index_root_id_lookup_failed");
            }
            return false;
        }
        *rootId = idQ.value(0).toLongLong();
    }

    return true;
}

bool MetaStore::getIndexRoot(const QString& rootPath, IndexRootRecord* out, QString* errorText)
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("null_output_record");
        }
        return false;
    }
    if (!m_connection || !m_connection->isOpen()) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }

    QSqlQuery q(m_connection->database());
    q.prepare(QStringLiteral("SELECT id, root_path, status, last_scan_version, last_indexed_utc, created_utc, updated_utc FROM index_roots WHERE root_path=? LIMIT 1;"));
    q.addBindValue(rootPath);
    if (!q.exec()) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }
    if (!q.next()) {
        if (errorText) {
            *errorText = QStringLiteral("not_found");
        }
        return false;
    }

    out->id = q.value(0).toLongLong();
    out->rootPath = q.value(1).toString();
    out->status = q.value(2).toString();
    out->lastScanVersion = q.value(3).toLongLong();
    out->lastIndexedUtc = q.value(4).toString();
    out->createdUtc = q.value(5).toString();
    out->updatedUtc = q.value(6).toString();
    return true;
}

bool MetaStore::listIndexRoots(QVector<IndexRootRecord>* out, QString* errorText)
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("null_output_vector");
        }
        return false;
    }
    if (!m_connection || !m_connection->isOpen()) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }

    out->clear();
    QSqlQuery q(m_connection->database());
    if (!q.exec(QStringLiteral("SELECT id, root_path, status, last_scan_version, last_indexed_utc, created_utc, updated_utc FROM index_roots ORDER BY id ASC;"))) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }

    while (q.next()) {
        IndexRootRecord r;
        r.id = q.value(0).toLongLong();
        r.rootPath = q.value(1).toString();
        r.status = q.value(2).toString();
        r.lastScanVersion = q.value(3).toLongLong();
        r.lastIndexedUtc = q.value(4).toString();
        r.createdUtc = q.value(5).toString();
        r.updatedUtc = q.value(6).toString();
        out->push_back(r);
    }
    return true;
}

bool MetaStore::appendIndexJournal(const IndexJournalRecord& record, qint64* journalId, QString* errorText)
{
    if (!m_connection || !m_connection->isOpen()) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }

    QSqlQuery q(m_connection->database());
    q.prepare(QStringLiteral(
        "INSERT INTO index_journal(root_path, path, event_type, scan_version, payload, created_utc) "
        "VALUES(?, ?, ?, ?, ?, ?);"));
    q.addBindValue(record.rootPath);
    q.addBindValue(record.path);
    q.addBindValue(record.eventType);
    q.addBindValue(record.scanVersion);
    q.addBindValue(record.payload);
    q.addBindValue(record.createdUtc.isEmpty() ? SqlHelpers::utcNowIso() : record.createdUtc);
    if (!q.exec()) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }

    if (journalId) {
        *journalId = q.lastInsertId().toLongLong();
    }
    return true;
}

bool MetaStore::listIndexJournal(const QString& rootPath,
                                 int limit,
                                 QVector<IndexJournalRecord>* out,
                                 QString* errorText)
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("null_output_vector");
        }
        return false;
    }
    if (!m_connection || !m_connection->isOpen()) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }

    out->clear();
    QSqlQuery q(m_connection->database());
    q.prepare(QStringLiteral(
        "SELECT id, root_path, path, event_type, scan_version, payload, created_utc "
        "FROM index_journal "
        "WHERE (? = '' OR root_path = ?) "
        "ORDER BY id DESC LIMIT ?;"));
    q.addBindValue(rootPath);
    q.addBindValue(rootPath);
    q.addBindValue(limit > 0 ? limit : 50);
    if (!q.exec()) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }

    while (q.next()) {
        IndexJournalRecord r;
        r.id = q.value(0).toLongLong();
        r.rootPath = q.value(1).toString();
        r.path = q.value(2).toString();
        r.eventType = q.value(3).toString();
        r.scanVersion = q.value(4).toLongLong();
        r.payload = q.value(5).toString();
        r.createdUtc = q.value(6).toString();
        out->push_back(r);
    }
    return true;
}

bool MetaStore::upsertIndexStat(const IndexStatRecord& record, QString* errorText)
{
    if (!m_connection || !m_connection->isOpen()) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }

    QSqlQuery q(m_connection->database());
    q.prepare(QStringLiteral(
        "INSERT INTO index_stats(key, value, updated_utc) VALUES(?, ?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value, updated_utc=excluded.updated_utc;"));
    q.addBindValue(record.key);
    q.addBindValue(record.value);
    q.addBindValue(record.updatedUtc.isEmpty() ? SqlHelpers::utcNowIso() : record.updatedUtc);
    if (!q.exec()) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }
    return true;
}

bool MetaStore::listIndexStats(QVector<IndexStatRecord>* out, QString* errorText)
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("null_output_vector");
        }
        return false;
    }
    if (!m_connection || !m_connection->isOpen()) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }

    out->clear();
    QSqlQuery q(m_connection->database());
    if (!q.exec(QStringLiteral("SELECT key, value, updated_utc FROM index_stats ORDER BY key ASC;"))) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }

    while (q.next()) {
        IndexStatRecord r;
        r.key = q.value(0).toString();
        r.value = q.value(1).toString();
        r.updatedUtc = q.value(2).toString();
        out->push_back(r);
    }
    return true;
}

bool MetaStore::listVolumes(QVector<VolumeRecord>* out, QString* errorText)
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("null_output_vector");
        }
        return false;
    }
    if (!m_connection || !m_connection->isOpen()) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }

    out->clear();
    QSqlQuery q(m_connection->database());
    if (!q.exec(QStringLiteral(
            "SELECT id, volume_key, root_path, display_name, fs_type, serial_number, created_utc, updated_utc FROM volumes ORDER BY id ASC;"))) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }

    while (q.next()) {
        VolumeRecord r;
        r.id = q.value(0).toLongLong();
        r.volumeKey = q.value(1).toString();
        r.rootPath = q.value(2).toString();
        r.displayName = q.value(3).toString();
        r.fsType = q.value(4).toString();
        r.serialNumber = q.value(5).toString();
        r.createdUtc = q.value(6).toString();
        r.updatedUtc = q.value(7).toString();
        out->push_back(r);
    }
    return true;
}

bool MetaStore::listFavorites(QVector<FavoriteRecord>* out, QString* errorText)
{
    if (!m_favorites) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }
    return m_favorites->listAll(out, errorText);
}
