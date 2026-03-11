#include "DbMigrations.h"

#include <QStringList>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "DbConnection.h"
#include "SqlHelpers.h"

namespace {
bool execBatch(QSqlDatabase db, const QStringList& sqlStatements, QString* errorText)
{
    QSqlQuery q(db);
    for (const QString& statement : sqlStatements) {
        if (!q.exec(statement)) {
            if (errorText) {
                *errorText = q.lastError().text();
            }
            return false;
        }
    }
    return true;
}

bool columnExists(QSqlDatabase db, const QString& tableName, const QString& columnName, QString* errorText)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA table_info(%1);").arg(tableName))) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }

    while (q.next()) {
        if (q.value(1).toString().compare(columnName, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}
}

bool DbMigrations::migrate(DbConnection& connection, QString* migrationLog)
{
    QString errorText;
    if (!ensureSchemaInfoTable(connection, &errorText)) {
        if (migrationLog) {
            migrationLog->append(QStringLiteral("ensure_schema_info_failed: %1\n").arg(errorText));
        }
        return false;
    }

    const int current = currentVersion(connection, &errorText);
    if (current < 0) {
        if (migrationLog) {
            migrationLog->append(QStringLiteral("read_current_version_failed: %1\n").arg(errorText));
        }
        return false;
    }

    if (migrationLog) {
        migrationLog->append(QStringLiteral("current_version=%1\n").arg(current));
    }

    if (current >= kSchemaVersion) {
        if (migrationLog) {
            migrationLog->append(QStringLiteral("schema_up_to_date=true\n"));
        }
        return true;
    }

    if (!connection.beginTransaction()) {
        if (migrationLog) {
            migrationLog->append(QStringLiteral("begin_tx_failed: %1\n").arg(connection.lastError()));
        }
        return false;
    }

    bool ok = true;
    if (current < 1) {
        ok = applyV1(connection, &errorText);
        if (migrationLog) {
            migrationLog->append(ok ? QStringLiteral("applied_v1=true\n") : QStringLiteral("applied_v1=false\n"));
        }
    }

    if (ok && current < 2) {
        ok = applyV2(connection, &errorText);
        if (migrationLog) {
            migrationLog->append(ok ? QStringLiteral("applied_v2=true\n") : QStringLiteral("applied_v2=false\n"));
        }
    }

    if (!ok) {
        connection.rollback();
        if (migrationLog) {
            migrationLog->append(QStringLiteral("migration_error=%1\n").arg(errorText));
        }
        return false;
    }

    QSqlQuery q(connection.database());
    q.prepare(QStringLiteral("INSERT INTO schema_info(schema_version, applied_utc) VALUES(?, ?);"));
    q.addBindValue(kSchemaVersion);
    q.addBindValue(SqlHelpers::utcNowIso());
    if (!q.exec()) {
        connection.rollback();
        if (migrationLog) {
            migrationLog->append(QStringLiteral("schema_info_insert_failed=%1\n").arg(q.lastError().text()));
        }
        return false;
    }

    if (!connection.commit()) {
        if (migrationLog) {
            migrationLog->append(QStringLiteral("commit_failed=%1\n").arg(connection.lastError()));
        }
        return false;
    }

    if (migrationLog) {
        migrationLog->append(QStringLiteral("schema_version=%1\n").arg(kSchemaVersion));
    }
    return true;
}

int DbMigrations::currentVersion(DbConnection& connection, QString* errorText)
{
    if (!ensureSchemaInfoTable(connection, errorText)) {
        return -1;
    }

    QSqlQuery q(connection.database());
    if (!q.exec(QStringLiteral("SELECT COALESCE(MAX(schema_version), 0) FROM schema_info;"))) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return -1;
    }

    if (!q.next()) {
        return 0;
    }

    return q.value(0).toInt();
}

bool DbMigrations::ensureSchemaInfoTable(DbConnection& connection, QString* errorText)
{
    QSqlQuery q(connection.database());
    if (!q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS schema_info(" \
                               "schema_version INTEGER NOT NULL," \
                               "applied_utc TEXT NOT NULL" \
                               ");"))) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }
    return true;
}

bool DbMigrations::applyV1(DbConnection& connection, QString* errorText)
{
    const QStringList statements = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS volumes(" \
                       "id INTEGER PRIMARY KEY AUTOINCREMENT," \
                       "volume_key TEXT NOT NULL UNIQUE," \
                       "root_path TEXT NOT NULL," \
                       "display_name TEXT," \
                       "fs_type TEXT," \
                       "serial_number TEXT," \
                       "created_utc TEXT NOT NULL," \
                       "updated_utc TEXT NOT NULL" \
                       ");"),

        QStringLiteral("CREATE TABLE IF NOT EXISTS scan_sessions(" \
                       "id INTEGER PRIMARY KEY AUTOINCREMENT," \
                       "root_path TEXT NOT NULL," \
                       "mode TEXT NOT NULL," \
                       "started_utc TEXT NOT NULL," \
                       "completed_utc TEXT," \
                       "status TEXT NOT NULL," \
                       "total_seen INTEGER NOT NULL DEFAULT 0," \
                       "total_inserted INTEGER NOT NULL DEFAULT 0," \
                       "total_updated INTEGER NOT NULL DEFAULT 0," \
                       "total_removed INTEGER NOT NULL DEFAULT 0," \
                       "error_text TEXT" \
                       ");"),

        QStringLiteral("CREATE TABLE IF NOT EXISTS entries(" \
                       "id INTEGER PRIMARY KEY AUTOINCREMENT," \
                       "volume_id INTEGER NOT NULL," \
                       "parent_id INTEGER," \
                       "path TEXT NOT NULL UNIQUE," \
                       "name TEXT NOT NULL," \
                       "normalized_name TEXT NOT NULL," \
                       "extension TEXT," \
                       "is_dir INTEGER NOT NULL," \
                       "size_bytes INTEGER," \
                       "created_utc TEXT," \
                       "modified_utc TEXT," \
                       "accessed_utc TEXT," \
                       "hidden_flag INTEGER NOT NULL DEFAULT 0," \
                       "system_flag INTEGER NOT NULL DEFAULT 0," \
                       "readonly_flag INTEGER NOT NULL DEFAULT 0," \
                       "archive_flag INTEGER NOT NULL DEFAULT 0," \
                       "reparse_flag INTEGER NOT NULL DEFAULT 0," \
                       "exists_flag INTEGER NOT NULL DEFAULT 1," \
                       "file_id TEXT," \
                       "indexed_at_utc TEXT NOT NULL," \
                       "last_seen_scan_id INTEGER," \
                       "metadata_version INTEGER NOT NULL DEFAULT 1," \
                       "FOREIGN KEY(volume_id) REFERENCES volumes(id)," \
                       "FOREIGN KEY(parent_id) REFERENCES entries(id)" \
                       ");"),

        QStringLiteral("CREATE TABLE IF NOT EXISTS favorites(" \
                       "id INTEGER PRIMARY KEY AUTOINCREMENT," \
                       "path TEXT NOT NULL UNIQUE," \
                       "label TEXT," \
                       "pinned_utc TEXT NOT NULL," \
                       "sort_order INTEGER NOT NULL DEFAULT 0" \
                       ");"),

        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entries_parent_id ON entries(parent_id);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entries_path ON entries(path);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entries_name ON entries(normalized_name);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entries_extension ON entries(extension);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entries_is_dir ON entries(is_dir);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entries_modified_utc ON entries(modified_utc);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entries_parent_name ON entries(parent_id, normalized_name);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entries_parent_dir ON entries(parent_id, is_dir);")
    };

    return execBatch(connection.database(), statements, errorText);
}

bool DbMigrations::applyV2(DbConnection& connection, QString* errorText)
{
    QSqlDatabase db = connection.database();

    const QStringList newTables = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS index_roots(" \
                       "id INTEGER PRIMARY KEY AUTOINCREMENT," \
                       "root_path TEXT NOT NULL UNIQUE," \
                       "status TEXT NOT NULL," \
                       "last_scan_version INTEGER NOT NULL DEFAULT 0," \
                       "last_indexed_utc TEXT," \
                       "created_utc TEXT NOT NULL," \
                       "updated_utc TEXT NOT NULL" \
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS index_journal(" \
                       "id INTEGER PRIMARY KEY AUTOINCREMENT," \
                       "root_path TEXT NOT NULL," \
                       "path TEXT NOT NULL," \
                       "event_type TEXT NOT NULL," \
                       "scan_version INTEGER NOT NULL DEFAULT 0," \
                       "payload TEXT," \
                       "created_utc TEXT NOT NULL" \
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS index_stats(" \
                       "key TEXT PRIMARY KEY," \
                       "value TEXT NOT NULL," \
                       "updated_utc TEXT NOT NULL" \
                       ");")
    };

    if (!execBatch(db, newTables, errorText)) {
        return false;
    }

    struct ColumnDef
    {
        QString name;
        QString typeSql;
    };
    const QVector<ColumnDef> newColumns = {
        {QStringLiteral("parent_path"), QStringLiteral("TEXT")},
        {QStringLiteral("scan_version"), QStringLiteral("INTEGER NOT NULL DEFAULT 0")},
        {QStringLiteral("entry_hash"), QStringLiteral("TEXT")}
    };

    for (const ColumnDef& col : newColumns) {
        const bool exists = columnExists(db, QStringLiteral("entries"), col.name, errorText);
        if (!exists && errorText && !errorText->isEmpty()) {
            return false;
        }
        if (exists) {
            continue;
        }

        QSqlQuery alter(db);
        const QString sql = QStringLiteral("ALTER TABLE entries ADD COLUMN %1 %2;").arg(col.name, col.typeSql);
        if (!alter.exec(sql)) {
            if (errorText) {
                *errorText = alter.lastError().text();
            }
            return false;
        }
    }

    const QStringList indexes = {
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entries_parent_path ON entries(parent_path);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entries_extension_v2 ON entries(extension);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entries_modified_time ON entries(modified_utc);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_index_journal_root_created ON index_journal(root_path, created_utc);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_index_journal_path ON index_journal(path);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_index_roots_path ON index_roots(root_path);")
    };

    return execBatch(db, indexes, errorText);
}
