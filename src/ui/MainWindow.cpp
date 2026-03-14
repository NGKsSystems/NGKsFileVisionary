#include "MainWindow.h"

#include <QApplication>
#include <QAction>
#include <QClipboard>
#include <QCheckBox>
#include <QCoreApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QDockWidget>
#include <QEventLoop>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QFormLayout>
#include <QHash>
#include <QInputDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QProgressDialog>
#include <QProcess>
#include <QPushButton>
#include <QSaveFile>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSet>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QShortcut>
#include <QTextStream>
#include <QThread>
#include <QToolBar>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

#include "TreeSnapshotDialog.h"
#include "TreeSnapshotPreviewDialog.h"
#include "graph/StructuralGraphWidget.h"
#include "timeline/StructuralTimelineWidget.h"
#include "core/history/HistoryEntry.h"
#include "core/history/HistoryViewEngine.h"
#include "core/db/MetaStore.h"
#include "core/db/SqlHelpers.h"
#include "core/query/QueryCore.h"
#include "core/snapshot/SnapshotDiffEngine.h"
#include "core/snapshot/SnapshotRepository.h"
#include "model/DirectoryModel.h"
#include "model/QueryResultAdapter.h"
#include "model/StructuralFilterEngine.h"
#include "model/StructuralRankingEngine.h"
#include "model/StructuralResultAdapter.h"
#include "model/StructuralSortEngine.h"
#include "query/QueryBarWidget.h"
#include "query/QueryController.h"
#include "core/services/RefreshTypes.h"
#include "../util/PathUtils.h"

namespace {
QString resolveBundled7zaPath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(QStringLiteral("../third_party/7zip/7za.exe")),
        QDir(appDir).filePath(QStringLiteral("../../third_party/7zip/7za.exe")),
        QDir(appDir).filePath(QStringLiteral("../../../third_party/7zip/7za.exe")),
        QDir::current().filePath(QStringLiteral("third_party/7zip/7za.exe")),
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return candidates.first();
}
}

MainWindow::MainWindow(QWidget* parent)
    : MainWindow(false, QString(), QString(), QString(), QString(), parent)
{
}

MainWindow::MainWindow(bool testMode,
                       const QString& startupRoot,
                       const QString& actionLogPath,
                       const QString& testScriptPath,
                       const QString& uiDbPathOverride,
                       QWidget* parent)
    : QMainWindow(parent)
    , m_testMode(testMode)
    , m_startupRoot(startupRoot)
    , m_actionTracePath(actionLogPath)
    , m_testScriptPath(testScriptPath)
{
    setupUi();
    setupActionRegistry();
    setupTestSurface();
    setupScanner();
    m_directoryModel = new DirectoryModel();
    m_queryController = new QueryController(m_directoryModel);
    m_viewModeController.setModeFromIndex(m_viewModeCombo ? m_viewModeCombo->currentIndex() : 0);
    m_viewMode = m_viewModeController.toFileViewMode();
    m_uiDbPath = uiDbPathOverride.trimmed().isEmpty() ? resolveUiDbPath() : QDir::cleanPath(uiDbPathOverride);
    configureObjectNames();
    ensureUiActionTracePath();
    maybeOpenStartupRoot();

    m_publishTimer.setInterval(16);
    connect(&m_publishTimer, &QTimer::timeout, this, &MainWindow::onPublishTick);
    m_refreshPollTimer.setInterval(250);
    connect(&m_refreshPollTimer, &QTimer::timeout, this, &MainWindow::onRefreshPollTick);
    m_refreshPollTimer.start();

    if (m_testMode && !m_testScriptPath.trimmed().isEmpty()) {
        QTimer::singleShot(250, this, &MainWindow::onRunTestScript);
    }
}

MainWindow::~MainWindow()
{
    m_snapshotCancelRequested.store(true);
    if (m_snapshotWatcher) {
        m_snapshotWatcher->waitForFinished();
        delete m_snapshotWatcher;
        m_snapshotWatcher = nullptr;
    }
    if (m_snapshotProgressDialog) {
        m_snapshotProgressDialog->close();
        delete m_snapshotProgressDialog;
        m_snapshotProgressDialog = nullptr;
    }

    if (m_fileScanner) {
        QMetaObject::invokeMethod(m_fileScanner, "cancel", Qt::QueuedConnection);
    }
    m_scannerThread.quit();
    m_scannerThread.wait();

    delete m_queryController;
    m_queryController = nullptr;

    delete m_directoryModel;
    m_directoryModel = nullptr;
}

bool MainWindow::triggerShowHistoryForTesting(const QString& rootPath,
                                              const QString& selectedFilePath,
                                              int* rowCount,
                                              QString* errorText)
{
    auto writeTrace = [&](const QString& msg) {
        if (m_actionTracePath.trimmed().isEmpty()) {
            return;
        }
        QFile f(m_actionTracePath + QStringLiteral(".trace"));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
            return;
        }
        QTextStream ts(&f);
        ts << msg << '\n';
    };

    writeTrace(QStringLiteral("trigger_show_history:start"));
    if (m_rootEdit) {
        m_rootEdit->setText(QDir::cleanPath(rootPath));
    }
    writeTrace(QStringLiteral("trigger_show_history:root_set"));

    m_publishTimer.stop();
    m_refreshPollTimer.stop();
    m_scanInProgress = false;
    writeTrace(QStringLiteral("trigger_show_history:timers_stopped"));

    QString loadError;
    int rows = 0;
    writeTrace(QStringLiteral("trigger_show_history:before_load"));
    const bool ok = loadHistoryRowsForPath(selectedFilePath, &loadError, &rows, nullptr, nullptr);
    writeTrace(QStringLiteral("trigger_show_history:after_load"));
    if (rowCount) {
        *rowCount = rows;
    }
    if (errorText) {
        *errorText = loadError;
    }
    writeTrace(QStringLiteral("trigger_show_history:end ok=%1 rows=%2 error=%3")
                   .arg(ok ? QStringLiteral("true") : QStringLiteral("false"))
                   .arg(rows)
                   .arg(loadError));
    return ok;
}

bool MainWindow::triggerSnapshotsForTesting(const QString& rootPath,
                                            int* rowCount,
                                            qint64* createdSnapshotId,
                                            QString* errorText)
{
    if (m_rootEdit) {
        m_rootEdit->setText(QDir::cleanPath(rootPath));
    }

    m_publishTimer.stop();
    m_refreshPollTimer.stop();
    m_scanInProgress = false;

    QString renderError;
    const bool ok = renderSnapshotListForRoot(rootPath,
                                              true,
                                              rowCount,
                                              createdSnapshotId,
                                              nullptr,
                                              nullptr,
                                              &renderError);
    if (errorText) {
        *errorText = renderError;
    }
    return ok;
}

bool MainWindow::triggerCompareSnapshotsForTesting(const QString& rootPath,
                                                   int* rowCount,
                                                   int* addedCount,
                                                   int* removedCount,
                                                   int* changedCount,
                                                   QString* errorText)
{
    if (m_rootEdit) {
        m_rootEdit->setText(QDir::cleanPath(rootPath));
    }

    m_publishTimer.stop();
    m_refreshPollTimer.stop();
    m_scanInProgress = false;

    qint64 newestSnapshotId = 0;
    qint64 previousSnapshotId = 0;
    QString listError;
    if (!renderSnapshotListForRoot(rootPath,
                                   false,
                                   nullptr,
                                   nullptr,
                                   &newestSnapshotId,
                                   &previousSnapshotId,
                                   &listError)) {
        if (errorText) {
            *errorText = listError;
        }
        return false;
    }

    if (errorText) {
        *errorText = QStringLiteral("selected_old_snapshot_id=%1 selected_new_snapshot_id=%2")
                         .arg(previousSnapshotId)
                         .arg(newestSnapshotId);
    }

    QString diffError;
    const bool ok = renderSnapshotDiffForRoot(rootPath,
                                              previousSnapshotId,
                                              newestSnapshotId,
                                              rowCount,
                                              addedCount,
                                              removedCount,
                                              changedCount,
                                              &diffError);
    if (errorText) {
        if (ok) {
            *errorText = errorText->isEmpty()
                ? QStringLiteral("compare_ok")
                : QStringLiteral("%1 compare_ok").arg(*errorText);
        } else {
            *errorText = errorText->isEmpty()
                ? diffError
                : QStringLiteral("%1 %2").arg(*errorText, diffError);
        }
    }
    return ok;
}

void MainWindow::setupUi()
{
    setWindowTitle(QStringLiteral("NGKsFileVisionary"));
    resize(1200, 800);

    m_viewToolbar = addToolBar(QStringLiteral("View"));
    m_viewToolbar->setMovable(false);
    m_backButton = new QPushButton(QStringLiteral("Back"), this);
    m_forwardButton = new QPushButton(QStringLiteral("Forward"), this);
    m_upButton = new QPushButton(QStringLiteral("Up"), this);
    m_viewModeCombo = new QComboBox(this);
    m_viewModeCombo->addItem(QStringLiteral("Standard"));
    m_viewModeCombo->addItem(QStringLiteral("Full Hierarchy"));
    m_viewModeCombo->addItem(QStringLiteral("Flat Files"));
    m_viewToolbar->addWidget(m_backButton);
    m_viewToolbar->addWidget(m_forwardButton);
    m_viewToolbar->addWidget(m_upButton);
    m_viewToolbar->addSeparator();
    m_viewToolbar->addWidget(new QLabel(QStringLiteral("View Mode:"), this));
    m_viewToolbar->addWidget(m_viewModeCombo);

    QWidget* central = new QWidget(this);
    QVBoxLayout* rootLayout = new QVBoxLayout(central);

    QHBoxLayout* row1 = new QHBoxLayout();
    m_rootEdit = new QLineEdit(central);
    m_rootEdit->setPlaceholderText(QStringLiteral("Select root folder..."));
    m_rootEdit->setText(QDir::homePath());
    m_browseButton = new QPushButton(QStringLiteral("Browse"), central);
    m_rescanButton = new QPushButton(QStringLiteral("Rescan"), central);
    m_cancelButton = new QPushButton(QStringLiteral("Cancel"), central);
    m_pinCurrentButton = new QPushButton(QStringLiteral("Pin Current"), central);
    row1->addWidget(new QLabel(QStringLiteral("Root:"), central));
    row1->addWidget(m_rootEdit, 1);
    row1->addWidget(m_browseButton);
    row1->addWidget(m_rescanButton);
    row1->addWidget(m_cancelButton);
    row1->addWidget(m_pinCurrentButton);

    QHBoxLayout* row2 = new QHBoxLayout();
    m_showHiddenCheck = new QCheckBox(QStringLiteral("Show Hidden"), central);
    m_showSystemCheck = new QCheckBox(QStringLiteral("Show System"), central);
    m_extensionFilterEdit = new QLineEdit(central);
    m_extensionFilterEdit->setPlaceholderText(QStringLiteral(".png;.mp3;"));
    m_searchEdit = new QLineEdit(central);
    m_searchEdit->setPlaceholderText(QStringLiteral("Search substring..."));
    m_queryBarWidget = new QueryBarWidget(central);
    m_queryBarWidget->setObjectName(QStringLiteral("queryBarWidget"));
    row2->addWidget(m_showHiddenCheck);
    row2->addWidget(m_showSystemCheck);
    row2->addWidget(new QLabel(QStringLiteral("Ext Filter:"), central));
    row2->addWidget(m_extensionFilterEdit, 1);
    row2->addWidget(new QLabel(QStringLiteral("Search:"), central));
    row2->addWidget(m_searchEdit, 1);

    QSplitter* splitter = new QSplitter(Qt::Horizontal, central);
    m_sidebarTree = new QTreeWidget(splitter);
    m_sidebarTree->setHeaderHidden(true);
    m_sidebarTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_sidebarTree->setSelectionMode(QAbstractItemView::SingleSelection);

    m_treeView = new QTreeView(splitter);
    m_proxyModel.setSourceModel(&m_fileModel);
    m_proxyModel.setRecursiveFilteringEnabled(true);
    m_proxyModel.setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxyModel.setFilterKeyColumn(0);
    m_treeView->setModel(&m_proxyModel);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_treeView->setSortingEnabled(true);
    m_treeView->header()->setSectionResizeMode(QHeaderView::ResizeToContents);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 4);

    m_statusLabel = new QLabel(QStringLiteral("Idle"), central);

    rootLayout->addLayout(row1);
    rootLayout->addLayout(row2);
    rootLayout->addWidget(m_queryBarWidget);
    rootLayout->addWidget(splitter, 1);
    rootLayout->addWidget(m_statusLabel);
    setCentralWidget(central);
    setupStructuralPanel();

    connect(m_browseButton, &QPushButton::clicked, this, &MainWindow::onBrowseRoot);
    connect(m_rescanButton, &QPushButton::clicked, this, &MainWindow::onRescan);
    connect(m_cancelButton, &QPushButton::clicked, this, &MainWindow::onCancelScan);
    connect(m_pinCurrentButton, &QPushButton::clicked, this, &MainWindow::onPinCurrentFolder);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MainWindow::onSearchChanged);
    connect(m_queryBarWidget, &QueryBarWidget::querySubmitted, this, &MainWindow::onQuerySubmitted);
    connect(m_queryBarWidget, &QueryBarWidget::queryCleared, this, &MainWindow::onQueryCleared);
    connect(m_viewModeCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onViewModeChanged);
    connect(m_backButton, &QPushButton::clicked, this, &MainWindow::onNavigateBack);
    connect(m_forwardButton, &QPushButton::clicked, this, &MainWindow::onNavigateForward);
    connect(m_upButton, &QPushButton::clicked, this, &MainWindow::onNavigateUp);
    connect(m_treeView, &QWidget::customContextMenuRequested, this, &MainWindow::onTreeContextMenu);
    connect(m_treeView, &QTreeView::activated, this, &MainWindow::onTreeActivated);
    connect(m_sidebarTree, &QTreeWidget::itemActivated, this, &MainWindow::onSidebarItemActivated);
    connect(m_sidebarTree, &QWidget::customContextMenuRequested, this, &MainWindow::onSidebarContextMenu);

    auto* focusQueryShortcutL = new QShortcut(QKeySequence(QStringLiteral("Ctrl+L")), this);
    connect(focusQueryShortcutL, &QShortcut::activated, this, &MainWindow::onFocusQueryBar);

    auto* focusQueryShortcutF = new QShortcut(QKeySequence(QStringLiteral("Ctrl+F")), this);
    connect(focusQueryShortcutF, &QShortcut::activated, this, &MainWindow::onFocusQueryBar);

    loadFavorites();
    rebuildSidebar();
    updateNavigationButtons();

    appendRuntimeLog(QStringLiteral("MainWindow setup complete. sidebar_created=true favorites_config=%1 root=%2 startup_autorescan=false")
                         .arg(favoritesConfigPath(), m_rootEdit->text()));
}

void MainWindow::setupStructuralPanel()
{
    m_structuralPanelDock = new QDockWidget(QStringLiteral("Structural Panel"), this);
    m_structuralPanelDock->setObjectName(QStringLiteral("structuralPanelDock"));
    m_structuralPanelDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::RightDockWidgetArea);

    QWidget* panelRoot = new QWidget(m_structuralPanelDock);
    QVBoxLayout* panelLayout = new QVBoxLayout(panelRoot);
    panelLayout->setContentsMargins(8, 8, 8, 8);

    m_structuralContextLabel = new QLabel(QStringLiteral("Root: (none) | Target: (none)"), panelRoot);
    m_structuralContextLabel->setObjectName(QStringLiteral("structuralContextLabel"));
    panelLayout->addWidget(m_structuralContextLabel);

    QHBoxLayout* structuralNavigationLayout = new QHBoxLayout();
    m_structuralBackButton = new QPushButton(QStringLiteral("Back"), panelRoot);
    m_structuralForwardButton = new QPushButton(QStringLiteral("Forward"), panelRoot);
    m_structuralRefreshButton = new QPushButton(QStringLiteral("Refresh"), panelRoot);
    structuralNavigationLayout->addWidget(m_structuralBackButton);
    structuralNavigationLayout->addWidget(m_structuralForwardButton);
    structuralNavigationLayout->addWidget(m_structuralRefreshButton);
    structuralNavigationLayout->addStretch(1);
    panelLayout->addLayout(structuralNavigationLayout);

    QHBoxLayout* viewModeLayout = new QHBoxLayout();
    QLabel* viewModeLabel = new QLabel(QStringLiteral("View Mode:"), panelRoot);
    m_structuralTableViewButton = new QPushButton(QStringLiteral("Table"), panelRoot);
    m_structuralGraphViewButton = new QPushButton(QStringLiteral("Graph"), panelRoot);
    m_structuralTimelineViewButton = new QPushButton(QStringLiteral("Timeline"), panelRoot);
    m_structuralGraphModeCombo = new QComboBox(panelRoot);
    m_structuralTableViewButton->setCheckable(true);
    m_structuralGraphViewButton->setCheckable(true);
    m_structuralTimelineViewButton->setCheckable(true);
    m_structuralGraphModeCombo->addItem(QStringLiteral("Dependency Graph"), static_cast<int>(StructuralGraphMode::Dependency));
    m_structuralGraphModeCombo->addItem(QStringLiteral("Reverse Dependency Graph"), static_cast<int>(StructuralGraphMode::ReverseDependency));
    viewModeLayout->addWidget(viewModeLabel);
    viewModeLayout->addWidget(m_structuralTableViewButton);
    viewModeLayout->addWidget(m_structuralGraphViewButton);
    viewModeLayout->addWidget(m_structuralTimelineViewButton);
    viewModeLayout->addWidget(m_structuralGraphModeCombo);
    viewModeLayout->addStretch(1);
    panelLayout->addLayout(viewModeLayout);

    QHBoxLayout* filterLayout = new QHBoxLayout();
    m_structuralCategoryFilterCombo = new QComboBox(panelRoot);
    m_structuralStatusFilterCombo = new QComboBox(panelRoot);
    m_structuralExtensionFilterCombo = new QComboBox(panelRoot);
    m_structuralRelationshipFilterCombo = new QComboBox(panelRoot);
    m_structuralTextFilterEdit = new QLineEdit(panelRoot);
    m_structuralClearFiltersButton = new QPushButton(QStringLiteral("Clear Filters"), panelRoot);
    m_structuralSortFieldCombo = new QComboBox(panelRoot);
    m_structuralSortDirectionButton = new QPushButton(QStringLiteral("Sort: Asc"), panelRoot);

    m_structuralCategoryFilterCombo->addItem(QStringLiteral("Category: All"), QString());
    m_structuralCategoryFilterCombo->addItem(QStringLiteral("history"), QStringLiteral("history"));
    m_structuralCategoryFilterCombo->addItem(QStringLiteral("snapshot"), QStringLiteral("snapshot"));
    m_structuralCategoryFilterCombo->addItem(QStringLiteral("diff"), QStringLiteral("diff"));
    m_structuralCategoryFilterCombo->addItem(QStringLiteral("reference"), QStringLiteral("reference"));

    m_structuralStatusFilterCombo->addItem(QStringLiteral("Status: All"), QString());
    m_structuralStatusFilterCombo->addItem(QStringLiteral("added"), QStringLiteral("added"));
    m_structuralStatusFilterCombo->addItem(QStringLiteral("changed"), QStringLiteral("changed"));
    m_structuralStatusFilterCombo->addItem(QStringLiteral("removed"), QStringLiteral("removed"));
    m_structuralStatusFilterCombo->addItem(QStringLiteral("unchanged"), QStringLiteral("unchanged"));
    m_structuralStatusFilterCombo->addItem(QStringLiteral("absent"), QStringLiteral("absent"));

    m_structuralExtensionFilterCombo->addItem(QStringLiteral("Ext: All"), QString());
    m_structuralRelationshipFilterCombo->addItem(QStringLiteral("Rel: All"), QString());
    m_structuralRelationshipFilterCombo->addItem(QStringLiteral("include_ref"), QStringLiteral("include_ref"));
    m_structuralRelationshipFilterCombo->addItem(QStringLiteral("require_ref"), QStringLiteral("require_ref"));
    m_structuralRelationshipFilterCombo->addItem(QStringLiteral("import_ref"), QStringLiteral("import_ref"));
    m_structuralRelationshipFilterCombo->addItem(QStringLiteral("path_ref"), QStringLiteral("path_ref"));

    m_structuralTextFilterEdit->setPlaceholderText(QStringLiteral("Path / note text filter"));
    m_structuralSortFieldCombo->addItem(QStringLiteral("Sort: Path"), static_cast<int>(StructuralSortField::PrimaryPath));
    m_structuralSortFieldCombo->addItem(QStringLiteral("Sort: Status"), static_cast<int>(StructuralSortField::Status));
    m_structuralSortFieldCombo->addItem(QStringLiteral("Sort: Time"), static_cast<int>(StructuralSortField::Timestamp));
    m_structuralSortFieldCombo->addItem(QStringLiteral("Sort: Snapshot"), static_cast<int>(StructuralSortField::SnapshotId));
    m_structuralSortFieldCombo->addItem(QStringLiteral("Sort: Relationship"), static_cast<int>(StructuralSortField::Relationship));
    m_structuralSortFieldCombo->addItem(QStringLiteral("Sort: Size"), static_cast<int>(StructuralSortField::SizeBytes));
    m_structuralSortFieldCombo->addItem(QStringLiteral("Sort: Symbol"), static_cast<int>(StructuralSortField::Symbol));
    m_structuralSortFieldCombo->addItem(QStringLiteral("Sort: Rank"), static_cast<int>(StructuralSortField::RankScore));

    filterLayout->addWidget(m_structuralCategoryFilterCombo);
    filterLayout->addWidget(m_structuralStatusFilterCombo);
    filterLayout->addWidget(m_structuralExtensionFilterCombo);
    filterLayout->addWidget(m_structuralRelationshipFilterCombo);
    filterLayout->addWidget(m_structuralTextFilterEdit, 1);
    filterLayout->addWidget(m_structuralSortFieldCombo);
    filterLayout->addWidget(m_structuralSortDirectionButton);
    filterLayout->addWidget(m_structuralClearFiltersButton);
    panelLayout->addLayout(filterLayout);

    m_structuralTabWidget = new QTabWidget(panelRoot);
    m_structuralTabWidget->setObjectName(QStringLiteral("structuralTabWidget"));

    QWidget* historyTab = new QWidget(m_structuralTabWidget);
    QVBoxLayout* historyLayout = new QVBoxLayout(historyTab);
    m_structuralHistoryLoadButton = new QPushButton(QStringLiteral("Load Path History"), historyTab);
    m_structuralHistoryStatusLabel = new QLabel(QStringLiteral("History tab idle"), historyTab);
    historyLayout->addWidget(m_structuralHistoryLoadButton);
    historyLayout->addWidget(m_structuralHistoryStatusLabel);
    historyLayout->addStretch(1);
    m_structuralTabWidget->addTab(historyTab, QStringLiteral("History"));

    QWidget* snapshotsTab = new QWidget(m_structuralTabWidget);
    QVBoxLayout* snapshotLayout = new QVBoxLayout(snapshotsTab);
    m_structuralSnapshotLoadButton = new QPushButton(QStringLiteral("Load Snapshot List"), snapshotsTab);
    m_structuralSnapshotStatusLabel = new QLabel(QStringLiteral("Snapshots tab idle"), snapshotsTab);
    snapshotLayout->addWidget(m_structuralSnapshotLoadButton);
    snapshotLayout->addWidget(m_structuralSnapshotStatusLabel);
    snapshotLayout->addStretch(1);
    m_structuralTabWidget->addTab(snapshotsTab, QStringLiteral("Snapshots"));

    QWidget* diffTab = new QWidget(m_structuralTabWidget);
    QVBoxLayout* diffLayout = new QVBoxLayout(diffTab);
    QFormLayout* selectorLayout = new QFormLayout();
    m_structuralOldSnapshotCombo = new QComboBox(diffTab);
    m_structuralNewSnapshotCombo = new QComboBox(diffTab);
    selectorLayout->addRow(QStringLiteral("Old Snapshot"), m_structuralOldSnapshotCombo);
    selectorLayout->addRow(QStringLiteral("New Snapshot"), m_structuralNewSnapshotCombo);
    m_structuralDiffCompareButton = new QPushButton(QStringLiteral("Compare Selected Snapshots"), diffTab);
    m_structuralDiffStatusLabel = new QLabel(QStringLiteral("Diff tab idle"), diffTab);
    diffLayout->addLayout(selectorLayout);
    diffLayout->addWidget(m_structuralDiffCompareButton);
    diffLayout->addWidget(m_structuralDiffStatusLabel);
    diffLayout->addStretch(1);
    m_structuralTabWidget->addTab(diffTab, QStringLiteral("Diff"));

    QWidget* referenceTab = new QWidget(m_structuralTabWidget);
    QVBoxLayout* referenceLayout = new QVBoxLayout(referenceTab);
    m_structuralShowReferencesButton = new QPushButton(QStringLiteral("Show References"), referenceTab);
    m_structuralShowUsedByButton = new QPushButton(QStringLiteral("Show Used By"), referenceTab);
    m_structuralReferenceStatusLabel = new QLabel(QStringLiteral("References tab idle"), referenceTab);
    referenceLayout->addWidget(m_structuralShowReferencesButton);
    referenceLayout->addWidget(m_structuralShowUsedByButton);
    referenceLayout->addWidget(m_structuralReferenceStatusLabel);
    referenceLayout->addStretch(1);
    m_structuralTabWidget->addTab(referenceTab, QStringLiteral("References"));

    panelLayout->addWidget(m_structuralTabWidget);

    m_structuralGraphStatusLabel = new QLabel(QStringLiteral("Graph view idle"), panelRoot);
    panelLayout->addWidget(m_structuralGraphStatusLabel);

    m_structuralGraphWidget = new StructuralGraphWidget(panelRoot);
    m_structuralGraphWidget->setObjectName(QStringLiteral("structuralGraphWidget"));
    panelLayout->addWidget(m_structuralGraphWidget, 1);

    m_structuralTimelineStatusLabel = new QLabel(QStringLiteral("Timeline view idle"), panelRoot);
    panelLayout->addWidget(m_structuralTimelineStatusLabel);

    m_structuralTimelineWidget = new StructuralTimelineWidget(panelRoot);
    m_structuralTimelineWidget->setObjectName(QStringLiteral("structuralTimelineWidget"));
    panelLayout->addWidget(m_structuralTimelineWidget, 1);

    m_structuralPanelDock->setWidget(panelRoot);
    addDockWidget(Qt::BottomDockWidgetArea, m_structuralPanelDock);
    m_structuralPanelDock->hide();

    connect(m_structuralHistoryLoadButton, &QPushButton::clicked, this, &MainWindow::onActionShowHistory);
    connect(m_structuralSnapshotLoadButton, &QPushButton::clicked, this, &MainWindow::onActionSnapshots);
    connect(m_structuralDiffCompareButton, &QPushButton::clicked, this, &MainWindow::onStructuralCompareSnapshots);
    connect(m_structuralShowReferencesButton, &QPushButton::clicked, this, &MainWindow::onStructuralShowReferences);
    connect(m_structuralShowUsedByButton, &QPushButton::clicked, this, &MainWindow::onStructuralShowUsedBy);
    connect(m_structuralTabWidget, &QTabWidget::currentChanged, this, &MainWindow::onStructuralPanelTabChanged);
    connect(m_structuralTableViewButton, &QPushButton::clicked, this, [this]() {
        setStructuralViewMode(0);
    });
    connect(m_structuralGraphViewButton, &QPushButton::clicked, this, [this]() {
        setStructuralViewMode(1);
    });
    connect(m_structuralTimelineViewButton, &QPushButton::clicked, this, [this]() {
        setStructuralViewMode(2);
    });
    connect(m_structuralGraphModeCombo,
            &QComboBox::currentIndexChanged,
            this,
            [this](int) {
                if (!m_structuralGraphModeCombo || m_structuralGraphModeCombo->currentIndex() < 0) {
                    return;
                }
                bool ok = false;
                const int token = m_structuralGraphModeCombo->currentData().toInt(&ok);
                if (ok) {
                    m_structuralGraphMode = static_cast<StructuralGraphMode>(token);
                }
                updateStructuralGraphFromCanonicalRows();
            });
    connect(m_structuralGraphWidget, &StructuralGraphWidget::nodeActivated, this, &MainWindow::onStructuralGraphNodeActivated);
    connect(m_structuralTimelineWidget, &StructuralTimelineWidget::eventActivated, this, &MainWindow::onStructuralTimelineEventActivated);
    connect(m_structuralBackButton, &QPushButton::clicked, this, [this]() {
        QString errorText;
        if (!navigateStructuralBack(&errorText, nullptr, nullptr) && m_statusLabel && !errorText.isEmpty()) {
            m_statusLabel->setText(QStringLiteral("Structural back failed: %1").arg(errorText));
        }
    });
    connect(m_structuralForwardButton, &QPushButton::clicked, this, [this]() {
        QString errorText;
        if (!navigateStructuralForward(&errorText, nullptr, nullptr) && m_statusLabel && !errorText.isEmpty()) {
            m_statusLabel->setText(QStringLiteral("Structural forward failed: %1").arg(errorText));
        }
    });
    connect(m_structuralRefreshButton, &QPushButton::clicked, this, [this]() {
        QString errorText;
        if (!refreshStructuralCurrentQuery(&errorText, nullptr, nullptr) && m_statusLabel && !errorText.isEmpty()) {
            m_statusLabel->setText(QStringLiteral("Structural refresh failed: %1").arg(errorText));
        }
    });

    auto applyFilters = [this]() {
        updateStructuralFilterStateFromControls();
        updateStructuralSortStateFromControls();
        applyStructuralFiltersToCurrentRows(m_structuralStatusPrefix);
    };
    connect(m_structuralCategoryFilterCombo, &QComboBox::currentIndexChanged, this, [applyFilters](int) { applyFilters(); });
    connect(m_structuralStatusFilterCombo, &QComboBox::currentIndexChanged, this, [applyFilters](int) { applyFilters(); });
    connect(m_structuralExtensionFilterCombo, &QComboBox::currentIndexChanged, this, [applyFilters](int) { applyFilters(); });
    connect(m_structuralRelationshipFilterCombo, &QComboBox::currentIndexChanged, this, [applyFilters](int) { applyFilters(); });
    connect(m_structuralTextFilterEdit, &QLineEdit::textChanged, this, [applyFilters](const QString&) { applyFilters(); });
    connect(m_structuralSortFieldCombo, &QComboBox::currentIndexChanged, this, [applyFilters](int) { applyFilters(); });
    connect(m_structuralSortDirectionButton, &QPushButton::clicked, this, [this, applyFilters]() {
        m_structuralSortDirection = (m_structuralSortDirection == StructuralSortDirection::Ascending)
            ? StructuralSortDirection::Descending
            : StructuralSortDirection::Ascending;
        if (m_structuralSortDirectionButton) {
            m_structuralSortDirectionButton->setText(m_structuralSortDirection == StructuralSortDirection::Ascending
                                                         ? QStringLiteral("Sort: Asc")
                                                         : QStringLiteral("Sort: Desc"));
        }
        applyFilters();
    });
    connect(m_structuralClearFiltersButton, &QPushButton::clicked, this, [this]() {
        clearStructuralFilters(true);
    });

    clearStructuralFilters(false);
    setStructuralViewMode(0);
    updateStructuralGraphFromCanonicalRows();
    updateStructuralTimelineFromCanonicalRows();
    refreshQueryAutocompleteContext();
    updateStructuralNavigationButtons();
}

void MainWindow::setupActionRegistry()
{
    m_actionTreeSnapshot = new QAction(QStringLiteral("Tree Snapshot..."), this);
    m_actionTreeSnapshot->setObjectName(QStringLiteral("actionTreeSnapshot"));
    m_actionTreeSnapshot->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+T")));
    connect(m_actionTreeSnapshot, &QAction::triggered, this, &MainWindow::onActionTreeSnapshot);

    m_actionCompressZip = new QAction(QStringLiteral("Compress to Zip"), this);
    m_actionCompressZip->setObjectName(QStringLiteral("actionCompressZip"));
    m_actionCompressZip->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Z")));
    connect(m_actionCompressZip, &QAction::triggered, this, &MainWindow::onActionCompressZip);

    m_actionCompress7z = new QAction(QStringLiteral("Compress to 7z"), this);
    m_actionCompress7z->setObjectName(QStringLiteral("actionCompress7z"));
    connect(m_actionCompress7z, &QAction::triggered, this, &MainWindow::onActionCompress7z);

    m_actionCompressTar = new QAction(QStringLiteral("Compress to Tar"), this);
    m_actionCompressTar->setObjectName(QStringLiteral("actionCompressTar"));
    connect(m_actionCompressTar, &QAction::triggered, this, &MainWindow::onActionCompressTar);

    m_actionExtractHere = new QAction(QStringLiteral("Extract Here"), this);
    m_actionExtractHere->setObjectName(QStringLiteral("actionExtractHere"));
    connect(m_actionExtractHere, &QAction::triggered, this, &MainWindow::onActionExtractHere);

    m_actionExtractTo = new QAction(QStringLiteral("Extract To..."), this);
    m_actionExtractTo->setObjectName(QStringLiteral("actionExtractTo"));
    connect(m_actionExtractTo, &QAction::triggered, this, &MainWindow::onActionExtractTo);

    m_actionExploreArchive = new QAction(QStringLiteral("Explore Archive..."), this);
    m_actionExploreArchive->setObjectName(QStringLiteral("actionExploreArchive"));
    connect(m_actionExploreArchive, &QAction::triggered, this, &MainWindow::onActionExploreArchive);

    m_actionOpenStructuralPanel = new QAction(QStringLiteral("Open Structural Panel"), this);
    m_actionOpenStructuralPanel->setObjectName(QStringLiteral("actionOpenStructuralPanel"));
    connect(m_actionOpenStructuralPanel, &QAction::triggered, this, &MainWindow::onActionOpenStructuralPanel);

    m_actionShowHistory = new QAction(QStringLiteral("Show History"), this);
    m_actionShowHistory->setObjectName(QStringLiteral("actionShowHistory"));
    connect(m_actionShowHistory, &QAction::triggered, this, &MainWindow::onActionShowHistory);

    m_actionSnapshots = new QAction(QStringLiteral("Snapshots"), this);
    m_actionSnapshots->setObjectName(QStringLiteral("actionSnapshots"));
    connect(m_actionSnapshots, &QAction::triggered, this, &MainWindow::onActionSnapshots);

    m_actionCompareSnapshots = new QAction(QStringLiteral("Compare Snapshots"), this);
    m_actionCompareSnapshots->setObjectName(QStringLiteral("actionCompareSnapshots"));
    connect(m_actionCompareSnapshots, &QAction::triggered, this, &MainWindow::onActionCompareSnapshots);

    m_actionCopyPath = new QAction(QStringLiteral("Copy Path"), this);
    m_actionCopyPath->setObjectName(QStringLiteral("actionCopyPath"));
    connect(m_actionCopyPath, &QAction::triggered, this, &MainWindow::onActionCopyPath);

    m_actionRename = new QAction(QStringLiteral("Rename"), this);
    m_actionRename->setObjectName(QStringLiteral("actionRename"));
    connect(m_actionRename, &QAction::triggered, this, &MainWindow::onActionRename);

    m_actionPinFavorite = new QAction(QStringLiteral("Pin to Favorites"), this);
    m_actionPinFavorite->setObjectName(QStringLiteral("actionPinFavorite"));
    connect(m_actionPinFavorite, &QAction::triggered, this, &MainWindow::onActionPinFavorite);

    m_actionUnpinFavorite = new QAction(QStringLiteral("Unpin from Favorites"), this);
    m_actionUnpinFavorite->setObjectName(QStringLiteral("actionUnpinFavorite"));
    connect(m_actionUnpinFavorite, &QAction::triggered, this, &MainWindow::onActionUnpinFavorite);
}

void MainWindow::setupTestSurface()
{
#if !defined(NDEBUG)
    const bool enableSurface = true;
#else
    const bool enableSurface = m_testMode;
#endif
    if (!enableSurface) {
        return;
    }

    QMenu* menu = menuBar()->addMenu(QStringLiteral("Test Actions"));
    menu->setObjectName(QStringLiteral("testActionsMenu"));
    menu->addAction(m_actionTreeSnapshot);
    menu->addAction(m_actionCompressZip);
    menu->addAction(m_actionCompress7z);
    menu->addAction(m_actionCompressTar);
    menu->addSeparator();
    menu->addAction(m_actionExtractHere);
    menu->addAction(m_actionExtractTo);
    menu->addAction(m_actionExploreArchive);
    menu->addAction(m_actionOpenStructuralPanel);
    menu->addAction(m_actionShowHistory);
    menu->addAction(m_actionSnapshots);
    menu->addAction(m_actionCompareSnapshots);
    menu->addSeparator();
    menu->addAction(m_actionCopyPath);
    menu->addAction(m_actionRename);
    menu->addAction(m_actionPinFavorite);
    menu->addAction(m_actionUnpinFavorite);
}

void MainWindow::configureObjectNames()
{
    setObjectName(QStringLiteral("mainWindow"));
    if (m_rootEdit) {
        m_rootEdit->setObjectName(QStringLiteral("rootPathEdit"));
    }
    if (m_browseButton) {
        m_browseButton->setObjectName(QStringLiteral("browseButton"));
    }
    if (m_rescanButton) {
        m_rescanButton->setObjectName(QStringLiteral("rescanButton"));
    }
    if (m_cancelButton) {
        m_cancelButton->setObjectName(QStringLiteral("cancelButton"));
    }
    if (m_viewModeCombo) {
        m_viewModeCombo->setObjectName(QStringLiteral("viewModeCombo"));
    }
    if (m_sidebarTree) {
        m_sidebarTree->setObjectName(QStringLiteral("sidebarTree"));
    }
    if (m_treeView) {
        m_treeView->setObjectName(QStringLiteral("fileTree"));
    }
    if (m_statusLabel) {
        m_statusLabel->setObjectName(QStringLiteral("statusLabel"));
    }
}

void MainWindow::ensureUiActionTracePath()
{
    if (!m_actionTracePath.trimmed().isEmpty()) {
        QFileInfo traceInfo(m_actionTracePath);
        if (!traceInfo.dir().exists()) {
            QDir().mkpath(traceInfo.dir().absolutePath());
        }
        return;
    }

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString proofDir = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../_proof/filevisionary_ui_testability_%1").arg(stamp));
    QDir().mkpath(proofDir);
    m_actionTracePath = QDir(proofDir).filePath(QStringLiteral("08_ui_action_trace.log"));
}

void MainWindow::setActionContext(const QStringList& paths, const QString& selectionType)
{
    m_actionContextPaths = paths;
    m_actionContextType = selectionType;
}

QStringList MainWindow::actionPathsForCurrentContext() const
{
    if (!m_actionContextPaths.isEmpty()) {
        return m_actionContextPaths;
    }

    const QStringList selected = selectedPaths();
    if (!selected.isEmpty()) {
        return selected;
    }

    const QString root = currentRootPath();
    if (!root.isEmpty()) {
        return {root};
    }
    return {};
}

QString MainWindow::primaryActionPathForCurrentContext() const
{
    const QStringList paths = actionPathsForCurrentContext();
    return paths.isEmpty() ? QString() : paths.first();
}

void MainWindow::logUiAction(const QString& actionName,
                             const QString& handler,
                             const QString& result,
                             const QString& outputPath,
                             const QString& error) const
{
    if (m_actionTracePath.isEmpty()) {
        return;
    }

    QFile file(m_actionTracePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
        return;
    }

    const QStringList paths = m_actionContextPaths;
    QJsonObject record;
    record.insert(QStringLiteral("timestamp"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    record.insert(QStringLiteral("action"), actionName);
    record.insert(QStringLiteral("selection_type"), m_actionContextType);
    record.insert(QStringLiteral("handler"), handler);
    record.insert(QStringLiteral("result"), result);
    record.insert(QStringLiteral("output"), outputPath);
    record.insert(QStringLiteral("error"), error);

    QJsonArray arr;
    for (const QString& p : paths) {
        arr.push_back(p);
    }
    record.insert(QStringLiteral("paths"), arr);

    QTextStream out(&file);
    out << QString::fromUtf8(QJsonDocument(record).toJson(QJsonDocument::Compact)) << '\n';
}

void MainWindow::maybeOpenStartupRoot()
{
    if (m_startupRoot.trimmed().isEmpty()) {
        return;
    }
    m_rootEdit->setText(m_startupRoot);
    onRescan();
}

void MainWindow::triggerNamedAction(const QString& actionName, const QStringList& paths, const QString& selectionType)
{
    setActionContext(paths, selectionType);
    if (actionName == QStringLiteral("TreeSnapshot") && m_actionTreeSnapshot) {
        m_actionTreeSnapshot->trigger();
    } else if (actionName == QStringLiteral("CompressZip") && m_actionCompressZip) {
        m_actionCompressZip->trigger();
    } else if (actionName == QStringLiteral("Compress7z") && m_actionCompress7z) {
        m_actionCompress7z->trigger();
    } else if (actionName == QStringLiteral("CompressTar") && m_actionCompressTar) {
        m_actionCompressTar->trigger();
    } else if (actionName == QStringLiteral("ExtractHere") && m_actionExtractHere) {
        m_actionExtractHere->trigger();
    } else if (actionName == QStringLiteral("ExtractTo") && m_actionExtractTo) {
        m_actionExtractTo->trigger();
    } else if (actionName == QStringLiteral("ExploreArchive") && m_actionExploreArchive) {
        m_actionExploreArchive->trigger();
    } else if (actionName == QStringLiteral("OpenStructuralPanel") && m_actionOpenStructuralPanel) {
        m_actionOpenStructuralPanel->trigger();
    } else if (actionName == QStringLiteral("ShowHistory") && m_actionShowHistory) {
        m_actionShowHistory->trigger();
    } else if (actionName == QStringLiteral("Snapshots") && m_actionSnapshots) {
        m_actionSnapshots->trigger();
    } else if (actionName == QStringLiteral("CompareSnapshots") && m_actionCompareSnapshots) {
        m_actionCompareSnapshots->trigger();
    } else if (actionName == QStringLiteral("CopyPath") && m_actionCopyPath) {
        m_actionCopyPath->trigger();
    } else if (actionName == QStringLiteral("Rename") && m_actionRename) {
        m_actionRename->trigger();
    } else if (actionName == QStringLiteral("PinFavorite") && m_actionPinFavorite) {
        m_actionPinFavorite->trigger();
    } else if (actionName == QStringLiteral("UnpinFavorite") && m_actionUnpinFavorite) {
        m_actionUnpinFavorite->trigger();
    } else {
        logUiAction(actionName, QStringLiteral("triggerNamedAction"), QStringLiteral("error"), QString(), QStringLiteral("unknown_action"));
    }
}

void MainWindow::onRunTestScript()
{
    if (m_testScriptPath.trimmed().isEmpty()) {
        return;
    }

    QFile file(m_testScriptPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        logUiAction(QStringLiteral("TestScript"), QStringLiteral("onRunTestScript"), QStringLiteral("error"), QString(), QStringLiteral("script_open_failed"));
        return;
    }

    m_testScriptRunning = true;
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }

        const QStringList parts = line.split('|');
        const QString actionName = parts.value(0).trimmed();
        QStringList paths;
        if (parts.size() > 1 && !parts[1].trimmed().isEmpty()) {
            paths = parts[1].split(';', Qt::SkipEmptyParts);
            for (QString& p : paths) {
                p = p.trimmed();
            }
        }
        const QString selectionType = (parts.size() > 2) ? parts[2].trimmed() : QStringLiteral("script");
        triggerNamedAction(actionName, paths, selectionType);
        if (m_archiveProcess && m_archiveProcess->state() != QProcess::NotRunning) {
            QElapsedTimer waitTimer;
            waitTimer.start();
            while (m_archiveProcess->state() != QProcess::NotRunning && waitTimer.elapsed() < 20000) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                QThread::msleep(20);
            }
        }
        if (m_snapshotWatcher && !m_snapshotWatcher->isFinished()) {
            QElapsedTimer snapshotWait;
            snapshotWait.start();
            while (!m_snapshotWatcher->isFinished() && snapshotWait.elapsed() < 20000) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                QThread::msleep(20);
            }
        }
        QCoreApplication::processEvents();
    }
    m_testScriptRunning = false;
}

void MainWindow::setupScanner()
{
    m_fileScanner = nullptr;
}

void MainWindow::onBrowseRoot()
{
    const QString selected = QFileDialog::getExistingDirectory(this, QStringLiteral("Select Root"), m_rootEdit->text());
    if (!selected.isEmpty()) {
        navigateToDirectory(selected);
    }
}

void MainWindow::onRescan()
{
    if (m_scanInProgress) {
        m_rescanPending = true;
        appendRuntimeLog(QStringLiteral("rescan_requested_while_busy queued=true"));
        m_statusLabel->setText(QStringLiteral("Canceling previous scan..."));
        return;
    }

    startScanNow();
}

void MainWindow::onRefreshPollTick()
{
    if (!m_directoryModel || !m_directoryModel->isReady()) {
        return;
    }

    const QVector<RefreshEvent> events = m_directoryModel->takeRefreshEvents();
    if (events.isEmpty()) {
        return;
    }

    const QString visibleRoot = currentRootPath();
    bool shouldRequery = false;

    for (const RefreshEvent& event : events) {
        appendRuntimeLog(QStringLiteral("refresh_event request_id=%1 state=%2 path=%3 mode=%4 reason=%5 session=%6 inserted=%7 updated=%8 error=%9")
                             .arg(event.requestId)
                             .arg(RefreshTypes::stateToString(event.state))
                             .arg(event.path)
                             .arg(event.mode)
                             .arg(event.reason)
                             .arg(event.sessionId)
                             .arg(event.totalInserted)
                             .arg(event.totalUpdated)
                             .arg(event.errorText));

        if (event.state == RefreshState::Completed
            && !visibleRoot.isEmpty()
            && (QString::compare(QDir::cleanPath(event.path), QDir::cleanPath(visibleRoot), Qt::CaseInsensitive) == 0
                || QDir::cleanPath(visibleRoot).startsWith(QDir::cleanPath(event.path), Qt::CaseInsensitive))) {
            shouldRequery = true;
        }
    }

    if (shouldRequery && !m_scanInProgress) {
        const QDateTime now = QDateTime::currentDateTimeUtc();
        if (!m_lastRefreshRequeryAt.isValid() || m_lastRefreshRequeryAt.msecsTo(now) > 300) {
            m_lastRefreshRequeryAt = now;
            appendRuntimeLog(QStringLiteral("refresh_triggered_requery root=%1").arg(visibleRoot));
            QMetaObject::invokeMethod(this, "onRescan", Qt::QueuedConnection);
        }
    }
}

void MainWindow::onCancelScan()
{
    m_rescanPending = false;
    m_publishQueue.clear();
    m_publishTimer.stop();
    m_scanInProgress = false;
    m_statusLabel->setText(QStringLiteral("Canceled"));
    appendRuntimeLog(QStringLiteral("cancel_requested"));
}

void MainWindow::onBatchReady(quint64 scanId, const QVector<FileEntry>& entries)
{
    if (scanId != m_activeScanId) {
        appendRuntimeLog(QStringLiteral("stale_batch_ignored scan_id=%1 active=%2")
                             .arg(scanId)
                             .arg(m_activeScanId));
        return;
    }

    m_publishQueue += entries;
    m_scanBatchCount += 1;
    m_scanEntryCount += static_cast<quint64>(entries.size());
    if (!m_publishTimer.isActive()) {
        m_publishTimer.start();
    }
    if ((m_scanBatchCount % 50ULL) == 0ULL) {
        appendRuntimeLog(QStringLiteral("batch_progress batches=%1 entries=%2")
                             .arg(m_scanBatchCount)
                             .arg(m_scanEntryCount));
    }
}

void MainWindow::onScanProgress(quint64 scanId, const QString& stage, quint64 enumerated, quint64 matched)
{
    if (scanId != m_activeScanId) {
        return;
    }

    m_scanEnumeratedCount = enumerated;
    m_statusLabel->setText(QStringLiteral("%1... enumerated=%2 matched=%3 shown=%4")
                               .arg(stage)
                               .arg(enumerated)
                               .arg(matched)
                               .arg(m_fileModel.itemCount()));

    if ((enumerated % 500ULL) == 0ULL || stage == QStringLiteral("Publishing")) {
        appendRuntimeLog(QStringLiteral("scan_progress stage=%1 enumerated=%2 matched=%3 shown=%4")
                             .arg(stage)
                             .arg(enumerated)
                             .arg(matched)
                             .arg(m_fileModel.itemCount()));
    }
}

void MainWindow::onScanFinished(quint64 scanId,
                                bool canceled,
                                quint64 enumerated,
                                quint64 matched,
                                const QString& error)
{
    if (scanId != m_activeScanId) {
        appendRuntimeLog(QStringLiteral("stale_finished_ignored scan_id=%1 active=%2")
                             .arg(scanId)
                             .arg(m_activeScanId));
        return;
    }

    m_scanInProgress = false;
    if (!m_publishQueue.isEmpty() && !m_publishTimer.isActive()) {
        m_publishTimer.start();
    } else if (m_publishQueue.isEmpty()) {
        m_treeView->setSortingEnabled(true);
    }

    if (!error.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("Error: %1").arg(error));
    } else if (canceled) {
        m_statusLabel->setText(QStringLiteral("Canceled"));
    } else {
        m_statusLabel->setText(QStringLiteral("Complete: %1 items (enumerated=%2, matched=%3)")
                                   .arg(m_fileModel.itemCount())
                                   .arg(enumerated)
                                   .arg(matched));
        if (m_viewMode == FileViewMode::FullHierarchy) {
            m_treeView->expandAll();
        } else if (m_viewMode == FileViewMode::Standard) {
            m_treeView->collapseAll();
        }
    }

    appendRuntimeLog(QStringLiteral("scan_finished canceled=%1 error=%2 enumerated=%3 matched=%4 shown=%5 total_batches=%6 total_entries=%7")
                         .arg(canceled ? QStringLiteral("true") : QStringLiteral("false"))
                         .arg(error)
                         .arg(enumerated)
                         .arg(matched)
                         .arg(m_fileModel.itemCount())
                         .arg(m_scanBatchCount)
                         .arg(m_scanEntryCount));

    if (m_rescanPending) {
        m_rescanPending = false;
        appendRuntimeLog(QStringLiteral("rescan_restarting_after_cancel"));
        QMetaObject::invokeMethod(this, "onRescan", Qt::QueuedConnection);
    }
}

void MainWindow::onSearchChanged(const QString& text)
{
    appendRuntimeLog(QStringLiteral("search_changed text=%1").arg(text));
    onRescan();
}

void MainWindow::onQuerySubmitted(const QString& text)
{
    const QString query = text.trimmed();
    if (query.isEmpty()) {
        onQueryCleared();
        return;
    }

    QString structuralError;
    int structuralTab = -1;
    int structuralRows = 0;
    if (dispatchQueryToExistingPanel(query, &structuralError, &structuralTab, &structuralRows, true, false)) {
        m_queryModeActive = false;
        m_activeQueryString.clear();
        appendRuntimeLog(QStringLiteral("querybar_structural_dispatch_ok query=%1 tab=%2 rows=%3")
                             .arg(query)
                             .arg(structuralTab)
                             .arg(structuralRows));
        return;
    }
    if (!structuralError.isEmpty()) {
        m_queryModeActive = false;
        m_activeQueryString.clear();
        m_statusLabel->setText(QStringLiteral("Structural query error: %1").arg(structuralError));
        appendRuntimeLog(QStringLiteral("querybar_structural_dispatch_failed query=%1 error=%2")
                             .arg(query)
                             .arg(structuralError));
        return;
    }

    m_activeQueryString = query;
    m_queryModeActive = true;

    appendRuntimeLog(QStringLiteral("querybar_submit query=%1").arg(query));
    m_statusLabel->setText(QStringLiteral("Query: parsing and executing..."));
    onRescan();
}

void MainWindow::onQueryCleared()
{
    const bool wasActive = m_queryModeActive || !m_activeQueryString.isEmpty();
    m_queryModeActive = false;
    m_activeQueryString.clear();

    appendRuntimeLog(QStringLiteral("querybar_cleared restore_normal_view=true"));
    if (wasActive) {
        m_statusLabel->setText(QStringLiteral("Query cleared. Restoring directory view..."));
        onRescan();
    }
}

void MainWindow::onFocusQueryBar()
{
    if (m_queryBarWidget) {
        m_queryBarWidget->focusInput();
    }
}

void MainWindow::onTreeContextMenu(const QPoint& pos)
{
    const QModelIndex proxyIndex = m_treeView->indexAt(pos);
    if (!proxyIndex.isValid()) {
        setActionContext({currentRootPath()}, QStringLiteral("background"));
        QMenu backgroundMenu(this);
        QAction* refreshAction = backgroundMenu.addAction(QStringLiteral("Refresh"));
        QAction* rescanAction = backgroundMenu.addAction(QStringLiteral("Rescan"));
        QAction* pasteAction = backgroundMenu.addAction(QStringLiteral("Paste"));
        pasteAction->setEnabled(false);
        backgroundMenu.addSeparator();
        QAction* newFolderAction = backgroundMenu.addAction(QStringLiteral("New Folder"));
        QAction* newTextFileAction = backgroundMenu.addAction(QStringLiteral("New Text File"));
        backgroundMenu.addSeparator();
        backgroundMenu.addAction(m_actionOpenStructuralPanel);
        backgroundMenu.addAction(m_actionTreeSnapshot);
        backgroundMenu.addAction(m_actionSnapshots);
        backgroundMenu.addAction(m_actionCompareSnapshots);
        QAction* openStructuralPanelAction = m_actionOpenStructuralPanel;
        QAction* snapshotCurrentAction = m_actionTreeSnapshot;
        QAction* snapshotsAction = m_actionSnapshots;
        QAction* compareSnapshotsAction = m_actionCompareSnapshots;
        snapshotCurrentAction->setText(QStringLiteral("Tree Snapshot of Current Root..."));
        QAction* openTerminalAction = backgroundMenu.addAction(QStringLiteral("Open in Terminal"));
        QAction* openWithCodeAction = backgroundMenu.addAction(QStringLiteral("Open with Code"));
        QAction* propertiesAction = backgroundMenu.addAction(QStringLiteral("Properties for Current Folder"));

        QAction* chosen = backgroundMenu.exec(m_treeView->viewport()->mapToGlobal(pos));
        if (!chosen) {
            return;
        }
        if (chosen == refreshAction || chosen == rescanAction) {
            if (chosen == refreshAction && ensureDirectoryModelReady()) {
                const RefreshRequestResult rr = m_directoryModel->requestRefresh(currentRootPath(), true, QStringLiteral("visible_refresh"), QStringLiteral("ui_context_refresh"));
                appendRuntimeLog(QStringLiteral("ui_refresh_request accepted=%1 state=%2 path=%3 reason=%4 error=%5")
                                     .arg(rr.accepted ? QStringLiteral("true") : QStringLiteral("false"))
                                     .arg(RefreshTypes::stateToString(rr.state))
                                     .arg(rr.path)
                                     .arg(rr.reason)
                                     .arg(rr.errorText));
                m_statusLabel->setText(QStringLiteral("Refresh request: %1").arg(RefreshTypes::stateToString(rr.state)));
            }
            if (chosen == rescanAction) {
                onRescan();
            }
        } else if (chosen == pasteAction) {
            QMessageBox::information(this, QStringLiteral("Paste"), QStringLiteral("Paste integration is not available in this pass."));
        } else if (chosen == newFolderAction) {
            createNewFolderInCurrentRoot();
        } else if (chosen == newTextFileAction) {
            createNewTextFileInCurrentRoot();
        } else if (chosen == openStructuralPanelAction) {
            onActionOpenStructuralPanel();
        } else if (chosen == snapshotCurrentAction) {
            onActionTreeSnapshot();
        } else if (chosen == snapshotsAction) {
            onActionSnapshots();
        } else if (chosen == compareSnapshotsAction) {
            onActionCompareSnapshots();
        } else if (chosen == openTerminalAction) {
            openInTerminal(currentRootPath());
        } else if (chosen == openWithCodeAction) {
            openWithCode(currentRootPath());
        } else if (chosen == propertiesAction) {
            showPropertiesDialog({currentRootPath()});
        }
        return;
    }

    const QStringList paths = selectedPaths();
    if (paths.isEmpty()) {
        return;
    }

    const QString firstPath = paths.first();
    const QFileInfo firstInfo(firstPath);
    const bool multiple = (paths.size() > 1);
    const bool isFolder = isInternalNavigableDirectory(firstInfo);
    const bool isArchive = isArchivePath(firstPath);
    const QString selectionType = multiple ? QStringLiteral("multi")
                                           : (isArchive ? QStringLiteral("archive") : (isFolder ? QStringLiteral("folder") : QStringLiteral("file")));
    setActionContext(paths, selectionType);

    if (multiple) {
        QMenu multiMenu(this);
        QAction* openSelectedAction = multiMenu.addAction(QStringLiteral("Open Selected"));
        QAction* revealAction = multiMenu.addAction(QStringLiteral("Reveal in Explorer"));
        multiMenu.addAction(m_actionCopyPath);
        QAction* copyPathsAction = m_actionCopyPath;
        copyPathsAction->setText(QStringLiteral("Copy Paths"));
        multiMenu.addSeparator();
        QAction* createArchiveAction = multiMenu.addAction(QStringLiteral("Create Archive..."));
        multiMenu.addAction(m_actionCompressZip);
        multiMenu.addAction(m_actionCompress7z);
        multiMenu.addAction(m_actionCompressTar);
        QAction* zipAction = m_actionCompressZip;
        QAction* sevenZipAction = m_actionCompress7z;
        QAction* tarAction = m_actionCompressTar;
        multiMenu.addSeparator();
        QAction* moveAction = multiMenu.addAction(QStringLiteral("Move to..."));
        QAction* copyToAction = multiMenu.addAction(QStringLiteral("Copy to..."));
        QAction* deleteAction = multiMenu.addAction(QStringLiteral("Delete..."));
        QAction* propertiesAction = multiMenu.addAction(QStringLiteral("Properties Summary"));

        QAction* chosen = multiMenu.exec(m_treeView->viewport()->mapToGlobal(pos));
        if (!chosen) {
            return;
        }
        if (chosen == openSelectedAction) {
            for (const QString& path : paths) {
                const QFileInfo info(path);
                if (isInternalNavigableDirectory(info)) {
                    navigateToDirectory(path);
                    break;
                }
                if (isArchivePath(path)) {
                    auto* explorer = new ArchiveExplorer(this);
                    explorer->openArchive(path);
                    explorer->show();
                } else {
                    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
                }
            }
        } else if (chosen == revealAction) {
            QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,"), QDir::toNativeSeparators(firstPath)});
        } else if (chosen == copyPathsAction) {
            onActionCopyPath();
        } else if (chosen == createArchiveAction) {
            createArchiveFromPaths(paths);
        } else if (chosen == zipAction) {
            onActionCompressZip();
        } else if (chosen == sevenZipAction) {
            onActionCompress7z();
        } else if (chosen == tarAction) {
            onActionCompressTar();
        } else if (chosen == moveAction) {
            copyMovePaths(paths, true);
        } else if (chosen == copyToAction) {
            copyMovePaths(paths, false);
        } else if (chosen == deleteAction) {
            deletePathsWithConfirm(paths);
        } else if (chosen == propertiesAction) {
            showPropertiesDialog(paths);
        }
        return;
    }

    QMenu menu(this);
    QAction* openAction = menu.addAction(QStringLiteral("Open"));
    QAction* openInNewTabAction = nullptr;
    QAction* openInNewWindowAction = nullptr;
    QAction* openWithAction = nullptr;
    QAction* exploreArchiveAction = nullptr;
    QAction* extractHereAction = nullptr;
    QAction* extractToAction = nullptr;
    QAction* extractAllAction = nullptr;
    QAction* openExternallyAction = nullptr;

    if (isFolder) {
        openInNewTabAction = menu.addAction(QStringLiteral("Open in New Tab"));
        openInNewTabAction->setEnabled(false);
        openInNewWindowAction = menu.addAction(QStringLiteral("Open in New Window"));
    }

    if (isArchive) {
        menu.addAction(m_actionExploreArchive);
        menu.addAction(m_actionExtractHere);
        menu.addAction(m_actionExtractTo);
        exploreArchiveAction = m_actionExploreArchive;
        extractHereAction = m_actionExtractHere;
        extractToAction = m_actionExtractTo;
        extractAllAction = menu.addAction(QStringLiteral("Extract All..."));
        openExternallyAction = menu.addAction(QStringLiteral("Open Externally"));
        menu.addSeparator();
    } else if (!isFolder) {
        openWithAction = menu.addAction(QStringLiteral("Open With..."));
    }

    QAction* revealAction = menu.addAction(QStringLiteral("Reveal in Explorer"));
    QAction* openContainingAction = menu.addAction(QStringLiteral("Open Containing Folder"));
    menu.addAction(m_actionCopyPath);
    QAction* copyPathAction = m_actionCopyPath;
    QAction* copyNameAction = menu.addAction(QStringLiteral("Copy Name"));
    QAction* copyFullPathListAction = menu.addAction(QStringLiteral("Copy Full Path List"));
    menu.addSeparator();
    menu.addAction(m_actionRename);
    QAction* renameAction = m_actionRename;
    QAction* deleteAction = menu.addAction(QStringLiteral("Delete..."));
    QAction* moveAction = menu.addAction(QStringLiteral("Move to..."));
    QAction* copyToAction = menu.addAction(QStringLiteral("Copy to..."));
    QAction* duplicateAction = menu.addAction(QStringLiteral("Duplicate"));
    menu.addSeparator();
    QAction* createArchiveAction = menu.addAction(QStringLiteral("Create Archive..."));
    menu.addAction(m_actionCompressZip);
    menu.addAction(m_actionCompress7z);
    menu.addAction(m_actionCompressTar);
    QAction* zipAction = m_actionCompressZip;
    QAction* sevenZipAction = m_actionCompress7z;
    QAction* tarAction = m_actionCompressTar;

    QAction* pinFavoriteAction = nullptr;
    QAction* unpinFavoriteAction = nullptr;
    QAction* treeSnapshotAction = nullptr;
    QAction* openStructuralPanelAction = nullptr;
    if (isFolder) {
        menu.addAction(m_actionOpenStructuralPanel);
        openStructuralPanelAction = m_actionOpenStructuralPanel;
        menu.addAction(m_actionTreeSnapshot);
        treeSnapshotAction = m_actionTreeSnapshot;
        menu.addAction(m_actionSnapshots);
        menu.addAction(m_actionCompareSnapshots);
        if (isFavoritePath(firstPath)) {
            menu.addAction(m_actionUnpinFavorite);
            unpinFavoriteAction = m_actionUnpinFavorite;
        } else {
            menu.addAction(m_actionPinFavorite);
            pinFavoriteAction = m_actionPinFavorite;
        }
    }

    menu.addSeparator();
    QAction* openTerminalAction = menu.addAction(QStringLiteral("Open in Terminal"));
    QAction* openWithCodeAction = menu.addAction(QStringLiteral("Open with Code"));
    QAction* hashFileAction = nullptr;
    QAction* previewAction = nullptr;
    QAction* showReferencesAction = nullptr;
    QAction* showUsedByAction = nullptr;
    QAction* showHistoryAction = nullptr;
    QAction* snapshotsAction = nullptr;
    QAction* compareSnapshotsAction = nullptr;
    if (isFolder) {
        snapshotsAction = m_actionSnapshots;
        compareSnapshotsAction = m_actionCompareSnapshots;
    }
    if (!isFolder) {
        openStructuralPanelAction = menu.addAction(QStringLiteral("Open Structural Panel"));
        hashFileAction = menu.addAction(QStringLiteral("Hash File..."));
        previewAction = menu.addAction(QStringLiteral("Preview"));

        if (!isArchive && !PathUtils::isArchiveVirtualPath(firstPath)) {
            showHistoryAction = menu.addAction(QStringLiteral("Show History"));
        }

        const bool supportsGraphQuery = !isArchive && !PathUtils::isArchiveVirtualPath(firstPath);
        if (supportsGraphQuery) {
            menu.addSeparator();
            showReferencesAction = menu.addAction(QStringLiteral("Show References"));
            showUsedByAction = menu.addAction(QStringLiteral("Show Used By"));
        }
    }
    QAction* propertiesAction = menu.addAction(QStringLiteral("Properties"));

    QAction* chosen = menu.exec(m_treeView->viewport()->mapToGlobal(pos));
    if (!chosen) {
        return;
    }

    if (chosen == openAction) {
        if (isArchive) {
            navigateToDirectory(firstPath);
        } else if (isFolder) {
            navigateToDirectory(firstPath);
        } else {
            QDesktopServices::openUrl(QUrl::fromLocalFile(firstPath));
        }
    } else if (openInNewTabAction && chosen == openInNewTabAction) {
        QMessageBox::information(this, QStringLiteral("Open in New Tab"), QStringLiteral("Tabbed filesystem view is not available yet."));
    } else if (openInNewWindowAction && chosen == openInNewWindowAction) {
        const QString appPath = QCoreApplication::applicationFilePath();
        QProcess::startDetached(appPath, {firstPath});
    } else if (openWithAction && chosen == openWithAction) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(firstPath));
    } else if (exploreArchiveAction && chosen == exploreArchiveAction) {
        onActionExploreArchive();
    } else if (extractHereAction && chosen == extractHereAction) {
        onActionExtractHere();
    } else if (extractToAction && chosen == extractToAction) {
        onActionExtractTo();
    } else if (extractAllAction && chosen == extractAllAction) {
        extractArchive(firstPath, true);
    } else if (openExternallyAction && chosen == openExternallyAction) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(firstPath));
    } else if (chosen == revealAction) {
        QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,"), QDir::toNativeSeparators(firstPath)});
    } else if (chosen == openContainingAction) {
        openContainingFolder(firstPath);
    } else if (showReferencesAction && chosen == showReferencesAction) {
        executeGraphQueryFromSelection(QueryGraphMode::References, firstPath);
    } else if (showUsedByAction && chosen == showUsedByAction) {
        executeGraphQueryFromSelection(QueryGraphMode::UsedBy, firstPath);
    } else if (openStructuralPanelAction && chosen == openStructuralPanelAction) {
        onActionOpenStructuralPanel();
    } else if (showHistoryAction && chosen == showHistoryAction) {
        onActionShowHistory();
    } else if (snapshotsAction && chosen == snapshotsAction) {
        onActionSnapshots();
    } else if (compareSnapshotsAction && chosen == compareSnapshotsAction) {
        onActionCompareSnapshots();
    } else if (chosen == copyPathAction) {
        onActionCopyPath();
    } else if (chosen == copyNameAction) {
        QApplication::clipboard()->setText(QFileInfo(firstPath).fileName());
    } else if (chosen == copyFullPathListAction) {
        copyPathListToClipboard(paths);
    } else if (chosen == renameAction) {
        onActionRename();
    } else if (chosen == deleteAction) {
        deletePathsWithConfirm(paths);
    } else if (chosen == moveAction) {
        copyMovePaths(paths, true);
    } else if (chosen == copyToAction) {
        copyMovePaths(paths, false);
    } else if (chosen == duplicateAction) {
        duplicatePath(firstPath);
    } else if (chosen == createArchiveAction) {
        createArchiveFromPaths(paths);
    } else if (chosen == zipAction) {
        onActionCompressZip();
    } else if (chosen == sevenZipAction) {
        onActionCompress7z();
    } else if (chosen == tarAction) {
        onActionCompressTar();
    } else if (treeSnapshotAction && chosen == treeSnapshotAction) {
        onActionTreeSnapshot();
    } else if (pinFavoriteAction && chosen == pinFavoriteAction) {
        onActionPinFavorite();
    } else if (unpinFavoriteAction && chosen == unpinFavoriteAction) {
        onActionUnpinFavorite();
    } else if (chosen == openTerminalAction) {
        openInTerminal(isFolder ? firstPath : QFileInfo(firstPath).absolutePath());
    } else if (chosen == openWithCodeAction) {
        openWithCode(isFolder ? firstPath : QFileInfo(firstPath).absolutePath());
    } else if (hashFileAction && chosen == hashFileAction) {
        QFile file(firstPath);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, QStringLiteral("Hash File"), QStringLiteral("Unable to read file."));
        } else {
            QCryptographicHash sha256(QCryptographicHash::Sha256);
            while (!file.atEnd()) {
                sha256.addData(file.read(1 << 16));
            }
            QMessageBox::information(this,
                                     QStringLiteral("Hash File"),
                                     QStringLiteral("SHA-256:\n%1").arg(QString::fromLatin1(sha256.result().toHex())));
        }
    } else if (previewAction && chosen == previewAction) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(firstPath));
    } else if (chosen == propertiesAction) {
        showPropertiesDialog(paths);
    }
}

void MainWindow::onTreeActivated(const QModelIndex& index)
{
    const QModelIndex sourceIndex = m_proxyModel.mapToSource(index);
    const QString path = selectedPath(sourceIndex);
    const QString type = m_fileModel.data(m_fileModel.index(sourceIndex.row(), 1, sourceIndex.parent()), Qt::DisplayRole).toString();
    const bool modelSaysFolder = QString::compare(type, QStringLiteral("Folder"), Qt::CaseInsensitive) == 0;
    const QFileInfo fileInfo(path);

    auto rememberPathForAutocomplete = [this](const QString& rawPath) {
        const QString normalized = QDir::fromNativeSeparators(QDir::cleanPath(rawPath));
        if (normalized.trimmed().isEmpty()) {
            return;
        }
        m_recentStructuralPaths.push_back(normalized);
        m_recentStructuralPaths.removeDuplicates();
        while (m_recentStructuralPaths.size() > 200) {
            m_recentStructuralPaths.removeFirst();
        }
        refreshQueryAutocompleteContext();
    };

    if (PathUtils::isArchiveVirtualPath(path)) {
        if (modelSaysFolder) {
            appendRuntimeLog(QStringLiteral("tree_activated_archive_virtual_dir path=%1").arg(path));
            navigateToDirectory(path);
        }
        rememberPathForAutocomplete(path);
        return;
    }

    if (PathUtils::isArchivePath(path)) {
        appendRuntimeLog(QStringLiteral("tree_activated_archive_file path=%1").arg(path));
        navigateToDirectory(path);
        rememberPathForAutocomplete(path);
        return;
    }

    if (isInternalNavigableDirectory(fileInfo)) {
        appendRuntimeLog(QStringLiteral("tree_activated_internal_dir path=%1").arg(path));
        navigateToDirectory(path);
        rememberPathForAutocomplete(path);
        return;
    }

    if (fileInfo.exists() && fileInfo.isFile()) {
        appendRuntimeLog(QStringLiteral("tree_activated_external_file path=%1 suffix=%2 is_link=%3")
                             .arg(path)
                             .arg(fileInfo.suffix())
                             .arg(fileInfo.isSymLink() ? QStringLiteral("true") : QStringLiteral("false")));
        rememberPathForAutocomplete(path);
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}

void MainWindow::onSidebarItemActivated(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column);
    if (!item) {
        return;
    }

    const QString path = item->data(0, Qt::UserRole).toString();
    if (path.isEmpty()) {
        item->setExpanded(!item->isExpanded());
        return;
    }

    appendRuntimeLog(QStringLiteral("sidebar_activated path=%1").arg(path));
    navigateToDirectory(path);
}

void MainWindow::onSidebarContextMenu(const QPoint& pos)
{
    if (!m_sidebarTree) {
        return;
    }

    QTreeWidgetItem* item = m_sidebarTree->itemAt(pos);
    if (!item) {
        return;
    }

    const QString path = item->data(0, Qt::UserRole).toString();
    if (path.isEmpty()) {
        return;
    }

    const bool favoriteItem = (item->parent() == m_favoritesRootItem);
    const bool canPin = !favoriteItem && !isFavoritePath(path);
    setActionContext({path}, QStringLiteral("sidebar"));

    QMenu menu(this);
    QAction* openAction = menu.addAction(QStringLiteral("Open"));
    QAction* pinAction = nullptr;
    if (canPin) {
        menu.addAction(m_actionPinFavorite);
        pinAction = m_actionPinFavorite;
    }
    QAction* unpinAction = nullptr;
    if (favoriteItem) {
        menu.addAction(m_actionUnpinFavorite);
        unpinAction = m_actionUnpinFavorite;
    }
    if (unpinAction) {
        unpinAction->setText(QStringLiteral("Remove from Favorites"));
    }
    QAction* chosen = menu.exec(m_sidebarTree->viewport()->mapToGlobal(pos));
    if (!chosen) {
        return;
    }

    if (chosen == openAction) {
        navigateToDirectory(path);
    } else if (pinAction && chosen == pinAction) {
        onActionPinFavorite();
    } else if (unpinAction && chosen == unpinAction) {
        onActionUnpinFavorite();
    }
}

void MainWindow::onPinCurrentFolder()
{
    setActionContext({m_rootEdit ? m_rootEdit->text() : QString()}, QStringLiteral("toolbar"));
    onActionPinFavorite();
}

void MainWindow::onActionTreeSnapshot()
{
    const QString path = primaryActionPathForCurrentContext();
    if (path.isEmpty()) {
        logUiAction(QStringLiteral("Tree Snapshot"), QStringLiteral("onActionTreeSnapshot"), QStringLiteral("error"), QString(), QStringLiteral("empty_path"));
        return;
    }

    const QFileInfo info(path);
    if (!isInternalNavigableDirectory(info)) {
        logUiAction(QStringLiteral("Tree Snapshot"), QStringLiteral("onActionTreeSnapshot"), QStringLiteral("error"), QString(), QStringLiteral("not_folder"));
        return;
    }

    logUiAction(QStringLiteral("Tree Snapshot"), QStringLiteral("onActionTreeSnapshot"), QStringLiteral("started"));
    onTreeSnapshotRequested(path);
}

void MainWindow::onActionCompressZip()
{
    const QStringList paths = actionPathsForCurrentContext();
    if (paths.isEmpty()) {
        logUiAction(QStringLiteral("Compress Zip"), QStringLiteral("onActionCompressZip"), QStringLiteral("error"), QString(), QStringLiteral("empty_selection"));
        return;
    }
    logUiAction(QStringLiteral("Compress Zip"), QStringLiteral("onActionCompressZip"), QStringLiteral("started"));
    createArchiveFromPaths(paths, QStringLiteral(".zip"));
}

void MainWindow::onActionCompress7z()
{
    const QStringList paths = actionPathsForCurrentContext();
    if (paths.isEmpty()) {
        logUiAction(QStringLiteral("Compress 7z"), QStringLiteral("onActionCompress7z"), QStringLiteral("error"), QString(), QStringLiteral("empty_selection"));
        return;
    }
    logUiAction(QStringLiteral("Compress 7z"), QStringLiteral("onActionCompress7z"), QStringLiteral("started"));
    createArchiveFromPaths(paths, QStringLiteral(".7z"));
}

void MainWindow::onActionCompressTar()
{
    const QStringList paths = actionPathsForCurrentContext();
    if (paths.isEmpty()) {
        logUiAction(QStringLiteral("Compress Tar"), QStringLiteral("onActionCompressTar"), QStringLiteral("error"), QString(), QStringLiteral("empty_selection"));
        return;
    }
    logUiAction(QStringLiteral("Compress Tar"), QStringLiteral("onActionCompressTar"), QStringLiteral("started"));
    createArchiveFromPaths(paths, QStringLiteral(".tar"));
}

void MainWindow::onActionExtractHere()
{
    const QString path = primaryActionPathForCurrentContext();
    if (path.isEmpty() || !isArchivePath(path)) {
        logUiAction(QStringLiteral("Extract Here"), QStringLiteral("onActionExtractHere"), QStringLiteral("error"), QString(), QStringLiteral("invalid_archive"));
        return;
    }
    logUiAction(QStringLiteral("Extract Here"), QStringLiteral("onActionExtractHere"), QStringLiteral("started"));
    extractArchive(path, false);
}

void MainWindow::onActionExtractTo()
{
    const QString path = primaryActionPathForCurrentContext();
    if (path.isEmpty() || !isArchivePath(path)) {
        logUiAction(QStringLiteral("Extract To"), QStringLiteral("onActionExtractTo"), QStringLiteral("error"), QString(), QStringLiteral("invalid_archive"));
        return;
    }
    logUiAction(QStringLiteral("Extract To"), QStringLiteral("onActionExtractTo"), QStringLiteral("started"));
    extractArchive(path, true);
}

void MainWindow::onActionExploreArchive()
{
    const QString path = primaryActionPathForCurrentContext();
    if (path.isEmpty() || !isArchivePath(path)) {
        logUiAction(QStringLiteral("Explore Archive"), QStringLiteral("onActionExploreArchive"), QStringLiteral("error"), QString(), QStringLiteral("invalid_archive"));
        return;
    }
    auto* explorer = new ArchiveExplorer(this);
    explorer->openArchive(path);
    explorer->show();
    logUiAction(QStringLiteral("Explore Archive"), QStringLiteral("onActionExploreArchive"), QStringLiteral("ok"));
}

QString MainWindow::resolveSnapshotRootForAction(const QString& actionPath) const
{
    const QString rootPath = QDir::fromNativeSeparators(QDir::cleanPath(currentRootPath()));
    if (!rootPath.isEmpty()) {
        return rootPath;
    }

    const QString normalizedActionPath = QDir::fromNativeSeparators(QDir::cleanPath(actionPath));
    if (normalizedActionPath.isEmpty()) {
        return QString();
    }

    const QFileInfo info(normalizedActionPath);
    if (info.exists() && info.isDir()) {
        return normalizedActionPath;
    }
    return QDir::fromNativeSeparators(QDir::cleanPath(info.absolutePath()));
}

bool MainWindow::renderSnapshotListForRoot(const QString& rootPath,
                                           bool createNewSnapshot,
                                           int* rowCount,
                                           qint64* createdSnapshotId,
                                           qint64* newestSnapshotId,
                                           qint64* previousSnapshotId,
                                           QString* errorText)
{
    const QString normalizedRoot = QDir::fromNativeSeparators(QDir::cleanPath(rootPath));
    if (normalizedRoot.isEmpty()) {
        if (errorText) {
            *errorText = QStringLiteral("invalid_root_path");
        }
        return false;
    }

    if (PathUtils::isArchivePath(normalizedRoot) || PathUtils::isArchiveVirtualPath(normalizedRoot)) {
        if (errorText) {
            *errorText = QStringLiteral("snapshot_root_must_be_native_directory");
        }
        return false;
    }

    const QFileInfo rootInfo(normalizedRoot);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        if (errorText) {
            *errorText = QStringLiteral("snapshot_root_missing_or_not_directory");
        }
        return false;
    }

    MetaStore store;
    QString initError;
    QString migrationLog;
    if (!store.initialize(m_uiDbPath, &initError, &migrationLog)) {
        if (errorText) {
            *errorText = QStringLiteral("snapshot_store_init_failed:%1").arg(initError);
        }
        return false;
    }

    SnapshotRepository repository(store);

    auto collectSnapshotEntriesFromFilesystem = [&](QVector<SnapshotEntryRecord>* outEntries) {
        outEntries->clear();

        QStringList paths;
        paths.push_back(normalizedRoot);

        QDirIterator it(normalizedRoot,
                        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            paths.push_back(QDir::fromNativeSeparators(QDir::cleanPath(it.next())));
        }

        std::sort(paths.begin(), paths.end(), [](const QString& a, const QString& b) {
            return QString::compare(a, b, Qt::CaseInsensitive) < 0;
        });

        outEntries->reserve(paths.size());
        for (const QString& path : paths) {
            QFileInfo info(path);
            if (!info.exists()) {
                continue;
            }

            SnapshotEntryRecord entry;
            entry.entryPath = QDir::fromNativeSeparators(QDir::cleanPath(info.absoluteFilePath()));
            entry.virtualPath = entry.entryPath;
            entry.parentPath = QDir::fromNativeSeparators(QDir::cleanPath(info.absolutePath()));
            entry.name = info.fileName().isEmpty() ? entry.entryPath : info.fileName();
            entry.normalizedName = entry.name.toLower();
            entry.extension = info.isDir() || info.suffix().isEmpty() ? QString() : (QStringLiteral(".") + info.suffix().toLower());
            entry.isDir = info.isDir();
            entry.hasSizeBytes = !entry.isDir;
            entry.sizeBytes = entry.isDir ? 0 : info.size();
            entry.modifiedUtc = info.lastModified().toUTC().toString(Qt::ISODate);
            entry.hiddenFlag = info.isHidden();
            entry.systemFlag = false;
            entry.archiveFlag = false;
            entry.existsFlag = true;
            entry.archiveSource = QString();
            entry.archiveEntryPath = QString();

            const QString hashPayload = QStringLiteral("%1|%2|%3")
                                            .arg(entry.entryPath)
                                            .arg(entry.hasSizeBytes ? QString::number(entry.sizeBytes) : QStringLiteral("null"))
                                            .arg(entry.modifiedUtc);
            entry.entryHash = QString::fromLatin1(QCryptographicHash::hash(hashPayload.toUtf8(), QCryptographicHash::Sha256).toHex());
            entry.hasEntryHash = !entry.entryHash.isEmpty();
            outEntries->push_back(entry);
        }

        return true;
    };

    auto createSnapshotFromFilesystem = [&](const QString& snapshotName, const QString& noteText, qint64* snapshotIdOut) {
        QVector<SnapshotEntryRecord> entries;
        if (!collectSnapshotEntriesFromFilesystem(&entries)) {
            if (errorText) {
                *errorText = QStringLiteral("collect_snapshot_entries_failed");
            }
            return false;
        }

        SnapshotRecord snapshot;
        snapshot.rootPath = normalizedRoot;
        snapshot.snapshotName = snapshotName;
        snapshot.snapshotType = QStringLiteral("structural_full");
        snapshot.createdUtc = SqlHelpers::utcNowIso();
        snapshot.optionsJson = SnapshotTypesUtil::optionsToJson(SnapshotCreateOptions{});
        snapshot.itemCount = entries.size();
        snapshot.noteText = noteText;

        QString txError;
        if (!store.beginTransaction(&txError)) {
            if (errorText) {
                *errorText = QStringLiteral("snapshot_tx_begin_failed:%1").arg(txError);
            }
            return false;
        }

        qint64 snapshotId = 0;
        if (!repository.createSnapshot(snapshot, &snapshotId, &txError)) {
            store.rollbackTransaction(nullptr);
            if (errorText) {
                *errorText = QStringLiteral("snapshot_create_failed:%1").arg(txError);
            }
            return false;
        }

        for (SnapshotEntryRecord& entry : entries) {
            entry.snapshotId = snapshotId;
        }

        if (!repository.insertSnapshotEntries(snapshotId, entries, &txError)
            || !repository.updateSnapshotItemCount(snapshotId, entries.size(), &txError)
            || !store.commitTransaction(&txError)) {
            store.rollbackTransaction(nullptr);
            if (errorText) {
                *errorText = QStringLiteral("snapshot_insert_failed:%1").arg(txError);
            }
            return false;
        }

        if (snapshotIdOut) {
            *snapshotIdOut = snapshotId;
        }
        return true;
    };

    qint64 createdId = 0;
    if (createNewSnapshot) {
        const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
        if (!createSnapshotFromFilesystem(QStringLiteral("ui_snapshot_%1").arg(stamp),
                                          QStringLiteral("Snapshot UI action"),
                                          &createdId)) {
            store.shutdown();
            return false;
        }
    }

    QVector<SnapshotRecord> snapshots;
    QString listError;
    if (!repository.listSnapshots(normalizedRoot, &snapshots, &listError)) {
        store.shutdown();
        if (errorText) {
            *errorText = QStringLiteral("snapshot_list_failed:%1").arg(listError);
        }
        return false;
    }

    const QVector<StructuralResultRow> structuralRows = StructuralResultAdapter::fromSnapshotRows(snapshots);
    setStructuralCanonicalRows(structuralRows, normalizedRoot, QStringLiteral("Snapshots view"));

    store.shutdown();

    if (rowCount) {
        *rowCount = m_structuralFilteredRows.size();
    }
    if (createdSnapshotId) {
        *createdSnapshotId = createdId;
    }
    if (newestSnapshotId) {
        *newestSnapshotId = snapshots.isEmpty() ? 0 : snapshots.first().id;
    }
    if (previousSnapshotId) {
        *previousSnapshotId = snapshots.size() >= 2 ? snapshots.at(1).id : 0;
    }
    return true;
}

bool MainWindow::renderSnapshotDiffForRoot(const QString& rootPath,
                                           qint64 oldSnapshotId,
                                           qint64 newSnapshotId,
                                           int* rowCount,
                                           int* addedCount,
                                           int* removedCount,
                                           int* changedCount,
                                           QString* errorText)
{
    const QString normalizedRoot = QDir::fromNativeSeparators(QDir::cleanPath(rootPath));
    if (normalizedRoot.isEmpty() || oldSnapshotId <= 0 || newSnapshotId <= 0) {
        if (errorText) {
            *errorText = QStringLiteral("invalid_snapshot_diff_context");
        }
        return false;
    }

    MetaStore store;
    QString initError;
    QString migrationLog;
    if (!store.initialize(m_uiDbPath, &initError, &migrationLog)) {
        if (errorText) {
            *errorText = QStringLiteral("snapshot_store_init_failed:%1").arg(initError);
        }
        return false;
    }

    SnapshotRepository repository(store);
    SnapshotDiffEngine diffEngine(repository);
    SnapshotDiffOptions diffOptions;
    diffOptions.includeUnchanged = true;

    const SnapshotDiffResult diff = diffEngine.compareSnapshots(oldSnapshotId, newSnapshotId, diffOptions);
    if (!diff.ok) {
        store.shutdown();
        if (errorText) {
            *errorText = QStringLiteral("snapshot_diff_failed:%1").arg(diff.errorText);
        }
        return false;
    }

    SnapshotDiffResult effectiveDiff = diff;
    if (effectiveDiff.rows.isEmpty()) {
        QVector<SnapshotEntryRecord> oldRows;
        QVector<SnapshotEntryRecord> newRows;
        QString listError;
        if (repository.listSnapshotEntries(oldSnapshotId, &oldRows, &listError)
            && repository.listSnapshotEntries(newSnapshotId, &newRows, &listError)) {
            QHash<QString, SnapshotEntryRecord> oldByPath;
            QHash<QString, SnapshotEntryRecord> newByPath;
            for (const SnapshotEntryRecord& row : oldRows) {
                oldByPath.insert(row.entryPath, row);
            }
            for (const SnapshotEntryRecord& row : newRows) {
                newByPath.insert(row.entryPath, row);
            }

            QSet<QString> allPaths;
            for (auto it = oldByPath.constBegin(); it != oldByPath.constEnd(); ++it) {
                allPaths.insert(it.key());
            }
            for (auto it = newByPath.constBegin(); it != newByPath.constEnd(); ++it) {
                allPaths.insert(it.key());
            }

            QList<QString> sortedPaths = allPaths.values();
            std::sort(sortedPaths.begin(), sortedPaths.end(), [](const QString& a, const QString& b) {
                return QString::compare(a, b, Qt::CaseInsensitive) < 0;
            });

            auto entriesSame = [](const SnapshotEntryRecord& oldRow, const SnapshotEntryRecord& newRow) {
                if (oldRow.isDir != newRow.isDir) {
                    return false;
                }
                if (oldRow.hasSizeBytes != newRow.hasSizeBytes) {
                    return false;
                }
                if (oldRow.hasSizeBytes && oldRow.sizeBytes != newRow.sizeBytes) {
                    return false;
                }
                if (oldRow.modifiedUtc != newRow.modifiedUtc) {
                    return false;
                }
                if (oldRow.hiddenFlag != newRow.hiddenFlag) {
                    return false;
                }
                if (oldRow.systemFlag != newRow.systemFlag) {
                    return false;
                }
                if (oldRow.archiveFlag != newRow.archiveFlag) {
                    return false;
                }
                return true;
            };

            QVector<SnapshotDiffRow> fallbackRows;
            fallbackRows.reserve(sortedPaths.size());
            for (const QString& path : sortedPaths) {
                const bool hasOld = oldByPath.contains(path);
                const bool hasNew = newByPath.contains(path);
                if (!hasOld && hasNew) {
                    const SnapshotEntryRecord newRow = newByPath.value(path);
                    SnapshotDiffRow row;
                    row.path = newRow.entryPath;
                    row.status = SnapshotDiffStatus::Added;
                    row.newHasSizeBytes = newRow.hasSizeBytes;
                    row.newSizeBytes = newRow.sizeBytes;
                    row.newModifiedUtc = newRow.modifiedUtc;
                    row.newIsDir = newRow.isDir;
                    row.newHiddenFlag = newRow.hiddenFlag;
                    row.newSystemFlag = newRow.systemFlag;
                    row.newArchiveFlag = newRow.archiveFlag;
                    fallbackRows.push_back(row);
                    continue;
                }
                if (hasOld && !hasNew) {
                    const SnapshotEntryRecord oldRow = oldByPath.value(path);
                    SnapshotDiffRow row;
                    row.path = oldRow.entryPath;
                    row.status = SnapshotDiffStatus::Removed;
                    row.oldHasSizeBytes = oldRow.hasSizeBytes;
                    row.oldSizeBytes = oldRow.sizeBytes;
                    row.oldModifiedUtc = oldRow.modifiedUtc;
                    row.oldIsDir = oldRow.isDir;
                    row.oldHiddenFlag = oldRow.hiddenFlag;
                    row.oldSystemFlag = oldRow.systemFlag;
                    row.oldArchiveFlag = oldRow.archiveFlag;
                    fallbackRows.push_back(row);
                    continue;
                }

                const SnapshotEntryRecord oldRow = oldByPath.value(path);
                const SnapshotEntryRecord newRow = newByPath.value(path);
                SnapshotDiffRow row;
                row.path = oldRow.entryPath;
                row.status = entriesSame(oldRow, newRow) ? SnapshotDiffStatus::Unchanged : SnapshotDiffStatus::Changed;
                row.oldHasSizeBytes = oldRow.hasSizeBytes;
                row.oldSizeBytes = oldRow.sizeBytes;
                row.newHasSizeBytes = newRow.hasSizeBytes;
                row.newSizeBytes = newRow.sizeBytes;
                row.oldModifiedUtc = oldRow.modifiedUtc;
                row.newModifiedUtc = newRow.modifiedUtc;
                row.oldIsDir = oldRow.isDir;
                row.newIsDir = newRow.isDir;
                row.oldHiddenFlag = oldRow.hiddenFlag;
                row.newHiddenFlag = newRow.hiddenFlag;
                row.oldSystemFlag = oldRow.systemFlag;
                row.newSystemFlag = newRow.systemFlag;
                row.oldArchiveFlag = oldRow.archiveFlag;
                row.newArchiveFlag = newRow.archiveFlag;
                fallbackRows.push_back(row);
            }

            effectiveDiff.rows = fallbackRows;
            effectiveDiff.summary = diffEngine.summarizeDiff(effectiveDiff);
            appendRuntimeLog(QStringLiteral("snapshot_diff_fallback_applied old=%1 new=%2 rows=%3")
                                 .arg(oldSnapshotId)
                                 .arg(newSnapshotId)
                                 .arg(effectiveDiff.rows.size()));
        }
    }

    const QVector<StructuralResultRow> structuralRows = StructuralResultAdapter::fromDiffRows(effectiveDiff.rows);
    setStructuralCanonicalRows(structuralRows, normalizedRoot, QStringLiteral("Snapshot diff view"));

    store.shutdown();

    if (rowCount) {
        *rowCount = m_structuralFilteredRows.size();
    }
    if (addedCount) {
        *addedCount = static_cast<int>(effectiveDiff.summary.added);
    }
    if (removedCount) {
        *removedCount = static_cast<int>(effectiveDiff.summary.removed);
    }
    if (changedCount) {
        *changedCount = static_cast<int>(effectiveDiff.summary.changed);
    }
    return true;
}

bool MainWindow::loadHistoryRowsForPath(const QString& selectedFilePath,
                                        QString* errorText,
                                        int* rowCount,
                                        QString* resolvedRoot,
                                        QString* resolvedTarget)
{
    auto writeTrace = [&](const QString& msg) {
        if (m_actionTracePath.trimmed().isEmpty()) {
            return;
        }
        QFile f(m_actionTracePath + QStringLiteral(".trace"));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
            return;
        }
        QTextStream ts(&f);
        ts << msg << '\n';
    };

    writeTrace(QStringLiteral("load_history:start"));
    const QString normalizedFilePath = QDir::fromNativeSeparators(QDir::cleanPath(selectedFilePath));
    const QString rootPath = QDir::fromNativeSeparators(QDir::cleanPath(currentRootPath()));
    writeTrace(QStringLiteral("load_history:paths normalized_file=%1 root=%2").arg(normalizedFilePath, rootPath));

    if (normalizedFilePath.isEmpty() || rootPath.isEmpty()) {
        if (errorText) {
            *errorText = QStringLiteral("invalid_history_context");
        }
        return false;
    }

    const QString normalizedTarget = QDir::fromNativeSeparators(QDir::cleanPath(QDir(rootPath).relativeFilePath(normalizedFilePath)));
    if (normalizedTarget.startsWith(QStringLiteral(".."))) {
        if (errorText) {
            *errorText = QStringLiteral("selected_file_outside_root");
        }
        return false;
    }

    MetaStore store;
    QString initError;
    QString migrationLog;
    writeTrace(QStringLiteral("load_history:before_store_init db=%1").arg(m_uiDbPath));
    if (!store.initialize(m_uiDbPath, &initError, &migrationLog)) {
        if (errorText) {
            *errorText = QStringLiteral("history_store_init_failed:%1").arg(initError);
        }
        writeTrace(QStringLiteral("load_history:store_init_failed error=%1").arg(initError));
        return false;
    }
    writeTrace(QStringLiteral("load_history:after_store_init"));

    SnapshotRepository repository(store);
    SnapshotDiffEngine diffEngine(repository);
    HistoryViewEngine historyEngine(repository, diffEngine);

    QString historyError;
    QVector<HistoryEntry> historyRows;
    writeTrace(QStringLiteral("load_history:before_getPathHistory target=%1").arg(normalizedTarget));
    if (!historyEngine.getPathHistory(rootPath, normalizedTarget, &historyRows, &historyError)) {
        store.shutdown();
        if (errorText) {
            *errorText = QStringLiteral("history_query_failed:%1").arg(historyError);
        }
        writeTrace(QStringLiteral("load_history:getPathHistory_failed error=%1").arg(historyError));
        return false;
    }
    writeTrace(QStringLiteral("load_history:after_getPathHistory rows=%1").arg(historyRows.size()));

    const QVector<StructuralResultRow> structuralRows = StructuralResultAdapter::fromHistoryRows(historyRows,
                                                                                                  normalizedFilePath);
    writeTrace(QStringLiteral("load_history:before_display_transform"));
    for (int i = 0; i < structuralRows.size(); ++i) {
        writeTrace(QStringLiteral("load_history:row_transform i=%1 category=%2 status=%3")
                       .arg(i)
                       .arg(StructuralResultRowUtil::categoryToString(structuralRows.at(i).category))
                       .arg(structuralRows.at(i).status));
    }
    setStructuralCanonicalRows(structuralRows, rootPath, QStringLiteral("History view"));
    writeTrace(QStringLiteral("load_history:after_display_transform rows=%1").arg(m_structuralFilteredRows.size()));

    appendRuntimeLog(QStringLiteral("history_view_loaded root=%1 target=%2 rows=%3")
                         .arg(rootPath)
                         .arg(normalizedTarget)
                         .arg(m_structuralFilteredRows.size()));

    writeTrace(QStringLiteral("load_history:before_store_shutdown"));
    store.shutdown();
    writeTrace(QStringLiteral("load_history:after_store_shutdown"));

    if (rowCount) {
        *rowCount = m_structuralFilteredRows.size();
    }
    if (resolvedRoot) {
        *resolvedRoot = rootPath;
    }
    if (resolvedTarget) {
        *resolvedTarget = normalizedTarget;
    }
    writeTrace(QStringLiteral("load_history:end ok=true rows=%1").arg(m_structuralFilteredRows.size()));
    return true;
}

void MainWindow::onActionShowHistory()
{
    QString errorText;
    int tabIndex = -1;
    int rows = 0;
    if (!dispatchQueryToExistingPanel(QStringLiteral("history:"), &errorText, &tabIndex, &rows, true, false)) {
        logUiAction(QStringLiteral("Show History"), QStringLiteral("onActionShowHistory"), QStringLiteral("error"), QString(), errorText);
        if (!(m_testMode && m_testScriptRunning)) {
            QMessageBox::warning(this, QStringLiteral("Show History"), QStringLiteral("Unable to load history: %1").arg(errorText));
        }
        return;
    }

    logUiAction(QStringLiteral("Show History"),
                QStringLiteral("onActionShowHistory"),
                QStringLiteral("ok"),
                QStringLiteral("rows=%1 root=%2 target=%3").arg(rows).arg(m_structuralRootPath, m_structuralTargetPath),
                QString());
}

void MainWindow::onActionSnapshots()
{
    QString errorText;
    int tabIndex = -1;
    int rows = 0;
    if (!dispatchQueryToExistingPanel(QStringLiteral("snapshots:"), &errorText, &tabIndex, &rows, true, false)) {
        logUiAction(QStringLiteral("Snapshots"), QStringLiteral("onActionSnapshots"), QStringLiteral("error"), QString(), errorText);
        if (!(m_testMode && m_testScriptRunning)) {
            QMessageBox::warning(this, QStringLiteral("Snapshots"), QStringLiteral("Unable to load snapshots: %1").arg(errorText));
        }
        return;
    }

    logUiAction(QStringLiteral("Snapshots"),
                QStringLiteral("onActionSnapshots"),
                QStringLiteral("ok"),
                QStringLiteral("rows=%1 root=%2")
                    .arg(rows)
                    .arg(m_structuralRootPath),
                QString());
}

void MainWindow::onActionCompareSnapshots()
{
    QString errorText;
    if (!ensureStructuralPanel()
        || !resolveStructuralPanelContextFromCurrentSelection(&m_structuralRootPath, &m_structuralTargetPath, &errorText)) {
        logUiAction(QStringLiteral("Compare Snapshots"), QStringLiteral("onActionCompareSnapshots"), QStringLiteral("error"), QString(), errorText);
        if (!(m_testMode && m_testScriptRunning)) {
            QMessageBox::warning(this, QStringLiteral("Compare Snapshots"), QStringLiteral("Unable to resolve structural context: %1").arg(errorText));
        }
        return;
    }

    updateStructuralPanelContextLabel();
    m_structuralPanelDock->show();
    m_structuralPanelDock->raise();
    if (m_structuralTabWidget) {
        m_structuralTabWidget->setCurrentIndex(2);
    }

    onStructuralCompareSnapshots();
}

void MainWindow::onActionOpenStructuralPanel()
{
    QString errorText;
    if (!ensureStructuralPanel()
        || !resolveStructuralPanelContextFromCurrentSelection(&m_structuralRootPath, &m_structuralTargetPath, &errorText)) {
        logUiAction(QStringLiteral("Open Structural Panel"),
                    QStringLiteral("onActionOpenStructuralPanel"),
                    QStringLiteral("error"),
                    QString(),
                    errorText);
        if (!(m_testMode && m_testScriptRunning)) {
            QMessageBox::warning(this,
                                 QStringLiteral("Open Structural Panel"),
                                 QStringLiteral("Unable to resolve panel context: %1").arg(errorText));
        }
        return;
    }

    updateStructuralPanelContextLabel();
    refreshStructuralSnapshotSelectors(nullptr);
    if (m_structuralPanelDock) {
        m_structuralPanelDock->show();
        m_structuralPanelDock->raise();
    }
    m_structuralPanelState.activeTab = m_structuralTabWidget ? m_structuralTabWidget->currentIndex() : 0;
    updateStructuralNavigationButtons();

    logUiAction(QStringLiteral("Open Structural Panel"),
                QStringLiteral("onActionOpenStructuralPanel"),
                QStringLiteral("ok"),
                QStringLiteral("root=%1 target=%2").arg(m_structuralRootPath, m_structuralTargetPath),
                QString());
}

void MainWindow::onStructuralPanelTabChanged(int index)
{
    m_structuralPanelState.activeTab = qMax(0, index);
    if (index == 3 && m_structuralReferenceStatusLabel) {
        m_structuralReferenceStatusLabel->setText(QStringLiteral("References tab ready"));
    }
    updateStructuralNavigationButtons();
}

void MainWindow::onStructuralCompareSnapshots()
{
    QString errorText;
    if (m_structuralRootPath.isEmpty() || m_structuralTargetPath.isEmpty()) {
        if (!resolveStructuralPanelContextFromCurrentSelection(&m_structuralRootPath, &m_structuralTargetPath, &errorText)) {
            logUiAction(QStringLiteral("Compare Snapshots"), QStringLiteral("onStructuralCompareSnapshots"), QStringLiteral("error"), QString(), errorText);
            return;
        }
        updateStructuralPanelContextLabel();
    }

    if (!refreshStructuralSnapshotSelectors(&errorText)) {
        logUiAction(QStringLiteral("Compare Snapshots"), QStringLiteral("onStructuralCompareSnapshots"), QStringLiteral("error"), QString(), errorText);
        if (!(m_testMode && m_testScriptRunning)) {
            QMessageBox::warning(this, QStringLiteral("Compare Snapshots"), QStringLiteral("Unable to list snapshots: %1").arg(errorText));
        }
        return;
    }

    if (!m_structuralOldSnapshotCombo || !m_structuralNewSnapshotCombo
        || m_structuralOldSnapshotCombo->currentIndex() < 0
        || m_structuralNewSnapshotCombo->currentIndex() < 0) {
        const QString noSelectionError = QStringLiteral("snapshot_compare_requires_two_snapshots");
        logUiAction(QStringLiteral("Compare Snapshots"), QStringLiteral("onStructuralCompareSnapshots"), QStringLiteral("error"), QString(), noSelectionError);
        if (!(m_testMode && m_testScriptRunning)) {
            QMessageBox::warning(this,
                                 QStringLiteral("Compare Snapshots"),
                                 QStringLiteral("At least two snapshots are required for comparison."));
        }
        return;
    }

    const qint64 oldSnapshotId = m_structuralOldSnapshotCombo->currentData().toLongLong();
    const qint64 newSnapshotId = m_structuralNewSnapshotCombo->currentData().toLongLong();
    if (oldSnapshotId <= 0 || newSnapshotId <= 0 || oldSnapshotId == newSnapshotId) {
        const QString badSelectionError = QStringLiteral("invalid_snapshot_compare_selection");
        logUiAction(QStringLiteral("Compare Snapshots"), QStringLiteral("onStructuralCompareSnapshots"), QStringLiteral("error"), QString(), badSelectionError);
        if (!(m_testMode && m_testScriptRunning)) {
            QMessageBox::warning(this,
                                 QStringLiteral("Compare Snapshots"),
                                 QStringLiteral("Select two different snapshots before comparison."));
        }
        return;
    }

    int rows = 0;
    int added = 0;
    int removed = 0;
    int changed = 0;
    if (!loadStructuralDiffView(oldSnapshotId,
                                newSnapshotId,
                                &errorText,
                                &rows,
                                &added,
                                &removed,
                                &changed)) {
        logUiAction(QStringLiteral("Compare Snapshots"), QStringLiteral("onStructuralCompareSnapshots"), QStringLiteral("error"), QString(), errorText);
        if (!(m_testMode && m_testScriptRunning)) {
            QMessageBox::warning(this, QStringLiteral("Compare Snapshots"), QStringLiteral("Unable to compare snapshots: %1").arg(errorText));
        }
        return;
    }

    if (m_structuralTabWidget) {
        m_structuralTabWidget->setCurrentIndex(2);
    }

    const QString queryText = QStringLiteral("diff:%1:%2").arg(oldSnapshotId).arg(newSnapshotId);
    pushStructuralQueryHistory(queryText);
    m_structuralPanelState.currentQuery = queryText;
    m_structuralPanelState.activeTab = 2;
    m_structuralPanelState.lastResults = collectCurrentModelRows();
    updateStructuralNavigationButtons();

    logUiAction(QStringLiteral("Compare Snapshots"),
                QStringLiteral("onStructuralCompareSnapshots"),
                QStringLiteral("ok"),
                QStringLiteral("rows=%1 added=%2 removed=%3 changed=%4 old_snapshot_id=%5 new_snapshot_id=%6 root=%7")
                    .arg(rows)
                    .arg(added)
                    .arg(removed)
                    .arg(changed)
                    .arg(oldSnapshotId)
                    .arg(newSnapshotId)
                    .arg(m_structuralRootPath),
                QString());
}

void MainWindow::onStructuralShowReferences()
{
    QString errorText;
    int rows = 0;
    if (!loadStructuralReferenceView(QueryGraphMode::References, &errorText, &rows)) {
        logUiAction(QStringLiteral("Show References"), QStringLiteral("onStructuralShowReferences"), QStringLiteral("error"), QString(), errorText);
        if (!(m_testMode && m_testScriptRunning)) {
            QMessageBox::warning(this, QStringLiteral("Show References"), QStringLiteral("Unable to load references: %1").arg(errorText));
        }
        return;
    }

    logUiAction(QStringLiteral("Show References"),
                QStringLiteral("onStructuralShowReferences"),
                QStringLiteral("ok"),
                QStringLiteral("rows=%1 root=%2 target=%3").arg(rows).arg(m_structuralRootPath, m_structuralTargetPath),
                QString());

    const QString queryText = QStringLiteral("references:%1").arg(m_structuralTargetPath);
    pushStructuralQueryHistory(queryText);
    m_structuralPanelState.currentQuery = queryText;
    m_structuralPanelState.activeTab = 3;
    m_structuralPanelState.lastResults = collectCurrentModelRows();
    updateStructuralNavigationButtons();
}

void MainWindow::onStructuralShowUsedBy()
{
    QString errorText;
    int rows = 0;
    if (!loadStructuralReferenceView(QueryGraphMode::UsedBy, &errorText, &rows)) {
        logUiAction(QStringLiteral("Show Used By"), QStringLiteral("onStructuralShowUsedBy"), QStringLiteral("error"), QString(), errorText);
        if (!(m_testMode && m_testScriptRunning)) {
            QMessageBox::warning(this, QStringLiteral("Show Used By"), QStringLiteral("Unable to load used-by rows: %1").arg(errorText));
        }
        return;
    }

    logUiAction(QStringLiteral("Show Used By"),
                QStringLiteral("onStructuralShowUsedBy"),
                QStringLiteral("ok"),
                QStringLiteral("rows=%1 root=%2 target=%3").arg(rows).arg(m_structuralRootPath, m_structuralTargetPath),
                QString());

    const QString queryText = QStringLiteral("usedby:%1").arg(m_structuralTargetPath);
    pushStructuralQueryHistory(queryText);
    m_structuralPanelState.currentQuery = queryText;
    m_structuralPanelState.activeTab = 3;
    m_structuralPanelState.lastResults = collectCurrentModelRows();
    updateStructuralNavigationButtons();
}

bool MainWindow::resolveStructuralPanelContextFromCurrentSelection(QString* rootPathOut,
                                                                   QString* targetPathOut,
                                                                   QString* errorText)
{
    const QString selectedPath = QDir::fromNativeSeparators(QDir::cleanPath(primaryActionPathForCurrentContext()));
    const QString rootPath = resolveSnapshotRootForAction(selectedPath);
    if (rootPath.trimmed().isEmpty()) {
        if (errorText) {
            *errorText = QStringLiteral("invalid_root_path");
        }
        return false;
    }

    QString targetPath;
    QFileInfo selectedInfo(selectedPath);
    if (!selectedPath.isEmpty() && selectedInfo.exists() && selectedInfo.isFile()) {
        targetPath = selectedPath;
    } else {
        const QString preferred = QDir(rootPath).filePath(QStringLiteral("src/main.cpp"));
        QFileInfo preferredInfo(preferred);
        if (preferredInfo.exists() && preferredInfo.isFile()) {
            targetPath = preferredInfo.absoluteFilePath();
        } else {
            QDirIterator it(rootPath,
                            QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                            QDirIterator::Subdirectories);
            if (it.hasNext()) {
                targetPath = QDir::fromNativeSeparators(QDir::cleanPath(it.next()));
            }
        }
    }

    if (targetPath.isEmpty()) {
        if (errorText) {
            *errorText = QStringLiteral("no_file_target_available");
        }
        return false;
    }

    if (m_rootEdit) {
        m_rootEdit->setText(rootPath);
    }

    if (rootPathOut) {
        *rootPathOut = rootPath;
    }
    if (targetPathOut) {
        *targetPathOut = targetPath;
    }
    return true;
}

void MainWindow::updateStructuralPanelContextLabel()
{
    if (!m_structuralContextLabel) {
        return;
    }
    m_structuralContextLabel->setText(
        QStringLiteral("Root: %1 | Target: %2")
            .arg(m_structuralRootPath.isEmpty() ? QStringLiteral("(none)") : m_structuralRootPath)
            .arg(m_structuralTargetPath.isEmpty() ? QStringLiteral("(none)") : m_structuralTargetPath));

    if (!m_structuralTargetPath.trimmed().isEmpty()) {
        const QString normalized = QDir::fromNativeSeparators(QDir::cleanPath(m_structuralTargetPath));
        m_recentStructuralPaths.push_back(normalized);
        m_recentStructuralPaths.removeDuplicates();
        while (m_recentStructuralPaths.size() > 200) {
            m_recentStructuralPaths.removeFirst();
        }
    }
    refreshQueryAutocompleteContext();
}

bool MainWindow::refreshStructuralSnapshotSelectors(QString* errorText)
{
    if (!m_structuralOldSnapshotCombo || !m_structuralNewSnapshotCombo) {
        if (errorText) {
            *errorText = QStringLiteral("snapshot_selectors_missing");
        }
        return false;
    }

    m_structuralOldSnapshotCombo->clear();
    m_structuralNewSnapshotCombo->clear();

    if (m_structuralRootPath.trimmed().isEmpty()) {
        if (errorText) {
            *errorText = QStringLiteral("structural_root_missing");
        }
        return false;
    }

    MetaStore store;
    QString initError;
    QString migrationLog;
    if (!store.initialize(m_uiDbPath, &initError, &migrationLog)) {
        if (errorText) {
            *errorText = QStringLiteral("snapshot_store_init_failed:%1").arg(initError);
        }
        return false;
    }

    SnapshotRepository repository(store);
    QVector<SnapshotRecord> snapshots;
    QString listError;
    const bool listed = repository.listSnapshots(m_structuralRootPath, &snapshots, &listError);
    store.shutdown();
    if (!listed) {
        if (errorText) {
            *errorText = QStringLiteral("snapshot_list_failed:%1").arg(listError);
        }
        return false;
    }

    for (const SnapshotRecord& snapshot : snapshots) {
        const QString label = QStringLiteral("%1 (%2)").arg(snapshot.snapshotName, QString::number(snapshot.id));
        m_structuralOldSnapshotCombo->addItem(label, snapshot.id);
        m_structuralNewSnapshotCombo->addItem(label, snapshot.id);
        m_recentSnapshotTokens.push_back(QString::number(snapshot.id));
        if (!snapshot.snapshotName.trimmed().isEmpty()) {
            m_recentSnapshotTokens.push_back(QStringLiteral("%1:%2").arg(snapshot.id).arg(snapshot.snapshotName));
        }
    }

    m_recentSnapshotTokens.removeDuplicates();
    while (m_recentSnapshotTokens.size() > 200) {
        m_recentSnapshotTokens.removeFirst();
    }

    refreshQueryAutocompleteContext();

    if (snapshots.size() >= 2) {
        m_structuralOldSnapshotCombo->setCurrentIndex(1);
        m_structuralNewSnapshotCombo->setCurrentIndex(0);
    }

    return true;
}

bool MainWindow::loadStructuralHistoryView(QString* errorText, int* rowCount)
{
    QString rootPath;
    QString targetPath;
    int rows = 0;
    const bool ok = loadHistoryRowsForPath(m_structuralTargetPath, errorText, &rows, &rootPath, &targetPath);
    if (ok) {
        if (m_structuralHistoryStatusLabel) {
            m_structuralHistoryStatusLabel->setText(QStringLiteral("History rows: %1").arg(rows));
        }
        if (rowCount) {
            *rowCount = rows;
        }
    }
    return ok;
}

bool MainWindow::loadStructuralSnapshotView(QString* errorText, int* rowCount)
{
    int rows = 0;
    const bool ok = renderSnapshotListForRoot(m_structuralRootPath,
                                              false,
                                              &rows,
                                              nullptr,
                                              nullptr,
                                              nullptr,
                                              errorText);
    if (ok) {
        refreshStructuralSnapshotSelectors(nullptr);
        if (m_structuralSnapshotStatusLabel) {
            m_structuralSnapshotStatusLabel->setText(QStringLiteral("Snapshot rows: %1").arg(rows));
        }
        if (rowCount) {
            *rowCount = rows;
        }
    }
    return ok;
}

bool MainWindow::loadStructuralDiffView(qint64 oldSnapshotId,
                                        qint64 newSnapshotId,
                                        QString* errorText,
                                        int* rowCount,
                                        int* addedCount,
                                        int* removedCount,
                                        int* changedCount)
{
    int rows = 0;
    int added = 0;
    int removed = 0;
    int changed = 0;
    const bool ok = renderSnapshotDiffForRoot(m_structuralRootPath,
                                              oldSnapshotId,
                                              newSnapshotId,
                                              &rows,
                                              &added,
                                              &removed,
                                              &changed,
                                              errorText);
    if (ok) {
        if (m_structuralDiffStatusLabel) {
            m_structuralDiffStatusLabel->setText(
                QStringLiteral("Diff rows: %1 (added=%2 removed=%3 changed=%4)")
                    .arg(rows)
                    .arg(added)
                    .arg(removed)
                    .arg(changed));
        }
        if (rowCount) {
            *rowCount = rows;
        }
        if (addedCount) {
            *addedCount = added;
        }
        if (removedCount) {
            *removedCount = removed;
        }
        if (changedCount) {
            *changedCount = changed;
        }
    }
    return ok;
}

bool MainWindow::loadStructuralReferenceView(QueryGraphMode mode,
                                             QString* errorText,
                                             int* rowCount)
{
    if (m_structuralRootPath.trimmed().isEmpty() || m_structuralTargetPath.trimmed().isEmpty()) {
        if (errorText) {
            *errorText = QStringLiteral("invalid_reference_context");
        }
        return false;
    }

    MetaStore store;
    QString initError;
    QString migrationLog;
    if (!store.initialize(m_uiDbPath, &initError, &migrationLog)) {
        if (errorText) {
            *errorText = QStringLiteral("reference_store_init_failed:%1").arg(initError);
        }
        return false;
    }

    QueryCore queryCore(store);
    QueryOptions options;
    options.sortField = QuerySortField::Path;
    options.ascending = true;
    const QString graphTarget = graphQueryTargetForPath(m_structuralTargetPath);
    const QueryResult queryResult = queryCore.queryGraph(m_structuralRootPath, mode, graphTarget, options);
    if (!queryResult.ok) {
        store.shutdown();
        if (errorText) {
            *errorText = QStringLiteral("reference_query_failed:%1").arg(queryResult.errorText);
        }
        return false;
    }

    const QVector<StructuralResultRow> structuralRows = StructuralResultAdapter::fromReferenceRows(queryResult.rows);

    const QString modeText = (mode == QueryGraphMode::References)
        ? QStringLiteral("References")
        : QStringLiteral("Used By");
    setStructuralCanonicalRows(structuralRows,
                               m_structuralRootPath,
                               QStringLiteral("%1 view").arg(modeText));
    if (m_structuralReferenceStatusLabel) {
        m_structuralReferenceStatusLabel->setText(QStringLiteral("%1 rows: %2").arg(modeText).arg(m_structuralFilteredRows.size()));
    }

    store.shutdown();
    if (rowCount) {
        *rowCount = m_structuralFilteredRows.size();
    }
    return true;
}

bool MainWindow::ensureStructuralPanel()
{
    if (!m_structuralPanelDock || !m_structuralTabWidget) {
        setupStructuralPanel();
    }
    const bool ok = m_structuralPanelDock && m_structuralTabWidget;
    if (ok) {
        updateStructuralNavigationButtons();
    }
    return ok;
}

void MainWindow::pushStructuralQueryHistory(const QString& queryText)
{
    const QString normalized = queryText.trimmed();
    if (normalized.isEmpty()) {
        return;
    }

    if (m_structuralPanelState.queryHistoryIndex >= 0
        && m_structuralPanelState.queryHistoryIndex < m_structuralPanelState.queryHistory.size() - 1) {
        m_structuralPanelState.queryHistory = m_structuralPanelState.queryHistory.mid(0, m_structuralPanelState.queryHistoryIndex + 1);
    }

    if (!m_structuralPanelState.queryHistory.isEmpty()
        && m_structuralPanelState.queryHistory.constLast() == normalized) {
        m_structuralPanelState.queryHistoryIndex = m_structuralPanelState.queryHistory.size() - 1;
        return;
    }

    m_structuralPanelState.queryHistory.push_back(normalized);
    m_structuralPanelState.queryHistoryIndex = m_structuralPanelState.queryHistory.size() - 1;
}

void MainWindow::updateStructuralNavigationButtons()
{
    const bool hasCurrentQuery = !m_structuralPanelState.currentQuery.trimmed().isEmpty();
    const bool canBack = m_structuralPanelState.queryHistoryIndex > 0;
    const bool canForward = m_structuralPanelState.queryHistoryIndex >= 0
        && m_structuralPanelState.queryHistoryIndex < (m_structuralPanelState.queryHistory.size() - 1);

    if (m_structuralBackButton) {
        m_structuralBackButton->setEnabled(canBack);
    }
    if (m_structuralForwardButton) {
        m_structuralForwardButton->setEnabled(canForward);
    }
    if (m_structuralRefreshButton) {
        m_structuralRefreshButton->setEnabled(hasCurrentQuery);
    }
}

void MainWindow::updateStructuralFilterStateFromControls()
{
    m_structuralFilterState.clear();

    auto readComboToken = [](QComboBox* combo) {
        if (!combo || combo->currentIndex() < 0) {
            return QString();
        }
        return StructuralFilterStateUtil::normalizeToken(combo->currentData().toString());
    };

    const QString category = readComboToken(m_structuralCategoryFilterCombo);
    if (!category.isEmpty()) {
        m_structuralFilterState.categories.insert(category);
    }

    const QString status = readComboToken(m_structuralStatusFilterCombo);
    if (!status.isEmpty()) {
        m_structuralFilterState.statuses.insert(status);
    }

    const QString extension = readComboToken(m_structuralExtensionFilterCombo);
    if (!extension.isEmpty()) {
        m_structuralFilterState.extensions.insert(extension);
    }

    const QString relationship = readComboToken(m_structuralRelationshipFilterCombo);
    if (!relationship.isEmpty()) {
        m_structuralFilterState.relationships.insert(relationship);
    }

    if (m_structuralTextFilterEdit) {
        m_structuralFilterState.substring = m_structuralTextFilterEdit->text().trimmed();
    }
}

void MainWindow::updateStructuralSortStateFromControls()
{
    if (!m_structuralSortFieldCombo || m_structuralSortFieldCombo->currentIndex() < 0) {
        return;
    }

    bool ok = false;
    const int value = m_structuralSortFieldCombo->currentData().toInt(&ok);
    if (ok) {
        m_structuralSortField = static_cast<StructuralSortField>(value);
    }

    if (m_structuralSortDirectionButton) {
        m_structuralSortDirectionButton->setText(m_structuralSortDirection == StructuralSortDirection::Ascending
                                                     ? QStringLiteral("Sort: Asc")
                                                     : QStringLiteral("Sort: Desc"));
    }
}

void MainWindow::setStructuralGraphModeEnabled(bool enabled)
{
    setStructuralViewMode(enabled ? 1 : 0);
}

void MainWindow::setStructuralViewMode(int mode)
{
    m_structuralViewMode = mode;
    m_structuralGraphModeEnabled = (mode == 1);

    if (m_structuralTableViewButton) {
        QSignalBlocker blocker(m_structuralTableViewButton);
        m_structuralTableViewButton->setChecked(mode == 0);
    }
    if (m_structuralGraphViewButton) {
        QSignalBlocker blocker(m_structuralGraphViewButton);
        m_structuralGraphViewButton->setChecked(mode == 1);
    }
    if (m_structuralTimelineViewButton) {
        QSignalBlocker blocker(m_structuralTimelineViewButton);
        m_structuralTimelineViewButton->setChecked(mode == 2);
    }

    if (m_structuralTabWidget) {
        m_structuralTabWidget->setVisible(mode == 0);
    }
    if (m_structuralGraphWidget) {
        m_structuralGraphWidget->setVisible(mode == 1);
    }
    if (m_structuralTimelineWidget) {
        m_structuralTimelineWidget->setVisible(mode == 2);
    }

    if (m_structuralGraphStatusLabel) {
        const QString modeLabel = (mode == 0)
            ? QStringLiteral("table")
            : ((mode == 1) ? QStringLiteral("graph") : QStringLiteral("timeline"));
        m_structuralGraphStatusLabel->setText(
            QStringLiteral("Structural mode=%1 graph_nodes=%2 graph_edges=%3")
                .arg(modeLabel)
                .arg(m_structuralGraphWidget ? m_structuralGraphWidget->nodePathsForTesting().size() : 0)
                .arg(m_structuralGraphWidget ? m_structuralGraphWidget->edgeKeysForTesting().size() : 0));
    }
}

void MainWindow::updateStructuralGraphFromCanonicalRows()
{
    if (!m_structuralGraphWidget) {
        return;
    }

    StructuralGraphBuildOptions options;
    options.mode = m_structuralGraphMode;
    options.maxNodes = 100;
    const StructuralGraphData graph = StructuralGraphBuilder::build(m_structuralCanonicalRows, options);
    m_structuralGraphWidget->setGraphData(graph);

    if (m_structuralGraphStatusLabel) {
        const QString modeLabel = (m_structuralGraphMode == StructuralGraphMode::Dependency)
            ? QStringLiteral("dependency")
            : QStringLiteral("reverse_dependency");
        m_structuralGraphStatusLabel->setText(
            QStringLiteral("Graph mode=%1 nodes=%2 edges=%3 source=canonical")
                .arg(modeLabel)
                .arg(graph.nodes.size())
                .arg(graph.edges.size()));
    }
}

void MainWindow::updateStructuralTimelineFromCanonicalRows()
{
    if (!m_structuralTimelineWidget) {
        return;
    }

    QHash<qint64, StructuralTimelineSnapshotRows> snapshotBuckets;
    QVector<qint64> snapshotIds;

    for (const StructuralResultRow& row : m_structuralCanonicalRows) {
        if (!row.hasSnapshotId || row.snapshotId <= 0) {
            continue;
        }

        if (!snapshotBuckets.contains(row.snapshotId)) {
            StructuralTimelineSnapshotRows bucket;
            bucket.snapshotId = row.snapshotId;
            bucket.timestamp = row.timestamp;
            snapshotBuckets.insert(row.snapshotId, bucket);
            snapshotIds.push_back(row.snapshotId);
        }

        StructuralTimelineSnapshotRows& bucket = snapshotBuckets[row.snapshotId];
        if (bucket.timestamp.trimmed().isEmpty() && !row.timestamp.trimmed().isEmpty()) {
            bucket.timestamp = row.timestamp;
        }
        bucket.rows.push_back(row);
    }

    std::sort(snapshotIds.begin(), snapshotIds.end());

    QVector<StructuralTimelineSnapshotRows> orderedSnapshots;
    orderedSnapshots.reserve(snapshotIds.size());
    for (qint64 snapshotId : snapshotIds) {
        orderedSnapshots.push_back(snapshotBuckets.value(snapshotId));
    }

    m_structuralTimelineEvents = StructuralTimelineBuilder::build(orderedSnapshots);
    m_structuralTimelineWidget->setTimelineEvents(m_structuralTimelineEvents);

    if (m_structuralTimelineStatusLabel) {
        m_structuralTimelineStatusLabel->setText(
            QStringLiteral("Timeline snapshots=%1 events=%2 source=canonical")
                .arg(orderedSnapshots.size())
                .arg(m_structuralTimelineEvents.size()));
    }
}

void MainWindow::onStructuralGraphNodeActivated(const QString& absolutePath)
{
    const QString candidate = QDir::fromNativeSeparators(QDir::cleanPath(absolutePath));
    const QFileInfo info(candidate);
    QString destination = candidate;
    if (!info.isDir()) {
        destination = info.absolutePath();
    }

    if (!destination.isEmpty() && isNavigablePath(destination)) {
        navigateToDirectory(destination);
        m_structuralTargetPath = candidate;
        updateStructuralPanelContextLabel();
        if (m_statusLabel) {
            m_statusLabel->setText(QStringLiteral("Graph node selected: %1").arg(candidate));
        }
        appendRuntimeLog(QStringLiteral("StructuralGraph node_selected path=%1 destination=%2")
                             .arg(candidate)
                             .arg(destination));
        return;
    }

    if (m_statusLabel) {
        m_statusLabel->setText(QStringLiteral("Graph node selection skipped (not navigable): %1").arg(candidate));
    }
}

void MainWindow::onStructuralTimelineEventActivated(const QString& absolutePath)
{
    const QString candidate = QDir::fromNativeSeparators(QDir::cleanPath(absolutePath));
    const QFileInfo info(candidate);
    QString destination = candidate;
    if (!info.isDir()) {
        destination = info.absolutePath();
    }

    if (!destination.isEmpty() && isNavigablePath(destination)) {
        navigateToDirectory(destination);
        m_structuralTargetPath = candidate;
        updateStructuralPanelContextLabel();
        if (m_statusLabel) {
            m_statusLabel->setText(QStringLiteral("Timeline event selected: %1").arg(candidate));
        }
        appendRuntimeLog(QStringLiteral("StructuralTimeline event_selected path=%1 destination=%2")
                             .arg(candidate)
                             .arg(destination));
        return;
    }

    if (m_statusLabel) {
        m_statusLabel->setText(QStringLiteral("Timeline event selection skipped (not navigable): %1").arg(candidate));
    }
}

void MainWindow::refreshQueryAutocompleteContext()
{
    if (!m_queryBarWidget) {
        return;
    }

    StructuralAutocompleteContext context;
    context.currentTargetPath = m_structuralTargetPath;

    QStringList knownPaths = m_recentStructuralPaths;
    if (!m_structuralTargetPath.trimmed().isEmpty()) {
        knownPaths.push_back(QDir::fromNativeSeparators(QDir::cleanPath(m_structuralTargetPath)));
    }

    for (const StructuralResultRow& row : m_structuralCanonicalRows) {
        if (!row.primaryPath.trimmed().isEmpty()) {
            knownPaths.push_back(QDir::fromNativeSeparators(QDir::cleanPath(row.primaryPath)));
        }
        if (!row.secondaryPath.trimmed().isEmpty()) {
            knownPaths.push_back(QDir::fromNativeSeparators(QDir::cleanPath(row.secondaryPath)));
        }
        if (!row.sourceFile.trimmed().isEmpty()) {
            knownPaths.push_back(QDir::fromNativeSeparators(QDir::cleanPath(row.sourceFile)));
        }
        if (row.hasSnapshotId && row.snapshotId > 0) {
            m_recentSnapshotTokens.push_back(QString::number(row.snapshotId));
        }
    }

    if (m_structuralOldSnapshotCombo) {
        for (int i = 0; i < m_structuralOldSnapshotCombo->count(); ++i) {
            const qint64 id = m_structuralOldSnapshotCombo->itemData(i).toLongLong();
            if (id > 0) {
                m_recentSnapshotTokens.push_back(QString::number(id));
                m_recentSnapshotTokens.push_back(QStringLiteral("%1:%2").arg(id).arg(m_structuralOldSnapshotCombo->itemText(i)));
            }
        }
    }

    knownPaths.removeDuplicates();
    while (knownPaths.size() > 300) {
        knownPaths.removeFirst();
    }

    m_recentSnapshotTokens.removeDuplicates();
    while (m_recentSnapshotTokens.size() > 300) {
        m_recentSnapshotTokens.removeFirst();
    }

    context.knownPaths = knownPaths;
    context.snapshotTokens = m_recentSnapshotTokens;
    m_queryBarWidget->setAutocompleteContext(context);
}

void MainWindow::updateStructuralFilterControlChoices(const QVector<StructuralResultRow>& canonicalRows)
{
    if (!m_structuralExtensionFilterCombo || !m_structuralRelationshipFilterCombo) {
        return;
    }

    QString previousExtension = StructuralFilterStateUtil::normalizeToken(m_structuralExtensionFilterCombo->currentData().toString());
    QString previousRelationship = StructuralFilterStateUtil::normalizeToken(m_structuralRelationshipFilterCombo->currentData().toString());

    QSet<QString> extensions;
    QSet<QString> relationships;
    for (const StructuralResultRow& row : canonicalRows) {
        const QString extension = StructuralFilterEngine::extensionForPath(row.primaryPath);
        if (!extension.isEmpty()) {
            extensions.insert(StructuralFilterStateUtil::normalizeToken(extension));
        }

        const QString relationship = StructuralFilterStateUtil::normalizeToken(row.relationship);
        if (!relationship.isEmpty()) {
            relationships.insert(relationship);
        }
    }

    QStringList sortedExtensions = extensions.values();
    QStringList sortedRelationships = relationships.values();
    std::sort(sortedExtensions.begin(), sortedExtensions.end());
    std::sort(sortedRelationships.begin(), sortedRelationships.end());

    {
        QSignalBlocker blocker(m_structuralExtensionFilterCombo);
        m_structuralExtensionFilterCombo->clear();
        m_structuralExtensionFilterCombo->addItem(QStringLiteral("Ext: All"), QString());
        for (const QString& extension : sortedExtensions) {
            m_structuralExtensionFilterCombo->addItem(extension, extension);
        }
        int targetIndex = m_structuralExtensionFilterCombo->findData(previousExtension);
        if (targetIndex < 0) {
            targetIndex = 0;
        }
        m_structuralExtensionFilterCombo->setCurrentIndex(targetIndex);
    }

    {
        QSignalBlocker blocker(m_structuralRelationshipFilterCombo);
        m_structuralRelationshipFilterCombo->clear();
        m_structuralRelationshipFilterCombo->addItem(QStringLiteral("Rel: All"), QString());
        for (const QString& relationship : sortedRelationships) {
            m_structuralRelationshipFilterCombo->addItem(relationship, relationship);
        }
        int targetIndex = m_structuralRelationshipFilterCombo->findData(previousRelationship);
        if (targetIndex < 0) {
            targetIndex = 0;
        }
        m_structuralRelationshipFilterCombo->setCurrentIndex(targetIndex);
    }
}

void MainWindow::applyStructuralFiltersToCurrentRows(const QString& statusPrefix)
{
    QVector<StructuralResultRow> rankedRows = m_structuralCanonicalRows;
    StructuralRankingEngine::computeRanking(&rankedRows);
    m_structuralFilteredRows = StructuralFilterEngine::apply(rankedRows, m_structuralFilterState);
    StructuralSortEngine::sortRows(&m_structuralFilteredRows, m_structuralSortField, m_structuralSortDirection);
    const QVector<FileEntry> displayRows = StructuralResultAdapter::toFileEntries(m_structuralFilteredRows);

    m_fileModel.clear();
    m_viewMode = FileViewMode::Standard;
    m_viewModeController.setModeFromIndex(0);
    if (m_viewModeCombo) {
        QSignalBlocker blocker(m_viewModeCombo);
        m_viewModeCombo->setCurrentIndex(0);
    }
    m_fileModel.setViewMode(FileViewMode::Standard, m_structuralViewRoot);
    if (!displayRows.isEmpty()) {
        m_fileModel.appendBatch(displayRows);
    }
    if (m_treeView) {
        m_treeView->setSortingEnabled(true);
    }

    m_publishQueue.clear();
    m_publishTimer.stop();
    m_scanBatchCount = 1;
    m_scanEntryCount = static_cast<quint64>(displayRows.size());
    m_scanEnumeratedCount = static_cast<quint64>(displayRows.size());
    m_scanInProgress = false;

    const QString statusBase = statusPrefix.trimmed().isEmpty() ? QStringLiteral("Structural view") : statusPrefix;
    const QString activeFilters = m_structuralFilterState.isEmpty() ? QStringLiteral("none") : QStringLiteral("active");
    const QString sortDirection = (m_structuralSortDirection == StructuralSortDirection::Ascending)
        ? QStringLiteral("asc")
        : QStringLiteral("desc");
    if (m_statusLabel) {
        m_statusLabel->setText(QStringLiteral("%1: %2/%3 rows (filters=%4 sort=%5)")
                                   .arg(statusBase)
                                   .arg(displayRows.size())
                                   .arg(m_structuralCanonicalRows.size())
                                   .arg(activeFilters)
                                   .arg(sortDirection));
    }

    updateStructuralGraphFromCanonicalRows();
    updateStructuralTimelineFromCanonicalRows();
}

void MainWindow::clearStructuralFilters(bool applyNow)
{
    m_structuralFilterState.clear();
    if (m_structuralCategoryFilterCombo) {
        QSignalBlocker blocker(m_structuralCategoryFilterCombo);
        m_structuralCategoryFilterCombo->setCurrentIndex(0);
    }
    if (m_structuralStatusFilterCombo) {
        QSignalBlocker blocker(m_structuralStatusFilterCombo);
        m_structuralStatusFilterCombo->setCurrentIndex(0);
    }
    if (m_structuralExtensionFilterCombo) {
        QSignalBlocker blocker(m_structuralExtensionFilterCombo);
        m_structuralExtensionFilterCombo->setCurrentIndex(0);
    }
    if (m_structuralRelationshipFilterCombo) {
        QSignalBlocker blocker(m_structuralRelationshipFilterCombo);
        m_structuralRelationshipFilterCombo->setCurrentIndex(0);
    }
    if (m_structuralTextFilterEdit) {
        QSignalBlocker blocker(m_structuralTextFilterEdit);
        m_structuralTextFilterEdit->clear();
    }
    if (m_structuralSortFieldCombo) {
        QSignalBlocker blocker(m_structuralSortFieldCombo);
        const int index = m_structuralSortFieldCombo->findData(static_cast<int>(m_structuralSortField));
        m_structuralSortFieldCombo->setCurrentIndex(index >= 0 ? index : 0);
    }

    if (applyNow) {
        applyStructuralFiltersToCurrentRows(m_structuralStatusPrefix);
    }
}

void MainWindow::setStructuralCanonicalRows(const QVector<StructuralResultRow>& rows,
                                            const QString& viewRoot,
                                            const QString& statusPrefix)
{
    m_structuralCanonicalRows = rows;
    m_structuralViewRoot = viewRoot;
    m_structuralStatusPrefix = statusPrefix;

    if (!rows.isEmpty()) {
        const StructuralResultCategory category = rows.first().category;
        bool uniformCategory = true;
        for (const StructuralResultRow& row : rows) {
            if (row.category != category) {
                uniformCategory = false;
                break;
            }
        }

        if (uniformCategory) {
            StructuralSortField defaultField = m_structuralSortField;
            StructuralSortDirection defaultDirection = m_structuralSortDirection;
            if (StructuralSortEngine::defaultSortForCategory(category, &defaultField, &defaultDirection)) {
                m_structuralSortField = defaultField;
                m_structuralSortDirection = defaultDirection;
                if (m_structuralSortFieldCombo) {
                    QSignalBlocker blocker(m_structuralSortFieldCombo);
                    const int index = m_structuralSortFieldCombo->findData(static_cast<int>(m_structuralSortField));
                    if (index >= 0) {
                        m_structuralSortFieldCombo->setCurrentIndex(index);
                    }
                }
                if (m_structuralSortDirectionButton) {
                    m_structuralSortDirectionButton->setText(
                        m_structuralSortDirection == StructuralSortDirection::Ascending
                            ? QStringLiteral("Sort: Asc")
                            : QStringLiteral("Sort: Desc"));
                }
            }
        }
    }

    updateStructuralFilterControlChoices(rows);
    updateStructuralFilterStateFromControls();
    updateStructuralSortStateFromControls();
    updateStructuralGraphFromCanonicalRows();
    updateStructuralTimelineFromCanonicalRows();
    refreshQueryAutocompleteContext();
    applyStructuralFiltersToCurrentRows(statusPrefix);
}

bool MainWindow::dispatchQueryToExistingPanel(const QString& queryText,
                                              QString* errorText,
                                              int* activeTabIndex,
                                              int* rowCount,
                                              bool pushHistory,
                                              bool forceRefresh)
{
    if (!ensureStructuralPanel()) {
        if (errorText) {
            *errorText = QStringLiteral("structural_panel_unavailable");
        }
        return false;
    }

    const bool handled = dispatchStructuralQueryToPanel(queryText,
                                                        errorText,
                                                        activeTabIndex,
                                                        rowCount,
                                                        pushHistory,
                                                        forceRefresh);
    if (handled) {
        m_structuralPanelState.lastResults = collectCurrentModelRows();
    }
    updateStructuralNavigationButtons();
    return handled;
}

bool MainWindow::navigateStructuralBack(QString* errorText,
                                        int* activeTabIndex,
                                        int* rowCount)
{
    if (m_structuralPanelState.queryHistoryIndex <= 0) {
        if (errorText) {
            *errorText = QStringLiteral("structural_history_back_unavailable");
        }
        updateStructuralNavigationButtons();
        return false;
    }

    const int targetIndex = m_structuralPanelState.queryHistoryIndex - 1;
    const QString query = m_structuralPanelState.queryHistory.at(targetIndex);
    if (!dispatchQueryToExistingPanel(query, errorText, activeTabIndex, rowCount, false, true)) {
        updateStructuralNavigationButtons();
        return false;
    }

    m_structuralPanelState.queryHistoryIndex = targetIndex;
    m_structuralPanelState.currentQuery = query;
    updateStructuralNavigationButtons();
    return true;
}

bool MainWindow::navigateStructuralForward(QString* errorText,
                                           int* activeTabIndex,
                                           int* rowCount)
{
    if (m_structuralPanelState.queryHistoryIndex < 0
        || m_structuralPanelState.queryHistoryIndex >= (m_structuralPanelState.queryHistory.size() - 1)) {
        if (errorText) {
            *errorText = QStringLiteral("structural_history_forward_unavailable");
        }
        updateStructuralNavigationButtons();
        return false;
    }

    const int targetIndex = m_structuralPanelState.queryHistoryIndex + 1;
    const QString query = m_structuralPanelState.queryHistory.at(targetIndex);
    if (!dispatchQueryToExistingPanel(query, errorText, activeTabIndex, rowCount, false, true)) {
        updateStructuralNavigationButtons();
        return false;
    }

    m_structuralPanelState.queryHistoryIndex = targetIndex;
    m_structuralPanelState.currentQuery = query;
    updateStructuralNavigationButtons();
    return true;
}

bool MainWindow::refreshStructuralCurrentQuery(QString* errorText,
                                               int* activeTabIndex,
                                               int* rowCount)
{
    const QString query = m_structuralPanelState.currentQuery.trimmed();
    if (query.isEmpty()) {
        if (errorText) {
            *errorText = QStringLiteral("structural_refresh_query_missing");
        }
        updateStructuralNavigationButtons();
        return false;
    }

    const bool ok = dispatchQueryToExistingPanel(query,
                                                 errorText,
                                                 activeTabIndex,
                                                 rowCount,
                                                 false,
                                                 true);
    updateStructuralNavigationButtons();
    return ok;
}

bool MainWindow::dispatchStructuralQueryToPanel(const QString& queryText,
                                                QString* errorText,
                                                int* activeTabIndex,
                                                int* rowCount,
                                                bool pushHistory,
                                                bool forceRefresh)
{
    Q_UNUSED(forceRefresh);
    const QString query = queryText.trimmed();
    const QString lower = query.toLower();

    auto setError = [&](const QString& value) {
        if (errorText) {
            *errorText = value;
        }
    };

    auto resolveRootAndTarget = [&](const QString& rawTarget,
                                    QString* resolvedRoot,
                                    QString* resolvedTarget,
                                    QString* resolveError) {
        QString root = QDir::fromNativeSeparators(QDir::cleanPath(currentRootPath()));
        QString target;

        const QString hint = QDir::fromNativeSeparators(QDir::cleanPath(rawTarget.trimmed()));
        if (!hint.isEmpty()) {
            QFileInfo hintInfo(hint);
            if (hintInfo.isAbsolute()) {
                target = QDir::fromNativeSeparators(QDir::cleanPath(hintInfo.absoluteFilePath()));
            } else if (!root.isEmpty()) {
                target = QDir::fromNativeSeparators(QDir::cleanPath(QDir(root).filePath(hint)));
            } else {
                target = hint;
            }
        }

        if (!target.isEmpty()) {
            const QFileInfo targetInfo(target);
            if (targetInfo.exists() && targetInfo.isDir()) {
                root = QDir::fromNativeSeparators(QDir::cleanPath(targetInfo.absoluteFilePath()));
                target.clear();
            } else {
                root = resolveSnapshotRootForAction(target);
            }
        }

        if (root.trimmed().isEmpty()) {
            if (resolveError) {
                *resolveError = QStringLiteral("invalid_root_for_structural_query");
            }
            return false;
        }

        if (target.isEmpty()) {
            const QString preferred = QDir(root).filePath(QStringLiteral("src/main.cpp"));
            const QFileInfo preferredInfo(preferred);
            if (preferredInfo.exists() && preferredInfo.isFile()) {
                target = QDir::fromNativeSeparators(QDir::cleanPath(preferredInfo.absoluteFilePath()));
            } else {
                QDirIterator it(root,
                                QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                                QDirIterator::Subdirectories);
                if (it.hasNext()) {
                    target = QDir::fromNativeSeparators(QDir::cleanPath(it.next()));
                }
            }
        }

        if (target.trimmed().isEmpty()) {
            if (resolveError) {
                *resolveError = QStringLiteral("no_structural_target_file");
            }
            return false;
        }

        if (resolvedRoot) {
            *resolvedRoot = root;
        }
        if (resolvedTarget) {
            *resolvedTarget = target;
        }
        return true;
    };

    auto activatePanel = [&](int tabIndex) {
        if (!m_structuralPanelDock || !m_structuralTabWidget) {
            return false;
        }
        updateStructuralPanelContextLabel();
        m_structuralPanelDock->show();
        m_structuralPanelDock->raise();
        if (m_structuralTabWidget->currentIndex() != tabIndex) {
            m_structuralTabWidget->setCurrentIndex(tabIndex);
        }
        m_structuralPanelState.activeTab = tabIndex;
        if (activeTabIndex) {
            *activeTabIndex = tabIndex;
        }
        return true;
    };

    if (lower.startsWith(QStringLiteral("history:"))) {
        const QString targetHint = query.mid(QStringLiteral("history:").size()).trimmed();
        QString localError;
        if (!resolveRootAndTarget(targetHint, &m_structuralRootPath, &m_structuralTargetPath, &localError)) {
            setError(localError);
            return false;
        }
        if (m_rootEdit) {
            m_rootEdit->setText(m_structuralRootPath);
        }
        if (!activatePanel(0)) {
            setError(QStringLiteral("structural_panel_unavailable"));
            return false;
        }
        int rows = 0;
        if (!loadStructuralHistoryView(&localError, &rows)) {
            setError(localError);
            return false;
        }
        if (pushHistory) {
            pushStructuralQueryHistory(query);
        }
        m_structuralPanelState.currentQuery = query;
        m_structuralPanelState.lastResults = collectCurrentModelRows();
        updateStructuralNavigationButtons();
        if (rowCount) {
            *rowCount = rows;
        }
        return true;
    }

    if (lower.startsWith(QStringLiteral("snapshots:"))) {
        const QString targetHint = query.mid(QStringLiteral("snapshots:").size()).trimmed();
        QString localError;
        if (!resolveRootAndTarget(targetHint, &m_structuralRootPath, &m_structuralTargetPath, &localError)) {
            setError(localError);
            return false;
        }
        if (m_rootEdit) {
            m_rootEdit->setText(m_structuralRootPath);
        }
        if (!activatePanel(1)) {
            setError(QStringLiteral("structural_panel_unavailable"));
            return false;
        }
        int rows = 0;
        if (!loadStructuralSnapshotView(&localError, &rows)) {
            setError(localError);
            return false;
        }
        if (pushHistory) {
            pushStructuralQueryHistory(query);
        }
        m_structuralPanelState.currentQuery = query;
        m_structuralPanelState.lastResults = collectCurrentModelRows();
        updateStructuralNavigationButtons();
        if (rowCount) {
            *rowCount = rows;
        }
        return true;
    }

    if (lower.startsWith(QStringLiteral("references:")) || lower.startsWith(QStringLiteral("usedby:"))) {
        const bool referencesMode = lower.startsWith(QStringLiteral("references:"));
        const QString prefix = referencesMode ? QStringLiteral("references:") : QStringLiteral("usedby:");
        const QString targetHint = query.mid(prefix.size()).trimmed();
        QString localError;
        if (!resolveRootAndTarget(targetHint, &m_structuralRootPath, &m_structuralTargetPath, &localError)) {
            setError(localError);
            return false;
        }
        if (m_rootEdit) {
            m_rootEdit->setText(m_structuralRootPath);
        }
        if (!activatePanel(3)) {
            setError(QStringLiteral("structural_panel_unavailable"));
            return false;
        }

        int rows = 0;
        const QueryGraphMode graphMode = referencesMode ? QueryGraphMode::References : QueryGraphMode::UsedBy;
        if (!loadStructuralReferenceView(graphMode, &localError, &rows)) {
            setError(localError);
            return false;
        }
        if (pushHistory) {
            pushStructuralQueryHistory(query);
        }
        m_structuralPanelState.currentQuery = query;
        m_structuralPanelState.lastResults = collectCurrentModelRows();
        updateStructuralNavigationButtons();
        if (rowCount) {
            *rowCount = rows;
        }
        return true;
    }

    if (lower.startsWith(QStringLiteral("diff:"))) {
        const QString payload = query.mid(QStringLiteral("diff:").size()).trimmed();
        const QStringList tokens = payload.split(':', Qt::KeepEmptyParts);
        if (tokens.size() != 2) {
            setError(QStringLiteral("invalid_diff_query_expected=diff:<snapshotA>:<snapshotB>"));
            return false;
        }

        bool oldOk = false;
        bool newOk = false;
        const qint64 oldSnapshotId = tokens.at(0).trimmed().toLongLong(&oldOk);
        const qint64 newSnapshotId = tokens.at(1).trimmed().toLongLong(&newOk);
        if (!oldOk || !newOk || oldSnapshotId <= 0 || newSnapshotId <= 0 || oldSnapshotId == newSnapshotId) {
            setError(QStringLiteral("invalid_diff_snapshot_ids"));
            return false;
        }

        QString localError;
        if (!resolveRootAndTarget(QString(), &m_structuralRootPath, &m_structuralTargetPath, &localError)) {
            setError(localError);
            return false;
        }
        if (m_rootEdit) {
            m_rootEdit->setText(m_structuralRootPath);
        }
        if (!activatePanel(2)) {
            setError(QStringLiteral("structural_panel_unavailable"));
            return false;
        }

        int rows = 0;
        int added = 0;
        int removed = 0;
        int changed = 0;
        if (!loadStructuralDiffView(oldSnapshotId,
                                    newSnapshotId,
                                    &localError,
                                    &rows,
                                    &added,
                                    &removed,
                                    &changed)) {
            setError(localError);
            return false;
        }

        if (pushHistory) {
            pushStructuralQueryHistory(query);
        }
        m_structuralPanelState.currentQuery = query;
        m_structuralPanelState.lastResults = collectCurrentModelRows();
        updateStructuralNavigationButtons();

        if (rowCount) {
            *rowCount = rows;
        }
        return true;
    }

    return false;
}

bool MainWindow::navigateFromCurrentModelRow(int rowIndex, QString* navigatedPathOut)
{
    if (rowIndex < 0 || rowIndex >= m_proxyModel.rowCount()) {
        return false;
    }

    const QModelIndex proxyIndex = m_proxyModel.index(rowIndex, 0);
    if (!proxyIndex.isValid()) {
        return false;
    }

    const QModelIndex sourceIndex = m_proxyModel.mapToSource(proxyIndex);
    QString candidatePath = selectedPath(sourceIndex);
    const int snapshotSuffix = candidatePath.indexOf(QStringLiteral("#snapshot_"));
    if (snapshotSuffix >= 0) {
        candidatePath = candidatePath.left(snapshotSuffix);
    }

    QFileInfo info(candidatePath);
    if (!info.exists()) {
        return false;
    }

    const QString destination = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    if (!isNavigablePath(destination)) {
        return false;
    }

    navigateToDirectory(destination);
    if (navigatedPathOut) {
        *navigatedPathOut = destination;
    }
    return true;
}

QStringList MainWindow::collectCurrentModelRows(int maxRows) const
{
    QStringList rows;
    const int count = qMin(maxRows, m_proxyModel.rowCount());
    rows.reserve(count);
    for (int i = 0; i < count; ++i) {
        const QString c0 = m_proxyModel.data(m_proxyModel.index(i, 0), Qt::DisplayRole).toString();
        const QString c1 = m_proxyModel.data(m_proxyModel.index(i, 1), Qt::DisplayRole).toString();
        const QString c2 = m_proxyModel.data(m_proxyModel.index(i, 2), Qt::DisplayRole).toString();
        const QString c3 = m_proxyModel.data(m_proxyModel.index(i, 3), Qt::DisplayRole).toString();
        const QString c4 = m_proxyModel.data(m_proxyModel.index(i, 4), Qt::DisplayRole).toString();
        rows << QStringLiteral("row[%1] col0=%2 col1=%3 col2=%4 col3=%5 col4=%6")
                    .arg(i)
                    .arg(c0)
                    .arg(c1)
                    .arg(c2)
                    .arg(c3)
                    .arg(c4);
    }
    return rows;
}

bool MainWindow::triggerHistorySnapshotPanelForTesting(const QString& rootPath,
                                                       const QString& selectedFilePath,
                                                       qint64 oldSnapshotId,
                                                       qint64 newSnapshotId,
                                                       int* historyRowCount,
                                                       int* snapshotRowCount,
                                                       int* diffRowCount,
                                                       QStringList* historyRowsOut,
                                                       QStringList* snapshotRowsOut,
                                                       QStringList* diffRowsOut,
                                                       QString* navigationPathOut,
                                                       QString* errorText)
{
    if (!m_structuralPanelDock || !m_structuralTabWidget) {
        if (errorText) {
            *errorText = QStringLiteral("structural_panel_not_initialized");
        }
        return false;
    }

    setActionContext({selectedFilePath}, QStringLiteral("panel_smoke"));
    m_structuralRootPath = QDir::fromNativeSeparators(QDir::cleanPath(rootPath));
    m_structuralTargetPath = QDir::fromNativeSeparators(QDir::cleanPath(selectedFilePath));
    if (m_rootEdit) {
        m_rootEdit->setText(m_structuralRootPath);
    }

    updateStructuralPanelContextLabel();
    m_structuralPanelDock->show();
    m_structuralPanelDock->raise();

    QString localError;
    int historyRows = 0;
    m_structuralTabWidget->setCurrentIndex(0);
    if (!loadStructuralHistoryView(&localError, &historyRows)) {
        if (errorText) {
            *errorText = QStringLiteral("history_load_failed:%1").arg(localError);
        }
        return false;
    }
    if (historyRowsOut) {
        *historyRowsOut = collectCurrentModelRows();
    }

    int snapshotRows = 0;
    m_structuralTabWidget->setCurrentIndex(1);
    if (!loadStructuralSnapshotView(&localError, &snapshotRows)) {
        if (errorText) {
            *errorText = QStringLiteral("snapshot_load_failed:%1").arg(localError);
        }
        return false;
    }
    if (snapshotRowsOut) {
        *snapshotRowsOut = collectCurrentModelRows();
    }

    if (!refreshStructuralSnapshotSelectors(&localError)) {
        if (errorText) {
            *errorText = QStringLiteral("snapshot_selector_load_failed:%1").arg(localError);
        }
        return false;
    }

    const int oldIndex = m_structuralOldSnapshotCombo ? m_structuralOldSnapshotCombo->findData(oldSnapshotId) : -1;
    const int newIndex = m_structuralNewSnapshotCombo ? m_structuralNewSnapshotCombo->findData(newSnapshotId) : -1;
    if (oldIndex < 0 || newIndex < 0) {
        if (errorText) {
            *errorText = QStringLiteral("snapshot_ids_not_available old=%1 new=%2").arg(oldSnapshotId).arg(newSnapshotId);
        }
        return false;
    }
    m_structuralOldSnapshotCombo->setCurrentIndex(oldIndex);
    m_structuralNewSnapshotCombo->setCurrentIndex(newIndex);

    int diffRows = 0;
    int added = 0;
    int removed = 0;
    int changed = 0;
    m_structuralTabWidget->setCurrentIndex(2);
    if (!loadStructuralDiffView(oldSnapshotId,
                                newSnapshotId,
                                &localError,
                                &diffRows,
                                &added,
                                &removed,
                                &changed)) {
        if (errorText) {
            *errorText = QStringLiteral("diff_load_failed:%1").arg(localError);
        }
        return false;
    }
    if (diffRowsOut) {
        *diffRowsOut = collectCurrentModelRows();
    }

    QString navigatedPath;
    const bool navigationOk = navigateFromCurrentModelRow(0, &navigatedPath);
    if (!navigationOk) {
        if (errorText) {
            *errorText = QStringLiteral("navigation_from_diff_row_failed");
        }
        return false;
    }

    if (navigationPathOut) {
        *navigationPathOut = navigatedPath;
    }
    if (historyRowCount) {
        *historyRowCount = historyRows;
    }
    if (snapshotRowCount) {
        *snapshotRowCount = snapshotRows;
    }
    if (diffRowCount) {
        *diffRowCount = diffRows;
    }
    return (historyRows > 0) && (snapshotRows > 0) && (diffRows > 0);
}

bool MainWindow::triggerReferencePanelForTesting(const QString& rootPath,
                                                 const QString& selectedFilePath,
                                                 int* referencesRowCount,
                                                 int* usedByRowCount,
                                                 QStringList* referencesRowsOut,
                                                 QStringList* usedByRowsOut,
                                                 QString* navigationPathOut,
                                                 QString* errorText)
{
    if (!m_structuralPanelDock || !m_structuralTabWidget) {
        if (errorText) {
            *errorText = QStringLiteral("structural_panel_not_initialized");
        }
        return false;
    }

    setActionContext({selectedFilePath}, QStringLiteral("reference_panel_smoke"));
    m_structuralRootPath = QDir::fromNativeSeparators(QDir::cleanPath(rootPath));
    m_structuralTargetPath = QDir::fromNativeSeparators(QDir::cleanPath(selectedFilePath));
    if (m_rootEdit) {
        m_rootEdit->setText(m_structuralRootPath);
    }

    updateStructuralPanelContextLabel();
    m_structuralPanelDock->show();
    m_structuralPanelDock->raise();
    m_structuralTabWidget->setCurrentIndex(3);

    QString localError;
    int referenceRows = 0;
    if (!loadStructuralReferenceView(QueryGraphMode::References, &localError, &referenceRows)) {
        if (errorText) {
            *errorText = QStringLiteral("references_load_failed:%1").arg(localError);
        }
        return false;
    }
    if (referencesRowsOut) {
        *referencesRowsOut = collectCurrentModelRows();
    }

    int usedByRows = 0;
    if (!loadStructuralReferenceView(QueryGraphMode::UsedBy, &localError, &usedByRows)) {
        if (errorText) {
            *errorText = QStringLiteral("usedby_load_failed:%1").arg(localError);
        }
        return false;
    }
    if (usedByRowsOut) {
        *usedByRowsOut = collectCurrentModelRows();
    }

    QString navigatedPath;
    if (!navigateFromCurrentModelRow(0, &navigatedPath)) {
        if (errorText) {
            *errorText = QStringLiteral("navigation_from_reference_row_failed");
        }
        return false;
    }

    if (referencesRowCount) {
        *referencesRowCount = referenceRows;
    }
    if (usedByRowCount) {
        *usedByRowCount = usedByRows;
    }
    if (navigationPathOut) {
        *navigationPathOut = navigatedPath;
    }
    return referenceRows > 0 && usedByRows > 0 && !navigatedPath.isEmpty();
}

bool MainWindow::triggerStructuralQueryDispatchForTesting(const QString& rootPath,
                                                          const QString& queryText,
                                                          int* activeTabIndex,
                                                          QString* activeTabLabel,
                                                          int* rowCount,
                                                          QStringList* rowsOut,
                                                          QString* navigationPathOut,
                                                          QString* errorText)
{
    if (!m_structuralPanelDock || !m_structuralTabWidget) {
        if (errorText) {
            *errorText = QStringLiteral("structural_panel_not_initialized");
        }
        return false;
    }

    if (m_rootEdit) {
        m_rootEdit->setText(QDir::fromNativeSeparators(QDir::cleanPath(rootPath)));
    }
    setActionContext({QDir(rootPath).filePath(QStringLiteral("src/main.cpp"))}, QStringLiteral("structural_query_smoke"));

    QString dispatchError;
    int dispatchTab = -1;
    int dispatchRows = 0;
    const bool handled = dispatchQueryToExistingPanel(queryText, &dispatchError, &dispatchTab, &dispatchRows, true, false);
    if (!handled) {
        if (errorText) {
            *errorText = dispatchError.isEmpty() ? QStringLiteral("query_not_structural") : dispatchError;
        }
        if (activeTabIndex) {
            *activeTabIndex = dispatchTab;
        }
        if (activeTabLabel) {
            *activeTabLabel = (m_structuralTabWidget && dispatchTab >= 0)
                ? m_structuralTabWidget->tabText(dispatchTab)
                : QString();
        }
        if (rowCount) {
            *rowCount = dispatchRows;
        }
        return false;
    }

    const int tabIndex = m_structuralTabWidget->currentIndex();
    const QString tabLabel = m_structuralTabWidget->tabText(tabIndex);
    const int rows = m_proxyModel.rowCount();

    if (activeTabIndex) {
        *activeTabIndex = tabIndex;
    }
    if (activeTabLabel) {
        *activeTabLabel = tabLabel;
    }
    if (rowCount) {
        *rowCount = rows;
    }
    if (rowsOut) {
        *rowsOut = collectCurrentModelRows();
    }

    QString navigationPath;
    if (rows > 0) {
        navigateFromCurrentModelRow(0, &navigationPath);
    }
    if (navigationPathOut) {
        *navigationPathOut = navigationPath;
    }

    if (errorText) {
        *errorText = (rows <= 0)
            ? QStringLiteral("structural_query_returned_zero_rows")
            : QString();
    }

    return rows > 0;
}

bool MainWindow::triggerStructuralBackForTesting(int* activeTabIndex,
                                                 QString* activeTabLabel,
                                                 int* rowCount,
                                                 QString* currentQueryOut,
                                                 int* historySizeOut,
                                                 int* historyIndexOut,
                                                 QString* errorText)
{
    int tabIndex = -1;
    int rows = 0;
    const bool ok = navigateStructuralBack(errorText, &tabIndex, &rows);
    if (activeTabIndex) {
        *activeTabIndex = tabIndex;
    }
    if (activeTabLabel) {
        *activeTabLabel = (m_structuralTabWidget && tabIndex >= 0) ? m_structuralTabWidget->tabText(tabIndex) : QString();
    }
    if (rowCount) {
        *rowCount = rows;
    }
    if (currentQueryOut) {
        *currentQueryOut = m_structuralPanelState.currentQuery;
    }
    if (historySizeOut) {
        *historySizeOut = m_structuralPanelState.queryHistory.size();
    }
    if (historyIndexOut) {
        *historyIndexOut = m_structuralPanelState.queryHistoryIndex;
    }
    return ok;
}

bool MainWindow::triggerStructuralForwardForTesting(int* activeTabIndex,
                                                    QString* activeTabLabel,
                                                    int* rowCount,
                                                    QString* currentQueryOut,
                                                    int* historySizeOut,
                                                    int* historyIndexOut,
                                                    QString* errorText)
{
    int tabIndex = -1;
    int rows = 0;
    const bool ok = navigateStructuralForward(errorText, &tabIndex, &rows);
    if (activeTabIndex) {
        *activeTabIndex = tabIndex;
    }
    if (activeTabLabel) {
        *activeTabLabel = (m_structuralTabWidget && tabIndex >= 0) ? m_structuralTabWidget->tabText(tabIndex) : QString();
    }
    if (rowCount) {
        *rowCount = rows;
    }
    if (currentQueryOut) {
        *currentQueryOut = m_structuralPanelState.currentQuery;
    }
    if (historySizeOut) {
        *historySizeOut = m_structuralPanelState.queryHistory.size();
    }
    if (historyIndexOut) {
        *historyIndexOut = m_structuralPanelState.queryHistoryIndex;
    }
    return ok;
}

bool MainWindow::triggerStructuralRefreshForTesting(int* activeTabIndex,
                                                    QString* activeTabLabel,
                                                    int* rowCount,
                                                    QString* currentQueryOut,
                                                    int* historySizeOut,
                                                    int* historyIndexOut,
                                                    QString* errorText)
{
    int tabIndex = -1;
    int rows = 0;
    const bool ok = refreshStructuralCurrentQuery(errorText, &tabIndex, &rows);
    if (activeTabIndex) {
        *activeTabIndex = tabIndex;
    }
    if (activeTabLabel) {
        *activeTabLabel = (m_structuralTabWidget && tabIndex >= 0) ? m_structuralTabWidget->tabText(tabIndex) : QString();
    }
    if (rowCount) {
        *rowCount = rows;
    }
    if (currentQueryOut) {
        *currentQueryOut = m_structuralPanelState.currentQuery;
    }
    if (historySizeOut) {
        *historySizeOut = m_structuralPanelState.queryHistory.size();
    }
    if (historyIndexOut) {
        *historyIndexOut = m_structuralPanelState.queryHistoryIndex;
    }
    return ok;
}

quintptr MainWindow::structuralPanelInstanceTokenForTesting() const
{
    return reinterpret_cast<quintptr>(m_structuralPanelDock);
}

int MainWindow::structuralQueryHistorySizeForTesting() const
{
    return m_structuralPanelState.queryHistory.size();
}

int MainWindow::structuralQueryHistoryIndexForTesting() const
{
    return m_structuralPanelState.queryHistoryIndex;
}

QString MainWindow::structuralCurrentQueryForTesting() const
{
    return m_structuralPanelState.currentQuery;
}

void MainWindow::onActionCopyPath()
{
    const QStringList paths = actionPathsForCurrentContext();
    if (paths.isEmpty()) {
        logUiAction(QStringLiteral("Copy Path"), QStringLiteral("onActionCopyPath"), QStringLiteral("error"), QString(), QStringLiteral("empty_selection"));
        return;
    }
    copyPathListToClipboard(paths);
    logUiAction(QStringLiteral("Copy Path"), QStringLiteral("onActionCopyPath"), QStringLiteral("ok"));
}

void MainWindow::onActionRename()
{
    const QString path = primaryActionPathForCurrentContext();
    if (path.isEmpty()) {
        logUiAction(QStringLiteral("Rename"), QStringLiteral("onActionRename"), QStringLiteral("error"), QString(), QStringLiteral("empty_selection"));
        return;
    }
    const bool ok = renamePath(path);
    logUiAction(QStringLiteral("Rename"), QStringLiteral("onActionRename"), ok ? QStringLiteral("ok") : QStringLiteral("failed"));
}

void MainWindow::onActionPinFavorite()
{
    const QString path = primaryActionPathForCurrentContext();
    if (path.isEmpty()) {
        logUiAction(QStringLiteral("Pin Favorite"), QStringLiteral("onActionPinFavorite"), QStringLiteral("error"), QString(), QStringLiteral("empty_selection"));
        return;
    }
    addFavoritePath(path);
    logUiAction(QStringLiteral("Pin Favorite"), QStringLiteral("onActionPinFavorite"), QStringLiteral("ok"));
}

void MainWindow::onActionUnpinFavorite()
{
    const QString path = primaryActionPathForCurrentContext();
    if (path.isEmpty()) {
        logUiAction(QStringLiteral("Unpin Favorite"), QStringLiteral("onActionUnpinFavorite"), QStringLiteral("error"), QString(), QStringLiteral("empty_selection"));
        return;
    }
    removeFavoritePath(path);
    logUiAction(QStringLiteral("Unpin Favorite"), QStringLiteral("onActionUnpinFavorite"), QStringLiteral("ok"));
}

void MainWindow::onViewModeChanged(int index)
{
    if (index < 0 || index > 2) {
        return;
    }

    m_viewModeController.setModeFromIndex(index);
    const FileViewMode requestedMode = m_viewModeController.toFileViewMode();
    if (requestedMode == m_viewMode) {
        return;
    }

    m_viewMode = requestedMode;
    appendRuntimeLog(QStringLiteral("view_mode_changed mode=%1")
                         .arg(index));
    onRescan();
}

void MainWindow::onNavigateBack()
{
    if (m_navigationIndex <= 0 || m_navigationIndex >= m_navigationHistory.size()) {
        return;
    }
    m_navigationIndex -= 1;
    navigateToDirectory(m_navigationHistory[m_navigationIndex], false);
}

void MainWindow::onNavigateForward()
{
    if (m_navigationIndex < 0 || m_navigationIndex >= (m_navigationHistory.size() - 1)) {
        return;
    }
    m_navigationIndex += 1;
    navigateToDirectory(m_navigationHistory[m_navigationIndex], false);
}

void MainWindow::onNavigateUp()
{
    const QString current = m_rootEdit ? m_rootEdit->text().trimmed() : QString();
    if (current.isEmpty()) {
        return;
    }

    if (PathUtils::isArchiveVirtualPath(current)) {
        const QString parentPath = PathUtils::archiveVirtualParentPath(current);
        if (!parentPath.isEmpty()) {
            navigateToDirectory(parentPath);
        }
        return;
    }

    if (PathUtils::isArchivePath(current)) {
        const QString parentPath = QFileInfo(current).absolutePath();
        if (!parentPath.isEmpty()) {
            navigateToDirectory(parentPath);
        }
        return;
    }

    QDir dir(current);
    if (!dir.cdUp()) {
        return;
    }
    navigateToDirectory(dir.absolutePath());
}

void MainWindow::rebuildSidebar()
{
    if (!m_sidebarTree) {
        return;
    }

    m_sidebarTree->clear();

    m_favoritesRootItem = new QTreeWidgetItem(QStringList() << QStringLiteral("Favorites"));
    m_favoritesRootItem->setData(0, Qt::UserRole, QString());
    m_sidebarTree->addTopLevelItem(m_favoritesRootItem);

    for (const QString& favoritePath : m_favorites) {
        QTreeWidgetItem* item = new QTreeWidgetItem(QStringList() << displayNameForPath(favoritePath));
        item->setData(0, Qt::UserRole, favoritePath);
        item->setToolTip(0, favoritePath);
        m_favoritesRootItem->addChild(item);
    }

    m_standardRootItem = new QTreeWidgetItem(QStringList() << QStringLiteral("Standard Locations"));
    m_standardRootItem->setData(0, Qt::UserRole, QString());
    m_sidebarTree->addTopLevelItem(m_standardRootItem);

    const QString desktop = normalizedDirectoryPath(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
    const QString documents = normalizedDirectoryPath(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    const QString downloads = normalizedDirectoryPath(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    const QList<QPair<QString, QString>> standardItems = {
        {QStringLiteral("Desktop"), desktop},
        {QStringLiteral("Documents"), documents},
        {QStringLiteral("Downloads"), downloads},
    };

    for (const auto& entry : standardItems) {
        if (entry.second.isEmpty()) {
            continue;
        }
        QTreeWidgetItem* item = new QTreeWidgetItem(QStringList() << entry.first);
        item->setData(0, Qt::UserRole, entry.second);
        item->setToolTip(0, entry.second);
        m_standardRootItem->addChild(item);
    }

    m_favoritesRootItem->setExpanded(true);
    m_standardRootItem->setExpanded(true);
}

void MainWindow::loadFavorites()
{
    m_favorites.clear();
    const QString path = favoritesConfigPath();
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    const QJsonObject root = doc.object();
    const QJsonArray items = root.value(QStringLiteral("favorites")).toArray();
    for (const QJsonValue& value : items) {
        const QString normalized = normalizedDirectoryPath(value.toString());
        if (normalized.isEmpty()) {
            continue;
        }
        if (!isFavoritePath(normalized)) {
            m_favorites.push_back(normalized);
        }
    }
}

void MainWindow::saveFavorites() const
{
    const QString configFile = favoritesConfigPath();
    const QFileInfo configInfo(configFile);
    QDir().mkpath(configInfo.absolutePath());

    QJsonArray items;
    for (const QString& favorite : m_favorites) {
        items.append(favorite);
    }

    QJsonObject root;
    root.insert(QStringLiteral("favorites"), items);

    QSaveFile out(configFile);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    out.commit();
}

void MainWindow::addFavoritePath(const QString& path)
{
    const QString normalized = normalizedDirectoryPath(path);
    if (normalized.isEmpty()) {
        return;
    }

    const QFileInfo fileInfo(normalized);
    if (!isInternalNavigableDirectory(fileInfo)) {
        appendRuntimeLog(QStringLiteral("favorite_add_rejected path=%1").arg(normalized));
        return;
    }
    if (isFavoritePath(normalized)) {
        return;
    }

    m_favorites.push_back(normalized);
    saveFavorites();
    rebuildSidebar();
    appendRuntimeLog(QStringLiteral("favorite_added path=%1 config=%2").arg(normalized, favoritesConfigPath()));
}

void MainWindow::removeFavoritePath(const QString& path)
{
    const QString normalized = normalizedDirectoryPath(path);
    if (normalized.isEmpty()) {
        return;
    }

    for (qsizetype i = 0; i < m_favorites.size(); ++i) {
        if (QString::compare(m_favorites[i], normalized, Qt::CaseInsensitive) == 0) {
            m_favorites.removeAt(i);
            saveFavorites();
            rebuildSidebar();
            appendRuntimeLog(QStringLiteral("favorite_removed path=%1 config=%2").arg(normalized, favoritesConfigPath()));
            return;
        }
    }
}

bool MainWindow::isFavoritePath(const QString& path) const
{
    for (const QString& favorite : m_favorites) {
        if (QString::compare(favorite, path, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

QString MainWindow::displayNameForPath(const QString& path) const
{
    const QString normalized = normalizedDirectoryPath(path);
    const QString desktop = normalizedDirectoryPath(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
    if (!desktop.isEmpty() && QString::compare(normalized, desktop, Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Desktop");
    }

    const QString documents = normalizedDirectoryPath(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    if (!documents.isEmpty() && QString::compare(normalized, documents, Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Documents");
    }

    const QString downloads = normalizedDirectoryPath(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    if (!downloads.isEmpty() && QString::compare(normalized, downloads, Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Downloads");
    }

    const QFileInfo info(normalized);
    const QString name = info.fileName().trimmed();
    if (!name.isEmpty()) {
        return name;
    }
    return normalized;
}

QString MainWindow::normalizedDirectoryPath(const QString& path) const
{
    if (path.trimmed().isEmpty()) {
        return QString();
    }
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString MainWindow::favoritesConfigPath() const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (base.isEmpty()) {
        return QDir::cleanPath(QDir::home().filePath(QStringLiteral(".ngksfilevisionary/favorites.json")));
    }
    return QDir::cleanPath(QDir(base).filePath(QStringLiteral("favorites.json")));
}

QStringList MainWindow::selectedPaths() const
{
    if (!m_treeView) {
        return {};
    }

    QStringList paths;
    const QModelIndexList indexes = m_treeView->selectionModel()->selectedRows();
    for (const QModelIndex& proxyIndex : indexes) {
        const QModelIndex sourceIndex = m_proxyModel.mapToSource(proxyIndex);
        const QString path = selectedPath(sourceIndex);
        if (!path.isEmpty()) {
            paths.push_back(path);
        }
    }

    if (paths.isEmpty()) {
        const QModelIndex proxyIndex = m_treeView->currentIndex();
        if (proxyIndex.isValid()) {
            const QModelIndex sourceIndex = m_proxyModel.mapToSource(proxyIndex);
            const QString path = selectedPath(sourceIndex);
            if (!path.isEmpty()) {
                paths.push_back(path);
            }
        }
    }

    paths.removeDuplicates();
    return paths;
}

bool MainWindow::isArchivePath(const QString& path) const
{
    return PathUtils::isArchivePath(path);
}

void MainWindow::copyPathListToClipboard(const QStringList& paths) const
{
    if (QClipboard* clipboard = QApplication::clipboard()) {
        clipboard->setText(paths.join('\n'));
    }
}

void MainWindow::showPropertiesDialog(const QStringList& paths)
{
    if (paths.isEmpty()) {
        return;
    }

    quint64 totalSize = 0;
    int folders = 0;
    int archives = 0;
    QStringList lines;
    for (const QString& path : paths) {
        const QFileInfo info(path);
        if (info.isDir()) {
            folders += 1;
        } else {
            totalSize += static_cast<quint64>(info.size());
        }
        if (isArchivePath(path)) {
            archives += 1;
        }

        lines << QStringLiteral("Name: %1").arg(info.fileName())
              << QStringLiteral("Path: %1").arg(info.absoluteFilePath())
              << QStringLiteral("Type: %1").arg(info.isDir() ? QStringLiteral("Folder") : QStringLiteral("File"))
              << QStringLiteral("Size: %1").arg(info.isDir() ? QStringLiteral("-") : QString::number(info.size()))
              << QStringLiteral("Modified: %1").arg(info.lastModified().toString(Qt::ISODate))
              << QStringLiteral("Archive: %1").arg(isArchivePath(path) ? QStringLiteral("Yes") : QStringLiteral("No"))
              << QStringLiteral("Favorite: %1").arg(isFavoritePath(path) ? QStringLiteral("Yes") : QStringLiteral("No"))
              << QString();
    }

    if (paths.size() > 1) {
        lines.prepend(QStringLiteral("Selection Summary: count=%1 folders=%2 files=%3 archives=%4 total_file_size=%5")
                          .arg(paths.size())
                          .arg(folders)
                          .arg(paths.size() - folders)
                          .arg(archives)
                          .arg(totalSize));
        lines.prepend(QString());
    }

    QMessageBox::information(this, QStringLiteral("Properties"), lines.join('\n'));
}

bool MainWindow::renamePath(const QString& path)
{
    const QFileInfo info(path);
    const QString newName = QInputDialog::getText(this,
                                                  QStringLiteral("Rename"),
                                                  QStringLiteral("New name:"),
                                                  QLineEdit::Normal,
                                                  info.fileName());
    if (newName.trimmed().isEmpty() || newName == info.fileName()) {
        return false;
    }

    const QString destPath = QDir(info.absolutePath()).filePath(newName);
    bool ok = false;
    if (info.isDir()) {
        ok = QDir().rename(path, destPath);
    } else {
        QFile file(path);
        ok = file.rename(destPath);
    }

    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("Rename"), QStringLiteral("Failed to rename item."));
        return false;
    }
    onRescan();
    return true;
}

bool MainWindow::deletePathsWithConfirm(const QStringList& paths)
{
    if (paths.isEmpty()) {
        return false;
    }
    const auto reply = QMessageBox::question(this,
                                             QStringLiteral("Delete"),
                                             QStringLiteral("Delete %1 item(s)?").arg(paths.size()));
    if (reply != QMessageBox::Yes) {
        return false;
    }

    bool allOk = true;
    for (const QString& path : paths) {
        const QFileInfo info(path);
        bool ok = false;
        if (info.isDir()) {
            QDir dir(path);
            ok = dir.removeRecursively();
        } else {
            ok = QFile::remove(path);
        }
        allOk = allOk && ok;
    }
    if (!allOk) {
        QMessageBox::warning(this, QStringLiteral("Delete"), QStringLiteral("Some items could not be deleted."));
    }
    onRescan();
    return allOk;
}

bool MainWindow::copyMovePaths(const QStringList& paths, bool move)
{
    if (paths.isEmpty()) {
        return false;
    }

    const QString destination = QFileDialog::getExistingDirectory(this,
                                                                   move ? QStringLiteral("Move to") : QStringLiteral("Copy to"),
                                                                   currentRootPath());
    if (destination.isEmpty()) {
        return false;
    }

    bool allOk = true;
    for (const QString& path : paths) {
        const QFileInfo info(path);
        const QString targetPath = QDir(destination).filePath(info.fileName());
        bool ok = false;
        if (move) {
            if (info.isDir()) {
                ok = QDir().rename(path, targetPath);
            } else {
                QFile file(path);
                ok = file.rename(targetPath);
            }
        } else {
            if (info.isDir()) {
                ok = false;
            } else {
                ok = QFile::copy(path, targetPath);
            }
        }
        allOk = allOk && ok;
    }

    if (!allOk) {
        QMessageBox::warning(this,
                             move ? QStringLiteral("Move") : QStringLiteral("Copy"),
                             QStringLiteral("Some items could not be processed (directory copy is limited in this pass)."));
    }
    onRescan();
    return allOk;
}

bool MainWindow::duplicatePath(const QString& path)
{
    const QFileInfo info(path);
    if (info.isDir()) {
        QMessageBox::warning(this, QStringLiteral("Duplicate"), QStringLiteral("Folder duplication is not supported in this pass."));
        return false;
    }

    const QString duplicateName = info.completeBaseName() + QStringLiteral("_copy")
                                  + (info.suffix().isEmpty() ? QString() : QStringLiteral(".") + info.suffix());
    const QString targetPath = QDir(info.absolutePath()).filePath(duplicateName);
    const bool ok = QFile::copy(path, targetPath);
    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("Duplicate"), QStringLiteral("Failed to duplicate file."));
        return false;
    }
    onRescan();
    return true;
}

void MainWindow::openInTerminal(const QString& path)
{
    QProcess::startDetached(QStringLiteral("cmd.exe"), {QStringLiteral("/K"), QStringLiteral("cd /d \"%1\"").arg(QDir::toNativeSeparators(path))});
}

void MainWindow::openWithCode(const QString& path)
{
    QProcess::startDetached(QStringLiteral("code"), {path});
}

void MainWindow::openContainingFolder(const QString& path)
{
    QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,"), QDir::toNativeSeparators(path)});
}

void MainWindow::createArchiveFromPaths(const QStringList& paths, const QString& forcedExtension)
{
    if (paths.isEmpty()) {
        return;
    }

    QString suggested = QFileInfo(paths.first()).fileName();
    if (suggested.isEmpty()) {
        suggested = QStringLiteral("archive");
    }
    QString extension = forcedExtension;
    if (extension.isEmpty()) {
        extension = QStringLiteral(".zip");
    }
    if (!extension.startsWith('.')) {
        extension.prepend('.');
    }

    QString output;
    if (m_testMode && m_testScriptRunning && !forcedExtension.isEmpty()) {
        const QFileInfo firstInfo(paths.first());
        output = QDir(firstInfo.absolutePath()).filePath(firstInfo.completeBaseName() + extension);
    } else {
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Create Archive"));
        dialog.setObjectName(QStringLiteral("createArchiveDialog"));
        auto* layout = new QVBoxLayout(&dialog);
        auto* form = new QFormLayout();

        auto* formatCombo = new QComboBox(&dialog);
        formatCombo->setObjectName(QStringLiteral("archiveFormatCombo"));
        formatCombo->addItem(QStringLiteral("zip"), QStringLiteral(".zip"));
        formatCombo->addItem(QStringLiteral("7z"), QStringLiteral(".7z"));
        formatCombo->addItem(QStringLiteral("tar"), QStringLiteral(".tar"));

        auto* outputEdit = new QLineEdit(QDir(currentRootPath()).filePath(suggested + extension), &dialog);
        outputEdit->setObjectName(QStringLiteral("archiveOutputPathEdit"));
        auto* browseButton = new QPushButton(QStringLiteral("Browse"), &dialog);
        browseButton->setObjectName(QStringLiteral("archiveBrowseButton"));
        auto* outputRow = new QWidget(&dialog);
        auto* outputLayout = new QHBoxLayout(outputRow);
        outputLayout->setContentsMargins(0, 0, 0, 0);
        outputLayout->addWidget(outputEdit, 1);
        outputLayout->addWidget(browseButton);

        form->addRow(QStringLiteral("Format"), formatCombo);
        form->addRow(QStringLiteral("Output"), outputRow);
        layout->addLayout(form);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        if (QPushButton* okButton = buttons->button(QDialogButtonBox::Ok)) {
            okButton->setObjectName(QStringLiteral("archiveOkButton"));
        }
        if (QPushButton* cancelButton = buttons->button(QDialogButtonBox::Cancel)) {
            cancelButton->setObjectName(QStringLiteral("archiveCancelButton"));
        }
        layout->addWidget(buttons);

        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(browseButton, &QPushButton::clicked, &dialog, [&dialog, outputEdit]() {
            const QString chosen = QFileDialog::getSaveFileName(&dialog,
                                                                 QStringLiteral("Create Archive"),
                                                                 outputEdit->text(),
                                                                 QStringLiteral("Archives (*.zip *.7z *.tar);;All Files (*.*)"));
            if (!chosen.isEmpty()) {
                outputEdit->setText(chosen);
            }
        });

        int forcedIndex = -1;
        for (int i = 0; i < formatCombo->count(); ++i) {
            if (formatCombo->itemData(i).toString() == extension) {
                forcedIndex = i;
                break;
            }
        }
        if (forcedIndex >= 0) {
            formatCombo->setCurrentIndex(forcedIndex);
        }
        if (!forcedExtension.isEmpty()) {
            formatCombo->setEnabled(false);
        }

        if (dialog.exec() != QDialog::Accepted) {
            return;
        }

        output = outputEdit->text().trimmed();
        if (output.isEmpty()) {
            return;
        }

        const QString selectedExtension = formatCombo->currentData().toString();
        if (!output.endsWith(selectedExtension, Qt::CaseInsensitive)) {
            output += selectedExtension;
        }
    }

    QStringList args = {QStringLiteral("a"), output};
    for (const QString& path : paths) {
        args.push_back(path);
    }
    args << QStringLiteral("-y") << QStringLiteral("-mx=5") << QStringLiteral("-bsp1") << QStringLiteral("-bb1");
    runArchiveCommand(QStringLiteral("Create Archive"), args);
}

void MainWindow::extractArchive(const QString& archivePath, bool chooseDestination)
{
    QString dest = QFileInfo(archivePath).absolutePath();
    if (chooseDestination) {
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Extract To"));
        dialog.setObjectName(QStringLiteral("extractDialog"));
        auto* layout = new QVBoxLayout(&dialog);
        auto* form = new QFormLayout();

        auto* destinationEdit = new QLineEdit(dest, &dialog);
        destinationEdit->setObjectName(QStringLiteral("extractDestinationEdit"));
        auto* browseButton = new QPushButton(QStringLiteral("Browse"), &dialog);
        browseButton->setObjectName(QStringLiteral("extractBrowseButton"));

        auto* row = new QWidget(&dialog);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->addWidget(destinationEdit, 1);
        rowLayout->addWidget(browseButton);

        form->addRow(QStringLiteral("Destination"), row);
        layout->addLayout(form);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        if (QPushButton* okButton = buttons->button(QDialogButtonBox::Ok)) {
            okButton->setObjectName(QStringLiteral("extractOkButton"));
        }
        if (QPushButton* cancelButton = buttons->button(QDialogButtonBox::Cancel)) {
            cancelButton->setObjectName(QStringLiteral("extractCancelButton"));
        }
        layout->addWidget(buttons);

        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(browseButton, &QPushButton::clicked, &dialog, [&dialog, destinationEdit]() {
            const QString chosenDir = QFileDialog::getExistingDirectory(&dialog,
                                                                         QStringLiteral("Extract To"),
                                                                         destinationEdit->text());
            if (!chosenDir.isEmpty()) {
                destinationEdit->setText(chosenDir);
            }
        });

        if (dialog.exec() != QDialog::Accepted) {
            return;
        }

        dest = destinationEdit->text().trimmed();
        if (dest.isEmpty()) {
            return;
        }
    }

    QStringList args = {
        QStringLiteral("x"),
        archivePath,
        QStringLiteral("-o%1").arg(dest),
        QStringLiteral("-y"),
        QStringLiteral("-bsp1"),
        QStringLiteral("-bb1"),
    };
    runArchiveCommand(QStringLiteral("Extract Archive"), args);
}

void MainWindow::ensureArchiveProcessUi()
{
    if (!m_archiveProcess) {
        m_archiveProcess = new QProcess(this);
        connect(m_archiveProcess, &QProcess::readyReadStandardOutput, this, &MainWindow::onArchiveReadyRead);
        connect(m_archiveProcess, &QProcess::readyReadStandardError, this, &MainWindow::onArchiveReadyRead);
        connect(m_archiveProcess,
                qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                this,
                &MainWindow::onArchiveFinished);
    }
}

void MainWindow::runArchiveCommand(const QString& title, const QStringList& args)
{
    ensureArchiveProcessUi();
    if (m_archiveProcess->state() != QProcess::NotRunning) {
        QMessageBox::warning(this, QStringLiteral("Archive Operation"), QStringLiteral("Another archive operation is already running."));
        logUiAction(title, QStringLiteral("runArchiveCommand"), QStringLiteral("error"), QString(), QStringLiteral("process_busy"));
        return;
    }

    const QString sevenZip = resolveBundled7zaPath();
    if (!QFileInfo::exists(sevenZip)) {
        QMessageBox::warning(this, QStringLiteral("Archive Operation"), QStringLiteral("7za.exe was not found in third_party/7zip."));
        logUiAction(title, QStringLiteral("runArchiveCommand"), QStringLiteral("error"), QString(), QStringLiteral("missing_7za"));
        return;
    }

    m_archiveOperationTitle = title;
    m_archiveLogBuffer.clear();
    if (!m_archiveProgressDialog) {
        m_archiveProgressDialog = new QProgressDialog(title + QStringLiteral("..."), QStringLiteral("Cancel"), 0, 100, this);
        m_archiveProgressDialog->setWindowModality(Qt::WindowModal);
        m_archiveProgressDialog->setAutoClose(false);
        connect(m_archiveProgressDialog, &QProgressDialog::canceled, this, [this]() {
            if (m_archiveProcess && m_archiveProcess->state() != QProcess::NotRunning) {
                m_archiveProcess->kill();
            }
        });
    }
    m_archiveProgressDialog->setValue(0);
    m_archiveProgressDialog->setLabelText(title + QStringLiteral("..."));
    m_archiveProgressDialog->show();

    m_archiveProcess->start(sevenZip, args);
    logUiAction(title, QStringLiteral("runArchiveCommand"), QStringLiteral("started"));
    appendRuntimeLog(QStringLiteral("archive_command_started title=%1 cmd=%2 args=%3")
                         .arg(title, sevenZip, args.join(' ')));
}

void MainWindow::onArchiveReadyRead()
{
    if (!m_archiveProcess) {
        return;
    }

    const QString out = QString::fromLocal8Bit(m_archiveProcess->readAllStandardOutput());
    const QString err = QString::fromLocal8Bit(m_archiveProcess->readAllStandardError());
    m_archiveLogBuffer += out;
    m_archiveLogBuffer += err;

    const QRegularExpression re(QStringLiteral(R"((\d{1,3})%)"));
    const auto matches = re.globalMatch(out + err);
    int lastPercent = -1;
    auto iterator = matches;
    while (iterator.hasNext()) {
        const auto match = iterator.next();
        lastPercent = match.captured(1).toInt();
    }
    if (lastPercent >= 0 && m_archiveProgressDialog) {
        m_archiveProgressDialog->setValue(lastPercent);
    }
}

void MainWindow::onArchiveFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_archiveProgressDialog) {
        m_archiveProgressDialog->setValue(100);
        m_archiveProgressDialog->hide();
    }

    const bool ok = (exitStatus == QProcess::NormalExit && exitCode == 0);
    logUiAction(m_archiveOperationTitle,
                QStringLiteral("onArchiveFinished"),
                ok ? QStringLiteral("ok") : QStringLiteral("failed"),
                QString(),
                ok ? QString() : QStringLiteral("exit=%1").arg(exitCode));
    appendRuntimeLog(QStringLiteral("archive_command_finished title=%1 ok=%2 exit=%3")
                         .arg(m_archiveOperationTitle)
                         .arg(ok ? QStringLiteral("true") : QStringLiteral("false"))
                         .arg(exitCode));
    if (m_testMode && m_testScriptRunning) {
        // Keep scripted test-mode runs non-blocking and auditable via action trace only.
    } else if (!ok) {
        QMessageBox::warning(this,
                             m_archiveOperationTitle,
                             QStringLiteral("Archive operation failed.\n\n%1").arg(m_archiveLogBuffer.left(2000)));
    } else {
        QMessageBox::information(this, m_archiveOperationTitle, QStringLiteral("Archive operation completed."));
    }
    onRescan();
}

QString MainWindow::currentRootPath() const
{
    return m_rootEdit ? m_rootEdit->text().trimmed() : QString();
}

QString MainWindow::graphQueryTargetForPath(const QString& absolutePath) const
{
    const QString normalizedPath = QDir::cleanPath(absolutePath);
    const QString root = QDir::cleanPath(currentRootPath());
    if (root.isEmpty()) {
        return normalizedPath;
    }

    QDir rootDir(root);
    QString relative = QDir::cleanPath(rootDir.relativeFilePath(normalizedPath));
    const bool outsideRoot = relative.startsWith(QStringLiteral(".."));
    if (outsideRoot) {
        relative = normalizedPath;
    }

    return QDir::fromNativeSeparators(relative);
}

void MainWindow::executeGraphQueryFromSelection(QueryGraphMode graphMode, const QString& absolutePath)
{
    if (!m_queryBarWidget) {
        return;
    }

    const QString target = graphQueryTargetForPath(absolutePath);
    const QString prefix = (graphMode == QueryGraphMode::References)
                               ? QStringLiteral("references:")
                               : QStringLiteral("usedby:");
    const QString query = prefix + target;

    appendRuntimeLog(QStringLiteral("ui_graph_query_action mode=%1 target=%2 query=%3")
                         .arg(prefix.chopped(1))
                         .arg(target)
                         .arg(query));

    m_queryBarWidget->setQueryText(query);
    onQuerySubmitted(query);
}

void MainWindow::createNewFolderInCurrentRoot()
{
    const QString root = currentRootPath();
    if (root.isEmpty()) {
        return;
    }

    const QString name = QInputDialog::getText(this,
                                               QStringLiteral("New Folder"),
                                               QStringLiteral("Folder name:"),
                                               QLineEdit::Normal,
                                               QStringLiteral("New Folder"));
    if (name.trimmed().isEmpty()) {
        return;
    }
    QDir dir(root);
    if (!dir.mkdir(name)) {
        QMessageBox::warning(this, QStringLiteral("New Folder"), QStringLiteral("Failed to create folder."));
    }
    onRescan();
}

void MainWindow::createNewTextFileInCurrentRoot()
{
    const QString root = currentRootPath();
    if (root.isEmpty()) {
        return;
    }

    const QString name = QInputDialog::getText(this,
                                               QStringLiteral("New Text File"),
                                               QStringLiteral("File name:"),
                                               QLineEdit::Normal,
                                               QStringLiteral("NewFile.txt"));
    if (name.trimmed().isEmpty()) {
        return;
    }
    QFile file(QDir(root).filePath(name));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("New Text File"), QStringLiteral("Failed to create file."));
    }
    file.close();
    onRescan();
}

QString MainWindow::selectedPath(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return QString();
    }
    const QModelIndex pathIndex = m_fileModel.index(index.row(), 4, index.parent());
    return m_fileModel.data(pathIndex, Qt::DisplayRole).toString();
}

void MainWindow::appendRuntimeLog(const QString& message) const
{
    auto locateProofRoot = []() -> QDir {
        QStringList candidateRoots;
        candidateRoots << QDir::currentPath();
        candidateRoots << QCoreApplication::applicationDirPath();

        for (const QString& basePath : candidateRoots) {
            QDir dir(basePath);
            for (int i = 0; i < 8; ++i) {
                const QString proofPath = dir.filePath(QStringLiteral("_proof"));
                if (QDir(proofPath).exists()) {
                    return QDir(proofPath);
                }
                if (!dir.cdUp()) {
                    break;
                }
            }
        }
        return QDir();
    };

    QDir proofRoot = locateProofRoot();
    if (!proofRoot.exists()) {
        return;
    }

    QFileInfoList dirs;
    dirs = proofRoot.entryInfoList(
        QStringList() << QStringLiteral("ui_responsiveness_fix_*"),
        QDir::Dirs | QDir::NoDotAndDotDot,
        QDir::Name);
    if (dirs.isEmpty()) {
        dirs = proofRoot.entryInfoList(
            QStringList() << QStringLiteral("ui_launch_behavior_*"),
            QDir::Dirs | QDir::NoDotAndDotDot,
            QDir::Name);
    }
    if (dirs.isEmpty()) {
        return;
    }

    const QString logPath = dirs.last().absoluteFilePath() + QStringLiteral("/31_runtime_log.txt");
    QFile out(logPath);
    if (!out.open(QIODevice::Append | QIODevice::Text)) {
        return;
    }

    QTextStream stream(&out);
    stream << QDateTime::currentDateTime().toString(Qt::ISODate) << " | " << message << "\n";
}

void MainWindow::startScanNow()
{
    m_fileModel.clear();
    m_publishQueue.clear();
    m_publishTimer.stop();
    const QString root = m_rootEdit->text().trimmed();
    const bool archiveRoot = PathUtils::isArchivePath(root) || PathUtils::isArchiveVirtualPath(root);
    const QStringList extensions = PathUtils::splitExtensionsFilter(m_extensionFilterEdit->text());
    const QString search = m_searchEdit->text().trimmed();

    if (!archiveRoot && !ensureDirectoryModelReady()) {
        m_statusLabel->setText(QStringLiteral("Error: db_not_ready"));
        appendRuntimeLog(QStringLiteral("ui_query_error db_not_ready path=%1").arg(m_uiDbPath));
        return;
    }

    m_scanInProgress = true;
    m_activeScanId = ++m_nextScanId;
    m_scanEntryCount = 0;
    m_scanBatchCount = 0;
    m_scanEnumeratedCount = 0;
    m_statusLabel->setText(QStringLiteral("Enumerating..."));
    m_treeView->setSortingEnabled(false);

    QueryResult result;
    QString effectiveRoot = root;
    const bool queryActive = m_queryModeActive && !m_activeQueryString.trimmed().isEmpty();
    if (queryActive) {
        appendRuntimeLog(QStringLiteral("ui_querybar_begin scan_id=%1 root=%2 mode=%3 db=%4 query=%5")
                             .arg(m_activeScanId)
                             .arg(root)
                             .arg(static_cast<int>(m_viewModeController.mode()))
                             .arg(m_uiDbPath)
                             .arg(m_activeQueryString));

        if (!m_queryController) {
            m_scanInProgress = false;
            m_statusLabel->setText(QStringLiteral("Error: query_controller_not_ready"));
            appendRuntimeLog(QStringLiteral("ui_querybar_failed error=query_controller_not_ready"));
            return;
        }

        const QueryController::ExecutionResult queryExec = m_queryController->execute(
            m_activeQueryString,
            root,
            m_viewModeController.mode(),
            m_showHiddenCheck->isChecked(),
            m_showSystemCheck->isChecked(),
            currentQuerySortField(),
            m_treeView->header()->sortIndicatorOrder() == Qt::AscendingOrder);

        appendRuntimeLog(QStringLiteral("ui_querybar_parse parse_ok=%1 parse_error=%2 duplicate_policy=%3")
                             .arg(queryExec.parseOk ? QStringLiteral("true") : QStringLiteral("false"))
                             .arg(queryExec.parseError)
                             .arg(queryExec.parseResult.duplicatePolicy));

        if (!queryExec.parseOk) {
            m_scanInProgress = false;
            m_treeView->setSortingEnabled(true);
            m_statusLabel->setText(QStringLiteral("Query parse error: %1").arg(queryExec.parseError));
            appendRuntimeLog(QStringLiteral("ui_querybar_failed phase=parse error=%1").arg(queryExec.parseError));
            return;
        }

        effectiveRoot = queryExec.executionRoot;
        result = queryExec.queryResult;
        appendRuntimeLog(QStringLiteral("ui_querybar_execution query_ok=%1 rows=%2 execution_root=%3 query_execution_source=indexed_db_querycore_only")
                             .arg(result.ok ? QStringLiteral("true") : QStringLiteral("false"))
                             .arg(result.totalCount)
                             .arg(effectiveRoot));
    } else {
        appendRuntimeLog(QStringLiteral("ui_query_begin scan_id=%1 root=%2 mode=%3 db=%4 showHidden=%5 showSystem=%6 ext=%7 search=%8")
                             .arg(m_activeScanId)
                             .arg(root)
                             .arg(static_cast<int>(m_viewModeController.mode()))
                             .arg(m_uiDbPath)
                             .arg(m_showHiddenCheck->isChecked() ? QStringLiteral("true") : QStringLiteral("false"))
                             .arg(m_showSystemCheck->isChecked() ? QStringLiteral("true") : QStringLiteral("false"))
                             .arg(extensions.join(';'))
                             .arg(search));

        DirectoryModel::Request request;
        request.rootPath = root;
        request.mode = m_viewModeController.mode();
        request.includeHidden = m_showHiddenCheck->isChecked();
        request.includeSystem = m_showSystemCheck->isChecked();
        request.foldersFirst = true;
        request.extensionFilter = extensions.join(';');
        request.substringFilter = search;
        request.sortField = currentQuerySortField();
        request.ascending = m_treeView->header()->sortIndicatorOrder() == Qt::AscendingOrder;
        request.maxDepth = request.mode == ViewModeController::UiViewMode::Hierarchy ? 64 : -1;
        request.filesOnly = request.mode == ViewModeController::UiViewMode::Flat;

        result = m_directoryModel->query(request);
    }

    if (!result.ok) {
        m_scanInProgress = false;
        m_treeView->setSortingEnabled(true);
        m_statusLabel->setText(QStringLiteral("Error: %1").arg(result.errorText));
        appendRuntimeLog(QStringLiteral("ui_query_failed error=%1 query_mode=%2")
                             .arg(result.errorText)
                             .arg(queryActive ? QStringLiteral("true") : QStringLiteral("false")));
        return;
    }

    m_fileModel.setViewMode(m_viewModeController.toFileViewMode(), archiveRoot ? root : effectiveRoot);

    const QVector<FileEntry> rows = QueryResultAdapter::toFileEntries(result);
    m_publishQueue = rows;
    m_scanBatchCount = 1;
    m_scanEntryCount = static_cast<quint64>(rows.size());
    m_scanEnumeratedCount = static_cast<quint64>(rows.size());
    m_scanInProgress = false;
    if (!m_publishQueue.isEmpty()) {
        m_publishTimer.start();
    } else {
        m_treeView->setSortingEnabled(true);
    }

    m_statusLabel->setText(queryActive
                               ? QStringLiteral("Query complete: %1 items").arg(rows.size())
                               : QStringLiteral("Complete: %1 items").arg(rows.size()));
    appendRuntimeLog(QStringLiteral("ui_query_complete rows=%1 query_mode=%2")
                         .arg(rows.size())
                         .arg(queryActive ? QStringLiteral("true") : QStringLiteral("false")));

    if (m_viewModeController.mode() == ViewModeController::UiViewMode::Hierarchy) {
        m_treeView->expandAll();
    } else if (m_viewModeController.mode() == ViewModeController::UiViewMode::Standard) {
        m_treeView->collapseAll();
    }
}

bool MainWindow::ensureDirectoryModelReady()
{
    if (!m_directoryModel) {
        return false;
    }
    if (m_directoryModel->isReady()) {
        return true;
    }

    QString errorText;
    if (!m_directoryModel->initialize(m_uiDbPath, &errorText)) {
        appendRuntimeLog(QStringLiteral("directory_model_init_failed db=%1 error=%2").arg(m_uiDbPath, errorText));
        return false;
    }
    appendRuntimeLog(QStringLiteral("directory_model_ready db=%1").arg(m_uiDbPath));
    return true;
}

QString MainWindow::resolveUiDbPath() const
{
    const QStringList candidates = {
        QDir::cleanPath(QDir::current().filePath(QStringLiteral("debug/metastore_vie_p3.sqlite3"))),
        QDir::cleanPath(QDir::current().filePath(QStringLiteral("debug/metastore_vie_p2_main.sqlite3"))),
        QDir::cleanPath(QDir::current().filePath(QStringLiteral("debug/metastore_vie_p2.sqlite3"))),
        QDir::cleanPath(QDir::current().filePath(QStringLiteral("debug/metastore.sqlite3"))),
    };

    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return candidates.first();
}

QuerySortField MainWindow::currentQuerySortField() const
{
    const int column = m_treeView ? m_treeView->header()->sortIndicatorSection() : 0;
    switch (column) {
    case 2:
        return QuerySortField::Size;
    case 3:
        return QuerySortField::Modified;
    case 4:
        return QuerySortField::Path;
    case 0:
    default:
        return QuerySortField::Name;
    }
}

void MainWindow::onPublishTick()
{
    if (m_publishQueue.isEmpty()) {
        m_publishTimer.stop();
        m_treeView->setSortingEnabled(!m_scanInProgress);
        return;
    }

    const int chunkSize = qMin(256, m_publishQueue.size());
    QVector<FileEntry> chunk;
    chunk.reserve(chunkSize);
    for (int i = 0; i < chunkSize; ++i) {
        chunk.push_back(m_publishQueue[i]);
    }
    m_publishQueue.erase(m_publishQueue.begin(), m_publishQueue.begin() + chunkSize);

    m_fileModel.appendBatch(chunk);
    m_statusLabel->setText(QStringLiteral("Publishing... shown=%1 queued=%2")
                               .arg(m_fileModel.itemCount())
                               .arg(m_publishQueue.size()));
    if (m_viewMode == FileViewMode::FullHierarchy && (m_scanBatchCount % 10ULL) == 0ULL) {
        m_treeView->expandAll();
    }
}

bool MainWindow::isInternalNavigableDirectory(const QFileInfo& fileInfo) const
{
    if (!fileInfo.exists() || !fileInfo.isDir()) {
        return false;
    }
    if (fileInfo.isSymLink()) {
        return false;
    }
    const QString lowerName = fileInfo.fileName().toLower();
    if (lowerName.endsWith(QStringLiteral(".lnk"))) {
        return false;
    }
    return true;
}

bool MainWindow::isNavigablePath(const QString& path) const
{
    if (PathUtils::isArchiveVirtualPath(path)) {
        return true;
    }

    if (PathUtils::isArchivePath(path)) {
        const QFileInfo archiveInfo(path);
        return archiveInfo.exists() && archiveInfo.isFile();
    }

    return isInternalNavigableDirectory(QFileInfo(path));
}

void MainWindow::navigateToDirectory(const QString& path, bool pushHistory)
{
    const QFileInfo fileInfo(path);
    if (!isNavigablePath(path)) {
        appendRuntimeLog(QStringLiteral("navigate_rejected path=%1 exists=%2 is_dir=%3 is_link=%4")
                             .arg(path)
                             .arg(fileInfo.exists() ? QStringLiteral("true") : QStringLiteral("false"))
                             .arg(fileInfo.isDir() ? QStringLiteral("true") : QStringLiteral("false"))
                             .arg(fileInfo.isSymLink() ? QStringLiteral("true") : QStringLiteral("false")));
        return;
    }

    QString normalizedPath;
    if (PathUtils::isArchiveVirtualPath(path)) {
        QString archivePath;
        QString internalPath;
        if (PathUtils::splitArchiveVirtualPath(path, &archivePath, &internalPath)) {
            normalizedPath = PathUtils::buildArchiveVirtualPath(archivePath, internalPath);
        } else {
            normalizedPath = path;
        }
    } else if (PathUtils::isArchivePath(path)) {
        normalizedPath = fileInfo.absoluteFilePath();
    } else {
        normalizedPath = fileInfo.absoluteFilePath();
    }
    m_rootEdit->setText(normalizedPath);

    if (pushHistory) {
        while (m_navigationHistory.size() > (m_navigationIndex + 1)) {
            m_navigationHistory.removeLast();
        }
        if (m_navigationHistory.isEmpty() || QString::compare(m_navigationHistory.last(), normalizedPath, Qt::CaseInsensitive) != 0) {
            m_navigationHistory.push_back(normalizedPath);
        }
        m_navigationIndex = m_navigationHistory.size() - 1;
    }

    updateNavigationButtons();
    appendRuntimeLog(QStringLiteral("navigate_internal path=%1 push_history=%2")
                         .arg(normalizedPath)
                         .arg(pushHistory ? QStringLiteral("true") : QStringLiteral("false")));
    onRescan();
}

void MainWindow::updateNavigationButtons()
{
    if (m_backButton) {
        m_backButton->setEnabled(m_navigationIndex > 0);
    }
    if (m_forwardButton) {
        m_forwardButton->setEnabled(m_navigationIndex >= 0 && m_navigationIndex < (m_navigationHistory.size() - 1));
    }

    if (m_upButton && m_rootEdit) {
        const QString current = m_rootEdit->text().trimmed();
        bool canGoUp = false;
        if (PathUtils::isArchiveVirtualPath(current)) {
            canGoUp = !PathUtils::archiveVirtualParentPath(current).isEmpty();
        } else if (PathUtils::isArchivePath(current)) {
            canGoUp = !QFileInfo(current).absolutePath().isEmpty();
        } else {
            QDir dir(current);
            canGoUp = dir.exists() && dir.cdUp();
        }
        m_upButton->setEnabled(canGoUp);
    }
}

void MainWindow::onTreeSnapshotRequested(const QString& folderPath)
{
    const QFileInfo folderInfo(folderPath);
    if (!isInternalNavigableDirectory(folderInfo)) {
        logUiAction(QStringLiteral("Tree Snapshot"), QStringLiteral("onTreeSnapshotRequested"), QStringLiteral("error"), QString(), QStringLiteral("not_folder"));
        QMessageBox::warning(this,
                             QStringLiteral("Tree Snapshot"),
                             QStringLiteral("Tree Snapshot is available only for real filesystem folders."));
        return;
    }

    m_snapshotRootPath = folderInfo.absoluteFilePath();

    if (m_testMode && m_testScriptRunning) {
        m_snapshotOptions = TreeSnapshotService::Options();
        m_snapshotOptions.snapshotType = TreeSnapshotService::SnapshotType::FullRecursive;
        m_snapshotOptions.outputFormat = TreeSnapshotService::OutputFormat::PlainText;
        m_snapshotOptions.includeFiles = true;
        m_snapshotOptions.includeFolders = true;
        m_snapshotOptions.useUnicode = false;
        m_snapshotAction = QStringLiteral("Save to file");
    } else {
        TreeSnapshotDialog dialog(this);
        if (dialog.exec() != QDialog::Accepted) {
            logUiAction(QStringLiteral("Tree Snapshot"), QStringLiteral("onTreeSnapshotRequested"), QStringLiteral("canceled"));
            return;
        }
        m_snapshotOptions = dialog.options();
        m_snapshotAction = dialog.outputAction();
    }

    logUiAction(QStringLiteral("Tree Snapshot"), QStringLiteral("onTreeSnapshotRequested"), QStringLiteral("started"));
    startSnapshotGeneration(m_snapshotRootPath);
}

void MainWindow::startSnapshotGeneration(const QString& folderPath)
{
    if (m_snapshotWatcher && !m_snapshotWatcher->isFinished()) {
        return;
    }

    if (!m_snapshotWatcher) {
        m_snapshotWatcher = new QFutureWatcher<TreeSnapshotService::Result>(this);
        connect(m_snapshotWatcher, &QFutureWatcher<TreeSnapshotService::Result>::finished,
                this, &MainWindow::onSnapshotGenerationFinished);
    }

    m_snapshotCancelRequested.store(false);

    if (m_snapshotProgressDialog) {
        m_snapshotProgressDialog->close();
        delete m_snapshotProgressDialog;
        m_snapshotProgressDialog = nullptr;
    }

    m_snapshotProgressDialog = new QProgressDialog(QStringLiteral("Generating snapshot..."),
                                                   QStringLiteral("Cancel"),
                                                   0,
                                                   0,
                                                   this);
    m_snapshotProgressDialog->setWindowModality(Qt::WindowModal);
    m_snapshotProgressDialog->setMinimumDuration(0);
    connect(m_snapshotProgressDialog, &QProgressDialog::canceled, this, [this]() {
        m_snapshotCancelRequested.store(true);
    });
    m_snapshotProgressDialog->show();

    const TreeSnapshotService::Options options = m_snapshotOptions;
    const QString root = folderPath;

    auto task = [this, options, root]() -> TreeSnapshotService::Result {
        if (options.snapshotType == TreeSnapshotService::SnapshotType::VisibleView) {
            return TreeSnapshotService::generateVisibleView(m_treeView ? m_treeView->model() : nullptr,
                                                            QModelIndex(),
                                                            root,
                                                            options,
                                                            &m_snapshotCancelRequested);
        }
        return TreeSnapshotService::generateFromDisk(root, options, &m_snapshotCancelRequested);
    };

    m_snapshotWatcher->setFuture(QtConcurrent::run(task));
}

void MainWindow::onSnapshotGenerationFinished()
{
    if (!m_snapshotWatcher) {
        return;
    }

    if (m_snapshotProgressDialog) {
        m_snapshotProgressDialog->close();
        m_snapshotProgressDialog->deleteLater();
        m_snapshotProgressDialog = nullptr;
    }

    m_snapshotResult = m_snapshotWatcher->result();
    if (!m_snapshotResult.error.isEmpty()) {
        logUiAction(QStringLiteral("Tree Snapshot"), QStringLiteral("onSnapshotGenerationFinished"), QStringLiteral("error"), QString(), m_snapshotResult.error);
        QMessageBox::warning(this,
                             QStringLiteral("Tree Snapshot"),
                             m_snapshotResult.error);
        return;
    }

    const QString metadata = QStringLiteral("Root: %1\nFolders: %2  Files: %3\nDuration: %4 ms\nTruncated: %5  Canceled: %6")
                                 .arg(m_snapshotRootPath)
                                 .arg(m_snapshotResult.folderCount)
                                 .arg(m_snapshotResult.fileCount)
                                 .arg(m_snapshotResult.durationMs)
                                 .arg(m_snapshotResult.truncated ? QStringLiteral("Yes") : QStringLiteral("No"))
                                 .arg(m_snapshotResult.canceled ? QStringLiteral("Yes") : QStringLiteral("No"));

    if (m_snapshotAction.startsWith(QStringLiteral("Copy"), Qt::CaseInsensitive)) {
        if (QClipboard* clipboard = QApplication::clipboard()) {
            clipboard->setText(m_snapshotResult.text);
        }
        logUiAction(QStringLiteral("Tree Snapshot"), QStringLiteral("onSnapshotGenerationFinished"), QStringLiteral("ok"), QStringLiteral("clipboard"));
    } else if (m_snapshotAction.startsWith(QStringLiteral("Save"), Qt::CaseInsensitive)) {
        QString path;
        if (m_testMode && m_testScriptRunning) {
            const QFileInfo traceInfo(m_actionTracePath);
            const QString ext = (m_snapshotOptions.outputFormat == TreeSnapshotService::OutputFormat::Markdown)
                                    ? QStringLiteral("md")
                                    : QStringLiteral("txt");
            path = traceInfo.dir().filePath(QStringLiteral("tree_snapshot_testmode.%1").arg(ext));
        } else {
            const QString defaultName = QStringLiteral("tree_snapshot.%1")
                                            .arg(m_snapshotOptions.outputFormat == TreeSnapshotService::OutputFormat::Markdown
                                                     ? QStringLiteral("md")
                                                     : QStringLiteral("txt"));
            path = QFileDialog::getSaveFileName(this,
                                                QStringLiteral("Save Tree Snapshot"),
                                                defaultName,
                                                QStringLiteral("Text Files (*.txt);;Markdown Files (*.md);;All Files (*.*)"));
        }
        if (!path.isEmpty()) {
            QFile file(path);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream stream(&file);
                stream << m_snapshotResult.text;
                logUiAction(QStringLiteral("Tree Snapshot"), QStringLiteral("onSnapshotGenerationFinished"), QStringLiteral("ok"), path);
            }
        }
    } else {
        TreeSnapshotPreviewDialog preview(m_snapshotResult.text, metadata, this);
        preview.exec();
        logUiAction(QStringLiteral("Tree Snapshot"), QStringLiteral("onSnapshotGenerationFinished"), QStringLiteral("ok"), QStringLiteral("preview"));
    }

    appendRuntimeLog(QStringLiteral("tree_snapshot_done root=%1 folders=%2 files=%3 duration_ms=%4 canceled=%5 truncated=%6")
                         .arg(m_snapshotRootPath)
                         .arg(m_snapshotResult.folderCount)
                         .arg(m_snapshotResult.fileCount)
                         .arg(m_snapshotResult.durationMs)
                         .arg(m_snapshotResult.canceled ? QStringLiteral("true") : QStringLiteral("false"))
                         .arg(m_snapshotResult.truncated ? QStringLiteral("true") : QStringLiteral("false")));
}
