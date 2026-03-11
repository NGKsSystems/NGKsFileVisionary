#pragma once

#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QTreeView>

#include "../core/ArchiveModel.h"
#include "../core/ArchiveService.h"
#include "../core/NavigationHistory.h"

class ArchiveExplorer : public QMainWindow
{
    Q_OBJECT

public:
    explicit ArchiveExplorer(QWidget* parent = nullptr);
    void openArchive(const QString& archivePath);

private slots:
    void onBack();
    void onForward();
    void onSearchChanged(const QString& text);
    void onListReady(const QVector<ArchiveEntry>& entries);
    void onItemActivated(const QModelIndex& index);
    void onExtractSelected();
    void onExtractAll();
    void onCopyInternalPath();
    void onOpenEntry();
    void onTreeContextMenu(const QPoint& pos);

private:
    void refreshNavButtons();
    void navigateTo(const QString& internalPath, bool pushHistory);
    QString selectedInternalPath() const;

private:
    QString m_archivePath;
    NavigationHistory m_history;
    ArchiveService m_archiveService;
    ArchiveModel m_archiveModel;
    QSortFilterProxyModel m_proxyModel;

    QPushButton* m_backButton = nullptr;
    QPushButton* m_forwardButton = nullptr;
    QLineEdit* m_breadcrumbEdit = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QTreeView* m_treeView = nullptr;
    QPushButton* m_extractSelectedButton = nullptr;
    QPushButton* m_extractAllButton = nullptr;
    QPushButton* m_copyPathButton = nullptr;
    QPushButton* m_openEntryButton = nullptr;
};
