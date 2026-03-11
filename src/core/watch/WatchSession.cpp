#include "WatchSession.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {
ChangeEventType actionToType(unsigned long action)
{
#ifdef _WIN32
    switch (action) {
    case FILE_ACTION_ADDED:
        return ChangeEventType::Created;
    case FILE_ACTION_MODIFIED:
        return ChangeEventType::Modified;
    case FILE_ACTION_REMOVED:
        return ChangeEventType::Deleted;
    case FILE_ACTION_RENAMED_OLD_NAME:
        return ChangeEventType::RenamedOld;
    case FILE_ACTION_RENAMED_NEW_NAME:
        return ChangeEventType::RenamedNew;
    default:
        return ChangeEventType::Unknown;
    }
#else
    Q_UNUSED(action);
    return ChangeEventType::Unknown;
#endif
}
}

WatchSession::WatchSession()
    : m_running(false)
#ifdef _WIN32
    , m_dirHandle(INVALID_HANDLE_VALUE)
#endif
{
}

WatchSession::~WatchSession()
{
    stop();
}

bool WatchSession::start(const QString& rootPath, EventCallback callback, QString* errorText)
{
#ifndef _WIN32
    if (errorText) {
        *errorText = QStringLiteral("watch_session_windows_only");
    }
    return false;
#else
    if (m_running.load()) {
        if (errorText) {
            *errorText = QStringLiteral("watch_session_already_running");
        }
        return false;
    }

    QFileInfo rootInfo(rootPath);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        if (errorText) {
            *errorText = QStringLiteral("invalid_watch_root");
        }
        return false;
    }

    m_rootPath = QDir::cleanPath(rootPath);
    m_callback = std::move(callback);

    const std::wstring widePath = QDir::toNativeSeparators(m_rootPath).toStdWString();
    HANDLE handle = CreateFileW(widePath.c_str(),
                                FILE_LIST_DIRECTORY,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr,
                                OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS,
                                nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        if (errorText) {
            *errorText = QStringLiteral("CreateFileW_failed");
        }
        return false;
    }

    m_dirHandle = handle;
    m_running.store(true);
    m_thread = std::thread(&WatchSession::runLoop, this);
    return true;
#endif
}

void WatchSession::stop()
{
#ifdef _WIN32
    if (!m_running.exchange(false)) {
        return;
    }

    if (m_dirHandle && m_dirHandle != INVALID_HANDLE_VALUE) {
        CancelIoEx(static_cast<HANDLE>(m_dirHandle), nullptr);
        CloseHandle(static_cast<HANDLE>(m_dirHandle));
        m_dirHandle = INVALID_HANDLE_VALUE;
    }

    if (m_thread.joinable()) {
        m_thread.join();
    }
#endif
}

bool WatchSession::isRunning() const
{
    return m_running.load();
}

void WatchSession::runLoop()
{
#ifdef _WIN32
    static constexpr DWORD kBufferSize = 64 * 1024;
    static constexpr DWORD kNotifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME
        | FILE_NOTIFY_CHANGE_DIR_NAME
        | FILE_NOTIFY_CHANGE_LAST_WRITE
        | FILE_NOTIFY_CHANGE_CREATION
        | FILE_NOTIFY_CHANGE_SIZE;

    BYTE buffer[kBufferSize];

    while (m_running.load()) {
        DWORD bytesReturned = 0;
        const BOOL ok = ReadDirectoryChangesW(static_cast<HANDLE>(m_dirHandle),
                                              buffer,
                                              kBufferSize,
                                              TRUE,
                                              kNotifyFilter,
                                              &bytesReturned,
                                              nullptr,
                                              nullptr);
        if (!ok) {
            break;
        }

        if (bytesReturned == 0) {
            continue;
        }

        DWORD offset = 0;
        while (offset < bytesReturned) {
            const FILE_NOTIFY_INFORMATION* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer + offset);
            const QString relativePath = QString::fromWCharArray(info->FileName, static_cast<int>(info->FileNameLength / sizeof(WCHAR)));
            const QString absolutePath = QDir(m_rootPath).filePath(relativePath);

            ChangeEvent event;
            event.type = actionToType(info->Action);
            event.watchedRoot = m_rootPath;
            event.targetPath = QDir::cleanPath(absolutePath);
            event.timestampUtc = QDateTime::currentDateTimeUtc();

            if (m_callback) {
                m_callback(event);
            }

            if (info->NextEntryOffset == 0) {
                break;
            }
            offset += info->NextEntryOffset;
        }
    }
#endif
}
