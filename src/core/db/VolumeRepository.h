#pragma once

#include <QString>

#include "DbTypes.h"

class DbConnection;

class VolumeRepository
{
public:
    explicit VolumeRepository(DbConnection& connection);

    bool upsertVolume(const VolumeRecord& record, qint64* volumeId = nullptr, QString* errorText = nullptr);
    bool getByRootPath(const QString& rootPath, VolumeRecord* out, QString* errorText = nullptr);

private:
    DbConnection& m_connection;
};
