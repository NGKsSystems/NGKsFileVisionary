#include "DirectoryModel.h"

#include "core/archive/ArchiveProvider.h"
#include "core/services/VisionIndexService.h"

DirectoryModel::DirectoryModel()
    : m_visionService(new VisionIndexService())
    , m_archiveProvider(new ArchiveNav::ArchiveProvider())
{
}

DirectoryModel::~DirectoryModel()
{
    delete m_archiveProvider;
    m_archiveProvider = nullptr;

    delete m_visionService;
    m_visionService = nullptr;
}

bool DirectoryModel::initialize(const QString& dbPath, QString* errorText)
{
    if (!m_visionService) {
        if (errorText) {
            *errorText = QStringLiteral("vision_index_unavailable");
        }
        return false;
    }

    if (!m_visionService->initialize(dbPath, RefreshPolicy(), errorText)) {
        return false;
    }

    m_dbPath = dbPath;
    return true;
}

bool DirectoryModel::isReady() const
{
    return m_visionService && m_visionService->isReady();
}

QueryResult DirectoryModel::query(const Request& request)
{
    QueryOptions options;
    options.includeHidden = request.includeHidden;
    options.includeSystem = request.includeSystem;
    options.foldersFirst = request.foldersFirst;
    options.extensionFilter = request.extensionFilter;
    options.substringFilter = request.substringFilter;
    options.sortField = request.sortField;
    options.ascending = request.ascending;
    options.maxDepth = request.maxDepth;
    options.pageSize = request.pageSize;
    options.pageOffset = request.pageOffset;
    options.filesOnly = request.filesOnly;
    options.directoriesOnly = request.directoriesOnly;

    if (m_archiveProvider && m_archiveProvider->canHandlePath(request.rootPath)) {
        return m_archiveProvider->query(request.rootPath, request.mode, options, nullptr);
    }

    QueryResult result;
    if (!isReady()) {
        result.ok = false;
        result.errorText = QStringLiteral("directory_model_not_ready");
        return result;
    }

    switch (request.mode) {
    case ViewModeController::UiViewMode::Standard:
        return m_visionService->queryChildren(request.rootPath, options);
    case ViewModeController::UiViewMode::Hierarchy:
        return m_visionService->queryHierarchy(request.rootPath, options);
    case ViewModeController::UiViewMode::Flat:
        return m_visionService->queryFlat(request.rootPath, options);
    }

    result.ok = false;
    result.errorText = QStringLiteral("unknown_ui_mode");
    return result;
}

RefreshRequestResult DirectoryModel::requestRefresh(const QString& path,
                                                   bool force,
                                                   const QString& mode,
                                                   const QString& reason)
{
    RefreshRequestResult result;
    result.path = path;
    result.state = RefreshState::Failed;
    if (!isReady()) {
        result.errorText = QStringLiteral("directory_model_not_ready");
        return result;
    }
    return m_visionService->requestRefresh(path, mode, force, reason);
}

QVector<RefreshEvent> DirectoryModel::takeRefreshEvents()
{
    if (!isReady()) {
        return {};
    }
    return m_visionService->takeRefreshEvents();
}

bool DirectoryModel::waitForRefreshIdle(int timeoutMs)
{
    if (!isReady()) {
        return false;
    }
    return m_visionService->waitForRefreshIdle(timeoutMs);
}

QString DirectoryModel::dbPath() const
{
    return m_dbPath;
}
