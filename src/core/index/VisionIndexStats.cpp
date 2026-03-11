#include "VisionIndexStats.h"

#include <QDateTime>

#include "core/db/MetaStore.h"

namespace VisionIndexEngine
{
VisionIndexStats::VisionIndexStats(MetaStore& store)
    : m_store(store)
{
}

bool VisionIndexStats::collect(const QString& rootPath,
                               VisionIndexStatsSnapshot* out,
                               QString* errorText) const
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("null_stats_output");
        }
        return false;
    }

    out->totalEntries = m_store.countEntriesUnderRoot(rootPath, -1, errorText);
    if (out->totalEntries < 0) {
        return false;
    }

    QVector<EntryRecord> rows;
    if (!m_store.findSubtreeByRootPath(rootPath, -1, &rows, errorText)) {
        return false;
    }

    out->fileCount = 0;
    out->directoryCount = 0;
    QString latestIndexed;
    for (const EntryRecord& row : rows) {
        if (row.isDir) {
            out->directoryCount += 1;
        } else {
            out->fileCount += 1;
        }
        if (row.indexedAtUtc > latestIndexed) {
            latestIndexed = row.indexedAtUtc;
        }
    }

    out->lastRefreshUtc = latestIndexed;
    if (!latestIndexed.isEmpty()) {
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const QDateTime indexed = QDateTime::fromString(latestIndexed, Qt::ISODate);
        if (indexed.isValid()) {
            out->indexAgeSeconds = QString::number(indexed.secsTo(now));
        }
    }

    return true;
}
}
