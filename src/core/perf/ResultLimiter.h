#pragma once

#include "core/query/QueryTypes.h"

class ResultLimiter
{
public:
    static constexpr int kMaxResultsPerQuery = 10000;

    QueryResult limit(const QueryResult& input) const;
};
