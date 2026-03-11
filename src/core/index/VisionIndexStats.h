#pragma once

#include <QString>

class MetaStore;

namespace VisionIndexEngine
{
struct VisionIndexStatsSnapshot
{
    qint64 fileCount = 0;
    qint64 directoryCount = 0;
    qint64 totalEntries = 0;
    QString indexAgeSeconds;
    QString lastRefreshUtc;
};

class VisionIndexStats
{
public:
    explicit VisionIndexStats(MetaStore& store);

    bool collect(const QString& rootPath,
                 VisionIndexStatsSnapshot* out,
                 QString* errorText = nullptr) const;

private:
    MetaStore& m_store;
};
}
