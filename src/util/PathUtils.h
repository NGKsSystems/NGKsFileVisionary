#pragma once

#include <QString>
#include <QStringList>

namespace PathUtils
{
QString normalizeInternalPath(const QString& path);
bool isArchivePath(const QString& path);
bool isArchiveVirtualPath(const QString& path);
QString buildArchiveVirtualPath(const QString& archivePath, const QString& internalPath);
bool splitArchiveVirtualPath(const QString& path, QString* archivePath, QString* internalPath);
QString archiveVirtualParentPath(const QString& path);
QStringList splitExtensionsFilter(const QString& rawFilter);
}
