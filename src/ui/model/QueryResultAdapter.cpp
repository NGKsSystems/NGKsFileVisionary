#include "QueryResultAdapter.h"

#include <QDateTime>

namespace QueryResultAdapter
{
QVector<FileEntry> toFileEntries(const QueryResult& result)
{
    QVector<FileEntry> out;
    out.reserve(result.rows.size());

    for (const QueryRow& row : result.rows) {
        FileEntry entry;
        entry.name = row.name;
        entry.isDir = row.isDir;
        entry.size = row.hasSizeBytes ? static_cast<quint64>(row.sizeBytes) : 0;
        entry.modified = QDateTime::fromString(row.modifiedUtc, Qt::ISODate);
        entry.absolutePath = row.path;
        out.push_back(entry);
    }

    return out;
}
}
