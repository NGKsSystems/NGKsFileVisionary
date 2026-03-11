#pragma once

#include <chrono>

class PerfTimer
{
public:
    PerfTimer();

    void restart();
    double elapsedMs() const;

private:
    std::chrono::steady_clock::time_point m_start;
};
