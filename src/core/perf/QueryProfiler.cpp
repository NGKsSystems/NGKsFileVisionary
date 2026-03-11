#include "QueryProfiler.h"

#include "PerfTimer.h"

QueryProfiler::QueryProfiler(PerfMetrics& metrics)
    : m_metrics(metrics)
{
}

QueryResult QueryProfiler::profile(const QString& queryType,
                                   const std::function<QueryResult()>& fn,
                                   QString* outLogLine)
{
    PerfTimer timer;
    const QueryResult result = fn();
    const double durationMs = timer.elapsedMs();
    const qint64 rowCount = result.ok ? result.rows.size() : 0;
    m_metrics.addQuerySample(durationMs, rowCount);

    if (outLogLine) {
        *outLogLine = QStringLiteral("query_type=%1 row_count=%2 duration_ms=%3 ok=%4")
                          .arg(queryType)
                          .arg(rowCount)
                          .arg(durationMs, 0, 'f', 3)
                          .arg(result.ok ? QStringLiteral("true") : QStringLiteral("false"));
    }

    return result;
}
