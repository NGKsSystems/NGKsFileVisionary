#pragma once

#include <QDateTime>
#include <QString>

enum class ChangeEventType {
    Created,
    Modified,
    Deleted,
    RenamedOld,
    RenamedNew,
    RenamedPair,
    Unknown
};

struct ChangeEvent
{
    ChangeEventType type = ChangeEventType::Unknown;
    QString watchedRoot;
    QString targetPath;
    QString oldPath;
    QDateTime timestampUtc;
};

QString changeEventTypeToString(ChangeEventType type);
QString changeEventToJsonLine(const ChangeEvent& event);
