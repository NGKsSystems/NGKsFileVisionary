#pragma once

#include <QString>
#include <QVector>

#include "DbTypes.h"

class DbConnection;
class EntryRepository;
class FavoriteRepository;
class VolumeRepository;

class MetaStore
{
public:
    MetaStore();
    ~MetaStore();

    bool initialize(const QString& dbPath, QString* errorText = nullptr, QString* migrationLog = nullptr);
    void shutdown();

    bool isReady() const;
    int schemaVersion(QString* errorText = nullptr);

    bool upsertVolume(const VolumeRecord& record, qint64* volumeId = nullptr, QString* errorText = nullptr);
    bool upsertEntry(const EntryRecord& record, qint64* entryId = nullptr, QString* errorText = nullptr);
    bool upsertEntry(const EntryRecord& record,
                     qint64* entryId,
                     bool* inserted,
                     bool* updated,
                     QString* errorText = nullptr);
    bool upsertFavorite(const FavoriteRecord& record, qint64* favoriteId = nullptr, QString* errorText = nullptr);

    bool getEntryByPath(const QString& path, EntryRecord* out, QString* errorText = nullptr);
    bool findChildrenByParentId(qint64 parentId, QVector<EntryRecord>* out, QString* errorText = nullptr);
    bool findChildrenByParentPath(const QString& parentPath, QVector<EntryRecord>* out, QString* errorText = nullptr);
    bool findFlatDescendantsByRootPath(const QString& rootPath,
                                       int maxDepth,
                                       QVector<EntryRecord>* out,
                                       QString* errorText = nullptr);
    bool findSubtreeByRootPath(const QString& rootPath,
                               int maxDepth,
                               QVector<EntryRecord>* out,
                               QString* errorText = nullptr);
    qint64 countEntriesUnderRoot(const QString& rootPath, int maxDepth = -1, QString* errorText = nullptr);
    bool listSampleRows(int limit, QVector<EntryRecord>* out, QString* errorText = nullptr);
    bool resolveParentIdByPath(const QString& parentPath,
                               qint64* parentId,
                               bool* found,
                               QString* errorText = nullptr);
    qint64 countEntries(QString* errorText = nullptr);
    qint64 countDirectories(QString* errorText = nullptr);
    qint64 countFiles(QString* errorText = nullptr);
    bool listSomeEntries(int limit, QVector<EntryRecord>* out, QString* errorText = nullptr);

    bool beginTransaction(QString* errorText = nullptr);
    bool commitTransaction(QString* errorText = nullptr);
    bool rollbackTransaction(QString* errorText = nullptr);

    bool createScanSession(const ScanSessionRecord& record, qint64* sessionId, QString* errorText = nullptr);
    bool updateScanSessionProgress(qint64 sessionId,
                                   qint64 totalSeen,
                                   qint64 totalInserted,
                                   qint64 totalUpdated,
                                   qint64 totalRemoved,
                                   QString* errorText = nullptr);
    bool completeScanSession(qint64 sessionId,
                             qint64 totalSeen,
                             qint64 totalInserted,
                             qint64 totalUpdated,
                             qint64 totalRemoved,
                             QString* errorText = nullptr);
    bool failScanSession(qint64 sessionId,
                         qint64 totalSeen,
                         qint64 totalInserted,
                         qint64 totalUpdated,
                         qint64 totalRemoved,
                         const QString& error,
                         QString* errorText = nullptr);
    bool cancelScanSession(qint64 sessionId,
                           qint64 totalSeen,
                           qint64 totalInserted,
                           qint64 totalUpdated,
                           qint64 totalRemoved,
                           QString* errorText = nullptr);
    bool listScanSessions(QVector<ScanSessionRecord>* out, QString* errorText = nullptr);

    bool upsertIndexRoot(const IndexRootRecord& record, qint64* rootId = nullptr, QString* errorText = nullptr);
    bool getIndexRoot(const QString& rootPath, IndexRootRecord* out, QString* errorText = nullptr);
    bool listIndexRoots(QVector<IndexRootRecord>* out, QString* errorText = nullptr);

    bool appendIndexJournal(const IndexJournalRecord& record, qint64* journalId = nullptr, QString* errorText = nullptr);
    bool listIndexJournal(const QString& rootPath,
                          int limit,
                          QVector<IndexJournalRecord>* out,
                          QString* errorText = nullptr);

    bool upsertIndexStat(const IndexStatRecord& record, QString* errorText = nullptr);
    bool listIndexStats(QVector<IndexStatRecord>* out, QString* errorText = nullptr);

    bool listVolumes(QVector<VolumeRecord>* out, QString* errorText = nullptr);
    bool listFavorites(QVector<FavoriteRecord>* out, QString* errorText = nullptr);

private:
    DbConnection* m_connection;
    VolumeRepository* m_volumes;
    EntryRepository* m_entries;
    FavoriteRepository* m_favorites;
};
