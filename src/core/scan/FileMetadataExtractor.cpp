#include "FileMetadataExtractor.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>

#include "ScanTask.h"
#include "core/db/SqlHelpers.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
QString toUtcIso(const QDateTime& dt)
{
    if (!dt.isValid()) {
        return QString();
    }
    return dt.toUTC().toString(Qt::ISODate);
}

#ifdef Q_OS_WIN
DWORD readAttrs(const QString& absolutePath)
{
    const std::wstring widePath = absolutePath.toStdWString();
    return GetFileAttributesW(widePath.c_str());
}
#endif
}

bool FileMetadataExtractor::extract(const QFileInfo& fileInfo,
                                    const QString& parentPath,
                                    qint64 volumeId,
                                    qint64 scanSessionId,
                                    ScanIngestItem* out,
                                    QString* errorText)
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("null_output_item");
        }
        return false;
    }

    EntryRecord r;
    const QString absolutePath = QDir::cleanPath(fileInfo.absoluteFilePath());
    r.volumeId = volumeId;
    r.path = absolutePath;
    r.name = fileInfo.fileName().isEmpty() ? absolutePath : fileInfo.fileName();
    r.normalizedName = SqlHelpers::normalizedName(r.name);
    r.extension = fileInfo.isDir() || fileInfo.suffix().isEmpty()
        ? QString()
        : QStringLiteral(".") + fileInfo.suffix().toLower();

    r.isDir = fileInfo.isDir();
    if (!r.isDir) {
        r.hasSizeBytes = true;
        r.sizeBytes = fileInfo.size();
    }

    r.createdUtc = toUtcIso(fileInfo.birthTime());
    r.modifiedUtc = toUtcIso(fileInfo.lastModified());
    r.accessedUtc = toUtcIso(fileInfo.lastRead());

    r.hiddenFlag = fileInfo.isHidden();
    r.readonlyFlag = !fileInfo.isWritable();
    r.existsFlag = fileInfo.exists();
    r.indexedAtUtc = SqlHelpers::utcNowIso();
    r.parentPath = QDir::cleanPath(parentPath);
    r.hasLastSeenScanId = true;
    r.lastSeenScanId = scanSessionId;
    r.scanVersion = static_cast<int>(scanSessionId);
    r.entryHash = QString::fromLatin1(QCryptographicHash::hash(
                                           QStringLiteral("%1|%2|%3|%4")
                                               .arg(r.path)
                                               .arg(r.modifiedUtc)
                                               .arg(r.hasSizeBytes ? QString::number(r.sizeBytes) : QStringLiteral("dir"))
                                               .arg(r.isDir ? QStringLiteral("d") : QStringLiteral("f"))
                                               .toUtf8(),
                                           QCryptographicHash::Sha1)
                                           .toHex());
    r.metadataVersion = 1;

#ifdef Q_OS_WIN
    const DWORD attrs = readAttrs(absolutePath);
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        r.systemFlag = (attrs & FILE_ATTRIBUTE_SYSTEM) != 0;
        r.archiveFlag = (attrs & FILE_ATTRIBUTE_ARCHIVE) != 0;
        r.reparseFlag = (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        if ((attrs & FILE_ATTRIBUTE_HIDDEN) != 0) {
            r.hiddenFlag = true;
        }
        if ((attrs & FILE_ATTRIBUTE_READONLY) != 0) {
            r.readonlyFlag = true;
        }
    }
#endif

    out->record = r;
    out->parentPath = parentPath;
    return true;
}

bool FileMetadataExtractor::isReparsePoint(const QFileInfo& fileInfo)
{
#ifdef Q_OS_WIN
    const DWORD attrs = readAttrs(QDir::cleanPath(fileInfo.absoluteFilePath()));
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    Q_UNUSED(fileInfo);
    return false;
#endif
}
