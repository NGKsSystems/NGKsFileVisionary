#pragma once

#include <QString>

struct VolumeRecord
{
    qint64 id = 0;
    QString volumeKey;
    QString rootPath;
    QString displayName;
    QString fsType;
    QString serialNumber;
    QString createdUtc;
    QString updatedUtc;
};

struct EntryRecord
{
    qint64 id = 0;
    qint64 volumeId = 0;
    qint64 parentId = 0;
    bool hasParentId = false;

    QString path;
    QString name;
    QString normalizedName;
    QString extension;

    bool isDir = false;
    qint64 sizeBytes = 0;
    bool hasSizeBytes = false;

    QString createdUtc;
    QString modifiedUtc;
    QString accessedUtc;

    bool hiddenFlag = false;
    bool systemFlag = false;
    bool readonlyFlag = false;
    bool archiveFlag = false;
    bool reparseFlag = false;
    bool existsFlag = true;

    QString fileId;
    QString indexedAtUtc;
    QString parentPath;
    qint64 lastSeenScanId = 0;
    bool hasLastSeenScanId = false;
    int scanVersion = 0;
    QString entryHash;
    int metadataVersion = 1;
};

struct FavoriteRecord
{
    qint64 id = 0;
    QString path;
    QString label;
    QString pinnedUtc;
    int sortOrder = 0;
};

struct ScanSessionRecord
{
    qint64 id = 0;
    QString rootPath;
    QString mode;
    QString startedUtc;
    QString completedUtc;
    QString status;
    qint64 totalSeen = 0;
    qint64 totalInserted = 0;
    qint64 totalUpdated = 0;
    qint64 totalRemoved = 0;
    QString errorText;
};

struct IndexRootRecord
{
    qint64 id = 0;
    QString rootPath;
    QString status;
    qint64 lastScanVersion = 0;
    QString lastIndexedUtc;
    QString createdUtc;
    QString updatedUtc;
};

struct IndexJournalRecord
{
    qint64 id = 0;
    QString rootPath;
    QString path;
    QString eventType;
    qint64 scanVersion = 0;
    QString payload;
    QString createdUtc;
};

struct IndexStatRecord
{
    QString key;
    QString value;
    QString updatedUtc;
};
