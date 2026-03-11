#pragma once

#include <QVector>

#include "DbTypes.h"

class DbConnection;

class EntryRepository
{
public:
    explicit EntryRepository(DbConnection& connection);

    bool upsertEntry(const EntryRecord& record, qint64* entryId = nullptr, QString* errorText = nullptr);
    bool upsertEntry(const EntryRecord& record,
                     qint64* entryId,
                     bool* inserted,
                     bool* updated,
                     QString* errorText = nullptr);
    bool getByPath(const QString& path, EntryRecord* out, QString* errorText = nullptr);
    bool findByPath(const QString& path, EntryRecord* out, QString* errorText = nullptr);
    bool listChildren(qint64 parentId, QVector<EntryRecord>* out, QString* errorText = nullptr);
    bool findChildren(qint64 parentId, QVector<EntryRecord>* out, QString* errorText = nullptr);
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

private:
    DbConnection& m_connection;
};
