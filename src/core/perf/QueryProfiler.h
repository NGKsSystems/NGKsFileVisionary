#pragma once

#include <functional>

#include "PerfMetrics.h"
#include "core/query/QueryTypes.h"

class QueryProfiler
{
public:
    explicit QueryProfiler(PerfMetrics& metrics);

    QueryResult profile(const QString& queryType, const std::function<QueryResult()>& fn, QString* outLogLine = nullptr);

private:
    PerfMetrics& m_metrics;
};
