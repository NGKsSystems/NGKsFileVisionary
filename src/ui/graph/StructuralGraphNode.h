#pragma once

#include <QPointF>
#include <QString>

struct StructuralGraphNode
{
    QString path;
    QString label;
    QPointF position;
    int depth = 0;
};
