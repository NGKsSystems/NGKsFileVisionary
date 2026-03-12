#pragma once

#include <QFutureWatcher>
#include <QAction>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QTimer>
#include <QToolBar>
#include <QThread>
#include <QTreeView>
#include <QTreeWidget>
#include <QDateTime>

#include <atomic>

#include "../core/FileModel.h"
#include "../core/FileScanner.h"
#include "../core/TreeSnapshotService.h"
#include "ArchiveExplorer.h"
#include "model/ViewModeController.h"
#include "core/query/QueryTypes.h"
#include "core/services/RefreshTypes.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QProcess;
class QProgressDialog;
class QTreeWidgetItem;
class DirectoryModel;
class QueryBarWidget;
class QueryController;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent);
    explicit MainWindow(bool testMode = false,
                        const QString& startupRoot = QString(),
                        const QString& actionLogPath = QString(),
                        const QString& testScriptPath = QString(),
                        QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onBrowseRoot();
    void onRescan();
    void onCancelScan();
    void onBatchReady(quint64 scanId, const QVector<FileEntry>& entries);
    void onScanProgress(quint64 scanId, const QString& stage, quint64 enumerated, quint64 matched);
    void onScanFinished(quint64 scanId, bool canceled, quint64 enumerated, quint64 matched, const QString& error);
    void onSearchChanged(const QString& text);
    void onQuerySubmitted(const QString& text);
    void onQueryCleared();
    void onFocusQueryBar();
    void onTreeContextMenu(const QPoint& pos);
    void onTreeActivated(const QModelIndex& index);
    void onSidebarItemActivated(QTreeWidgetItem* item, int column);
    void onSidebarContextMenu(const QPoint& pos);
    void onPinCurrentFolder();
    void onViewModeChanged(int index);
    void onNavigateBack();
    void onNavigateForward();
    void onNavigateUp();
    void onPublishTick();
    void onTreeSnapshotRequested(const QString& folderPath);
    void onSnapshotGenerationFinished();
    void onArchiveReadyRead();
    void onArchiveFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onActionTreeSnapshot();
    void onActionCompressZip();
    void onActionCompress7z();
    void onActionCompressTar();
    void onActionExtractHere();
    void onActionExtractTo();
    void onActionExploreArchive();
    void onActionCopyPath();
    void onActionRename();
    void onActionPinFavorite();
    void onActionUnpinFavorite();
    void onRunTestScript();
    void onRefreshPollTick();

private:
    void setupUi();
    void setupScanner();
    bool ensureDirectoryModelReady();
    QString resolveUiDbPath() const;
    QuerySortField currentQuerySortField() const;
    QString selectedPath(const QModelIndex& index) const;
    void appendRuntimeLog(const QString& message) const;
    void startScanNow();
    bool isInternalNavigableDirectory(const QFileInfo& fileInfo) const;
    bool isNavigablePath(const QString& path) const;
    void navigateToDirectory(const QString& path, bool pushHistory = true);
    void updateNavigationButtons();
    void rebuildSidebar();
    void loadFavorites();
    void saveFavorites() const;
    void addFavoritePath(const QString& path);
    void removeFavoritePath(const QString& path);
    bool isFavoritePath(const QString& path) const;
    QString displayNameForPath(const QString& path) const;
    QString normalizedDirectoryPath(const QString& path) const;
    QString favoritesConfigPath() const;
    void startSnapshotGeneration(const QString& folderPath);
    QStringList selectedPaths() const;
    bool isArchivePath(const QString& path) const;
    void copyPathListToClipboard(const QStringList& paths) const;
    void showPropertiesDialog(const QStringList& paths);
    bool renamePath(const QString& path);
    bool deletePathsWithConfirm(const QStringList& paths);
    bool copyMovePaths(const QStringList& paths, bool move);
    bool duplicatePath(const QString& path);
    void openInTerminal(const QString& path);
    void openWithCode(const QString& path);
    void openContainingFolder(const QString& path);
    void createArchiveFromPaths(const QStringList& paths, const QString& forcedExtension = QString());
    void extractArchive(const QString& archivePath, bool chooseDestination);
    void runArchiveCommand(const QString& title, const QStringList& args);
    void ensureArchiveProcessUi();
    QString currentRootPath() const;
    void createNewFolderInCurrentRoot();
    void createNewTextFileInCurrentRoot();
    void setupActionRegistry();
    void setupTestSurface();
    void configureObjectNames();
    QStringList actionPathsForCurrentContext() const;
    QString primaryActionPathForCurrentContext() const;
    void setActionContext(const QStringList& paths, const QString& selectionType);
    void logUiAction(const QString& actionName,
                     const QString& handler,
                     const QString& result,
                     const QString& outputPath = QString(),
                     const QString& error = QString()) const;
    void ensureUiActionTracePath();
    void maybeOpenStartupRoot();
    void triggerNamedAction(const QString& actionName, const QStringList& paths, const QString& selectionType);

private:
    FileModel m_fileModel;
    QSortFilterProxyModel m_proxyModel;
    QThread m_scannerThread;
    FileScanner* m_fileScanner = nullptr;
    DirectoryModel* m_directoryModel = nullptr;
    ViewModeController m_viewModeController;
    QString m_uiDbPath;

    QLineEdit* m_rootEdit = nullptr;
    QPushButton* m_browseButton = nullptr;
    QPushButton* m_rescanButton = nullptr;
    QPushButton* m_cancelButton = nullptr;
    QPushButton* m_pinCurrentButton = nullptr;
    QCheckBox* m_showHiddenCheck = nullptr;
    QCheckBox* m_showSystemCheck = nullptr;
    QLineEdit* m_extensionFilterEdit = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QueryBarWidget* m_queryBarWidget = nullptr;
    QLabel* m_statusLabel = nullptr;
    QToolBar* m_viewToolbar = nullptr;
    QComboBox* m_viewModeCombo = nullptr;
    QPushButton* m_backButton = nullptr;
    QPushButton* m_forwardButton = nullptr;
    QPushButton* m_upButton = nullptr;
    QTreeView* m_treeView = nullptr;
    QTreeWidget* m_sidebarTree = nullptr;
    QTreeWidgetItem* m_favoritesRootItem = nullptr;
    QTreeWidgetItem* m_standardRootItem = nullptr;
    QStringList m_favorites;

    bool m_scanInProgress = false;
    bool m_rescanPending = false;
    quint64 m_activeScanId = 0;
    quint64 m_nextScanId = 0;
    quint64 m_scanEntryCount = 0;
    quint64 m_scanBatchCount = 0;
    quint64 m_scanEnumeratedCount = 0;
    QVector<FileEntry> m_publishQueue;
    QTimer m_publishTimer;
    QTimer m_refreshPollTimer;
    FileViewMode m_viewMode = FileViewMode::Standard;
    QStringList m_navigationHistory;
    int m_navigationIndex = -1;

    std::atomic_bool m_snapshotCancelRequested = false;
    QString m_snapshotRootPath;
    QString m_snapshotAction;
    TreeSnapshotService::Options m_snapshotOptions;
    TreeSnapshotService::Result m_snapshotResult;
    QFutureWatcher<TreeSnapshotService::Result>* m_snapshotWatcher = nullptr;
    QProgressDialog* m_snapshotProgressDialog = nullptr;

    QProcess* m_archiveProcess = nullptr;
    QProgressDialog* m_archiveProgressDialog = nullptr;
    QString m_archiveLogBuffer;
    QString m_archiveOperationTitle;

    bool m_testMode = false;
    bool m_testScriptRunning = false;
    QString m_startupRoot;
    QString m_actionTracePath;
    QString m_testScriptPath;
    QueryController* m_queryController = nullptr;
    QString m_activeQueryString;
    bool m_queryModeActive = false;
    QStringList m_actionContextPaths;
    QString m_actionContextType;
    QDateTime m_lastRefreshRequeryAt;

    QAction* m_actionTreeSnapshot = nullptr;
    QAction* m_actionCompressZip = nullptr;
    QAction* m_actionCompress7z = nullptr;
    QAction* m_actionCompressTar = nullptr;
    QAction* m_actionExtractHere = nullptr;
    QAction* m_actionExtractTo = nullptr;
    QAction* m_actionExploreArchive = nullptr;
    QAction* m_actionCopyPath = nullptr;
    QAction* m_actionRename = nullptr;
    QAction* m_actionPinFavorite = nullptr;
    QAction* m_actionUnpinFavorite = nullptr;
};
