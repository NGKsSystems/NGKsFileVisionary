#include "FavoriteRepository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "DbConnection.h"
#include "SqlHelpers.h"

FavoriteRepository::FavoriteRepository(DbConnection& connection)
    : m_connection(connection)
{
}

bool FavoriteRepository::upsertFavorite(const FavoriteRecord& record, qint64* favoriteId, QString* errorText)
{
    QSqlQuery q(m_connection.database());
    q.prepare(QStringLiteral(
        "INSERT INTO favorites(path, label, pinned_utc, sort_order) VALUES(?, ?, ?, ?) "
        "ON CONFLICT(path) DO UPDATE SET "
        "label=excluded.label, "
        "pinned_utc=excluded.pinned_utc, "
        "sort_order=excluded.sort_order;"));

    q.addBindValue(record.path);
    q.addBindValue(record.label);
    q.addBindValue(record.pinnedUtc.isEmpty() ? SqlHelpers::utcNowIso() : record.pinnedUtc);
    q.addBindValue(record.sortOrder);

    if (!q.exec()) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }

    if (!favoriteId) {
        return true;
    }

    QSqlQuery readQ(m_connection.database());
    readQ.prepare(QStringLiteral("SELECT id FROM favorites WHERE path=?;"));
    readQ.addBindValue(record.path);
    if (!readQ.exec() || !readQ.next()) {
        if (errorText) {
            *errorText = readQ.lastError().isValid() ? readQ.lastError().text() : QStringLiteral("favorite_id_lookup_failed");
        }
        return false;
    }

    *favoriteId = readQ.value(0).toLongLong();
    return true;
}

bool FavoriteRepository::removeByPath(const QString& path, QString* errorText)
{
    QSqlQuery q(m_connection.database());
    q.prepare(QStringLiteral("DELETE FROM favorites WHERE path=?;"));
    q.addBindValue(path);
    if (!q.exec()) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }
    return true;
}

bool FavoriteRepository::listAll(QVector<FavoriteRecord>* out, QString* errorText)
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("null_output_vector");
        }
        return false;
    }

    out->clear();

    QSqlQuery q(m_connection.database());
    if (!q.exec(QStringLiteral("SELECT id, path, label, pinned_utc, sort_order FROM favorites ORDER BY sort_order ASC, id ASC;"))) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }

    while (q.next()) {
        FavoriteRecord r;
        r.id = q.value(0).toLongLong();
        r.path = q.value(1).toString();
        r.label = q.value(2).toString();
        r.pinnedUtc = q.value(3).toString();
        r.sortOrder = q.value(4).toInt();
        out->push_back(r);
    }

    return true;
}
