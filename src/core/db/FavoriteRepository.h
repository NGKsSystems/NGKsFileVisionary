#pragma once

#include <QVector>

#include "DbTypes.h"

class DbConnection;

class FavoriteRepository
{
public:
    explicit FavoriteRepository(DbConnection& connection);

    bool upsertFavorite(const FavoriteRecord& record, qint64* favoriteId = nullptr, QString* errorText = nullptr);
    bool removeByPath(const QString& path, QString* errorText = nullptr);
    bool listAll(QVector<FavoriteRecord>* out, QString* errorText = nullptr);

private:
    DbConnection& m_connection;
};
