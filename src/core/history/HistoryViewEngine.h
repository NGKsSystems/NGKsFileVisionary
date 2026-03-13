#pragma once

#include <QString>
#include <QVector>

#include "HistoryEntry.h"
#include "HistorySummary.h"
#include "core/snapshot/SnapshotDiffEngine.h"
#include "core/snapshot/SnapshotRepository.h"

class HistoryViewEngine
{
public:
    HistoryViewEngine(SnapshotRepository& repository, const SnapshotDiffEngine& diffEngine);

    bool listSnapshotsForRoot(const QString& rootPath,
                              QVector<SnapshotRecord>* out,
                              QString* errorText = nullptr) const;

    bool getPathHistory(const QString& rootPath,
                        const QString& targetPath,
                        QVector<HistoryEntry>* out,
                        QString* errorText = nullptr) const;

    bool summarizePathHistory(const QString& rootPath,
                              const QString& targetPath,
                              PathHistorySummary* out,
                              QString* errorText = nullptr) const;

    bool getRootHistorySummary(const QString& rootPath,
                               RootHistorySummary* out,
                               QString* errorText = nullptr) const;

private:
    SnapshotRepository& m_repository;
    const SnapshotDiffEngine& m_diffEngine;
};
