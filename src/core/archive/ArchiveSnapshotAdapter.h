#pragma once

#include <QString>
#include <QVector>

#include "core/query/QueryTypes.h"
#include "core/snapshot/SnapshotTypes.h"

namespace ArchiveNav
{
class ArchiveSnapshotAdapter
{
public:
    bool canHandlePath(const QString& rootPath) const;

    bool collectSnapshotEntries(const QString& rootPath,
                                const QueryOptions& options,
                                QVector<SnapshotEntryRecord>* out,
                                QString* errorText = nullptr,
                                QString* adapterLog = nullptr) const;
};
}
