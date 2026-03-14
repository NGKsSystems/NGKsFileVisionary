#include "StructuralGraphBuilder.h"

#include <QFileInfo>
#include <QHash>
#include <QQueue>
#include <QSet>

#include <algorithm>

namespace {
QString normalizePath(const QString& path)
{
    return path.trimmed().replace('\\', '/');
}

bool isSupportedRelationship(const QString& relationship)
{
    const QString token = relationship.trimmed().toLower();
    return token == QStringLiteral("include_ref")
        || token == QStringLiteral("import_ref")
        || token == QStringLiteral("require_ref")
        || token == QStringLiteral("path_ref");
}

QString edgeKey(const QString& source, const QString& target, const QString& relationship)
{
    return source + QStringLiteral("|") + target + QStringLiteral("|") + relationship;
}

QStringList sortedValues(const QSet<QString>& values)
{
    QStringList out = values.values();
    std::sort(out.begin(), out.end(), [](const QString& a, const QString& b) {
        return QString::compare(a, b, Qt::CaseInsensitive) < 0;
    });
    return out;
}
}

namespace StructuralGraphBuilder
{
StructuralGraphData build(const QVector<StructuralResultRow>& rows,
                          const StructuralGraphBuildOptions& options)
{
    StructuralGraphData graph;

    QSet<QString> nodeSet;
    QSet<QString> edgeSet;
    QVector<StructuralGraphEdge> collectedEdges;

    for (const StructuralResultRow& row : rows) {
        if (row.category != StructuralResultCategory::Reference) {
            continue;
        }

        const QString relationship = row.relationship.trimmed().isEmpty() ? row.status.trimmed() : row.relationship.trimmed();
        if (!isSupportedRelationship(relationship)) {
            continue;
        }

        QString source = normalizePath(!row.sourceFile.trimmed().isEmpty() ? row.sourceFile : row.primaryPath);
        QString target = normalizePath(!row.secondaryPath.trimmed().isEmpty() ? row.secondaryPath : row.primaryPath);
        if (source.isEmpty() || target.isEmpty()) {
            continue;
        }

        if (options.mode == StructuralGraphMode::ReverseDependency) {
            qSwap(source, target);
        }

        const QString dedupe = edgeKey(source, target, relationship);
        if (edgeSet.contains(dedupe)) {
            continue;
        }

        edgeSet.insert(dedupe);

        StructuralGraphEdge edge;
        edge.fromPath = source;
        edge.toPath = target;
        edge.relationship = relationship;
        collectedEdges.push_back(edge);

        nodeSet.insert(source);
        nodeSet.insert(target);
    }

    QStringList orderedNodes = sortedValues(nodeSet);
    if (orderedNodes.size() > options.maxNodes) {
        orderedNodes = orderedNodes.mid(0, options.maxNodes);
    }
    const QSet<QString> limitedSet = QSet<QString>(orderedNodes.begin(), orderedNodes.end());

    QVector<StructuralGraphEdge> limitedEdges;
    limitedEdges.reserve(collectedEdges.size());
    for (const StructuralGraphEdge& edge : collectedEdges) {
        if (!limitedSet.contains(edge.fromPath) || !limitedSet.contains(edge.toPath)) {
            continue;
        }
        limitedEdges.push_back(edge);
    }

    std::sort(limitedEdges.begin(), limitedEdges.end(), [](const StructuralGraphEdge& a, const StructuralGraphEdge& b) {
        const int fromCmp = QString::compare(a.fromPath, b.fromPath, Qt::CaseInsensitive);
        if (fromCmp != 0) {
            return fromCmp < 0;
        }
        const int toCmp = QString::compare(a.toPath, b.toPath, Qt::CaseInsensitive);
        if (toCmp != 0) {
            return toCmp < 0;
        }
        return QString::compare(a.relationship, b.relationship, Qt::CaseInsensitive) < 0;
    });

    QHash<QString, QSet<QString>> adjacency;
    QHash<QString, int> indegree;
    for (const QString& node : orderedNodes) {
        adjacency.insert(node, {});
        indegree.insert(node, 0);
    }

    for (const StructuralGraphEdge& edge : limitedEdges) {
        adjacency[edge.fromPath].insert(edge.toPath);
        indegree[edge.toPath] = indegree.value(edge.toPath) + 1;
    }

    QHash<QString, int> depthByNode;
    QStringList roots;
    for (const QString& node : orderedNodes) {
        if (indegree.value(node) == 0) {
            roots.push_back(node);
        }
    }
    std::sort(roots.begin(), roots.end(), [](const QString& a, const QString& b) {
        return QString::compare(a, b, Qt::CaseInsensitive) < 0;
    });

    QQueue<QString> queue;
    for (const QString& root : roots) {
        depthByNode.insert(root, 0);
        queue.enqueue(root);
    }

    while (!queue.isEmpty()) {
        const QString current = queue.dequeue();
        const int currentDepth = depthByNode.value(current, 0);
        QStringList neighbors = sortedValues(adjacency.value(current));
        for (const QString& neighbor : neighbors) {
            const int candidateDepth = currentDepth + 1;
            const int existingDepth = depthByNode.contains(neighbor)
                ? depthByNode.value(neighbor)
                : -1;
            if (existingDepth < candidateDepth) {
                depthByNode.insert(neighbor, candidateDepth);
                queue.enqueue(neighbor);
            }
        }
    }

    int maxDepth = 0;
    for (const QString& node : orderedNodes) {
        if (!depthByNode.contains(node)) {
            depthByNode.insert(node, maxDepth + 1);
        }
        maxDepth = qMax(maxDepth, depthByNode.value(node));
    }

    QHash<int, QStringList> depthBuckets;
    for (const QString& node : orderedNodes) {
        depthBuckets[depthByNode.value(node)].push_back(node);
    }

    for (auto it = depthBuckets.begin(); it != depthBuckets.end(); ++it) {
        QStringList& bucket = it.value();
        std::sort(bucket.begin(), bucket.end(), [](const QString& a, const QString& b) {
            return QString::compare(a, b, Qt::CaseInsensitive) < 0;
        });
    }

    for (int depth = 0; depth <= maxDepth + 1; ++depth) {
        const QStringList bucket = depthBuckets.value(depth);
        for (int i = 0; i < bucket.size(); ++i) {
            const QString& path = bucket.at(i);
            StructuralGraphNode node;
            node.path = path;
            node.label = QFileInfo(path).fileName();
            if (node.label.isEmpty()) {
                node.label = path;
            }
            node.depth = depth;
            node.position = QPointF(60.0 + (static_cast<double>(depth) * 260.0),
                                    60.0 + (static_cast<double>(i) * 90.0));
            graph.nodes.push_back(node);
        }
    }

    graph.edges = limitedEdges;
    return graph;
}
}
