#include "QueryController.h"

#include <QDir>
#include <QRegularExpression>

#include "core/querylang/QueryParser.h"

QueryController::QueryController(DirectoryModel* directoryModel)
    : m_directoryModel(directoryModel)
{
}

QueryController::PrepareResult QueryController::prepare(const QString& queryString,
                                                        const QString& runtimeRoot,
                                                        bool includeHiddenDefault,
                                                        bool includeSystemDefault,
                                                        QuerySortField fallbackSortField,
                                                        bool fallbackAscending) const
{
    PrepareResult out;
    out.queryString = queryString.trimmed();

    QueryParser parser;
    out.parseResult = parser.parse(out.queryString);
    if (!out.parseResult.ok) {
        out.parseError = out.parseResult.errorMessage;
        return out;
    }

    out.plan = out.parseResult.plan;

    if (!queryContainsKey(out.queryString, QStringLiteral("hidden"))) {
        out.plan.includeHidden = includeHiddenDefault;
    }
    if (!queryContainsKey(out.queryString, QStringLiteral("system"))) {
        out.plan.includeSystem = includeSystemDefault;
    }

    if (!queryContainsKey(out.queryString, QStringLiteral("sort"))) {
        out.plan.sortField = fallbackSortField;
    }
    if (!queryContainsKey(out.queryString, QStringLiteral("order"))) {
        out.plan.ascending = fallbackAscending;
    }

    out.executionRoot = out.plan.resolveRootPath(runtimeRoot);
    out.options = out.plan.toQueryOptions(runtimeRoot);
    out.ok = true;
    return out;
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

    if (!m_directoryModel) {
        out.parseError = QStringLiteral("directory_model_not_ready");
        return out;
    }

    const PrepareResult prepared = prepare(queryString,
                                           runtimeRoot,
                                           includeHiddenDefault,
                                           includeSystemDefault,
                                           fallbackSortField,
                                           fallbackAscending);

    out.parseResult = prepared.parseResult;
    out.parseOk = prepared.ok;
    out.parseError = prepared.parseError;
    if (!out.parseOk) {
        return out;
    }

    const QueryPlan plan = prepared.plan;
    out.executionRoot = prepared.executionRoot;
    const QueryOptions options = prepared.options;

    if (plan.graphMode != QueryGraphMode::None) {
        out.queryResult = m_directoryModel->queryGraph(out.executionRoot,
                                                       plan.graphMode,
                                                       plan.graphTarget,
                                                       options);
        out.ok = out.queryResult.ok;
        return out;
    }

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
