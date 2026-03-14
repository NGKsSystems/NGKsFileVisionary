#include "StructuralTimelineBuilder.h"

#include <QHash>
#include <QSet>

#include <algorithm>

namespace {
QString normalizePath(const QString& path)
{
    return path.trimmed().replace('\\', '/');
}

bool isReferenceLike(const StructuralResultRow& row)
{
    if (row.category != StructuralResultCategory::Reference) {
        return false;
    }

    const QString rel = row.relationship.trimmed().toLower();
    const QString status = row.status.trimmed().toLower();
    const QString token = rel.isEmpty() ? status : rel;
    return token == QStringLiteral("include_ref")
        || token == QStringLiteral("import_ref")
        || token == QStringLiteral("require_ref")
        || token == QStringLiteral("path_ref");
}

QString relationshipToken(const StructuralResultRow& row)
{
    const QString rel = row.relationship.trimmed();
    return rel.isEmpty() ? row.status.trimmed() : rel;
}

QString eventKey(const QString& filePath, const QString& rel, const QString& targetPath)
{
    return filePath + QStringLiteral("|") + rel + QStringLiteral("|") + targetPath;
}
}

namespace StructuralTimelineBuilder
{
QVector<StructuralTimelineEvent> build(const QVector<StructuralTimelineSnapshotRows>& snapshots)
{
    QVector<StructuralTimelineEvent> events;
    if (snapshots.isEmpty()) {
        return events;
    }

    QVector<StructuralTimelineSnapshotRows> ordered = snapshots;
    std::sort(ordered.begin(), ordered.end(), [](const StructuralTimelineSnapshotRows& a, const StructuralTimelineSnapshotRows& b) {
        if (a.snapshotId != b.snapshotId) {
            return a.snapshotId < b.snapshotId;
        }
        return QString::compare(a.timestamp, b.timestamp, Qt::CaseInsensitive) < 0;
    });

    QSet<QString> previousKeys;

    for (int snapshotIndex = 0; snapshotIndex < ordered.size(); ++snapshotIndex) {
        const StructuralTimelineSnapshotRows& snapshot = ordered.at(snapshotIndex);
        QSet<QString> currentKeys;

        QHash<QString, StructuralTimelineEvent> eventByKey;
        for (const StructuralResultRow& row : snapshot.rows) {
            if (!isReferenceLike(row)) {
                continue;
            }

            const QString filePath = normalizePath(!row.sourceFile.isEmpty() ? row.sourceFile : row.primaryPath);
            const QString targetPath = normalizePath(!row.secondaryPath.isEmpty() ? row.secondaryPath : row.primaryPath);
            const QString rel = relationshipToken(row).toLower();
            if (filePath.isEmpty() || targetPath.isEmpty() || rel.isEmpty()) {
                continue;
            }

            const QString key = eventKey(filePath, rel, targetPath);
            currentKeys.insert(key);

            StructuralTimelineEvent event;
            event.timestamp = snapshot.timestamp;
            event.snapshotId = snapshot.snapshotId;
            event.filePath = filePath;
            event.relationshipType = rel;
            event.targetPath = targetPath;
            event.changeType = previousKeys.contains(key)
                ? StructuralTimelineChangeType::Persisting
                : StructuralTimelineChangeType::Added;
            eventByKey.insert(key, event);
        }

        QStringList stableKeys = currentKeys.values();
        std::sort(stableKeys.begin(), stableKeys.end(), [](const QString& a, const QString& b) {
            return QString::compare(a, b, Qt::CaseInsensitive) < 0;
        });

        for (const QString& key : stableKeys) {
            events.push_back(eventByKey.value(key));
        }

        if (snapshotIndex > 0) {
            QStringList removedKeys;
            for (const QString& oldKey : previousKeys) {
                if (!currentKeys.contains(oldKey)) {
                    removedKeys.push_back(oldKey);
                }
            }
            std::sort(removedKeys.begin(), removedKeys.end(), [](const QString& a, const QString& b) {
                return QString::compare(a, b, Qt::CaseInsensitive) < 0;
            });

            for (const QString& key : removedKeys) {
                const QStringList parts = key.split('|');
                if (parts.size() != 3) {
                    continue;
                }
                StructuralTimelineEvent event;
                event.timestamp = snapshot.timestamp;
                event.snapshotId = snapshot.snapshotId;
                event.filePath = parts.at(0);
                event.relationshipType = parts.at(1);
                event.targetPath = parts.at(2);
                event.changeType = StructuralTimelineChangeType::Removed;
                events.push_back(event);
            }
        }

        previousKeys = currentKeys;
    }

    std::sort(events.begin(), events.end(), [](const StructuralTimelineEvent& a, const StructuralTimelineEvent& b) {
        if (a.snapshotId != b.snapshotId) {
            return a.snapshotId < b.snapshotId;
        }
        const int typeCmp = QString::compare(StructuralTimelineEventUtil::changeTypeToString(a.changeType),
                                             StructuralTimelineEventUtil::changeTypeToString(b.changeType),
                                             Qt::CaseInsensitive);
        if (typeCmp != 0) {
            return typeCmp < 0;
        }
        const int fileCmp = QString::compare(a.filePath, b.filePath, Qt::CaseInsensitive);
        if (fileCmp != 0) {
            return fileCmp < 0;
        }
        const int relCmp = QString::compare(a.relationshipType, b.relationshipType, Qt::CaseInsensitive);
        if (relCmp != 0) {
            return relCmp < 0;
        }
        return QString::compare(a.targetPath, b.targetPath, Qt::CaseInsensitive) < 0;
    });

    return events;
}
}
