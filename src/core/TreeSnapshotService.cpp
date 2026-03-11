#include "TreeSnapshotService.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QStringList>

namespace {
QString treeBranch(bool unicode, bool isLast)
{
    if (unicode) {
        return isLast ? QStringLiteral("└─ ") : QStringLiteral("├─ ");
    }
    return isLast ? QStringLiteral("`-- ") : QStringLiteral("|-- ");
}

QString treeIndent(bool unicode, bool hasMore)
{
    if (unicode) {
        return hasMore ? QStringLiteral("│  ") : QStringLiteral("   ");
    }
    return hasMore ? QStringLiteral("|   ") : QStringLiteral("    ");
}

QString formatLabel(const QString& absolutePath, const QString& name, bool isDir, const TreeSnapshotService::Options& options)
{
    QString label = options.fullPaths ? absolutePath : name;
    if (label.isEmpty()) {
        label = absolutePath;
    }
    if (isDir && !label.endsWith('/')) {
        label += '/';
    }
    return label;
}

QStringList sortedEntries(const QFileInfoList& entries)
{
    QStringList lines;
    lines.reserve(entries.size());
    for (const QFileInfo& info : entries) {
        lines.push_back(info.absoluteFilePath());
    }
    return lines;
}

bool isNavigableDir(const QFileInfo& info)
{
    if (!info.exists() || !info.isDir()) {
        return false;
    }
    if (info.isSymLink()) {
        return false;
    }
    const QString lowerName = info.fileName().toLower();
    if (lowerName.endsWith(QStringLiteral(".lnk"))) {
        return false;
    }
    return true;
}
}

TreeSnapshotService::Result TreeSnapshotService::generateFromDisk(const QString& rootPath,
                                                                  const Options& options,
                                                                  std::atomic_bool* cancelRequested)
{
    Result result;
    const qint64 startMs = QDateTime::currentMSecsSinceEpoch();

    if (!options.includeFiles && !options.includeFolders) {
        result.error = QStringLiteral("At least one of include files/folders must be enabled.");
        return result;
    }

    QFileInfo rootInfo(rootPath);
    if (!isNavigableDir(rootInfo)) {
        result.error = QStringLiteral("Selected root is not a readable folder.");
        return result;
    }

    QStringList lines;
    lines.push_back(formatLabel(rootInfo.absoluteFilePath(), rootInfo.fileName(), true, options));

    struct StackFrame {
        QString path;
        QString prefix;
        int depth;
    };

    QVector<StackFrame> stack;
    stack.push_back({rootInfo.absoluteFilePath(), QString(), 0});

    while (!stack.isEmpty()) {
        if (cancelRequested && cancelRequested->load()) {
            result.canceled = true;
            result.truncated = true;
            break;
        }

        const StackFrame frame = stack.back();
        stack.pop_back();

        if (options.maxDepth >= 0 && frame.depth >= options.maxDepth) {
            result.truncated = true;
            continue;
        }

        QDir dir(frame.path);
        QFileInfoList children = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Readable,
                                                   QDir::DirsFirst | QDir::IgnoreCase | QDir::Name);

        QVector<QFileInfo> filtered;
        filtered.reserve(children.size());
        for (const QFileInfo& child : children) {
            if (!options.includeHidden && child.isHidden()) {
                continue;
            }
            if (child.isDir() && !options.includeFolders) {
                continue;
            }
            if (!child.isDir() && !options.includeFiles) {
                continue;
            }
            filtered.push_back(child);
        }

        for (int index = 0; index < filtered.size(); ++index) {
            const QFileInfo& child = filtered[index];
            const bool isLast = (index == filtered.size() - 1);
            const QString label = formatLabel(child.absoluteFilePath(), child.fileName(), child.isDir(), options);
            lines.push_back(frame.prefix + treeBranch(options.useUnicode, isLast) + label);

            if (child.isDir()) {
                result.folderCount += 1;
                if (isNavigableDir(child)) {
                    const QString nextPrefix = frame.prefix + treeIndent(options.useUnicode, !isLast);
                    stack.push_back({child.absoluteFilePath(), nextPrefix, frame.depth + 1});
                } else {
                    lines.push_back(frame.prefix + treeIndent(options.useUnicode, !isLast)
                                    + treeBranch(options.useUnicode, true)
                                    + QStringLiteral("[access denied]"));
                }
            } else {
                result.fileCount += 1;
            }
        }
    }

    QString body = lines.join('\n') + '\n';
    if (options.outputFormat == OutputFormat::Markdown) {
        const QString heading = QStringLiteral("# Tree Snapshot — %1\n\n").arg(rootInfo.fileName().isEmpty() ? rootInfo.absoluteFilePath() : rootInfo.fileName());
        result.text = heading + QStringLiteral("```text\n") + body + QStringLiteral("```\n");
    } else {
        result.text = body;
    }

    result.durationMs = QDateTime::currentMSecsSinceEpoch() - startMs;
    return result;
}

TreeSnapshotService::Result TreeSnapshotService::generateVisibleView(const QAbstractItemModel* model,
                                                                     const QModelIndex& rootIndex,
                                                                     const QString& rootPath,
                                                                     const Options& options,
                                                                     std::atomic_bool* cancelRequested)
{
    Result result;
    const qint64 startMs = QDateTime::currentMSecsSinceEpoch();

    if (!model) {
        result.error = QStringLiteral("No view model available.");
        return result;
    }
    if (!options.includeFiles && !options.includeFolders) {
        result.error = QStringLiteral("At least one of include files/folders must be enabled.");
        return result;
    }

    QFileInfo rootInfo(rootPath);
    const QString rootLabel = rootInfo.fileName().isEmpty() ? rootInfo.absoluteFilePath() : rootInfo.fileName();

    QStringList lines;
    lines.push_back(formatLabel(rootInfo.absoluteFilePath(), rootLabel, true, options));

    struct ModelFrame {
        QModelIndex parent;
        QString prefix;
        int depth;
    };

    QVector<ModelFrame> stack;
    stack.push_back({rootIndex, QString(), 0});

    while (!stack.isEmpty()) {
        if (cancelRequested && cancelRequested->load()) {
            result.canceled = true;
            result.truncated = true;
            break;
        }

        const ModelFrame frame = stack.back();
        stack.pop_back();

        if (options.maxDepth >= 0 && frame.depth >= options.maxDepth) {
            result.truncated = true;
            continue;
        }

        const int rows = model->rowCount(frame.parent);
        QVector<QModelIndex> children;
        children.reserve(rows);
        for (int row = 0; row < rows; ++row) {
            children.push_back(model->index(row, 0, frame.parent));
        }

        for (int index = 0; index < children.size(); ++index) {
            const QModelIndex& child = children[index];
            const bool isLast = (index == children.size() - 1);

            const QString name = model->data(child, Qt::DisplayRole).toString();
            const QString type = model->data(model->index(child.row(), 1, child.parent()), Qt::DisplayRole).toString();
            const QString path = model->data(model->index(child.row(), 4, child.parent()), Qt::DisplayRole).toString();
            const bool isDir = type.compare(QStringLiteral("Folder"), Qt::CaseInsensitive) == 0;

            if (isDir && !options.includeFolders) {
                continue;
            }
            if (!isDir && !options.includeFiles) {
                continue;
            }

            const QString label = formatLabel(path, name, isDir, options);
            lines.push_back(frame.prefix + treeBranch(options.useUnicode, isLast) + label);

            if (isDir) {
                result.folderCount += 1;
                const QString nextPrefix = frame.prefix + treeIndent(options.useUnicode, !isLast);
                stack.push_back({child, nextPrefix, frame.depth + 1});
            } else {
                result.fileCount += 1;
            }
        }
    }

    QString body = lines.join('\n') + '\n';
    if (options.outputFormat == OutputFormat::Markdown) {
        const QString heading = QStringLiteral("# Tree Snapshot — %1\n\n").arg(rootLabel);
        result.text = heading + QStringLiteral("```text\n") + body + QStringLiteral("```\n");
    } else {
        result.text = body;
    }

    result.durationMs = QDateTime::currentMSecsSinceEpoch() - startMs;
    return result;
}
