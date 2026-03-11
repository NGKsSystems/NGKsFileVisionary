#pragma once

#include <QString>
#include <QStringList>

namespace PathUtils
{
QString normalizeInternalPath(const QString& path);
bool isArchivePath(const QString& path);
QStringList splitExtensionsFilter(const QString& rawFilter);
}
