#include "VisionIndexJournal.h"

#include "core/db/MetaStore.h"

namespace VisionIndexEngine
{
VisionIndexJournal::VisionIndexJournal(MetaStore& store)
    : m_store(store)
{
}

bool VisionIndexJournal::append(const QString& rootPath,
                                const QString& path,
                                const QString& eventType,
                                qint64 scanVersion,
                                const QString& payload,
                                QString* errorText)
{
    IndexJournalRecord record;
    record.rootPath = rootPath;
    record.path = path;
    record.eventType = eventType;
    record.scanVersion = scanVersion;
    record.payload = payload;
    return m_store.appendIndexJournal(record, nullptr, errorText);
}

bool VisionIndexJournal::list(const QString& rootPath,
                              int limit,
                              QVector<IndexJournalRecord>* out,
                              QString* errorText) const
{
    return m_store.listIndexJournal(rootPath, limit, out, errorText);
}
}
