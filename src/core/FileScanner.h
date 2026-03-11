#pragma once

#include <QAtomicInteger>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

struct FileEntry
{
    QString name;
    bool isDir = false;
    quint64 size = 0;
    QDateTime modified;
    QString absolutePath;
};

enum class FileViewMode
{
    Standard = 0,
    FullHierarchy = 1,
    FlatFiles = 2,
};

class QFileInfo;

class FileScanner : public QObject
{
    Q_OBJECT

public:
    explicit FileScanner(QObject* parent = nullptr);

public slots:
    void startScan(quint64 scanId,
                   const QString& root,
                   bool showHidden,
                   bool showSystem,
                   const QStringList& extensions,
                   const QString& searchText,
                   int viewMode);
    void cancel();

signals:
    void batchReady(quint64 scanId, const QVector<FileEntry>& entries);
    void progress(quint64 scanId, const QString& stage, quint64 enumerated, quint64 matched);
    void finished(quint64 scanId, bool canceled, quint64 enumerated, quint64 matched, const QString& error);

private:
    bool matchesFilters(const QFileInfo& fileInfo,
                        bool showHidden,
                        bool showSystem,
                        const QStringList& extensions,
                        const QString& searchText) const;

private:
    QAtomicInteger<bool> m_cancelRequested;
};
