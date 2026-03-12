#pragma once

#include <QString>
#include <QVector>

struct SnapshotCreateOptions
{
    bool includeHidden = false;
    bool includeSystem = false;
    int maxDepth = -1;
    bool filesOnly = false;
    bool directoriesOnly = false;
    QString snapshotType = QStringLiteral("structural_full");
    QString noteText;
};

struct SnapshotRecord
{
    qint64 id = 0;
    QString rootPath;
    QString snapshotName;
    QString snapshotType;
    QString createdUtc;
    QString optionsJson;
    qint64 itemCount = 0;
    qint64 sourceScanSessionId = 0;
    bool hasSourceScanSessionId = false;
    QString noteText;
};

struct SnapshotEntryRecord
{
    qint64 id = 0;
    qint64 snapshotId = 0;
    QString entryPath;
    QString parentPath;
    QString name;
    QString normalizedName;
    QString extension;
    bool isDir = false;
    qint64 sizeBytes = 0;
    bool hasSizeBytes = false;
    QString modifiedUtc;
    bool hiddenFlag = false;
    bool systemFlag = false;
    bool archiveFlag = false;
    bool existsFlag = true;
};

struct SnapshotCreateResult
{
    bool ok = false;
    QString errorText;
    qint64 snapshotId = 0;
    qint64 itemCount = 0;
    QString snapshotName;
    QString rootPath;
    QString snapshotType;
    QString createdUtc;
};

namespace SnapshotTypesUtil
{
QString optionsToJson(const SnapshotCreateOptions& options);
QString normalizedSnapshotType(const QString& value);
bool isSupportedSnapshotType(const QString& value);
}
