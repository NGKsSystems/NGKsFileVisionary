#pragma once

#include <QAbstractItemModel>
#include <QDateTime>
#include <QString>
#include <QVector>

struct ArchiveEntry
{
    QString path;
    QString name;
    bool isFolder = false;
    quint64 size = 0;
    QDateTime modified;
};

class ArchiveModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    explicit ArchiveModel(QObject* parent = nullptr);
    ~ArchiveModel() override;

    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void setEntries(const QVector<ArchiveEntry>& entries);
    void setCurrentPath(const QString& internalPath);
    QString currentPath() const;
    QString modelPathForIndex(const QModelIndex& index) const;

private:
    struct Node {
        QString name;
        QString fullPath;
        bool isFolder = true;
        quint64 size = 0;
        QDateTime modified;
        Node* parent = nullptr;
        QVector<Node*> children;
    };

    Node* nodeFromIndex(const QModelIndex& index) const;
    void deleteNode(Node* node);
    void buildTree(const QVector<ArchiveEntry>& entries);

private:
    Node* m_root = nullptr;
    QString m_currentPath;
};
