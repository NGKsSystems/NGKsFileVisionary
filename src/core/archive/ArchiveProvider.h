#pragma once

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QVector>

#include "ArchiveEntry.h"
#include "core/query/QueryTypes.h"
#include "ui/model/ViewModeController.h"

namespace ArchiveNav
{
class ArchiveReader;

class ArchiveProvider
{
public:
    ArchiveProvider();
    ~ArchiveProvider();

    bool canHandlePath(const QString& path) const;

    QueryResult query(const QString& rootPath,
                      ViewModeController::UiViewMode mode,
                      const QueryOptions& options,
                      QString* providerLog = nullptr);

private:
    struct CacheRecord
    {
        QDateTime lastModified;
        QVector<ArchiveEntry> entries;
    };

    struct ResolvedPath
    {
        QString archivePath;
        QString internalPath;
    };

    bool resolvePath(const QString& path, ResolvedPath* out) const;
    bool listArchiveCached(const QString& archivePath,
                           QVector<ArchiveEntry>* out,
                           QString* errorText,
                           QString* providerLog = nullptr);

private:
    ArchiveReader* m_reader = nullptr;
    QHash<QString, CacheRecord> m_cache;
};
}
