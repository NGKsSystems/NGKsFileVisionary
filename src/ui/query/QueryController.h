#pragma once

#include <QString>

#include "core/query/QueryTypes.h"
#include "core/querylang/QueryParseResult.h"
#include "ui/model/DirectoryModel.h"
#include "ui/model/ViewModeController.h"

class QueryController
{
public:
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
