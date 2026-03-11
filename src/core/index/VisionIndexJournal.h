#pragma once

#include <QString>
#include <QVector>

#include "core/db/DbTypes.h"

class MetaStore;

namespace VisionIndexEngine
{
class VisionIndexJournal
{
public:
    explicit VisionIndexJournal(MetaStore& store);

    bool append(const QString& rootPath,
                const QString& path,
                const QString& eventType,
                qint64 scanVersion,
                const QString& payload,
                QString* errorText = nullptr);

    bool list(const QString& rootPath,
              int limit,
              QVector<IndexJournalRecord>* out,
              QString* errorText = nullptr) const;

private:
    MetaStore& m_store;
};
}
