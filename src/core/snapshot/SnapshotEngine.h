#pragma once

#include <QString>
#include <QVector>

#include "SnapshotTypes.h"

class MetaStore;
class SnapshotRepository;

class SnapshotEngine
{
public:
    explicit SnapshotEngine(MetaStore& store);
    ~SnapshotEngine();

    SnapshotCreateResult createSnapshot(const QString& rootPath,
                                        const QString& snapshotName,
                                        const SnapshotCreateOptions& options);

    bool listSnapshots(const QString& rootPath,
                       QVector<SnapshotRecord>* out,
                       QString* errorText = nullptr) const;
    bool getSnapshot(qint64 snapshotId, SnapshotRecord* out, QString* errorText = nullptr) const;
    bool getSnapshot(const QString& rootPath,
                     const QString& snapshotName,
                     SnapshotRecord* out,
                     QString* errorText = nullptr) const;
    bool getSnapshotEntries(qint64 snapshotId,
                            QVector<SnapshotEntryRecord>* out,
                            QString* errorText = nullptr) const;
    bool exportSnapshotSummary(qint64 snapshotId, QString* summaryOut, QString* errorText = nullptr) const;

private:
    MetaStore& m_store;
    SnapshotRepository* m_repo;
};
