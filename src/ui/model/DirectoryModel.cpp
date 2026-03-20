#include "DirectoryModel.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFileInfoList>

#include "core/archive/ArchiveQueryAdapter.h"
#include "core/services/VisionIndexService.h"

namespace {
QueryResult queryFilesystemFallback(const QString& rootPath,
                                    const QueryOptions& options,
                                    ViewModeController::UiViewMode mode)
{
    QueryResult result;
    const QDir root(rootPath);
    if (!root.exists()) {
        result.ok = false;
        result.errorText = QStringLiteral("not_found");
        return result;
    }

    QVector<QueryRow> rows;

    auto appendRow = [&](const QFileInfo& info, int depth) {
        QueryRow row;
        row.path = QDir::fromNativeSeparators(info.absoluteFilePath());
        row.name = info.fileName();
        row.normalizedName = row.name;
        row.extension = info.isDir() ? QString() : QStringLiteral(".") + info.suffix().toLower();
        row.isDir = info.isDir();
        row.hasSizeBytes = !row.isDir;
        row.sizeBytes = row.hasSizeBytes ? info.size() : 0;
        row.modifiedUtc = info.lastModified().toUTC().toString(Qt::ISODate);
        row.hiddenFlag = info.isHidden();
        row.systemFlag = false;
        row.archiveFlag = false;
        row.existsFlag = true;
        row.depth = depth;
        rows.push_back(row);
    };

    if (mode == ViewModeController::UiViewMode::Standard) {
        const QFileInfoList children = root.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries,
                                                          QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
        rows.reserve(children.size());
        for (const QFileInfo& info : children) {
            appendRow(info, 1);
        }
    } else {
        QDirIterator it(rootPath,
                        QDir::NoDotAndDotDot | QDir::AllEntries,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QFileInfo info(it.nextFileInfo());
            if (!info.exists()) {
                continue;
            }
            if (info.isSymLink() && info.isDir()) {
                continue;
            }

            const QString relative = root.relativeFilePath(info.absoluteFilePath());
            const int depth = qMax(1, relative.count(QLatin1Char('/')) + 1);
            appendRow(info, depth);
        }
    }

    QString filterError;
    if (!QueryTypesUtil::applyFiltersAndSort(options, &rows, &filterError)) {
        result.ok = false;
        result.errorText = filterError;
        return result;
    }

    result.ok = true;
    result.rows = rows;
    result.totalCount = rows.size();
    return result;
}
}

DirectoryModel::DirectoryModel()
    : m_visionService(new VisionIndexService())
    , m_archiveQueryAdapter(new ArchiveNav::ArchiveQueryAdapter())
{
}

DirectoryModel::~DirectoryModel()
{
    delete m_archiveQueryAdapter;
    m_archiveQueryAdapter = nullptr;

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

    if (m_archiveQueryAdapter && m_archiveQueryAdapter->canHandlePath(request.rootPath)) {
        return m_archiveQueryAdapter->query(request.rootPath, request.mode, options, nullptr);
    }

    if (request.authoritativeRebuild) {
        return queryFilesystemFallback(request.rootPath, options, request.mode);
    }

    QueryResult result;
    if (!isReady()) {
        result.ok = false;
        result.errorText = QStringLiteral("directory_model_not_ready");
        return result;
    }

    switch (request.mode) {
    case ViewModeController::UiViewMode::Standard: {
        QueryResult indexed = m_visionService->queryChildren(request.rootPath, options);
        if (indexed.ok) {
            return indexed;
        }

        if (QString::compare(indexed.errorText.trimmed(), QStringLiteral("not_found"), Qt::CaseInsensitive) != 0) {
            return indexed;
        }

        QueryResult fallback = queryFilesystemFallback(request.rootPath, options, request.mode);
        if (!fallback.ok) {
            return indexed;
        }

        return fallback;
    }
    case ViewModeController::UiViewMode::Hierarchy: {
        QueryResult indexed = m_visionService->queryHierarchy(request.rootPath, options);
        if (indexed.ok || QString::compare(indexed.errorText.trimmed(), QStringLiteral("not_found"), Qt::CaseInsensitive) != 0) {
            return indexed;
        }

        QueryResult fallback = queryFilesystemFallback(request.rootPath, options, request.mode);
        return fallback.ok ? fallback : indexed;
    }
    case ViewModeController::UiViewMode::Flat: {
        QueryResult indexed = m_visionService->queryFlat(request.rootPath, options);
        if (indexed.ok || QString::compare(indexed.errorText.trimmed(), QStringLiteral("not_found"), Qt::CaseInsensitive) != 0) {
            return indexed;
        }

        QueryResult fallback = queryFilesystemFallback(request.rootPath, options, request.mode);
        return fallback.ok ? fallback : indexed;
    }
    }

    result.ok = false;
    result.errorText = QStringLiteral("unknown_ui_mode");
    return result;
}

QueryResult DirectoryModel::queryGraph(const QString& rootPath,
                                       QueryGraphMode mode,
                                       const QString& graphTarget,
                                       const QueryOptions& options)
{
    QueryResult result;
    if (!isReady()) {
        result.ok = false;
        result.errorText = QStringLiteral("directory_model_not_ready");
        return result;
    }
    return m_visionService->queryGraph(rootPath, mode, graphTarget, options);
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
