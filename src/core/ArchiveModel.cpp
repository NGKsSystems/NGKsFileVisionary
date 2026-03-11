#include "ArchiveModel.h"

#include <QHash>

#include "../util/PathUtils.h"

ArchiveModel::ArchiveModel(QObject* parent)
    : QAbstractItemModel(parent)
{
    m_root = new Node();
    m_root->name = QStringLiteral("/");
    m_root->fullPath = QString();
}

ArchiveModel::~ArchiveModel()
{
    deleteNode(m_root);
}

QModelIndex ArchiveModel::index(int row, int column, const QModelIndex& parentIndex) const
{
    if (!hasIndex(row, column, parentIndex)) {
        return QModelIndex();
    }
    Node* parentNode = nodeFromIndex(parentIndex);
    if (!parentNode || row < 0 || row >= parentNode->children.size()) {
        return QModelIndex();
    }
    return createIndex(row, column, parentNode->children[row]);
}

QModelIndex ArchiveModel::parent(const QModelIndex& child) const
{
    if (!child.isValid()) {
        return QModelIndex();
    }
    Node* node = static_cast<Node*>(child.internalPointer());
    Node* parentNode = node ? node->parent : nullptr;
    if (!parentNode || parentNode == m_root) {
        return QModelIndex();
    }
    Node* grandParent = parentNode->parent;
    const int row = grandParent ? grandParent->children.indexOf(parentNode) : 0;
    return createIndex(row, 0, parentNode);
}

int ArchiveModel::rowCount(const QModelIndex& parentIndex) const
{
    const Node* parentNode = nodeFromIndex(parentIndex);
    return parentNode ? parentNode->children.size() : 0;
}

int ArchiveModel::columnCount(const QModelIndex& parentIndex) const
{
    Q_UNUSED(parentIndex);
    return 5;
}

QVariant ArchiveModel::data(const QModelIndex& idx, int role) const
{
    if (!idx.isValid()) {
        return QVariant();
    }
    Node* node = static_cast<Node*>(idx.internalPointer());
    if (!node) {
        return QVariant();
    }
    if (role == Qt::DisplayRole) {
        switch (idx.column()) {
        case 0: return node->name;
        case 1: return node->isFolder ? QStringLiteral("Folder") : QStringLiteral("File");
        case 2: return node->isFolder ? QString() : QString::number(node->size);
        case 3: return node->modified.toString(Qt::ISODate);
        case 4: return node->fullPath;
        default: return QVariant();
        }
    }
    return QVariant();
}

QVariant ArchiveModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QVariant();
    }
    switch (section) {
    case 0: return QStringLiteral("Name");
    case 1: return QStringLiteral("Type");
    case 2: return QStringLiteral("Size");
    case 3: return QStringLiteral("Modified");
    case 4: return QStringLiteral("Internal Path");
    default: return QVariant();
    }
}

void ArchiveModel::setEntries(const QVector<ArchiveEntry>& entries)
{
    beginResetModel();
    buildTree(entries);
    endResetModel();
}

void ArchiveModel::setCurrentPath(const QString& internalPath)
{
    m_currentPath = PathUtils::normalizeInternalPath(internalPath);
}

QString ArchiveModel::currentPath() const
{
    return m_currentPath;
}

QString ArchiveModel::modelPathForIndex(const QModelIndex& index) const
{
    Node* node = nodeFromIndex(index);
    return node ? node->fullPath : QString();
}

ArchiveModel::Node* ArchiveModel::nodeFromIndex(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return m_root;
    }
    return static_cast<Node*>(index.internalPointer());
}

void ArchiveModel::deleteNode(Node* node)
{
    if (!node) {
        return;
    }
    for (Node* child : node->children) {
        deleteNode(child);
    }
    delete node;
}

void ArchiveModel::buildTree(const QVector<ArchiveEntry>& entries)
{
    deleteNode(m_root);
    m_root = new Node();
    m_root->name = QStringLiteral("/");
    m_root->fullPath = QString();

    QHash<QString, Node*> pathMap;
    pathMap.insert(QString(), m_root);

    for (const ArchiveEntry& entry : entries) {
        const QString normalized = PathUtils::normalizeInternalPath(entry.path);
        const QStringList parts = normalized.split('/', Qt::SkipEmptyParts);
        QString runningPath;
        Node* parentNode = m_root;

        for (int i = 0; i < parts.size(); ++i) {
            if (!runningPath.isEmpty()) {
                runningPath += '/';
            }
            runningPath += parts[i];

            Node* node = pathMap.value(runningPath, nullptr);
            if (!node) {
                node = new Node();
                node->name = parts[i];
                node->fullPath = runningPath;
                node->isFolder = (i != parts.size() - 1) || entry.isFolder;
                node->size = (i == parts.size() - 1) ? entry.size : 0;
                node->modified = entry.modified;
                node->parent = parentNode;
                parentNode->children.push_back(node);
                pathMap.insert(runningPath, node);
            }
            parentNode = node;
        }
    }
}
