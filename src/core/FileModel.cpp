#include "FileModel.h"

#include <QDir>
#include <QFileInfo>
#include <QMimeDatabase>

FileModel::FileModel(QObject* parent)
    : QAbstractItemModel(parent)
{
    m_root = new Node();
    m_root->name = QStringLiteral("ROOT");
    m_root->isDir = true;
}

FileModel::~FileModel()
{
    deleteNode(m_root);
}

QModelIndex FileModel::index(int row, int column, const QModelIndex& parentIndex) const
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

QModelIndex FileModel::parent(const QModelIndex& child) const
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

int FileModel::rowCount(const QModelIndex& parentIndex) const
{
    const Node* parentNode = nodeFromIndex(parentIndex);
    return parentNode ? parentNode->children.size() : 0;
}

int FileModel::columnCount(const QModelIndex& parentIndex) const
{
    Q_UNUSED(parentIndex);
    return 5;
}

QVariant FileModel::data(const QModelIndex& idx, int role) const
{
    if (!idx.isValid()) {
        return QVariant();
    }
    const Node* node = static_cast<Node*>(idx.internalPointer());
    if (!node) {
        return QVariant();
    }
    if (role == Qt::DisplayRole) {
        switch (idx.column()) {
        case 0: return node->name;
        case 1: return node->type;
        case 2: return node->isDir ? QString() : QString::number(node->size);
        case 3: return node->modified.toString(Qt::ISODate);
        case 4: return node->fullPath;
        default: return QVariant();
        }
    }
    return QVariant();
}

QVariant FileModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QVariant();
    }
    switch (section) {
    case 0: return QStringLiteral("Name");
    case 1: return QStringLiteral("Type");
    case 2: return QStringLiteral("Size");
    case 3: return QStringLiteral("Modified");
    case 4: return QStringLiteral("Full Path");
    default: return QVariant();
    }
}

void FileModel::clear()
{
    beginResetModel();
    deleteNode(m_root);
    m_root = new Node();
    m_root->name = QStringLiteral("ROOT");
    m_root->isDir = true;
    m_pathToNode.clear();
    endResetModel();
}

void FileModel::setViewMode(FileViewMode mode, const QString& rootPath)
{
    m_viewMode = mode;
    m_rootPath = QDir::cleanPath(rootPath);
}

void FileModel::appendBatch(const QVector<FileEntry>& entries)
{
    for (const FileEntry& entry : entries) {
        const QString cleanedPath = QDir::cleanPath(entry.absolutePath);
        if (m_pathToNode.contains(cleanedPath)) {
            continue;
        }

        Node* node = makeNode(entry);

        Node* parentNode = m_root;
        if (m_viewMode == FileViewMode::FullHierarchy) {
            const QString parentPath = QDir::cleanPath(QFileInfo(cleanedPath).absolutePath());
            parentNode = ensureDirectoryNode(parentPath);
        }

        if (!parentNode) {
            parentNode = m_root;
        }

        node->parent = parentNode;
        const int insertRow = parentNode->children.size();
        const QModelIndex parentIndex = indexFromNode(parentNode);
        beginInsertRows(parentIndex, insertRow, insertRow);
        parentNode->children.push_back(node);
        m_pathToNode.insert(cleanedPath, node);
        endInsertRows();
    }
}

quint64 FileModel::itemCount() const
{
    return static_cast<quint64>(m_pathToNode.size());
}

FileModel::Node* FileModel::makeNode(const FileEntry& entry)
{
    const QFileInfo info(entry.absolutePath);
    Node* node = new Node();
    node->name = entry.name;
    node->isDir = entry.isDir;
    node->size = entry.size;
    node->modified = entry.modified;
    node->fullPath = QDir::cleanPath(entry.absolutePath);
    node->type = entry.isDir ? QStringLiteral("Folder") : QMimeDatabase().mimeTypeForFile(info).comment();
    node->parent = m_root;
    return node;
}

FileModel::Node* FileModel::ensureDirectoryNode(const QString& absolutePath)
{
    const QString cleanPath = QDir::cleanPath(absolutePath);
    if (cleanPath.isEmpty() || cleanPath == QStringLiteral(".") || cleanPath == m_rootPath) {
        return m_root;
    }

    if (m_pathToNode.contains(cleanPath)) {
        Node* existing = m_pathToNode.value(cleanPath);
        if (existing->isDir) {
            return existing;
        }
        return m_root;
    }

    const QFileInfo info(cleanPath);
    const QString parentPath = QDir::cleanPath(info.absolutePath());
    Node* parentNode = ensureDirectoryNode(parentPath);
    if (!parentNode) {
        parentNode = m_root;
    }

    Node* dirNode = new Node();
    dirNode->name = info.fileName().isEmpty() ? cleanPath : info.fileName();
    dirNode->type = QStringLiteral("Folder");
    dirNode->isDir = true;
    dirNode->fullPath = cleanPath;
    dirNode->parent = parentNode;

    const int insertRow = parentNode->children.size();
    const QModelIndex parentIndex = indexFromNode(parentNode);
    beginInsertRows(parentIndex, insertRow, insertRow);
    parentNode->children.push_back(dirNode);
    m_pathToNode.insert(cleanPath, dirNode);
    endInsertRows();

    return dirNode;
}

QModelIndex FileModel::indexFromNode(Node* node) const
{
    if (!node || node == m_root) {
        return QModelIndex();
    }

    Node* parentNode = node->parent;
    if (!parentNode) {
        return QModelIndex();
    }

    const int row = parentNode->children.indexOf(node);
    if (row < 0) {
        return QModelIndex();
    }

    return createIndex(row, 0, node);
}

FileModel::Node* FileModel::nodeFromIndex(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return m_root;
    }
    return static_cast<Node*>(index.internalPointer());
}

void FileModel::deleteNode(Node* node)
{
    if (!node) {
        return;
    }
    for (Node* child : node->children) {
        deleteNode(child);
    }
    delete node;
}
