#include "ArchiveReader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>

#include "util/PathUtils.h"

namespace ArchiveNav
{
namespace
{
struct ParsedBlock
{
    QString path;
    QString folder;
    QString size;
    QString modified;
};

bool parseModified(const QString& value, QDateTime* out)
{
    if (!out) {
        return false;
    }

    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    QDateTime dt = QDateTime::fromString(trimmed, QStringLiteral("yyyy-MM-dd hh:mm:ss"));
    if (!dt.isValid()) {
        dt = QDateTime::fromString(trimmed, QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"));
    }
    if (!dt.isValid()) {
        dt = QDateTime::fromString(trimmed, Qt::ISODate);
    }
    if (dt.isValid()) {
        *out = dt;
        return true;
    }

    return false;
}
}

bool ArchiveReader::listArchiveEntries(const QString& archivePath,
                                       QVector<ArchiveEntry>* out,
                                       QString* errorText,
                                       QString* readerLog) const
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("archive_reader_null_output");
        }
        return false;
    }

    out->clear();

    const QFileInfo archiveInfo(archivePath);
    if (!archiveInfo.exists() || !archiveInfo.isFile()) {
        if (errorText) {
            *errorText = QStringLiteral("archive_reader_archive_not_found");
        }
        return false;
    }

    const QString sevenZip = resolve7zaPath();
    if (!QFileInfo::exists(sevenZip)) {
        if (errorText) {
            *errorText = QStringLiteral("archive_reader_missing_7za");
        }
        return false;
    }

    QProcess proc;
    proc.setProgram(sevenZip);
    proc.setArguments({QStringLiteral("l"), QStringLiteral("-slt"), archiveInfo.absoluteFilePath()});
    proc.start();
    if (!proc.waitForStarted(5000)) {
        if (errorText) {
            *errorText = QStringLiteral("archive_reader_failed_to_start_7za");
        }
        return false;
    }
    if (!proc.waitForFinished(120000)) {
        proc.kill();
        if (errorText) {
            *errorText = QStringLiteral("archive_reader_7za_timeout");
        }
        return false;
    }

    const QString stdOut = QString::fromLocal8Bit(proc.readAllStandardOutput());
    const QString stdErr = QString::fromLocal8Bit(proc.readAllStandardError());
    if (readerLog) {
        *readerLog = stdOut + QStringLiteral("\n") + stdErr;
    }

    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        if (errorText) {
            *errorText = QStringLiteral("archive_reader_7za_failed_exit_%1").arg(proc.exitCode());
        }
        return false;
    }

    *out = parseSltOutput(archiveInfo.absoluteFilePath(), stdOut);
    return true;
}

QString ArchiveReader::resolve7zaPath() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(QStringLiteral("../third_party/7zip/7za.exe")),
        QDir(appDir).filePath(QStringLiteral("../../third_party/7zip/7za.exe")),
        QDir(appDir).filePath(QStringLiteral("../../../third_party/7zip/7za.exe")),
        QDir::current().filePath(QStringLiteral("third_party/7zip/7za.exe")),
    };

    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }

    return candidates.first();
}

QVector<ArchiveEntry> ArchiveReader::parseSltOutput(const QString& archivePath, const QString& outputText) const
{
    QVector<ArchiveEntry> out;

    const QString archiveName = QFileInfo(archivePath).fileName();
    const QString normalizedArchivePath = PathUtils::normalizeInternalPath(QDir::cleanPath(archivePath));
    const QStringList lines = outputText.split('\n');
    ParsedBlock current;
    bool inBlock = false;

    auto flushCurrent = [&]() {
        if (!inBlock) {
            return;
        }

        QString normalizedPath = PathUtils::normalizeInternalPath(current.path);
        while (normalizedPath.endsWith('/')) {
            normalizedPath.chop(1);
        }
        if (normalizedPath.isEmpty()) {
            inBlock = false;
            current = ParsedBlock();
            return;
        }

        if (QString::compare(normalizedPath, archiveName, Qt::CaseInsensitive) == 0
            || QString::compare(normalizedPath, normalizedArchivePath, Qt::CaseInsensitive) == 0) {
            inBlock = false;
            current = ParsedBlock();
            return;
        }

        ArchiveEntry entry;
        entry.archivePath = QDir::cleanPath(archivePath);
        entry.internalPath = normalizedPath;
        entry.name = normalizedPath.section('/', -1);
        entry.isDir = current.folder.trimmed() == QStringLiteral("+");
        entry.size = current.size.trimmed().toULongLong();
        parseModified(current.modified, &entry.modified);
        out.push_back(entry);

        inBlock = false;
        current = ParsedBlock();
    };

    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.startsWith(QStringLiteral("Path = "))) {
            flushCurrent();
            inBlock = true;
            current.path = line.mid(QStringLiteral("Path = ").size());
            continue;
        }

        if (!inBlock) {
            continue;
        }

        if (line.startsWith(QStringLiteral("Folder = "))) {
            current.folder = line.mid(QStringLiteral("Folder = ").size());
            continue;
        }
        if (line.startsWith(QStringLiteral("Size = "))) {
            current.size = line.mid(QStringLiteral("Size = ").size());
            continue;
        }
        if (line.startsWith(QStringLiteral("Modified = "))) {
            current.modified = line.mid(QStringLiteral("Modified = ").size());
            continue;
        }
    }

    flushCurrent();
    return out;
}
}
