#pragma once

#include <QString>

#include "QueryPlan.h"

struct QueryParseResult
{
    bool ok = false;
    QueryPlan plan;
    QString errorMessage;
    QString duplicatePolicy = QStringLiteral("last_token_wins");
};
