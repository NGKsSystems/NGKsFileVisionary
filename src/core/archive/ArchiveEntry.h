#pragma once

#include <QDateTime>
#include <QString>

namespace ArchiveNav
{
struct ArchiveEntry
{
    QString archivePath;
    QString internalPath;
    QString name;
    bool isDir = false;
    quint64 size = 0;
    QDateTime modified;
};
}
