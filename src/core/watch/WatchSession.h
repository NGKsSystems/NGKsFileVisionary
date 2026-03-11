#pragma once

#include <QString>

#include <atomic>
#include <functional>
#include <thread>

#include "ChangeEvent.h"

class WatchSession
{
public:
    using EventCallback = std::function<void(const ChangeEvent&)>;

    WatchSession();
    ~WatchSession();

    bool start(const QString& rootPath, EventCallback callback, QString* errorText = nullptr);
    void stop();
    bool isRunning() const;

private:
    void runLoop();

    QString m_rootPath;
    EventCallback m_callback;
    std::thread m_thread;
    std::atomic_bool m_running;
#ifdef _WIN32
    void* m_dirHandle;
#endif
};
