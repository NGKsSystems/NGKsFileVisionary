#include "SnapshotDiffTypes.h"

namespace SnapshotDiffTypesUtil
{
QString statusToString(SnapshotDiffStatus status)
{
    switch (status) {
    case SnapshotDiffStatus::Added:
        return QStringLiteral("added");
    case SnapshotDiffStatus::Removed:
        return QStringLiteral("removed");
    case SnapshotDiffStatus::Changed:
        return QStringLiteral("changed");
    case SnapshotDiffStatus::Unchanged:
        return QStringLiteral("unchanged");
    }
    return QStringLiteral("unchanged");
}
}
