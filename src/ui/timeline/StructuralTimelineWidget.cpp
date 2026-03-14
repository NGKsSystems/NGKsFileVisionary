#include "StructuralTimelineWidget.h"

#include <QSignalBlocker>

StructuralTimelineWidget::StructuralTimelineWidget(QWidget* parent)
    : QListWidget(parent)
{
    connect(this, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        if (!item) {
            return;
        }
        const QString path = item->data(Qt::UserRole + 1).toString();
        if (!path.isEmpty()) {
            emit eventActivated(path);
        }
    });

    connect(this, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (!item) {
            return;
        }
        const QString path = item->data(Qt::UserRole + 1).toString();
        if (!path.isEmpty()) {
            emit eventActivated(path);
        }
    });
}

void StructuralTimelineWidget::setTimelineEvents(const QVector<StructuralTimelineEvent>& events)
{
    m_events = events;

    QSignalBlocker blocker(this);
    clear();

    for (const StructuralTimelineEvent& event : m_events) {
        const QString label = QStringLiteral("[%1] snapshot=%2 %3 %4 -> %5 (%6)")
                                  .arg(event.timestamp)
                                  .arg(event.snapshotId)
                                  .arg(StructuralTimelineEventUtil::changeTypeToString(event.changeType))
                                  .arg(event.filePath)
                                  .arg(event.targetPath)
                                  .arg(event.relationshipType);
        auto* item = new QListWidgetItem(label, this);
        item->setData(Qt::UserRole + 1, event.filePath);
        item->setData(Qt::UserRole + 2, event.relationshipType);
        item->setData(Qt::UserRole + 3, event.targetPath);
        addItem(item);
    }
}

void StructuralTimelineWidget::clearTimeline()
{
    m_events.clear();
    clear();
}

QStringList StructuralTimelineWidget::eventLinesForTesting() const
{
    QStringList lines;
    lines.reserve(count());
    for (int i = 0; i < count(); ++i) {
        const QListWidgetItem* item = this->item(i);
        if (item) {
            lines.push_back(item->text());
        }
    }
    return lines;
}

bool StructuralTimelineWidget::emitEventActivatedForTesting(const QString& filePath,
                                                            const QString& relationshipType,
                                                            const QString& targetPath)
{
    for (int i = 0; i < count(); ++i) {
        QListWidgetItem* item = this->item(i);
        if (!item) {
            continue;
        }
        const QString path = item->data(Qt::UserRole + 1).toString();
        const QString rel = item->data(Qt::UserRole + 2).toString();
        const QString tgt = item->data(Qt::UserRole + 3).toString();
        if (QString::compare(path, filePath, Qt::CaseInsensitive) == 0
            && QString::compare(rel, relationshipType, Qt::CaseInsensitive) == 0
            && QString::compare(tgt, targetPath, Qt::CaseInsensitive) == 0) {
            emit eventActivated(path);
            return true;
        }
    }
    return false;
}
