#include "ArchiveQueryAdapter.h"

namespace ArchiveNav
{
bool ArchiveQueryAdapter::canHandlePath(const QString& path) const
{
    return m_provider.canHandlePath(path);
}

QueryResult ArchiveQueryAdapter::query(const QString& rootPath,
                                       ViewModeController::UiViewMode mode,
                                       const QueryOptions& options,
                                       QString* adapterLog)
{
    return m_provider.query(rootPath, mode, options, adapterLog);
}
}
