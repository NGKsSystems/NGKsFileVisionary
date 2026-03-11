#pragma once

#include <QString>

struct RefreshPolicy
{
    int staleThresholdSeconds = 120;
    int dedupeWindowSeconds = 5;
    QString defaultMode = QStringLiteral("visible_refresh");
    bool alwaysRefreshVisiblePath = false;
};

struct RefreshContext
{
    bool force = false;
    QString mode;
    int staleThresholdSeconds = -1;
    QString reason;
};
