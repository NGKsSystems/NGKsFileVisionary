#include "QueryController.h"

#include <QDir>
#include <QRegularExpression>

#include "core/querylang/QueryParser.h"

QueryController::QueryController(DirectoryModel* directoryModel)
    : m_directoryModel(directoryModel)
{
}

QueryController::ExecutionResult QueryController::execute(const QString& queryString,
                                                          const QString& runtimeRoot,
                                                          ViewModeController::UiViewMode viewMode,
                                                          bool includeHiddenDefault,
                                                          bool includeSystemDefault,
                                                          QuerySortField fallbackSortField,
                                                          bool fallbackAscending) const
{
    ExecutionResult out;
    out.queryString = queryString.trimmed();

    if (!m_directoryModel || !m_directoryModel->isReady()) {
        out.parseError = QStringLiteral("directory_model_not_ready");
        return out;
    }

    QueryParser parser;
    out.parseResult = parser.parse(out.queryString);
    out.parseOk = out.parseResult.ok;
    if (!out.parseOk) {
        out.parseError = out.parseResult.errorMessage;
        return out;
    }

    QueryPlan plan = out.parseResult.plan;

    if (!queryContainsKey(out.queryString, QStringLiteral("hidden"))) {
        plan.includeHidden = includeHiddenDefault;
    }
    if (!queryContainsKey(out.queryString, QStringLiteral("system"))) {
        plan.includeSystem = includeSystemDefault;
    }

    if (!queryContainsKey(out.queryString, QStringLiteral("sort"))) {
        plan.sortField = fallbackSortField;
    }
    if (!queryContainsKey(out.queryString, QStringLiteral("order"))) {
        plan.ascending = fallbackAscending;
    }

    out.executionRoot = plan.resolveRootPath(runtimeRoot);

    const QueryOptions options = plan.toQueryOptions();

    DirectoryModel::Request request;
    request.rootPath = out.executionRoot;
    request.mode = viewMode;
    request.includeHidden = options.includeHidden;
    request.includeSystem = options.includeSystem;
    request.foldersFirst = options.foldersFirst;
    request.extensionFilter = options.extensionFilter;
    request.substringFilter = options.substringFilter;
    request.sortField = options.sortField;
    request.ascending = options.ascending;
    request.maxDepth = (viewMode == ViewModeController::UiViewMode::Hierarchy) ? 64 : -1;
    request.pageSize = options.pageSize;
    request.pageOffset = options.pageOffset;
    request.filesOnly = options.filesOnly;
    request.directoriesOnly = options.directoriesOnly;

    if (viewMode == ViewModeController::UiViewMode::Flat && !request.directoriesOnly) {
        request.filesOnly = true;
    }
    if (request.directoriesOnly) {
        request.filesOnly = false;
    }
    if (request.filesOnly) {
        request.directoriesOnly = false;
    }

    out.request = request;
    out.queryResult = m_directoryModel->query(request);
    out.ok = out.queryResult.ok;
    return out;
}

bool QueryController::queryContainsKey(const QString& queryString, const QString& key) const
{
    const QString escaped = QRegularExpression::escape(key.toLower());
    const QRegularExpression pattern(QStringLiteral("(?:^|\\s)") + escaped + QStringLiteral("\\s*:"),
                                     QRegularExpression::CaseInsensitiveOption);
    return pattern.match(queryString).hasMatch();
}
