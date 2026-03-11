#pragma once

#include <QAbstractItemModel>
#include <QDateTime>
#include <QHash>
#include <QString>
#include <QVector>

#include "FileScanner.h"

class FileModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    explicit FileModel(QObject* parent = nullptr);
    ~FileModel() override;

    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void clear();
    void setViewMode(FileViewMode mode, const QString& rootPath);
    void appendBatch(const QVector<FileEntry>& entries);
    quint64 itemCount() const;

private:
    struct Node {
        QString name;
        QString type;
        quint64 size = 0;
        QDateTime modified;
        QString fullPath;
        Node* parent = nullptr;
        QVector<Node*> children;
        bool isDir = false;
    };

    Node* makeNode(const FileEntry& entry);
    Node* ensureDirectoryNode(const QString& absolutePath);
    QModelIndex indexFromNode(Node* node) const;
    Node* nodeFromIndex(const QModelIndex& index) const;
    void deleteNode(Node* node);

private:
    Node* m_root = nullptr;
    QHash<QString, Node*> m_pathToNode;
    FileViewMode m_viewMode = FileViewMode::Standard;
    QString m_rootPath;
};
