#pragma once

#include <QString>

#include "QueryParseResult.h"

class QueryParser
{
public:
    QueryParseResult parse(const QString& queryString) const;
};
