#pragma once

#include <QString>

#include "ArchiveProvider.h"
#include "core/query/QueryTypes.h"
#include "ui/model/ViewModeController.h"

namespace ArchiveNav
{
class ArchiveQueryAdapter
{
public:
    ArchiveQueryAdapter() = default;

    bool canHandlePath(const QString& path) const;
    QueryResult query(const QString& rootPath,
                      ViewModeController::UiViewMode mode,
                      const QueryOptions& options,
                      QString* adapterLog = nullptr);

private:
    ArchiveProvider m_provider;
};
}
