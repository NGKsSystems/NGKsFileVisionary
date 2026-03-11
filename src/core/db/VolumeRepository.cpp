#include "VolumeRepository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "DbConnection.h"
#include "SqlHelpers.h"

VolumeRepository::VolumeRepository(DbConnection& connection)
    : m_connection(connection)
{
}

bool VolumeRepository::upsertVolume(const VolumeRecord& record, qint64* volumeId, QString* errorText)
{
    QSqlQuery q(m_connection.database());
    q.prepare(QStringLiteral(
        "INSERT INTO volumes(volume_key, root_path, display_name, fs_type, serial_number, created_utc, updated_utc) "
        "VALUES(?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(volume_key) DO UPDATE SET "
        "root_path=excluded.root_path, "
        "display_name=excluded.display_name, "
        "fs_type=excluded.fs_type, "
        "serial_number=excluded.serial_number, "
        "updated_utc=excluded.updated_utc;"));

    const QString now = SqlHelpers::utcNowIso();
    q.addBindValue(record.volumeKey);
    q.addBindValue(record.rootPath);
    q.addBindValue(record.displayName);
    q.addBindValue(record.fsType);
    q.addBindValue(record.serialNumber);
    q.addBindValue(record.createdUtc.isEmpty() ? now : record.createdUtc);
    q.addBindValue(record.updatedUtc.isEmpty() ? now : record.updatedUtc);

    if (!q.exec()) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }

    if (!volumeId) {
        return true;
    }

    QSqlQuery readQ(m_connection.database());
    readQ.prepare(QStringLiteral("SELECT id FROM volumes WHERE volume_key=?;"));
    readQ.addBindValue(record.volumeKey);
    if (!readQ.exec() || !readQ.next()) {
        if (errorText) {
            *errorText = readQ.lastError().isValid() ? readQ.lastError().text() : QStringLiteral("volume_id_lookup_failed");
        }
        return false;
    }

    *volumeId = readQ.value(0).toLongLong();
    return true;
}

bool VolumeRepository::getByRootPath(const QString& rootPath, VolumeRecord* out, QString* errorText)
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("null_output_record");
        }
        return false;
    }

    QSqlQuery q(m_connection.database());
    q.prepare(QStringLiteral(
        "SELECT id, volume_key, root_path, display_name, fs_type, serial_number, created_utc, updated_utc "
        "FROM volumes WHERE root_path=? LIMIT 1;"));
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
    out->volumeKey = q.value(1).toString();
    out->rootPath = q.value(2).toString();
    out->displayName = q.value(3).toString();
    out->fsType = q.value(4).toString();
    out->serialNumber = q.value(5).toString();
    out->createdUtc = q.value(6).toString();
    out->updatedUtc = q.value(7).toString();
    return true;
}
