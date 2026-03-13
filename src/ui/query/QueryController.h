#pragma once

#include <QString>

#include "core/query/QueryTypes.h"
#include "core/querylang/QueryParseResult.h"
#include "ui/model/DirectoryModel.h"
#include "ui/model/ViewModeController.h"

class QueryController
{
public:
    struct PrepareResult
    {
        bool ok = false;
        QString parseError;
        QString queryString;
        QString executionRoot;
        QueryParseResult parseResult;
        QueryPlan plan;
        QueryOptions options;
    };

    struct ExecutionResult
    {
        bool ok = false;
        bool parseOk = false;
        QString parseError;
        QString executionRoot;
        QString queryString;
        QueryParseResult parseResult;
        DirectoryModel::Request request;
        QueryResult queryResult;
    };

    explicit QueryController(DirectoryModel* directoryModel);

    PrepareResult prepare(const QString& queryString,
                          const QString& runtimeRoot,
                          bool includeHiddenDefault,
                          bool includeSystemDefault,
                          QuerySortField fallbackSortField,
                          bool fallbackAscending) const;

    ExecutionResult execute(const QString& queryString,
                            const QString& runtimeRoot,
                            ViewModeController::UiViewMode viewMode,
                            bool includeHiddenDefault,
                            bool includeSystemDefault,
                            QuerySortField fallbackSortField,
                            bool fallbackAscending) const;

private:
    bool queryContainsKey(const QString& queryString, const QString& key) const;

private:
    DirectoryModel* m_directoryModel = nullptr;
};
