#pragma once

#include <QString>
#include <QVector>

namespace VisionIndexEngine
{
struct VisionIndexWorkItem
{
    QString rootPath;
    QString mode;
    bool force = false;
    bool visiblePriority = false;
};

class VisionIndexScheduler
{
public:
    bool enqueue(const VisionIndexWorkItem& item, QString* reason = nullptr);
    bool dequeueNext(VisionIndexWorkItem* out);
    bool isEmpty() const;

private:
    static bool isSameOrParentPath(const QString& parent, const QString& candidate);

private:
    QVector<VisionIndexWorkItem> m_queue;
};
}
