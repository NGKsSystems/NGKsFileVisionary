#include "PerfTimer.h"

PerfTimer::PerfTimer()
    : m_start(std::chrono::steady_clock::now())
{
}

void PerfTimer::restart()
{
    m_start = std::chrono::steady_clock::now();
}

double PerfTimer::elapsedMs() const
{
    const auto delta = std::chrono::steady_clock::now() - m_start;
    return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(delta).count();
}
