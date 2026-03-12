#include "SnapshotRepository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "core/db/MetaStore.h"
#include "core/db/SqlHelpers.h"

namespace {
SnapshotRecord snapshotFromQuery(const QSqlQuery& q)
{
    SnapshotRecord r;
    r.id = q.value(0).toLongLong();
    r.rootPath = q.value(1).toString();
    r.snapshotName = q.value(2).toString();
    r.snapshotType = q.value(3).toString();
    r.createdUtc = q.value(4).toString();
    r.optionsJson = q.value(5).toString();
    r.itemCount = q.value(6).toLongLong();
    r.sourceScanSessionId = q.value(7).toLongLong();
    r.hasSourceScanSessionId = !q.value(7).isNull();
    r.noteText = q.value(8).toString();
    return r;
}

SnapshotEntryRecord snapshotEntryFromQuery(const QSqlQuery& q)
{
    SnapshotEntryRecord r;
    r.id = q.value(0).toLongLong();
    r.snapshotId = q.value(1).toLongLong();
    r.entryPath = q.value(2).toString();
    r.virtualPath = q.value(3).toString();
    if (r.virtualPath.isEmpty()) {
        r.virtualPath = r.entryPath;
    }
    r.parentPath = q.value(4).toString();
    r.name = q.value(5).toString();
    r.normalizedName = q.value(6).toString();
    r.extension = q.value(7).toString();
    r.isDir = q.value(8).toInt() != 0;
    r.sizeBytes = q.value(9).toLongLong();
    r.hasSizeBytes = !q.value(9).isNull();
    r.modifiedUtc = q.value(10).toString();
    r.hiddenFlag = q.value(11).toInt() != 0;
    r.systemFlag = q.value(12).toInt() != 0;
    r.archiveFlag = q.value(13).toInt() != 0;
    r.existsFlag = q.value(14).toInt() != 0;
    r.archiveSource = q.value(15).toString();
    r.archiveEntryPath = q.value(16).toString();
    r.entryHash = q.value(17).toString();
    r.hasEntryHash = !q.value(17).isNull() && !r.entryHash.isEmpty();
    return r;
}
}

SnapshotRepository::SnapshotRepository(MetaStore& store)
    : m_store(store)
{
}

bool SnapshotRepository::createSnapshot(const SnapshotRecord& snapshot, qint64* snapshotId, QString* errorText)
{
    QSqlDatabase db = m_store.database();
    if (!db.isValid() || !db.isOpen()) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO snapshots(root_path, snapshot_name, snapshot_type, created_utc, options_json, item_count, source_scan_session_id, note_text) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?);"));
    q.addBindValue(snapshot.rootPath);
    q.addBindValue(snapshot.snapshotName);
    q.addBindValue(snapshot.snapshotType);
    q.addBindValue(snapshot.createdUtc.isEmpty() ? SqlHelpers::utcNowIso() : snapshot.createdUtc);
    q.addBindValue(snapshot.optionsJson);
    q.addBindValue(snapshot.itemCount);
    q.addBindValue(SqlHelpers::nullableInt64(snapshot.hasSourceScanSessionId, snapshot.sourceScanSessionId));
    q.addBindValue(snapshot.noteText);

    if (!q.exec()) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }

    if (snapshotId) {
        *snapshotId = q.lastInsertId().toLongLong();
    }
    return true;
}

bool SnapshotRepository::updateSnapshotItemCount(qint64 snapshotId, qint64 itemCount, QString* errorText)
{
    QSqlDatabase db = m_store.database();
    if (!db.isValid() || !db.isOpen()) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }

    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE snapshots SET item_count=? WHERE id=?;"));
    q.addBindValue(itemCount);
    q.addBindValue(snapshotId);
    if (!q.exec()) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }
    return true;
}

bool SnapshotRepository::insertSnapshotEntries(qint64 snapshotId,
                                               const QVector<SnapshotEntryRecord>& entries,
                                               QString* errorText)
{
    QSqlDatabase db = m_store.database();
    if (!db.isValid() || !db.isOpen()) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO snapshot_entries(snapshot_id, entry_path, virtual_path, parent_path, name, normalized_name, extension, is_dir, size_bytes, modified_utc, hidden_flag, system_flag, archive_flag, exists_flag, archive_source, archive_entry_path, entry_hash) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);"));

    for (const SnapshotEntryRecord& entry : entries) {
        const QString effectiveVirtualPath = entry.virtualPath.isEmpty() ? entry.entryPath : entry.virtualPath;
        q.bindValue(0, snapshotId);
        q.bindValue(1, entry.entryPath);
        q.bindValue(2, effectiveVirtualPath);
        q.bindValue(3, entry.parentPath);
        q.bindValue(4, entry.name);
        q.bindValue(5, entry.normalizedName);
        q.bindValue(6, entry.extension);
        q.bindValue(7, SqlHelpers::boolToInt(entry.isDir));
        q.bindValue(8, SqlHelpers::nullableInt64(entry.hasSizeBytes, entry.sizeBytes));
        q.bindValue(9, entry.modifiedUtc);
        q.bindValue(10, SqlHelpers::boolToInt(entry.hiddenFlag));
        q.bindValue(11, SqlHelpers::boolToInt(entry.systemFlag));
        q.bindValue(12, SqlHelpers::boolToInt(entry.archiveFlag));
        q.bindValue(13, SqlHelpers::boolToInt(entry.existsFlag));
        q.bindValue(14, entry.archiveSource);
        q.bindValue(15, entry.archiveEntryPath);
        q.bindValue(16, entry.hasEntryHash ? QVariant(entry.entryHash) : QVariant(QVariant::String));

        if (!q.exec()) {
            if (errorText) {
                *errorText = q.lastError().text();
            }
            return false;
        }
    }

    return true;
}

bool SnapshotRepository::listSnapshots(const QString& rootPath,
                                       QVector<SnapshotRecord>* out,
                                       QString* errorText)
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("null_output_vector");
        }
        return false;
    }

    QSqlDatabase db = m_store.database();
    if (!db.isValid() || !db.isOpen()) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }

    out->clear();
    QSqlQuery q(db);
    QString sql = QStringLiteral(
        "SELECT id, root_path, snapshot_name, snapshot_type, created_utc, options_json, item_count, source_scan_session_id, note_text "
        "FROM snapshots ");
    if (!rootPath.trimmed().isEmpty()) {
        sql += QStringLiteral("WHERE root_path=? ");
    }
    sql += QStringLiteral("ORDER BY created_utc DESC, id DESC;");

    q.prepare(sql);
    if (!rootPath.trimmed().isEmpty()) {
        q.addBindValue(rootPath);
    }

    if (!q.exec()) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }

    while (q.next()) {
        out->push_back(snapshotFromQuery(q));
    }
    return true;
}

bool SnapshotRepository::getSnapshotById(qint64 snapshotId, SnapshotRecord* out, QString* errorText)
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("null_output_record");
        }
        return false;
    }

    QSqlDatabase db = m_store.database();
    if (!db.isValid() || !db.isOpen()) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT id, root_path, snapshot_name, snapshot_type, created_utc, options_json, item_count, source_scan_session_id, note_text "
        "FROM snapshots WHERE id=? LIMIT 1;"));
    q.addBindValue(snapshotId);

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

    *out = snapshotFromQuery(q);
    return true;
}

bool SnapshotRepository::getSnapshotByName(const QString& rootPath,
                                           const QString& snapshotName,
                                           SnapshotRecord* out,
                                           QString* errorText)
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("null_output_record");
        }
        return false;
    }

    QSqlDatabase db = m_store.database();
    if (!db.isValid() || !db.isOpen()) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT id, root_path, snapshot_name, snapshot_type, created_utc, options_json, item_count, source_scan_session_id, note_text "
        "FROM snapshots WHERE root_path=? AND snapshot_name=? ORDER BY id DESC LIMIT 1;"));
    q.addBindValue(rootPath);
    q.addBindValue(snapshotName);

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

    *out = snapshotFromQuery(q);
    return true;
}

bool SnapshotRepository::listSnapshotEntries(qint64 snapshotId,
                                             QVector<SnapshotEntryRecord>* out,
                                             QString* errorText)
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("null_output_vector");
        }
        return false;
    }

    QSqlDatabase db = m_store.database();
    if (!db.isValid() || !db.isOpen()) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }

    out->clear();
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT id, snapshot_id, entry_path, virtual_path, parent_path, name, normalized_name, extension, is_dir, size_bytes, modified_utc, hidden_flag, system_flag, archive_flag, exists_flag, archive_source, archive_entry_path, entry_hash "
        "FROM snapshot_entries WHERE snapshot_id=? ORDER BY is_dir DESC, normalized_name ASC, entry_path ASC;"));
    q.addBindValue(snapshotId);

    if (!q.exec()) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }

    while (q.next()) {
        out->push_back(snapshotEntryFromQuery(q));
    }
    return true;
}
