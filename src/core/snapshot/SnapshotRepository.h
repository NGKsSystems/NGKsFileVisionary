#pragma once

#include <QVector>

#include "SnapshotTypes.h"

class MetaStore;

class SnapshotRepository
{
public:
    explicit SnapshotRepository(MetaStore& store);

    bool createSnapshot(const SnapshotRecord& snapshot, qint64* snapshotId, QString* errorText = nullptr);
    bool updateSnapshotItemCount(qint64 snapshotId, qint64 itemCount, QString* errorText = nullptr);
    bool insertSnapshotEntries(qint64 snapshotId,
                               const QVector<SnapshotEntryRecord>& entries,
                               QString* errorText = nullptr);

    bool listSnapshots(const QString& rootPath,
                       QVector<SnapshotRecord>* out,
                       QString* errorText = nullptr);
    bool getSnapshotById(qint64 snapshotId, SnapshotRecord* out, QString* errorText = nullptr);
    bool getSnapshotByName(const QString& rootPath,
                           const QString& snapshotName,
                           SnapshotRecord* out,
                           QString* errorText = nullptr);
    bool listSnapshotEntries(qint64 snapshotId,
                             QVector<SnapshotEntryRecord>* out,
                             QString* errorText = nullptr);

private:
    MetaStore& m_store;
};
