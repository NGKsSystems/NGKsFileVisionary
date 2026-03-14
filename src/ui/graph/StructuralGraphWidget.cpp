#include "StructuralGraphWidget.h"

#include <QGraphicsEllipseItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QMouseEvent>
#include <QPen>

#include <algorithm>

namespace {
constexpr double kNodeRadius = 22.0;
}

StructuralGraphWidget::StructuralGraphWidget(QWidget* parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
{
    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing, true);
    setMinimumHeight(260);
}

void StructuralGraphWidget::setGraphData(const StructuralGraphData& data)
{
    m_data = data;
    renderGraph();
}

void StructuralGraphWidget::clearGraph()
{
    m_data.nodes.clear();
    m_data.edges.clear();
    renderGraph();
}

QStringList StructuralGraphWidget::nodePathsForTesting() const
{
    QStringList nodes;
    nodes.reserve(m_data.nodes.size());
    for (const StructuralGraphNode& node : m_data.nodes) {
        nodes.push_back(node.path);
    }
    return nodes;
}

QStringList StructuralGraphWidget::edgeKeysForTesting() const
{
    QStringList edges;
    edges.reserve(m_data.edges.size());
    for (const StructuralGraphEdge& edge : m_data.edges) {
        edges.push_back(edge.fromPath + QStringLiteral(" -> ") + edge.toPath + QStringLiteral(" [") + edge.relationship + QStringLiteral("]"));
    }
    return edges;
}

QStringList StructuralGraphWidget::layoutLinesForTesting() const
{
    QStringList lines;
    lines.reserve(m_data.nodes.size());
    for (const StructuralGraphNode& node : m_data.nodes) {
        lines.push_back(QStringLiteral("%1|%2|%3")
                            .arg(node.path)
                            .arg(QString::number(node.position.x(), 'f', 1))
                            .arg(QString::number(node.position.y(), 'f', 1)));
    }
    return lines;
}

bool StructuralGraphWidget::emitNodeActivatedForTesting(const QString& path)
{
    for (const StructuralGraphNode& node : m_data.nodes) {
        if (QString::compare(node.path, path, Qt::CaseInsensitive) == 0) {
            emit nodeActivated(node.path);
            return true;
        }
    }
    return false;
}

void StructuralGraphWidget::mousePressEvent(QMouseEvent* event)
{
    if (event) {
        QGraphicsItem* item = itemAt(event->pos());
        while (item) {
            const QString path = item->data(0).toString();
            if (!path.isEmpty()) {
                emit nodeActivated(path);
                break;
            }
            item = item->parentItem();
        }
    }
    QGraphicsView::mousePressEvent(event);
}

void StructuralGraphWidget::renderGraph()
{
    m_scene->clear();

    QHash<QString, QPointF> nodeCenter;
    for (const StructuralGraphNode& node : m_data.nodes) {
        nodeCenter.insert(node.path, node.position);
    }

    QPen edgePen(QColor(95, 95, 110));
    edgePen.setWidth(2);
    for (const StructuralGraphEdge& edge : m_data.edges) {
        if (!nodeCenter.contains(edge.fromPath) || !nodeCenter.contains(edge.toPath)) {
            continue;
        }
        const QPointF from = nodeCenter.value(edge.fromPath);
        const QPointF to = nodeCenter.value(edge.toPath);
        m_scene->addLine(QLineF(from, to), edgePen);
    }

    QPen nodePen(QColor(30, 55, 85));
    nodePen.setWidth(2);
    QBrush nodeBrush(QColor(185, 220, 245));

    for (const StructuralGraphNode& node : m_data.nodes) {
        const QRectF ellipseRect(node.position.x() - kNodeRadius,
                                 node.position.y() - kNodeRadius,
                                 kNodeRadius * 2.0,
                                 kNodeRadius * 2.0);
        QGraphicsEllipseItem* ellipse = m_scene->addEllipse(ellipseRect, nodePen, nodeBrush);
        ellipse->setData(0, node.path);

        QGraphicsSimpleTextItem* text = m_scene->addSimpleText(node.label);
        text->setBrush(QBrush(QColor(25, 25, 25)));
        const QRectF bounds = text->boundingRect();
        text->setPos(node.position.x() - (bounds.width() * 0.5), node.position.y() + kNodeRadius + 4.0);
        text->setData(0, node.path);
    }

    const QRectF bounds = m_scene->itemsBoundingRect();
    if (!bounds.isNull()) {
        m_scene->setSceneRect(bounds.adjusted(-30.0, -30.0, 30.0, 30.0));
    } else {
        m_scene->setSceneRect(QRectF(0, 0, 640, 320));
    }
}
