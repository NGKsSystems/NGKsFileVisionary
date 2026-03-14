#pragma once

#include <QVector>

#include "StructuralGraphEdge.h"
#include "StructuralGraphNode.h"
#include "../model/StructuralResultRow.h"

enum class StructuralGraphMode
{
    Dependency,
    ReverseDependency,
};

struct StructuralGraphBuildOptions
{
    StructuralGraphMode mode = StructuralGraphMode::Dependency;
    int maxNodes = 100;
};

struct StructuralGraphData
{
    QVector<StructuralGraphNode> nodes;
    QVector<StructuralGraphEdge> edges;
};

namespace StructuralGraphBuilder
{
StructuralGraphData build(const QVector<StructuralResultRow>& rows,
                          const StructuralGraphBuildOptions& options = StructuralGraphBuildOptions());
}
