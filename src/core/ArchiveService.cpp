#include "ArchiveService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include "../util/PathUtils.h"

ArchiveService::ArchiveService(QObject* parent)
    : QObject(parent)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath("../third_party/7zip/7za.exe"),
        QDir(appDir).filePath("../../third_party/7zip/7za.exe"),
        QDir(appDir).filePath("../../../third_party/7zip/7za.exe"),
        QDir::current().filePath("third_party/7zip/7za.exe"),
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            m_7zaPath = candidate;
            break;
        }
    }
    if (m_7zaPath.isEmpty()) {
        m_7zaPath = candidates.first();
    }
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &ArchiveService::onReadyRead);
    connect(&m_process, &QProcess::readyReadStandardError, this, &ArchiveService::onReadyRead);
    connect(&m_process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            &ArchiveService::onProcessFinished);
}

void ArchiveService::set7zaPath(const QString& path)
{
    m_7zaPath = path;
}

void ArchiveService::listEntries(const QString& archivePath)
{
    runProcess({"l", "-slt", archivePath}, QStringLiteral("list"));
}

void ArchiveService::extractAll(const QString& archivePath, const QString& destDir)
{
    runProcess({"x", archivePath, QStringLiteral("-o%1").arg(destDir), "-y", "-bsp1", "-bb1"},
               QStringLiteral("extract_all"));
}

void ArchiveService::extractSelected(const QString& archivePath,
                                     const QStringList& internalPaths,
                                     const QString& destDir)
{
    QStringList args = {"x", archivePath};
    args.append(internalPaths);
    args << QStringLiteral("-o%1").arg(destDir) << "-y" << "-bsp1" << "-bb1";
    runProcess(args, QStringLiteral("extract_selected"));
}

void ArchiveService::createArchive(const QString& outputArchive, const QStringList& inputPaths)
{
    QStringList args = {"a", outputArchive};
    args.append(inputPaths);
    args << "-y" << "-mx=5" << "-bsp1" << "-bb1";
    runProcess(args, QStringLiteral("create"));
}

void ArchiveService::cancel()
{
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        emit operationFinished(false, QStringLiteral("Canceled"));
    }
}

void ArchiveService::onReadyRead()
{
    const QString out = QString::fromLocal8Bit(m_process.readAllStandardOutput());
    const QString err = QString::fromLocal8Bit(m_process.readAllStandardError());
    if (!out.isEmpty()) {
        m_collectedOutput += out;
        emit logLine(out);
        parseProgress(out);
    }
    if (!err.isEmpty()) {
        m_collectedOutput += err;
        emit logLine(err);
        parseProgress(err);
    }
}

void ArchiveService::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    const bool ok = (exitStatus == QProcess::NormalExit && exitCode == 0);
    if (m_operationName == QStringLiteral("list") && ok) {
        emit listReady(parseListOutput(m_collectedOutput));
    }
    emit operationFinished(ok, ok ? QStringLiteral("Completed") : QStringLiteral("Failed"));
}

void ArchiveService::runProcess(const QStringList& args, const QString& operationName)
{
    if (m_process.state() != QProcess::NotRunning) {
        emit operationFinished(false, QStringLiteral("Another operation is running"));
        return;
    }

    m_operationName = operationName;
    m_collectedOutput.clear();
    m_process.start(m_7zaPath, args);
}

QVector<ArchiveEntry> ArchiveService::parseListOutput(const QString& text) const
{
    QVector<ArchiveEntry> entries;
    ArchiveEntry current;
    bool inBlock = false;
    const QStringList lines = text.split('\n');
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.startsWith("Path = ")) {
            if (inBlock && !current.path.isEmpty()) {
                entries.push_back(current);
                current = ArchiveEntry();
            }
            inBlock = true;
            current.path = PathUtils::normalizeInternalPath(line.mid(QString("Path = ").size()));
            current.name = current.path.section('/', -1);
        } else if (line.startsWith("Size = ")) {
            current.size = line.mid(QString("Size = ").size()).toULongLong();
        } else if (line.startsWith("Modified = ")) {
            current.modified = QDateTime::fromString(line.mid(QString("Modified = ").size()), "yyyy-MM-dd hh:mm:ss");
        } else if (line.startsWith("Folder = ")) {
            current.isFolder = (line.endsWith('+'));
        }
    }
    if (inBlock && !current.path.isEmpty()) {
        entries.push_back(current);
    }
    return entries;
}

void ArchiveService::parseProgress(const QString& text)
{
    const QRegularExpression re(R"((\d{1,3})%)");
    QRegularExpressionMatchIterator iterator = re.globalMatch(text);
    int lastValue = -1;
    while (iterator.hasNext()) {
        const auto match = iterator.next();
        lastValue = match.captured(1).toInt();
    }
    if (lastValue >= 0) {
        emit progressPercent(lastValue);
    }
}
