#include "ScanWorker.h"

#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QSet>
#include <QStringList>

#include "FileMetadataExtractor.h"

namespace {
bool isCanceled(const ScanTask& task)
{
    return task.cancelRequested && task.cancelRequested->load();
}

QString normalizeKey(const QString& path)
{
    return QDir::cleanPath(path).toLower();
}
}

ScanWorker::Result ScanWorker::run(const ScanTask& task, const Callbacks& callbacks) const
{
    Result result;

    const QString rootPath = task.normalizedRootPath();
    QFileInfo rootInfo(rootPath);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        result.errorText = QStringLiteral("scan_root_not_usable: %1").arg(rootPath);
        return result;
    }

    QVector<ScanIngestItem> batch;
    batch.reserve(task.effectiveBatchSize());

    QSet<QString> visitedDirs;
    QVector<QString> pendingDirs;
    pendingDirs.push_back(rootPath);

    // Emit the root as the first indexed item.
    {
        ScanIngestItem rootItem;
        QString rootError;
        if (!FileMetadataExtractor::extract(rootInfo,
                                            QString(),
                                            task.volumeId,
                                            task.scanSessionId,
                                            &rootItem,
                                            &rootError)) {
            result.errorText = QStringLiteral("extract_root_failed: %1").arg(rootError);
            return result;
        }
        batch.push_back(rootItem);
        result.progress.totalSeen += 1;
        result.progress.totalEmitted += 1;
    }

    auto emitBatch = [&]() {
        if (batch.isEmpty()) {
            return;
        }
        if (callbacks.onBatch) {
            callbacks.onBatch(batch);
        }
        batch.clear();
    };

    auto emitProgress = [&]() {
        if (callbacks.onProgress) {
            callbacks.onProgress(result.progress);
        }
    };

    while (!pendingDirs.isEmpty()) {
        if (isCanceled(task)) {
            result.canceled = true;
            result.success = false;
            emitBatch();
            emitProgress();
            return result;
        }

        const QString currentDirPath = pendingDirs.back();
        pendingDirs.pop_back();

        const QString loopKey = normalizeKey(currentDirPath);
        if (visitedDirs.contains(loopKey)) {
            continue;
        }
        visitedDirs.insert(loopKey);

        QFileInfo dirInfo(currentDirPath);
        if (!dirInfo.exists()) {
            continue;
        }

        QDir currentDir(currentDirPath);
        if (!currentDir.isReadable()) {
            result.progress.errorCount += 1;
            if (callbacks.onLog) {
                callbacks.onLog(QStringLiteral("unreadable_dir path=%1").arg(currentDirPath));
            }
            emitProgress();
            continue;
        }

        const QFileInfoList children = currentDir.entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
            QDir::NoSort);

        for (const QFileInfo& childInfo : children) {
            if (isCanceled(task)) {
                result.canceled = true;
                result.success = false;
                emitBatch();
                emitProgress();
                return result;
            }

            ScanIngestItem item;
            QString itemError;
            if (!FileMetadataExtractor::extract(childInfo,
                                                currentDirPath,
                                                task.volumeId,
                                                task.scanSessionId,
                                                &item,
                                                &itemError)) {
                result.progress.errorCount += 1;
                if (callbacks.onLog) {
                    callbacks.onLog(QStringLiteral("extract_error path=%1 error=%2")
                                        .arg(childInfo.absoluteFilePath(), itemError));
                }
                continue;
            }

            batch.push_back(item);
            result.progress.totalSeen += 1;
            result.progress.totalEmitted += 1;

            if (childInfo.isDir() && !item.record.reparseFlag) {
                pendingDirs.push_back(QDir::cleanPath(childInfo.absoluteFilePath()));
            }

            if (batch.size() >= task.effectiveBatchSize()) {
                emitBatch();
            }

            if ((result.progress.totalSeen % 250) == 0) {
                emitProgress();
            }
        }
    }

    emitBatch();
    emitProgress();

    result.success = true;
    return result;
}
