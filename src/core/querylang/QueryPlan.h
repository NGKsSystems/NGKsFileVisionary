#pragma once

#include <QString>
#include <QStringList>

#include "core/query/QueryTypes.h"

struct QueryPlan
{
    QStringList extensions;
    QStringList excludedExtensions;
    QString underPath;
    QStringList excludedUnderPaths;
    QString nameContains;
    QStringList nameContainsAny;
    QStringList excludedNameContains;

    bool filesOnly = false;
    bool directoriesOnly = false;

    QuerySortField sortField = QuerySortField::Name;
    bool ascending = true;

    bool includeHidden = false;
    bool includeSystem = false;

    QueryComparator sizeComparator = QueryComparator::None;
    qint64 sizeBytes = 0;

    QueryComparator modifiedAgeComparator = QueryComparator::None;
    qint64 modifiedAgeSeconds = 0;

    QueryGraphMode graphMode = QueryGraphMode::None;
    QString graphTarget;
    QString graphMatchPrecedence = QStringLiteral("exact_path_then_suffix_then_basename");

    QueryOptions toQueryOptions(const QString& runtimeRoot) const;
    QString resolveRootPath(const QString& runtimeRoot) const;

    // Supported OR form is intentionally narrow for determinism: `ext:A OR ext:B` and `name:A OR name:B`.
    QString supportedOrSyntax = QStringLiteral("ext|name_only_two_term_or");
};
