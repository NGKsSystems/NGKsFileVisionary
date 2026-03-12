#pragma once

#include <QString>
#include <QStringList>

#include "core/query/QueryTypes.h"

struct QueryPlan
{
    QStringList extensions;
    QString underPath;
    QString nameContains;

    bool filesOnly = false;
    bool directoriesOnly = false;

    QuerySortField sortField = QuerySortField::Name;
    bool ascending = true;

    bool includeHidden = false;
    bool includeSystem = false;

    QueryOptions toQueryOptions() const;
    QString resolveRootPath(const QString& runtimeRoot) const;
};
