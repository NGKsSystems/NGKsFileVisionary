#pragma once

#include <QString>
#include <QtGlobal>

enum class StructuralTimelineChangeType
{
    Added,
    Removed,
    Persisting,
};

struct StructuralTimelineEvent
{
    QString timestamp;
    qint64 snapshotId = 0;
    QString filePath;
    QString relationshipType;
    QString targetPath;
    StructuralTimelineChangeType changeType = StructuralTimelineChangeType::Persisting;
};

namespace StructuralTimelineEventUtil
{
QString changeTypeToString(StructuralTimelineChangeType type);
}
