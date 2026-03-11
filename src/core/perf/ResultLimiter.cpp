#include "ResultLimiter.h"

QueryResult ResultLimiter::limit(const QueryResult& input) const
{
    QueryResult out = input;
    if (out.rows.size() > kMaxResultsPerQuery) {
        out.rows.resize(kMaxResultsPerQuery);
        out.totalCount = kMaxResultsPerQuery;
    }
    return out;
}
