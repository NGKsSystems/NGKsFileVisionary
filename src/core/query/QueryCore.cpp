#include "QueryCore.h"

#include <algorithm>
#include <QDir>
#include <QFileInfo>

#include "FolderQueryService.h"
#include "SearchQueryService.h"
#include "SnapshotQueryService.h"
#include "core/reference/ReferenceGraphRepository.h"
#include "core/db/MetaStore.h"
#include "core/perf/ResultLimiter.h"

namespace
{
QString normalizedPath(const QString& path)
{
    return QDir::fromNativeSeparators(QDir::cleanPath(path));
}

QStringList matchPathsByHint(const QStringList& candidates,
                             const QString& sourceRoot,
                             const QString& rawHint)
{
    const QString hint = normalizedPath(rawHint);
    if (hint.trimmed().isEmpty()) {
        return {};
    }

    QStringList exactMatches;
    QStringList suffixMatches;
    QStringList basenameMatches;

    QStringList exactCandidates;
    exactCandidates.push_back(hint);
    if (!QFileInfo(hint).isAbsolute()) {
        exactCandidates.push_back(normalizedPath(QDir(sourceRoot).filePath(hint)));
    }

    const QString suffixNeedle = hint;
    const QString basenameNeedle = QFileInfo(hint).fileName().toLower();

    for (const QString& candidateRaw : candidates) {
        const QString candidate = normalizedPath(candidateRaw);

        bool exact = false;
        for (const QString& exactCandidate : exactCandidates) {
            if (candidate.compare(exactCandidate, Qt::CaseInsensitive) == 0) {
                exact = true;
                break;
            }
        }
        if (exact) {
            if (!exactMatches.contains(candidate, Qt::CaseInsensitive)) {
                exactMatches.push_back(candidate);
            }
            continue;
        }

        if (suffixNeedle.contains('/')) {
            const QString suffixToken = QStringLiteral("/") + suffixNeedle;
            if (candidate.endsWith(suffixNeedle, Qt::CaseInsensitive)
                || candidate.endsWith(suffixToken, Qt::CaseInsensitive)) {
                if (!suffixMatches.contains(candidate, Qt::CaseInsensitive)) {
                    suffixMatches.push_back(candidate);
                }
                continue;
            }
        }

        if (!basenameNeedle.isEmpty() && QFileInfo(candidate).fileName().toLower() == basenameNeedle) {
            if (!basenameMatches.contains(candidate, Qt::CaseInsensitive)) {
                basenameMatches.push_back(candidate);
            }
        }
    }

    if (!exactMatches.isEmpty()) {
        std::sort(exactMatches.begin(), exactMatches.end(), [](const QString& a, const QString& b) {
            return a.compare(b, Qt::CaseInsensitive) < 0;
        });
        return exactMatches;
    }
    if (!suffixMatches.isEmpty()) {
        std::sort(suffixMatches.begin(), suffixMatches.end(), [](const QString& a, const QString& b) {
            return a.compare(b, Qt::CaseInsensitive) < 0;
        });
        return suffixMatches;
    }

    std::sort(basenameMatches.begin(), basenameMatches.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return basenameMatches;
}

QueryRow edgeToRow(const ReferenceGraph::ReferenceEdge& edge, QueryGraphMode mode)
{
    QueryRow row;
    row.hasGraphEdge = true;
    row.graphSourcePath = edge.sourcePath;
    row.graphTargetPath = edge.targetPath;
    row.graphReferenceType = edge.referenceType;
    row.graphResolvedFlag = edge.resolvedFlag;
    row.graphConfidence = edge.confidence;
    row.graphSourceLine = edge.sourceLine;
    row.path = (mode == QueryGraphMode::References) ? edge.targetPath : edge.sourcePath;
    row.name = QFileInfo(row.path).fileName();
    row.normalizedName = row.name.toLower();
    const QString suffix = QFileInfo(row.path).suffix().toLower();
    row.extension = suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix;
    row.isDir = false;
    row.existsFlag = true;
    return row;
}

int compareGraphRows(const QueryRow& a, const QueryRow& b, const QueryOptions& options)
{
    int primary = 0;
    switch (options.sortField) {
    case QuerySortField::Name:
        primary = QString::compare(a.normalizedName, b.normalizedName, Qt::CaseInsensitive);
        break;
    case QuerySortField::Modified:
        primary = QString::compare(a.graphSourcePath, b.graphSourcePath, Qt::CaseInsensitive);
        break;
    case QuerySortField::Size:
        primary = a.graphSourceLine - b.graphSourceLine;
        break;
    case QuerySortField::Path:
        primary = QString::compare(a.path, b.path, Qt::CaseInsensitive);
        break;
    }

    if (!options.ascending) {
        primary = -primary;
    }
    if (primary != 0) {
        return primary;
    }

    const int byType = QString::compare(a.graphReferenceType, b.graphReferenceType, Qt::CaseInsensitive);
    if (byType != 0) {
        return byType;
    }
    const int bySource = QString::compare(a.graphSourcePath, b.graphSourcePath, Qt::CaseInsensitive);
    if (bySource != 0) {
        return bySource;
    }
    const int byTarget = QString::compare(a.graphTargetPath, b.graphTargetPath, Qt::CaseInsensitive);
    if (byTarget != 0) {
        return byTarget;
    }
    return a.graphSourceLine - b.graphSourceLine;
}
}

QueryCore::QueryCore(MetaStore& store)
    : m_store(store)
    , m_folder(new FolderQueryService(store))
    , m_search(new SearchQueryService(store))
    , m_snapshot(new SnapshotQueryService(store))
    , m_reference(new ReferenceGraph::ReferenceGraphRepository(store))
{
}

QueryCore::~QueryCore()
{
    delete m_snapshot;
    m_snapshot = nullptr;

    delete m_search;
    m_search = nullptr;

    delete m_folder;
    m_folder = nullptr;

    delete m_reference;
    m_reference = nullptr;
}

QueryResult QueryCore::queryChildren(const QString& parentPath, const QueryOptions& options) const
{
    const QueryResult raw = m_folder->queryChildren(parentPath, options);
    ResultLimiter limiter;
    return limiter.limit(raw);
}

QueryResult QueryCore::queryFlat(const QString& rootPath, const QueryOptions& options) const
{
    const QueryResult raw = m_folder->queryFlatDescendants(rootPath, options);
    ResultLimiter limiter;
    return limiter.limit(raw);
}

QueryResult QueryCore::querySubtree(const QString& rootPath, const QueryOptions& options) const
{
    const QueryResult raw = m_snapshot->querySubtree(rootPath, options);
    ResultLimiter limiter;
    return limiter.limit(raw);
}

QueryResult QueryCore::querySearch(const QString& rootPath, const QueryOptions& options) const
{
    const QueryResult raw = m_search->queryByFilter(rootPath, options);
    ResultLimiter limiter;
    return limiter.limit(raw);
}

QueryResult QueryCore::queryGraph(const QString& rootPath,
                                  QueryGraphMode mode,
                                  const QString& graphTarget,
                                  const QueryOptions& options) const
{
    QueryResult result;
    result.ok = false;

    if (mode == QueryGraphMode::None) {
        result.errorText = QStringLiteral("graph_mode_not_set");
        return result;
    }
    if (!m_reference) {
        result.errorText = QStringLiteral("reference_repository_not_ready");
        return result;
    }

    const QString normalizedRoot = normalizedPath(rootPath);
    const QString normalizedTarget = normalizedPath(graphTarget);
    if (normalizedTarget.trimmed().isEmpty()) {
        result.errorText = QStringLiteral("empty_graph_target");
        return result;
    }

    QString errorText;
    QVector<ReferenceGraph::ReferenceEdge> allEdges;
    if (!m_reference->listBySourceRoot(normalizedRoot, &allEdges, &errorText)) {
        result.errorText = errorText;
        return result;
    }

    QStringList candidates;
    for (const ReferenceGraph::ReferenceEdge& edge : allEdges) {
        const QString candidate = (mode == QueryGraphMode::References) ? edge.sourcePath : edge.targetPath;
        if (!candidate.trimmed().isEmpty() && !candidates.contains(candidate, Qt::CaseInsensitive)) {
            candidates.push_back(candidate);
        }
    }

    const QStringList matchedPaths = matchPathsByHint(candidates, normalizedRoot, normalizedTarget);
    QVector<ReferenceGraph::ReferenceEdge> matchedEdges;
    for (const ReferenceGraph::ReferenceEdge& edge : allEdges) {
        const QString candidate = (mode == QueryGraphMode::References) ? edge.sourcePath : edge.targetPath;
        if (matchedPaths.contains(candidate, Qt::CaseInsensitive)) {
            matchedEdges.push_back(edge);
        }
    }

    QVector<QueryRow> rows;
    rows.reserve(matchedEdges.size());
    for (const ReferenceGraph::ReferenceEdge& edge : matchedEdges) {
        rows.push_back(edgeToRow(edge, mode));
    }

    // Graph queries are deterministic and intentionally narrow in VIE-P17:
    // allow sort/order and optional name substring, without generic filesystem filters.
    const QString nameNeedle = options.substringFilter.trimmed().toLower();
    if (!nameNeedle.isEmpty()) {
        QVector<QueryRow> filtered;
        filtered.reserve(rows.size());
        for (const QueryRow& row : rows) {
            const QString hayName = row.normalizedName.toLower();
            const QString hayPath = row.path.toLower();
            const QString haySource = row.graphSourcePath.toLower();
            const QString hayTarget = row.graphTargetPath.toLower();
            if (hayName.contains(nameNeedle)
                || hayPath.contains(nameNeedle)
                || haySource.contains(nameNeedle)
                || hayTarget.contains(nameNeedle)) {
                filtered.push_back(row);
            }
        }
        rows = filtered;
    }

    Q_UNUSED(options);

    result.ok = true;
    result.rows = rows;
    result.totalCount = rows.size();
    return result;
}
