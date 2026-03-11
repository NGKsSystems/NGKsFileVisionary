#pragma once

#include <QAbstractItemModel>
#include <QModelIndex>
#include <QString>

#include <atomic>

class TreeSnapshotService
{
public:
    enum class SnapshotType
    {
        FullRecursive = 0,
        VisibleView = 1,
    };

    enum class OutputFormat
    {
        PlainText = 0,
        Markdown = 1,
    };

    struct Options
    {
        SnapshotType snapshotType = SnapshotType::FullRecursive;
        OutputFormat outputFormat = OutputFormat::PlainText;
        bool includeFiles = true;
        bool includeFolders = true;
        bool includeHidden = false;
        bool namesOnly = true;
        bool fullPaths = false;
        int maxDepth = -1;
        bool useUnicode = true;
    };

    struct Result
    {
        QString text;
        int folderCount = 0;
        int fileCount = 0;
        qint64 durationMs = 0;
        bool truncated = false;
        bool canceled = false;
        QString error;
    };

    static Result generateFromDisk(const QString& rootPath,
                                   const Options& options,
                                   std::atomic_bool* cancelRequested = nullptr);

    static Result generateVisibleView(const QAbstractItemModel* model,
                                      const QModelIndex& rootIndex,
                                      const QString& rootPath,
                                      const Options& options,
                                      std::atomic_bool* cancelRequested = nullptr);
};
