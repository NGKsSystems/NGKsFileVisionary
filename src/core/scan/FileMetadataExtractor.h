#pragma once

#include <QString>

#include "core/db/DbTypes.h"
#include "ScanTask.h"

class QFileInfo;

class FileMetadataExtractor
{
public:
    static bool extract(const QFileInfo& fileInfo,
                        const QString& parentPath,
                        qint64 volumeId,
                        qint64 scanSessionId,
                        ScanIngestItem* out,
                        QString* errorText = nullptr);

    static bool isReparsePoint(const QFileInfo& fileInfo);
};
