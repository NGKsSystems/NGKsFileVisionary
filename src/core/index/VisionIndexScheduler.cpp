#include "VisionIndexScheduler.h"

#include <QDir>
#include <QFileInfo>

namespace {
QString canonicalPath(const QString& path)
{
    return QDir::fromNativeSeparators(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
}
}

namespace VisionIndexEngine
{
bool VisionIndexScheduler::enqueue(const VisionIndexWorkItem& item, QString* reason)
{
    const QString root = canonicalPath(item.rootPath);
    if (root.isEmpty()) {
        if (reason) {
            *reason = QStringLiteral("empty_root");
        }
        return false;
    }

    for (const VisionIndexWorkItem& queued : m_queue) {
        if (isSameOrParentPath(queued.rootPath, root) || isSameOrParentPath(root, queued.rootPath)) {
            if (reason) {
                *reason = QStringLiteral("deduped_overlap");
            }
            return false;
        }
    }

    VisionIndexWorkItem normalized = item;
    normalized.rootPath = root;
    m_queue.push_back(normalized);

    if (reason) {
        *reason = QStringLiteral("queued");
    }
    return true;
}

bool VisionIndexScheduler::dequeueNext(VisionIndexWorkItem* out)
{
    if (!out || m_queue.isEmpty()) {
        return false;
    }

    int selected = 0;
    for (int i = 1; i < m_queue.size(); ++i) {
        if (m_queue[i].visiblePriority && !m_queue[selected].visiblePriority) {
            selected = i;
        }
    }

    *out = m_queue[selected];
    m_queue.removeAt(selected);
    return true;
}

bool VisionIndexScheduler::isEmpty() const
{
    return m_queue.isEmpty();
}

bool VisionIndexScheduler::isSameOrParentPath(const QString& parent, const QString& candidate)
{
    const QString p = canonicalPath(parent).toLower();
    const QString c = canonicalPath(candidate).toLower();
    if (p == c) {
        return true;
    }
    return c.startsWith(p + QStringLiteral("/"));
}
}
