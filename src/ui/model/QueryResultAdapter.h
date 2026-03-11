#pragma once

#include <QVector>

#include "core/FileScanner.h"
#include "core/query/QueryTypes.h"

namespace QueryResultAdapter
{
QVector<FileEntry> toFileEntries(const QueryResult& result);
}
