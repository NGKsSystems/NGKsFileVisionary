#pragma once

#include <QGraphicsView>
#include <QStringList>

#include "StructuralGraphBuilder.h"

class QGraphicsScene;

class StructuralGraphWidget : public QGraphicsView
{
    Q_OBJECT

public:
    explicit StructuralGraphWidget(QWidget* parent = nullptr);

    void setGraphData(const StructuralGraphData& data);
    void clearGraph();

    QStringList nodePathsForTesting() const;
    QStringList edgeKeysForTesting() const;
    QStringList layoutLinesForTesting() const;
    bool emitNodeActivatedForTesting(const QString& path);

signals:
    void nodeActivated(const QString& absolutePath);

protected:
    void mousePressEvent(QMouseEvent* event) override;

private:
    void renderGraph();

private:
    QGraphicsScene* m_scene = nullptr;
    StructuralGraphData m_data;
};
