#include "EntryRepository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "DbConnection.h"
#include "SqlHelpers.h"

namespace {
EntryRecord fromQuery(const QSqlQuery& q)
{
    EntryRecord r;
    r.id = q.value(0).toLongLong();
    r.volumeId = q.value(1).toLongLong();
    r.parentId = q.value(2).toLongLong();
    r.hasParentId = !q.value(2).isNull();
    r.path = q.value(3).toString();
    r.name = q.value(4).toString();
    r.normalizedName = q.value(5).toString();
    r.extension = q.value(6).toString();
    r.isDir = q.value(7).toInt() != 0;
    r.sizeBytes = q.value(8).toLongLong();
    r.hasSizeBytes = !q.value(8).isNull();
    r.createdUtc = q.value(9).toString();
    r.modifiedUtc = q.value(10).toString();
    r.accessedUtc = q.value(11).toString();
    r.hiddenFlag = q.value(12).toInt() != 0;
    r.systemFlag = q.value(13).toInt() != 0;
    r.readonlyFlag = q.value(14).toInt() != 0;
    r.archiveFlag = q.value(15).toInt() != 0;
    r.reparseFlag = q.value(16).toInt() != 0;
    r.existsFlag = q.value(17).toInt() != 0;
    r.fileId = q.value(18).toString();
    r.indexedAtUtc = q.value(19).toString();
    r.lastSeenScanId = q.value(20).toLongLong();
    r.hasLastSeenScanId = !q.value(20).isNull();
    r.metadataVersion = q.value(21).toInt();
    return r;
}
}

EntryRepository::EntryRepository(DbConnection& connection)
    : m_connection(connection)
{
}

bool EntryRepository::upsertEntry(const EntryRecord& record, qint64* entryId, QString* errorText)
{
    return upsertEntry(record, entryId, nullptr, nullptr, errorText);
}

bool EntryRepository::upsertEntry(const EntryRecord& record,
                                  qint64* entryId,
                                  bool* inserted,
                                  bool* updated,
                                  QString* errorText)
{
    qint64 existingId = 0;
    bool existed = false;
    {
        QSqlQuery existsQ(m_connection.database());
        existsQ.prepare(QStringLiteral("SELECT id FROM entries WHERE path=? LIMIT 1;"));
        existsQ.addBindValue(record.path);
        if (!existsQ.exec()) {
            if (errorText) {
                *errorText = existsQ.lastError().text();
            }
            return false;
        }
        if (existsQ.next()) {
            existed = true;
            existingId = existsQ.value(0).toLongLong();
        }
    }

    QSqlQuery q(m_connection.database());
    q.prepare(QStringLiteral(
        "INSERT INTO entries(" \
        "volume_id, parent_id, path, name, normalized_name, extension, is_dir, size_bytes, " \
        "created_utc, modified_utc, accessed_utc, hidden_flag, system_flag, readonly_flag, archive_flag, reparse_flag, " \
        "exists_flag, file_id, indexed_at_utc, last_seen_scan_id, metadata_version" \
        ") VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(path) DO UPDATE SET "
        "volume_id=excluded.volume_id, "
        "parent_id=excluded.parent_id, "
        "name=excluded.name, "
        "normalized_name=excluded.normalized_name, "
        "extension=excluded.extension, "
        "is_dir=excluded.is_dir, "
        "size_bytes=excluded.size_bytes, "
        "created_utc=excluded.created_utc, "
        "modified_utc=excluded.modified_utc, "
        "accessed_utc=excluded.accessed_utc, "
        "hidden_flag=excluded.hidden_flag, "
        "system_flag=excluded.system_flag, "
        "readonly_flag=excluded.readonly_flag, "
        "archive_flag=excluded.archive_flag, "
        "reparse_flag=excluded.reparse_flag, "
        "exists_flag=excluded.exists_flag, "
        "file_id=excluded.file_id, "
        "indexed_at_utc=excluded.indexed_at_utc, "
        "last_seen_scan_id=excluded.last_seen_scan_id, "
        "metadata_version=excluded.metadata_version;"));

    q.addBindValue(record.volumeId);
    q.addBindValue(SqlHelpers::nullableInt64(record.hasParentId, record.parentId));
    q.addBindValue(record.path);
    q.addBindValue(record.name);
    q.addBindValue(record.normalizedName.isEmpty() ? SqlHelpers::normalizedName(record.name) : record.normalizedName);
    q.addBindValue(record.extension);
    q.addBindValue(SqlHelpers::boolToInt(record.isDir));
    q.addBindValue(SqlHelpers::nullableInt64(record.hasSizeBytes, record.sizeBytes));
    q.addBindValue(record.createdUtc);
    q.addBindValue(record.modifiedUtc);
    q.addBindValue(record.accessedUtc);
    q.addBindValue(SqlHelpers::boolToInt(record.hiddenFlag));
    q.addBindValue(SqlHelpers::boolToInt(record.systemFlag));
    q.addBindValue(SqlHelpers::boolToInt(record.readonlyFlag));
    q.addBindValue(SqlHelpers::boolToInt(record.archiveFlag));
    q.addBindValue(SqlHelpers::boolToInt(record.reparseFlag));
    q.addBindValue(SqlHelpers::boolToInt(record.existsFlag));
    q.addBindValue(record.fileId);
    q.addBindValue(record.indexedAtUtc.isEmpty() ? SqlHelpers::utcNowIso() : record.indexedAtUtc);
    q.addBindValue(SqlHelpers::nullableInt64(record.hasLastSeenScanId, record.lastSeenScanId));
    q.addBindValue(record.metadataVersion);

    if (!q.exec()) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }

    if (inserted) {
        *inserted = !existed;
    }
    if (updated) {
        *updated = existed;
    }

    if (!entryId) {
        return true;
    }

    if (existed) {
        *entryId = existingId;
        return true;
    }

    QSqlQuery readQ(m_connection.database());
    readQ.prepare(QStringLiteral("SELECT id FROM entries WHERE path=?;"));
    readQ.addBindValue(record.path);
    if (!readQ.exec() || !readQ.next()) {
        if (errorText) {
            *errorText = readQ.lastError().isValid() ? readQ.lastError().text() : QStringLiteral("entry_id_lookup_failed");
        }
        return false;
    }

    *entryId = readQ.value(0).toLongLong();
    return true;
}

bool EntryRepository::getByPath(const QString& path, EntryRecord* out, QString* errorText)
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("null_output_record");
        }
        return false;
    }

    QSqlQuery q(m_connection.database());
    q.prepare(QStringLiteral(
        "SELECT id, volume_id, parent_id, path, name, normalized_name, extension, is_dir, size_bytes, "
        "created_utc, modified_utc, accessed_utc, hidden_flag, system_flag, readonly_flag, archive_flag, reparse_flag, "
        "exists_flag, file_id, indexed_at_utc, last_seen_scan_id, metadata_version "
        "FROM entries WHERE path=? LIMIT 1;"));
    q.addBindValue(path);

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

    *out = fromQuery(q);
    return true;
}

bool EntryRepository::findByPath(const QString& path, EntryRecord* out, QString* errorText)
{
    return getByPath(path, out, errorText);
}

bool EntryRepository::listChildren(qint64 parentId, QVector<EntryRecord>* out, QString* errorText)
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("null_output_vector");
        }
        return false;
    }

    out->clear();

    QSqlQuery q(m_connection.database());
    q.prepare(QStringLiteral(
        "SELECT id, volume_id, parent_id, path, name, normalized_name, extension, is_dir, size_bytes, "
        "created_utc, modified_utc, accessed_utc, hidden_flag, system_flag, readonly_flag, archive_flag, reparse_flag, "
        "exists_flag, file_id, indexed_at_utc, last_seen_scan_id, metadata_version "
        "FROM entries WHERE parent_id=? ORDER BY is_dir DESC, normalized_name ASC;"));
    q.addBindValue(parentId);

    if (!q.exec()) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }

    while (q.next()) {
        out->push_back(fromQuery(q));
    }

    return true;
}

bool EntryRepository::findChildren(qint64 parentId, QVector<EntryRecord>* out, QString* errorText)
{
    return listChildren(parentId, out, errorText);
}

bool EntryRepository::findChildrenByParentId(qint64 parentId, QVector<EntryRecord>* out, QString* errorText)
{
    return listChildren(parentId, out, errorText);
}

bool EntryRepository::findChildrenByParentPath(const QString& parentPath, QVector<EntryRecord>* out, QString* errorText)
{
    EntryRecord parent;
    if (!getByPath(parentPath, &parent, errorText)) {
        return false;
    }
    return listChildren(parent.id, out, errorText);
}

bool EntryRepository::findFlatDescendantsByRootPath(const QString& rootPath,
                                                    int maxDepth,
                                                    QVector<EntryRecord>* out,
                                                    QString* errorText)
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("null_output_vector");
        }
        return false;
    }

    out->clear();
    EntryRecord root;
    if (!getByPath(rootPath, &root, errorText)) {
        return false;
    }

    QString sql = QStringLiteral(
        "WITH RECURSIVE tree(id, depth) AS ("
        "SELECT id, 0 FROM entries WHERE id=? "
        "UNION ALL "
        "SELECT e.id, tree.depth + 1 FROM entries e JOIN tree ON e.parent_id = tree.id ");
    if (maxDepth >= 0) {
        sql += QStringLiteral("WHERE tree.depth < ? ");
    }
    sql += QStringLiteral(
        ") "
        "SELECT e.id, e.volume_id, e.parent_id, e.path, e.name, e.normalized_name, e.extension, e.is_dir, e.size_bytes, "
        "e.created_utc, e.modified_utc, e.accessed_utc, e.hidden_flag, e.system_flag, e.readonly_flag, e.archive_flag, e.reparse_flag, "
        "e.exists_flag, e.file_id, e.indexed_at_utc, e.last_seen_scan_id, e.metadata_version "
        "FROM entries e JOIN tree t ON e.id=t.id WHERE t.depth > 0 "
        "ORDER BY t.depth ASC, e.is_dir DESC, e.normalized_name ASC, e.path ASC;");

    QSqlQuery q(m_connection.database());
    q.prepare(sql);
    q.addBindValue(root.id);
    if (maxDepth >= 0) {
        q.addBindValue(maxDepth);
    }

    if (!q.exec()) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }

    while (q.next()) {
        out->push_back(fromQuery(q));
    }
    return true;
}

bool EntryRepository::findSubtreeByRootPath(const QString& rootPath,
                                            int maxDepth,
                                            QVector<EntryRecord>* out,
                                            QString* errorText)
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("null_output_vector");
        }
        return false;
    }

    out->clear();
    EntryRecord root;
    if (!getByPath(rootPath, &root, errorText)) {
        return false;
    }

    QString sql = QStringLiteral(
        "WITH RECURSIVE tree(id, depth) AS ("
        "SELECT id, 0 FROM entries WHERE id=? "
        "UNION ALL "
        "SELECT e.id, tree.depth + 1 FROM entries e JOIN tree ON e.parent_id = tree.id ");
    if (maxDepth >= 0) {
        sql += QStringLiteral("WHERE tree.depth < ? ");
    }
    sql += QStringLiteral(
        ") "
        "SELECT e.id, e.volume_id, e.parent_id, e.path, e.name, e.normalized_name, e.extension, e.is_dir, e.size_bytes, "
        "e.created_utc, e.modified_utc, e.accessed_utc, e.hidden_flag, e.system_flag, e.readonly_flag, e.archive_flag, e.reparse_flag, "
        "e.exists_flag, e.file_id, e.indexed_at_utc, e.last_seen_scan_id, e.metadata_version "
        "FROM entries e JOIN tree t ON e.id=t.id "
        "ORDER BY t.depth ASC, e.path ASC;");

    QSqlQuery q(m_connection.database());
    q.prepare(sql);
    q.addBindValue(root.id);
    if (maxDepth >= 0) {
        q.addBindValue(maxDepth);
    }

    if (!q.exec()) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }

    while (q.next()) {
        out->push_back(fromQuery(q));
    }
    return true;
}

qint64 EntryRepository::countEntriesUnderRoot(const QString& rootPath, int maxDepth, QString* errorText)
{
    EntryRecord root;
    if (!getByPath(rootPath, &root, errorText)) {
        return -1;
    }

    QString sql = QStringLiteral(
        "WITH RECURSIVE tree(id, depth) AS ("
        "SELECT id, 0 FROM entries WHERE id=? "
        "UNION ALL "
        "SELECT e.id, tree.depth + 1 FROM entries e JOIN tree ON e.parent_id = tree.id ");
    if (maxDepth >= 0) {
        sql += QStringLiteral("WHERE tree.depth < ? ");
    }
    sql += QStringLiteral(
        ") "
        "SELECT COUNT(1) FROM tree WHERE depth > 0;");

    QSqlQuery q(m_connection.database());
    q.prepare(sql);
    q.addBindValue(root.id);
    if (maxDepth >= 0) {
        q.addBindValue(maxDepth);
    }
    if (!q.exec()) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return -1;
    }

    if (!q.next()) {
        return 0;
    }

    return q.value(0).toLongLong();
}

bool EntryRepository::listSampleRows(int limit, QVector<EntryRecord>* out, QString* errorText)
{
    return listSomeEntries(limit, out, errorText);
}

bool EntryRepository::resolveParentIdByPath(const QString& parentPath,
                                            qint64* parentId,
                                            bool* found,
                                            QString* errorText)
{
    if (!parentId || !found) {
        if (errorText) {
            *errorText = QStringLiteral("null_output_pointer");
        }
        return false;
    }

    *parentId = 0;
    *found = false;

    QSqlQuery q(m_connection.database());
    q.prepare(QStringLiteral("SELECT id FROM entries WHERE path=? LIMIT 1;"));
    q.addBindValue(parentPath);
    if (!q.exec()) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }

    if (!q.next()) {
        return true;
    }

    *parentId = q.value(0).toLongLong();
    *found = true;
    return true;
}

qint64 EntryRepository::countEntries(QString* errorText)
{
    QSqlQuery q(m_connection.database());
    if (!q.exec(QStringLiteral("SELECT COUNT(1) FROM entries;"))) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return -1;
    }
    if (!q.next()) {
        return 0;
    }
    return q.value(0).toLongLong();
}

qint64 EntryRepository::countDirectories(QString* errorText)
{
    QSqlQuery q(m_connection.database());
    if (!q.exec(QStringLiteral("SELECT COUNT(1) FROM entries WHERE is_dir=1;"))) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return -1;
    }
    if (!q.next()) {
        return 0;
    }
    return q.value(0).toLongLong();
}

qint64 EntryRepository::countFiles(QString* errorText)
{
    QSqlQuery q(m_connection.database());
    if (!q.exec(QStringLiteral("SELECT COUNT(1) FROM entries WHERE is_dir=0;"))) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return -1;
    }
    if (!q.next()) {
        return 0;
    }
    return q.value(0).toLongLong();
}

bool EntryRepository::listSomeEntries(int limit, QVector<EntryRecord>* out, QString* errorText)
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("null_output_vector");
        }
        return false;
    }

    out->clear();

    QSqlQuery q(m_connection.database());
    q.prepare(QStringLiteral(
        "SELECT id, volume_id, parent_id, path, name, normalized_name, extension, is_dir, size_bytes, "
        "created_utc, modified_utc, accessed_utc, hidden_flag, system_flag, readonly_flag, archive_flag, reparse_flag, "
        "exists_flag, file_id, indexed_at_utc, last_seen_scan_id, metadata_version "
        "FROM entries ORDER BY is_dir DESC, normalized_name ASC LIMIT ?;"));
    q.addBindValue(limit > 0 ? limit : 20);

    if (!q.exec()) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }

    while (q.next()) {
        out->push_back(fromQuery(q));
    }

    return true;
}
