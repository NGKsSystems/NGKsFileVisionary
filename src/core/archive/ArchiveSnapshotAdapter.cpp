#include "ArchiveSnapshotAdapter.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>

#include "ArchiveProvider.h"
#include "util/PathUtils.h"

namespace ArchiveNav
{
namespace
{
QString parentPathFor(const QString& path)
{
    if (PathUtils::isArchiveVirtualPath(path)) {
        return PathUtils::archiveVirtualParentPath(path);
    }
    const QFileInfo info(path);
    return QDir::fromNativeSeparators(QDir::cleanPath(info.dir().absolutePath()));
}

QString buildEntryHash(const SnapshotEntryRecord& entry)
{
    const QString payload = QStringLiteral("%1|%2|%3|%4")
                                .arg(entry.virtualPath)
                                .arg(entry.hasSizeBytes ? QString::number(entry.sizeBytes) : QStringLiteral("null"))
                                .arg(entry.modifiedUtc)
                                .arg(entry.archiveEntryPath);
    return QString::fromLatin1(QCryptographicHash::hash(payload.toUtf8(), QCryptographicHash::Sha256).toHex());
}
}

bool ArchiveSnapshotAdapter::canHandlePath(const QString& rootPath) const
{
    return PathUtils::isArchivePath(rootPath) || PathUtils::isArchiveVirtualPath(rootPath);
}

bool ArchiveSnapshotAdapter::collectSnapshotEntries(const QString& rootPath,
                                                   const QueryOptions& options,
                                                   QVector<SnapshotEntryRecord>* out,
                                                   QString* errorText,
                                                   QString* adapterLog) const
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("archive_snapshot_adapter_null_output");
        }
        return false;
    }

    out->clear();

    ArchiveProvider provider;
    if (!provider.canHandlePath(rootPath)) {
        if (errorText) {
            *errorText = QStringLiteral("archive_snapshot_adapter_unsupported_root");
        }
        return false;
    }

    QueryResult result = provider.query(rootPath, ViewModeController::UiViewMode::Hierarchy, options, adapterLog);
    if (!result.ok) {
        if (errorText) {
            *errorText = result.errorText;
        }
        return false;
    }

    out->reserve(result.rows.size());
    for (const QueryRow& row : result.rows) {
        if (!row.existsFlag) {
            continue;
        }

        SnapshotEntryRecord entry;
        entry.virtualPath = QDir::fromNativeSeparators(row.path);
        entry.entryPath = entry.virtualPath;
        entry.parentPath = parentPathFor(entry.virtualPath);
        entry.name = row.name;
        entry.normalizedName = row.normalizedName;
        entry.extension = row.extension;
        entry.isDir = row.isDir;
        entry.sizeBytes = row.sizeBytes;
        entry.hasSizeBytes = row.hasSizeBytes;
        entry.modifiedUtc = row.modifiedUtc;
        entry.hiddenFlag = row.hiddenFlag;
        entry.systemFlag = row.systemFlag;
        entry.archiveFlag = true;
        entry.existsFlag = row.existsFlag;

        PathUtils::splitArchiveVirtualPath(entry.virtualPath, &entry.archiveSource, &entry.archiveEntryPath);
        entry.entryHash = buildEntryHash(entry);
        entry.hasEntryHash = !entry.entryHash.isEmpty();
        out->push_back(entry);
    }

    return true;
}
}
