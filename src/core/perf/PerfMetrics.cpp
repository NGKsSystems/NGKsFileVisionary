#include "PerfMetrics.h"

void PerfMetrics::addQuerySample(double durationMs, qint64 rowCount)
{
    m_querySamples.push_back({durationMs, rowCount});
}

void PerfMetrics::addIngestSample(double durationMs, qint64 rowCount)
{
    m_ingestSamples.push_back({durationMs, rowCount});
}

void PerfMetrics::addWatcherSample(double durationMs, qint64 rowCount)
{
    m_watcherSamples.push_back({durationMs, rowCount});
}

void PerfMetrics::addDbCommitSample(double durationMs)
{
    m_dbCommitSamples.push_back({durationMs, 0});
}

double PerfMetrics::averageQueryMs() const
{
    return averageDuration(m_querySamples);
}

double PerfMetrics::averageIngestMs() const
{
    return averageDuration(m_ingestSamples);
}

double PerfMetrics::averageWatcherMs() const
{
    return averageDuration(m_watcherSamples);
}

double PerfMetrics::averageDbCommitMs() const
{
    return averageDuration(m_dbCommitSamples);
}

qint64 PerfMetrics::totalQueryRows() const
{
    return totalRows(m_querySamples);
}

qint64 PerfMetrics::totalIngestRows() const
{
    return totalRows(m_ingestSamples);
}

qint64 PerfMetrics::totalWatcherRows() const
{
    return totalRows(m_watcherSamples);
}

double PerfMetrics::averageDuration(const QVector<PerfSample>& samples)
{
    if (samples.isEmpty()) {
        return 0.0;
    }

    double total = 0.0;
    for (const PerfSample& sample : samples) {
        total += sample.durationMs;
    }
    return total / static_cast<double>(samples.size());
}

qint64 PerfMetrics::totalRows(const QVector<PerfSample>& samples)
{
    qint64 total = 0;
    for (const PerfSample& sample : samples) {
        total += sample.rowCount;
    }
    return total;
}
