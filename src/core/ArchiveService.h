#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVector>

#include "ArchiveModel.h"

class ArchiveService : public QObject
{
    Q_OBJECT

public:
    explicit ArchiveService(QObject* parent = nullptr);

    void set7zaPath(const QString& path);

public slots:
    void listEntries(const QString& archivePath);
    void extractAll(const QString& archivePath, const QString& destDir);
    void extractSelected(const QString& archivePath, const QStringList& internalPaths, const QString& destDir);
    void createArchive(const QString& outputArchive, const QStringList& inputPaths);
    void cancel();

signals:
    void logLine(const QString& line);
    void progressPercent(int value);
    void listReady(const QVector<ArchiveEntry>& entries);
    void operationFinished(bool success, const QString& message);

private slots:
    void onReadyRead();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    void runProcess(const QStringList& args, const QString& operationName);
    QVector<ArchiveEntry> parseListOutput(const QString& text) const;
    void parseProgress(const QString& text);

private:
    QString m_7zaPath;
    QProcess m_process;
    QString m_operationName;
    QString m_collectedOutput;
};
