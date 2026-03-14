#include "StructuralTimelineEvent.h"

namespace StructuralTimelineEventUtil
{
QString changeTypeToString(StructuralTimelineChangeType type)
{
    switch (type) {
    case StructuralTimelineChangeType::Added:
        return QStringLiteral("added");
    case StructuralTimelineChangeType::Removed:
        return QStringLiteral("removed");
    case StructuralTimelineChangeType::Persisting:
        return QStringLiteral("persisting");
    }

    return QStringLiteral("persisting");
}
}
