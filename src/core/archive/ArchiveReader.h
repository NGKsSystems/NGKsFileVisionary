#pragma once

#include <QString>
#include <QVector>

#include "ArchiveEntry.h"

namespace ArchiveNav
{
class ArchiveReader
{
public:
    bool listArchiveEntries(const QString& archivePath,
                            QVector<ArchiveEntry>* out,
                            QString* errorText = nullptr,
                            QString* readerLog = nullptr) const;

private:
    QString resolve7zaPath() const;
    QVector<ArchiveEntry> parseSltOutput(const QString& archivePath, const QString& outputText) const;
};
}
