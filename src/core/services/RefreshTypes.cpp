#include "RefreshTypes.h"

namespace RefreshTypes
{
QString stateToString(RefreshState state)
{
    switch (state) {
    case RefreshState::Idle:
        return QStringLiteral("idle");
    case RefreshState::Queued:
        return QStringLiteral("queued");
    case RefreshState::Running:
        return QStringLiteral("running");
    case RefreshState::Completed:
        return QStringLiteral("completed");
    case RefreshState::Failed:
        return QStringLiteral("failed");
    case RefreshState::Canceled:
        return QStringLiteral("canceled");
    case RefreshState::SkippedDuplicate:
        return QStringLiteral("skipped_duplicate");
    case RefreshState::SkippedFresh:
        return QStringLiteral("skipped_fresh");
    }
    return QStringLiteral("idle");
}

QString utcNowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

bool isTerminalState(RefreshState state)
{
    return state == RefreshState::Completed
        || state == RefreshState::Failed
        || state == RefreshState::Canceled
        || state == RefreshState::SkippedDuplicate
        || state == RefreshState::SkippedFresh;
}

bool isSuccessfulState(RefreshState state)
{
    return state == RefreshState::Completed || state == RefreshState::SkippedFresh;
}
}
