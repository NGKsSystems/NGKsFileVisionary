#pragma once

#include <QString>

#include "SnapshotDiffTypes.h"

class SnapshotRepository;

class SnapshotDiffEngine
{
public:
    explicit SnapshotDiffEngine(SnapshotRepository& repository);

    SnapshotDiffResult compareSnapshots(qint64 oldSnapshotId,
                                        qint64 newSnapshotId,
                                        const SnapshotDiffOptions& options) const;
    SnapshotDiffSummary summarizeDiff(const SnapshotDiffResult& result) const;
    bool exportDiffSummary(const SnapshotDiffResult& result, QString* summaryOut, QString* errorText = nullptr) const;

private:
    SnapshotRepository& m_repository;
};
