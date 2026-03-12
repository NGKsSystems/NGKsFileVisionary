#include "ArchiveProvider.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include "ArchiveReader.h"
#include "util/PathUtils.h"

namespace ArchiveNav
{
namespace
{
struct VirtualNode
{
    QString internalPath;
    QString name;
    bool isDir = true;
    quint64 size = 0;
    QDateTime modified;
};

QString parentInternalPath(const QString& value)
{
    const QString normalized = PathUtils::normalizeInternalPath(value);
    if (normalized.isEmpty()) {
        return QString();
    }
    const int slash = normalized.lastIndexOf('/');
    if (slash <= 0) {
        return QString();
    }
    return normalized.left(slash);
}

int slashDepth(const QString& value)
{
    if (value.isEmpty()) {
        return 0;
    }
    int depth = 0;
    for (const QChar ch : value) {
        if (ch == QLatin1Char('/')) {
            ++depth;
        }
    }
    return depth;
}

QString normalizedEntryExtension(const VirtualNode& node)
{
    if (node.isDir) {
        return QString();
    }

    const int dot = node.name.lastIndexOf('.');
    if (dot < 0 || dot == (node.name.size() - 1)) {
        return QString();
    }
    return node.name.mid(dot).toLower();
}

bool hasActiveFilters(const QueryOptions& options)
{
    return !options.extensionFilter.trimmed().isEmpty()
        || !options.substringFilter.trimmed().isEmpty()
        || options.filesOnly
        || options.directoriesOnly;
}

QueryRow toRow(const QString& archivePath, const VirtualNode& node, int baseDepth)
{
    QueryRow row;
    row.path = PathUtils::buildArchiveVirtualPath(archivePath, node.internalPath);
    row.name = node.name;
    row.normalizedName = node.name.toLower();
    row.extension = normalizedEntryExtension(node);
    row.isDir = node.isDir;
    row.sizeBytes = static_cast<qint64>(node.size);
    row.hasSizeBytes = !node.isDir;
    row.modifiedUtc = node.modified.isValid() ? node.modified.toUTC().toString(Qt::ISODate) : QString();
    row.hiddenFlag = false;
    row.systemFlag = false;
    row.archiveFlag = true;
    row.existsFlag = true;
    row.depth = qMax(0, slashDepth(node.internalPath) - baseDepth);
    return row;
}
}

ArchiveProvider::ArchiveProvider()
    : m_reader(new ArchiveReader())
{
}

ArchiveProvider::~ArchiveProvider()
{
    delete m_reader;
    m_reader = nullptr;
}

bool ArchiveProvider::canHandlePath(const QString& path) const
{
    return PathUtils::isArchivePath(path) || PathUtils::isArchiveVirtualPath(path);
}

QueryResult ArchiveProvider::query(const QString& rootPath,
                                   ViewModeController::UiViewMode mode,
                                   const QueryOptions& options,
                                   QString* providerLog)
{
    QueryResult result;

    ResolvedPath resolved;
    if (!resolvePath(rootPath, &resolved)) {
        result.ok = false;
        result.errorText = QStringLiteral("archive_provider_unresolvable_path");
        return result;
    }

    QVector<ArchiveEntry> archiveEntries;
    QString errorText;
    if (!listArchiveCached(resolved.archivePath, &archiveEntries, &errorText, providerLog)) {
        result.ok = false;
        result.errorText = errorText;
        return result;
    }

    QHash<QString, VirtualNode> nodeMap;
    auto ensureDirNode = [&](const QString& internalPath) {
        const QString normalized = PathUtils::normalizeInternalPath(internalPath);
        if (normalized.isEmpty()) {
            return;
        }

        if (!nodeMap.contains(normalized)) {
            VirtualNode node;
            node.internalPath = normalized;
            node.name = normalized.section('/', -1);
            node.isDir = true;
            nodeMap.insert(normalized, node);
        }
    };

    for (const ArchiveEntry& entry : archiveEntries) {
        const QString normalized = PathUtils::normalizeInternalPath(entry.internalPath);
        if (normalized.isEmpty()) {
            continue;
        }

        const QStringList parts = normalized.split('/', Qt::SkipEmptyParts);
        QString running;
        for (int i = 0; i < parts.size(); ++i) {
            if (!running.isEmpty()) {
                running += '/';
            }
            running += parts.at(i);
            if (i < (parts.size() - 1)) {
                ensureDirNode(running);
            }
        }

        VirtualNode node;
        node.internalPath = normalized;
        node.name = parts.isEmpty() ? normalized : parts.last();
        node.isDir = entry.isDir;
        node.size = entry.size;
        node.modified = entry.modified;
        nodeMap.insert(normalized, node);
    }

    QVector<VirtualNode> selected;
    const QString currentInternal = PathUtils::normalizeInternalPath(resolved.internalPath);
    const int baseDepth = slashDepth(currentInternal);
    const bool recursiveStandard = (mode == ViewModeController::UiViewMode::Standard) && hasActiveFilters(options);

    for (auto it = nodeMap.constBegin(); it != nodeMap.constEnd(); ++it) {
        const VirtualNode& node = it.value();

        bool underCurrent = false;
        if (currentInternal.isEmpty()) {
            underCurrent = true;
        } else if (node.internalPath == currentInternal) {
            underCurrent = false;
        } else {
            underCurrent = node.internalPath.startsWith(currentInternal + QStringLiteral("/"), Qt::CaseInsensitive);
        }

        if (!underCurrent) {
            continue;
        }

        if (mode == ViewModeController::UiViewMode::Standard && !recursiveStandard) {
            if (parentInternalPath(node.internalPath) != currentInternal) {
                continue;
            }
            selected.push_back(node);
            continue;
        }

        if (mode == ViewModeController::UiViewMode::Flat) {
            selected.push_back(node);
            continue;
        }

        if (mode == ViewModeController::UiViewMode::Hierarchy || recursiveStandard) {
            selected.push_back(node);
            continue;
        }
    }

    QVector<QueryRow> rows;
    rows.reserve(selected.size());
    for (const VirtualNode& node : selected) {
        rows.push_back(toRow(resolved.archivePath, node, baseDepth));
    }

    QueryOptions effectiveOptions = options;
    if (mode == ViewModeController::UiViewMode::Flat
        && !effectiveOptions.filesOnly
        && !effectiveOptions.directoriesOnly) {
        effectiveOptions.filesOnly = true;
    }

    if (!QueryTypesUtil::applyFiltersAndSort(effectiveOptions, &rows, &errorText)) {
        result.ok = false;
        result.errorText = errorText;
        return result;
    }

    result.ok = true;
    result.rows = rows;
    result.totalCount = rows.size();
    return result;
}

bool ArchiveProvider::resolvePath(const QString& path, ResolvedPath* out) const
{
    if (!out) {
        return false;
    }

    out->archivePath.clear();
    out->internalPath.clear();

    QString archivePath;
    QString internalPath;
    if (PathUtils::splitArchiveVirtualPath(path, &archivePath, &internalPath)) {
        out->archivePath = archivePath;
        out->internalPath = internalPath;
        return true;
    }

    if (!PathUtils::isArchivePath(path)) {
        return false;
    }

    out->archivePath = QDir::cleanPath(path);
    out->internalPath.clear();
    return true;
}

bool ArchiveProvider::listArchiveCached(const QString& archivePath,
                                        QVector<ArchiveEntry>* out,
                                        QString* errorText,
                                        QString* providerLog)
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("archive_provider_null_output");
        }
        return false;
    }

    const QFileInfo info(archivePath);
    if (!info.exists() || !info.isFile()) {
        if (errorText) {
            *errorText = QStringLiteral("archive_provider_archive_not_found");
        }
        return false;
    }

    const QString normalized = QDir::cleanPath(info.absoluteFilePath());
    const auto existing = m_cache.constFind(normalized);
    if (existing != m_cache.constEnd() && existing->lastModified == info.lastModified()) {
        *out = existing->entries;
        if (providerLog) {
            *providerLog = QStringLiteral("archive_cache_hit=true entries=%1").arg(out->size());
        }
        return true;
    }

    QVector<ArchiveEntry> parsed;
    QString readerLog;
    if (!m_reader->listArchiveEntries(normalized, &parsed, errorText, &readerLog)) {
        return false;
    }

    CacheRecord record;
    record.lastModified = info.lastModified();
    record.entries = parsed;
    m_cache.insert(normalized, record);

    *out = parsed;
    if (providerLog) {
        *providerLog = readerLog;
    }
    return true;
}
}
