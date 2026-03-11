#include "ChangeEvent.h"

#include <QJsonDocument>
#include <QJsonObject>

QString changeEventTypeToString(ChangeEventType type)
{
    switch (type) {
    case ChangeEventType::Created:
        return QStringLiteral("created");
    case ChangeEventType::Modified:
        return QStringLiteral("modified");
    case ChangeEventType::Deleted:
        return QStringLiteral("deleted");
    case ChangeEventType::RenamedOld:
        return QStringLiteral("renamed_old");
    case ChangeEventType::RenamedNew:
        return QStringLiteral("renamed_new");
    case ChangeEventType::RenamedPair:
        return QStringLiteral("renamed_pair");
    case ChangeEventType::Unknown:
    default:
        return QStringLiteral("unknown");
    }
}

QString changeEventToJsonLine(const ChangeEvent& event)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("type"), changeEventTypeToString(event.type));
    obj.insert(QStringLiteral("watched_root"), event.watchedRoot);
    obj.insert(QStringLiteral("target_path"), event.targetPath);
    obj.insert(QStringLiteral("old_path"), event.oldPath);
    obj.insert(QStringLiteral("timestamp_utc"), event.timestampUtc.toString(Qt::ISODate));
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}
