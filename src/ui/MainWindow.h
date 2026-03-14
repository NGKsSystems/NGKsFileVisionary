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
#include <QVector>

#include <atomic>

#include "../core/FileModel.h"
#include "../core/FileScanner.h"
#include "../core/TreeSnapshotService.h"
#include "ArchiveExplorer.h"
#include "graph/StructuralGraphBuilder.h"
#include "model/ViewModeController.h"
#include "model/StructuralFilterState.h"
#include "model/StructuralResultRow.h"
#include "model/StructuralSortEngine.h"
#include "timeline/StructuralTimelineBuilder.h"
#include "core/query/QueryTypes.h"
#include "core/services/RefreshTypes.h"

class QCheckBox;
class QComboBox;
class QDockWidget;
class QLabel;
class QProcess;
class QProgressDialog;
class QTabWidget;
class QTreeWidgetItem;
class DirectoryModel;
class QueryBarWidget;
class QueryController;
class StructuralGraphWidget;
class StructuralTimelineWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

    struct StructuralPanelState
    {
        int activeTab = 0;
        QStringList queryHistory;
        int queryHistoryIndex = -1;
        QString currentQuery;
        QStringList lastResults;
    };

public:
    explicit MainWindow(QWidget* parent);
    explicit MainWindow(bool testMode = false,
                        const QString& startupRoot = QString(),
                        const QString& actionLogPath = QString(),
                        const QString& testScriptPath = QString(),
                        const QString& uiDbPathOverride = QString(),
                        QWidget* parent = nullptr);
    ~MainWindow() override;

    bool triggerShowHistoryForTesting(const QString& rootPath,
                                      const QString& selectedFilePath,
                                      int* rowCount = nullptr,
                                      QString* errorText = nullptr);
    bool triggerSnapshotsForTesting(const QString& rootPath,
                                    int* rowCount = nullptr,
                                    qint64* createdSnapshotId = nullptr,
                                    QString* errorText = nullptr);
    bool triggerCompareSnapshotsForTesting(const QString& rootPath,
                                           int* rowCount = nullptr,
                                           int* addedCount = nullptr,
                                           int* removedCount = nullptr,
                                           int* changedCount = nullptr,
                                           QString* errorText = nullptr);
    bool triggerHistorySnapshotPanelForTesting(const QString& rootPath,
                                               const QString& selectedFilePath,
                                               qint64 oldSnapshotId,
                                               qint64 newSnapshotId,
                                               int* historyRowCount = nullptr,
                                               int* snapshotRowCount = nullptr,
                                               int* diffRowCount = nullptr,
                                               QStringList* historyRowsOut = nullptr,
                                               QStringList* snapshotRowsOut = nullptr,
                                               QStringList* diffRowsOut = nullptr,
                                               QString* navigationPathOut = nullptr,
                                               QString* errorText = nullptr);
    bool triggerReferencePanelForTesting(const QString& rootPath,
                                         const QString& selectedFilePath,
                                         int* referencesRowCount = nullptr,
                                         int* usedByRowCount = nullptr,
                                         QStringList* referencesRowsOut = nullptr,
                                         QStringList* usedByRowsOut = nullptr,
                                         QString* navigationPathOut = nullptr,
                                         QString* errorText = nullptr);
    bool triggerStructuralQueryDispatchForTesting(const QString& rootPath,
                                                  const QString& queryText,
                                                  int* activeTabIndex = nullptr,
                                                  QString* activeTabLabel = nullptr,
                                                  int* rowCount = nullptr,
                                                  QStringList* rowsOut = nullptr,
                                                  QString* navigationPathOut = nullptr,
                                                  QString* errorText = nullptr);
    bool triggerStructuralBackForTesting(int* activeTabIndex = nullptr,
                                         QString* activeTabLabel = nullptr,
                                         int* rowCount = nullptr,
                                         QString* currentQueryOut = nullptr,
                                         int* historySizeOut = nullptr,
                                         int* historyIndexOut = nullptr,
                                         QString* errorText = nullptr);
    bool triggerStructuralForwardForTesting(int* activeTabIndex = nullptr,
                                            QString* activeTabLabel = nullptr,
                                            int* rowCount = nullptr,
                                            QString* currentQueryOut = nullptr,
                                            int* historySizeOut = nullptr,
                                            int* historyIndexOut = nullptr,
                                            QString* errorText = nullptr);
    bool triggerStructuralRefreshForTesting(int* activeTabIndex = nullptr,
                                            QString* activeTabLabel = nullptr,
                                            int* rowCount = nullptr,
                                            QString* currentQueryOut = nullptr,
                                            int* historySizeOut = nullptr,
                                            int* historyIndexOut = nullptr,
                                            QString* errorText = nullptr);
    quintptr structuralPanelInstanceTokenForTesting() const;
    int structuralQueryHistorySizeForTesting() const;
    int structuralQueryHistoryIndexForTesting() const;
    QString structuralCurrentQueryForTesting() const;

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
    void onActionOpenStructuralPanel();
    void onActionShowHistory();
    void onActionSnapshots();
    void onActionCompareSnapshots();
    void onStructuralPanelTabChanged(int index);
    void onStructuralCompareSnapshots();
    void onStructuralShowReferences();
    void onStructuralShowUsedBy();
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
    QString graphQueryTargetForPath(const QString& absolutePath) const;
    void executeGraphQueryFromSelection(QueryGraphMode graphMode, const QString& absolutePath);
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
    void setupStructuralPanel();
    bool resolveStructuralPanelContextFromCurrentSelection(QString* rootPathOut,
                                                           QString* targetPathOut,
                                                           QString* errorText = nullptr);
    void updateStructuralPanelContextLabel();
    bool refreshStructuralSnapshotSelectors(QString* errorText = nullptr);
    bool loadStructuralHistoryView(QString* errorText = nullptr, int* rowCount = nullptr);
    bool loadStructuralSnapshotView(QString* errorText = nullptr, int* rowCount = nullptr);
    bool loadStructuralDiffView(qint64 oldSnapshotId,
                                qint64 newSnapshotId,
                                QString* errorText = nullptr,
                                int* rowCount = nullptr,
                                int* addedCount = nullptr,
                                int* removedCount = nullptr,
                                int* changedCount = nullptr);
    bool loadStructuralReferenceView(QueryGraphMode mode,
                                    QString* errorText = nullptr,
                                    int* rowCount = nullptr);
    bool dispatchStructuralQueryToPanel(const QString& queryText,
                                        QString* errorText = nullptr,
                                        int* activeTabIndex = nullptr,
                                        int* rowCount = nullptr,
                                        bool pushHistory = true,
                                        bool forceRefresh = false);
    bool ensureStructuralPanel();
    bool dispatchQueryToExistingPanel(const QString& queryText,
                                      QString* errorText = nullptr,
                                      int* activeTabIndex = nullptr,
                                      int* rowCount = nullptr,
                                      bool pushHistory = true,
                                      bool forceRefresh = false);
    void pushStructuralQueryHistory(const QString& queryText);
    bool navigateStructuralBack(QString* errorText = nullptr,
                                int* activeTabIndex = nullptr,
                                int* rowCount = nullptr);
    bool navigateStructuralForward(QString* errorText = nullptr,
                                   int* activeTabIndex = nullptr,
                                   int* rowCount = nullptr);
    bool refreshStructuralCurrentQuery(QString* errorText = nullptr,
                                       int* activeTabIndex = nullptr,
                                       int* rowCount = nullptr);
    void updateStructuralNavigationButtons();
    void updateStructuralFilterStateFromControls();
    void updateStructuralSortStateFromControls();
    void updateStructuralGraphFromCanonicalRows();
    void updateStructuralTimelineFromCanonicalRows();
    void setStructuralViewMode(int mode);
    void setStructuralGraphModeEnabled(bool enabled);
    void onStructuralGraphNodeActivated(const QString& absolutePath);
    void onStructuralTimelineEventActivated(const QString& absolutePath);
    void refreshQueryAutocompleteContext();
    void updateStructuralFilterControlChoices(const QVector<StructuralResultRow>& canonicalRows);
    void applyStructuralFiltersToCurrentRows(const QString& statusPrefix = QString());
    void clearStructuralFilters(bool applyNow = true);
    void setStructuralCanonicalRows(const QVector<StructuralResultRow>& rows,
                                    const QString& viewRoot,
                                    const QString& statusPrefix);
    bool navigateFromCurrentModelRow(int rowIndex, QString* navigatedPathOut = nullptr);
    QStringList collectCurrentModelRows(int maxRows = 200) const;
    bool loadHistoryRowsForPath(const QString& selectedFilePath,
                                QString* errorText = nullptr,
                                int* rowCount = nullptr,
                                QString* resolvedRoot = nullptr,
                                QString* resolvedTarget = nullptr);
    QString resolveSnapshotRootForAction(const QString& actionPath) const;
    bool renderSnapshotListForRoot(const QString& rootPath,
                                   bool createNewSnapshot,
                                   int* rowCount = nullptr,
                                   qint64* createdSnapshotId = nullptr,
                                   qint64* newestSnapshotId = nullptr,
                                   qint64* previousSnapshotId = nullptr,
                                   QString* errorText = nullptr);
    bool renderSnapshotDiffForRoot(const QString& rootPath,
                                   qint64 oldSnapshotId,
                                   qint64 newSnapshotId,
                                   int* rowCount = nullptr,
                                   int* addedCount = nullptr,
                                   int* removedCount = nullptr,
                                   int* changedCount = nullptr,
                                   QString* errorText = nullptr);

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
    QStringList m_recentStructuralPaths;
    QStringList m_recentSnapshotTokens;
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
    QAction* m_actionOpenStructuralPanel = nullptr;
    QAction* m_actionShowHistory = nullptr;
    QAction* m_actionSnapshots = nullptr;
    QAction* m_actionCompareSnapshots = nullptr;
    QAction* m_actionCopyPath = nullptr;
    QAction* m_actionRename = nullptr;
    QAction* m_actionPinFavorite = nullptr;
    QAction* m_actionUnpinFavorite = nullptr;

    QDockWidget* m_structuralPanelDock = nullptr;
    QTabWidget* m_structuralTabWidget = nullptr;
    QLabel* m_structuralContextLabel = nullptr;
    QLabel* m_structuralHistoryStatusLabel = nullptr;
    QLabel* m_structuralSnapshotStatusLabel = nullptr;
    QLabel* m_structuralDiffStatusLabel = nullptr;
    QLabel* m_structuralReferenceStatusLabel = nullptr;
    QPushButton* m_structuralHistoryLoadButton = nullptr;
    QPushButton* m_structuralSnapshotLoadButton = nullptr;
    QPushButton* m_structuralDiffCompareButton = nullptr;
    QPushButton* m_structuralShowReferencesButton = nullptr;
    QPushButton* m_structuralShowUsedByButton = nullptr;
    QPushButton* m_structuralBackButton = nullptr;
    QPushButton* m_structuralForwardButton = nullptr;
    QPushButton* m_structuralRefreshButton = nullptr;
    QComboBox* m_structuralCategoryFilterCombo = nullptr;
    QComboBox* m_structuralStatusFilterCombo = nullptr;
    QComboBox* m_structuralExtensionFilterCombo = nullptr;
    QComboBox* m_structuralRelationshipFilterCombo = nullptr;
    QLineEdit* m_structuralTextFilterEdit = nullptr;
    QPushButton* m_structuralClearFiltersButton = nullptr;
    QComboBox* m_structuralSortFieldCombo = nullptr;
    QPushButton* m_structuralSortDirectionButton = nullptr;
    QPushButton* m_structuralTableViewButton = nullptr;
    QPushButton* m_structuralGraphViewButton = nullptr;
    QPushButton* m_structuralTimelineViewButton = nullptr;
    QComboBox* m_structuralGraphModeCombo = nullptr;
    QLabel* m_structuralGraphStatusLabel = nullptr;
    StructuralGraphWidget* m_structuralGraphWidget = nullptr;
    QLabel* m_structuralTimelineStatusLabel = nullptr;
    StructuralTimelineWidget* m_structuralTimelineWidget = nullptr;
    QComboBox* m_structuralOldSnapshotCombo = nullptr;
    QComboBox* m_structuralNewSnapshotCombo = nullptr;
    QString m_structuralRootPath;
    QString m_structuralTargetPath;
    QString m_structuralStatusPrefix;
    QString m_structuralViewRoot;
    QVector<StructuralResultRow> m_structuralCanonicalRows;
    QVector<StructuralResultRow> m_structuralFilteredRows;
    StructuralFilterState m_structuralFilterState;
    StructuralSortField m_structuralSortField = StructuralSortField::PrimaryPath;
    StructuralSortDirection m_structuralSortDirection = StructuralSortDirection::Ascending;
    StructuralGraphMode m_structuralGraphMode = StructuralGraphMode::Dependency;
    bool m_structuralGraphModeEnabled = false;
    QVector<StructuralTimelineEvent> m_structuralTimelineEvents;
    int m_structuralViewMode = 0;
    StructuralPanelState m_structuralPanelState;
};
