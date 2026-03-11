#pragma once

#include <QVector>

struct PerfSample
{
    double durationMs = 0.0;
    qint64 rowCount = 0;
};

class PerfMetrics
{
public:
    void addQuerySample(double durationMs, qint64 rowCount);
    void addIngestSample(double durationMs, qint64 rowCount);
    void addWatcherSample(double durationMs, qint64 rowCount);
    void addDbCommitSample(double durationMs);

    double averageQueryMs() const;
    double averageIngestMs() const;
    double averageWatcherMs() const;
    double averageDbCommitMs() const;

    qint64 totalQueryRows() const;
    qint64 totalIngestRows() const;
    qint64 totalWatcherRows() const;

private:
    static double averageDuration(const QVector<PerfSample>& samples);
    static qint64 totalRows(const QVector<PerfSample>& samples);

    QVector<PerfSample> m_querySamples;
    QVector<PerfSample> m_ingestSamples;
    QVector<PerfSample> m_watcherSamples;
    QVector<PerfSample> m_dbCommitSamples;
};
