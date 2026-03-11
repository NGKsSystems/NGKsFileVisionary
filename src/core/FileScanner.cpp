#include "FileScanner.h"

#include <QDir>
#include <QFileInfo>
#include <QThread>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

FileScanner::FileScanner(QObject* parent)
    : QObject(parent)
    , m_cancelRequested(false)
{
}

void FileScanner::startScan(quint64 scanId,
                            const QString& root,
                            bool showHidden,
                            bool showSystem,
                            const QStringList& extensions,
                            const QString& searchText,
                            int viewMode)
{
    m_cancelRequested.storeRelease(false);

    QDir rootDir(root);
    if (!rootDir.exists()) {
        emit finished(scanId, false, 0, 0, QStringLiteral("Root does not exist"));
        return;
    }

    emit progress(scanId, QStringLiteral("Enumerating"), 0, 0);

    QVector<FileEntry> batch;
    batch.reserve(128);
    quint64 enumerated = 0;
    quint64 matched = 0;

    const FileViewMode mode = static_cast<FileViewMode>(viewMode);

    auto allowsTraversal = [&](const QFileInfo& fileInfo) -> bool {
        if (!fileInfo.isDir()) {
            return false;
        }
        if (!showHidden && fileInfo.isHidden()) {
            return false;
        }
#ifdef Q_OS_WIN
        if (!showSystem) {
            const std::wstring widePath = fileInfo.absoluteFilePath().toStdWString();
            const DWORD attrs = GetFileAttributesW(widePath.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_SYSTEM)) {
                return false;
            }
        }
#else
        Q_UNUSED(showSystem);
#endif
        return true;
    };

    auto publishEntry = [&](const QFileInfo& fileInfo) {
        FileEntry entry;
        entry.name = fileInfo.fileName();
        entry.isDir = fileInfo.isDir();
        entry.size = static_cast<quint64>(fileInfo.size());
        entry.modified = fileInfo.lastModified();
        entry.absolutePath = fileInfo.absoluteFilePath();
        batch.push_back(entry);

        if (batch.size() >= 128) {
            emit progress(scanId, QStringLiteral("Publishing"), enumerated, matched);
            emit batchReady(scanId, batch);
            batch.clear();
        }
    };

    auto processEntries = [&](const QFileInfoList& entries) {
        for (const QFileInfo& fileInfo : entries) {
            if (m_cancelRequested.loadAcquire()) {
                return false;
            }

            enumerated += 1;
            const bool passes = matchesFilters(fileInfo, showHidden, showSystem, extensions, searchText);
            if (!passes) {
                if ((enumerated % 200ULL) == 0ULL) {
                    emit progress(scanId, QStringLiteral("Filtering"), enumerated, matched);
                }
                continue;
            }

            if (mode == FileViewMode::FlatFiles && fileInfo.isDir()) {
                continue;
            }

            matched += 1;
            publishEntry(fileInfo);
        }
        return true;
    };

    if (mode == FileViewMode::Standard) {
        const QFileInfoList entries = rootDir.entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Readable,
            QDir::DirsFirst | QDir::IgnoreCase);
        if (!processEntries(entries)) {
            emit finished(scanId, true, enumerated, matched, QString());
            return;
        }
    } else {
        QVector<QString> pendingDirs;
        pendingDirs.push_back(rootDir.absolutePath());

        while (!pendingDirs.isEmpty()) {
            if (m_cancelRequested.loadAcquire()) {
                emit finished(scanId, true, enumerated, matched, QString());
                return;
            }

            const QString currentPath = pendingDirs.back();
            pendingDirs.pop_back();
            const QDir currentDir(currentPath);
            const QFileInfoList entries = currentDir.entryInfoList(
                QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Readable,
                QDir::DirsFirst | QDir::IgnoreCase);

            for (const QFileInfo& fileInfo : entries) {
                if (fileInfo.isDir() && allowsTraversal(fileInfo)) {
                    pendingDirs.push_back(fileInfo.absoluteFilePath());
                }
            }

            if (!processEntries(entries)) {
                emit finished(scanId, true, enumerated, matched, QString());
                return;
            }
        }
    }

    if (!batch.isEmpty()) {
        emit progress(scanId, QStringLiteral("Publishing"), enumerated, matched);
        emit batchReady(scanId, batch);
    }

    emit finished(scanId, false, enumerated, matched, QString());
}

void FileScanner::cancel()
{
    m_cancelRequested.storeRelease(true);
}

bool FileScanner::matchesFilters(const QFileInfo& fileInfo,
                                 bool showHidden,
                                 bool showSystem,
                                 const QStringList& extensions,
                                 const QString& searchText) const
{
    if (!showHidden && fileInfo.isHidden()) {
        return false;
    }
#ifdef Q_OS_WIN
    if (!showSystem) {
        const std::wstring widePath = fileInfo.absoluteFilePath().toStdWString();
        const DWORD attrs = GetFileAttributesW(widePath.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_SYSTEM)) {
            return false;
        }
    }
#else
    Q_UNUSED(showSystem);
#endif

    if (!searchText.isEmpty() && !fileInfo.fileName().contains(searchText, Qt::CaseInsensitive)) {
        return false;
    }

    if (!fileInfo.isDir() && !extensions.isEmpty()) {
        const QString suffix = QStringLiteral(".") + fileInfo.suffix().toLower();
        if (!extensions.contains(suffix)) {
            return false;
        }
    }

    return true;
}
