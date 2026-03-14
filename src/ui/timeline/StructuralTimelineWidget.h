#pragma once

#include <QListWidget>

#include "StructuralTimelineEvent.h"

class StructuralTimelineWidget : public QListWidget
{
    Q_OBJECT

public:
    explicit StructuralTimelineWidget(QWidget* parent = nullptr);

    void setTimelineEvents(const QVector<StructuralTimelineEvent>& events);
    void clearTimeline();

    QStringList eventLinesForTesting() const;
    bool emitEventActivatedForTesting(const QString& filePath,
                                      const QString& relationshipType,
                                      const QString& targetPath);

signals:
    void eventActivated(const QString& filePath);

private:
    QVector<StructuralTimelineEvent> m_events;
};
