#include "SnapshotEngine.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>

#include "SnapshotRepository.h"
#include "core/db/MetaStore.h"
#include "core/db/SqlHelpers.h"
#include "core/query/QueryCore.h"
#include "core/query/QueryTypes.h"

namespace {
QString parentPathFor(const QString& absolutePath)
{
    const QFileInfo info(absolutePath);
    return QDir::fromNativeSeparators(QDir::cleanPath(info.dir().absolutePath()));
}

SnapshotEntryRecord toSnapshotEntry(qint64 snapshotId, const QueryRow& row)
{
    SnapshotEntryRecord out;
    out.snapshotId = snapshotId;
    out.entryPath = QDir::fromNativeSeparators(QDir::cleanPath(row.path));
    out.parentPath = parentPathFor(out.entryPath);
    out.name = row.name;
    out.normalizedName = row.normalizedName;
    out.extension = row.extension;
    out.isDir = row.isDir;
    out.hasSizeBytes = row.hasSizeBytes;
    out.sizeBytes = row.sizeBytes;
    out.modifiedUtc = row.modifiedUtc;
    out.hiddenFlag = row.hiddenFlag;
    out.systemFlag = row.systemFlag;
    out.archiveFlag = row.archiveFlag;
    out.existsFlag = row.existsFlag;
    return out;
}
}

SnapshotEngine::SnapshotEngine(MetaStore& store)
    : m_store(store)
    , m_repo(new SnapshotRepository(store))
{
}

SnapshotEngine::~SnapshotEngine()
{
    delete m_repo;
    m_repo = nullptr;
}

SnapshotCreateResult SnapshotEngine::createSnapshot(const QString& rootPath,
                                                    const QString& snapshotName,
                                                    const SnapshotCreateOptions& options)
{
    SnapshotCreateResult result;
    result.rootPath = QDir::fromNativeSeparators(QDir::cleanPath(rootPath));
    result.snapshotName = snapshotName.trimmed();
    result.snapshotType = SnapshotTypesUtil::normalizedSnapshotType(options.snapshotType);

    if (result.rootPath.isEmpty()) {
        result.errorText = QStringLiteral("missing_root_path");
        return result;
    }
    if (result.snapshotName.isEmpty()) {
        result.errorText = QStringLiteral("missing_snapshot_name");
        return result;
    }
    if (options.filesOnly && options.directoriesOnly) {
        result.errorText = QStringLiteral("conflicting_file_dir_filters");
        return result;
    }
    if (!SnapshotTypesUtil::isSupportedSnapshotType(result.snapshotType)) {
        result.errorText = QStringLiteral("unsupported_snapshot_type");
        return result;
    }

    QString errorText;
    IndexRootRecord indexedRoot;
    if (!m_store.getIndexRoot(result.rootPath, &indexedRoot, &errorText)) {
        result.errorText = QStringLiteral("unindexed_root:%1").arg(errorText);
        return result;
    }

    EntryRecord rootEntry;
    if (!m_store.getEntryByPath(result.rootPath, &rootEntry, &errorText)) {
        result.errorText = QStringLiteral("indexed_root_missing_entry:%1").arg(errorText);
        return result;
    }

    QueryCore queryCore(m_store);
    QueryOptions queryOptions;
    queryOptions.includeHidden = options.includeHidden;
    queryOptions.includeSystem = options.includeSystem;
    queryOptions.maxDepth = options.maxDepth;
    queryOptions.filesOnly = options.filesOnly;
    queryOptions.directoriesOnly = options.directoriesOnly;
    queryOptions.sortField = QuerySortField::Path;
    queryOptions.ascending = true;

    const QueryResult queryResult = queryCore.querySubtree(result.rootPath, queryOptions);
    if (!queryResult.ok) {
        result.errorText = QStringLiteral("query_subtree_failed:%1").arg(queryResult.errorText);
        return result;
    }

    QVector<SnapshotEntryRecord> entries;
    entries.reserve(queryResult.rows.size());
    for (const QueryRow& row : queryResult.rows) {
        if (!row.existsFlag) {
            continue;
        }
        entries.push_back(toSnapshotEntry(0, row));
    }

    SnapshotRecord snapshot;
    snapshot.rootPath = result.rootPath;
    snapshot.snapshotName = result.snapshotName;
    snapshot.snapshotType = result.snapshotType;
    snapshot.createdUtc = SqlHelpers::utcNowIso();
    snapshot.optionsJson = SnapshotTypesUtil::optionsToJson(options);
    snapshot.itemCount = entries.size();
    snapshot.noteText = options.noteText.trimmed();

    if (!m_store.beginTransaction(&errorText)) {
        result.errorText = QStringLiteral("begin_tx_failed:%1").arg(errorText);
        return result;
    }

    qint64 snapshotId = 0;
    if (!m_repo->createSnapshot(snapshot, &snapshotId, &errorText)) {
        m_store.rollbackTransaction(nullptr);
        result.errorText = QStringLiteral("create_snapshot_failed:%1").arg(errorText);
        return result;
    }

    for (int i = 0; i < entries.size(); ++i) {
        entries[i].snapshotId = snapshotId;
    }
    if (!m_repo->insertSnapshotEntries(snapshotId, entries, &errorText)) {
        m_store.rollbackTransaction(nullptr);
        result.errorText = QStringLiteral("insert_snapshot_entries_failed:%1").arg(errorText);
        return result;
    }

    if (!m_repo->updateSnapshotItemCount(snapshotId, entries.size(), &errorText)) {
        m_store.rollbackTransaction(nullptr);
        result.errorText = QStringLiteral("update_snapshot_count_failed:%1").arg(errorText);
        return result;
    }

    if (!m_store.commitTransaction(&errorText)) {
        m_store.rollbackTransaction(nullptr);
        result.errorText = QStringLiteral("commit_tx_failed:%1").arg(errorText);
        return result;
    }

    result.ok = true;
    result.snapshotId = snapshotId;
    result.itemCount = entries.size();
    result.createdUtc = snapshot.createdUtc;
    return result;
}

bool SnapshotEngine::listSnapshots(const QString& rootPath,
                                   QVector<SnapshotRecord>* out,
                                   QString* errorText) const
{
    const QString normalized = rootPath.trimmed().isEmpty()
        ? QString()
        : QDir::fromNativeSeparators(QDir::cleanPath(rootPath));
    return m_repo->listSnapshots(normalized, out, errorText);
}

bool SnapshotEngine::getSnapshot(qint64 snapshotId, SnapshotRecord* out, QString* errorText) const
{
    return m_repo->getSnapshotById(snapshotId, out, errorText);
}

bool SnapshotEngine::getSnapshot(const QString& rootPath,
                                 const QString& snapshotName,
                                 SnapshotRecord* out,
                                 QString* errorText) const
{
    const QString normalized = QDir::fromNativeSeparators(QDir::cleanPath(rootPath));
    return m_repo->getSnapshotByName(normalized, snapshotName.trimmed(), out, errorText);
}

bool SnapshotEngine::getSnapshotEntries(qint64 snapshotId,
                                        QVector<SnapshotEntryRecord>* out,
                                        QString* errorText) const
{
    return m_repo->listSnapshotEntries(snapshotId, out, errorText);
}

bool SnapshotEngine::exportSnapshotSummary(qint64 snapshotId, QString* summaryOut, QString* errorText) const
{
    if (!summaryOut) {
        if (errorText) {
            *errorText = QStringLiteral("null_summary_output");
        }
        return false;
    }

    SnapshotRecord snapshot;
    if (!m_repo->getSnapshotById(snapshotId, &snapshot, errorText)) {
        return false;
    }

    QVector<SnapshotEntryRecord> entries;
    if (!m_repo->listSnapshotEntries(snapshotId, &entries, errorText)) {
        return false;
    }

    qint64 dirCount = 0;
    qint64 fileCount = 0;
    qint64 hiddenCount = 0;
    qint64 systemCount = 0;
    for (const SnapshotEntryRecord& entry : entries) {
        if (entry.isDir) {
            ++dirCount;
        } else {
            ++fileCount;
        }
        if (entry.hiddenFlag) {
            ++hiddenCount;
        }
        if (entry.systemFlag) {
            ++systemCount;
        }
    }

    QStringList lines;
    lines << QStringLiteral("snapshot_id=%1").arg(snapshot.id);
    lines << QStringLiteral("snapshot_name=%1").arg(snapshot.snapshotName);
    lines << QStringLiteral("snapshot_type=%1").arg(snapshot.snapshotType);
    lines << QStringLiteral("root_path=%1").arg(snapshot.rootPath);
    lines << QStringLiteral("created_utc=%1").arg(snapshot.createdUtc);
    lines << QStringLiteral("item_count=%1").arg(snapshot.itemCount);
    lines << QStringLiteral("entry_count=%1").arg(entries.size());
    lines << QStringLiteral("dir_count=%1").arg(dirCount);
    lines << QStringLiteral("file_count=%1").arg(fileCount);
    lines << QStringLiteral("hidden_count=%1").arg(hiddenCount);
    lines << QStringLiteral("system_count=%1").arg(systemCount);
    lines << QStringLiteral("options_json=%1").arg(snapshot.optionsJson);

    *summaryOut = lines.join(QStringLiteral("\n"));
    return true;
}
