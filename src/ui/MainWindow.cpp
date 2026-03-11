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
#include <QEventLoop>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QFormLayout>
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
#include <QSet>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent/QtConcurrentRun>

#include "TreeSnapshotDialog.h"
#include "TreeSnapshotPreviewDialog.h"
#include "model/DirectoryModel.h"
#include "model/QueryResultAdapter.h"
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
    : MainWindow(false, QString(), QString(), QString(), parent)
{
}

MainWindow::MainWindow(bool testMode,
                       const QString& startupRoot,
                       const QString& actionLogPath,
                       const QString& testScriptPath,
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
    m_viewModeController.setModeFromIndex(m_viewModeCombo ? m_viewModeCombo->currentIndex() : 0);
    m_viewMode = m_viewModeController.toFileViewMode();
    m_uiDbPath = resolveUiDbPath();
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

    delete m_directoryModel;
    m_directoryModel = nullptr;
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
    rootLayout->addWidget(splitter, 1);
    rootLayout->addWidget(m_statusLabel);
    setCentralWidget(central);

    connect(m_browseButton, &QPushButton::clicked, this, &MainWindow::onBrowseRoot);
    connect(m_rescanButton, &QPushButton::clicked, this, &MainWindow::onRescan);
    connect(m_cancelButton, &QPushButton::clicked, this, &MainWindow::onCancelScan);
    connect(m_pinCurrentButton, &QPushButton::clicked, this, &MainWindow::onPinCurrentFolder);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MainWindow::onSearchChanged);
    connect(m_viewModeCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onViewModeChanged);
    connect(m_backButton, &QPushButton::clicked, this, &MainWindow::onNavigateBack);
    connect(m_forwardButton, &QPushButton::clicked, this, &MainWindow::onNavigateForward);
    connect(m_upButton, &QPushButton::clicked, this, &MainWindow::onNavigateUp);
    connect(m_treeView, &QWidget::customContextMenuRequested, this, &MainWindow::onTreeContextMenu);
    connect(m_treeView, &QTreeView::activated, this, &MainWindow::onTreeActivated);
    connect(m_sidebarTree, &QTreeWidget::itemActivated, this, &MainWindow::onSidebarItemActivated);
    connect(m_sidebarTree, &QWidget::customContextMenuRequested, this, &MainWindow::onSidebarContextMenu);

    loadFavorites();
    rebuildSidebar();
    updateNavigationButtons();

    appendRuntimeLog(QStringLiteral("MainWindow setup complete. sidebar_created=true favorites_config=%1 root=%2 startup_autorescan=false")
                         .arg(favoritesConfigPath(), m_rootEdit->text()));
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
        backgroundMenu.addAction(m_actionTreeSnapshot);
        QAction* snapshotCurrentAction = m_actionTreeSnapshot;
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
        } else if (chosen == snapshotCurrentAction) {
            onActionTreeSnapshot();
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
    if (isFolder) {
        menu.addAction(m_actionTreeSnapshot);
        treeSnapshotAction = m_actionTreeSnapshot;
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
    if (!isFolder) {
        hashFileAction = menu.addAction(QStringLiteral("Hash File..."));
        previewAction = menu.addAction(QStringLiteral("Preview"));
    }
    QAction* propertiesAction = menu.addAction(QStringLiteral("Properties"));

    QAction* chosen = menu.exec(m_treeView->viewport()->mapToGlobal(pos));
    if (!chosen) {
        return;
    }

    if (chosen == openAction) {
        if (isArchive) {
            auto* explorer = new ArchiveExplorer(this);
            explorer->openArchive(firstPath);
            explorer->show();
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
    const QFileInfo fileInfo(path);

    if (isInternalNavigableDirectory(fileInfo)) {
        appendRuntimeLog(QStringLiteral("tree_activated_internal_dir path=%1").arg(path));
        navigateToDirectory(path);
        return;
    }

    if (PathUtils::isArchivePath(path)) {
        auto* explorer = new ArchiveExplorer(this);
        explorer->openArchive(path);
        explorer->show();
        return;
    }

    if (fileInfo.exists() && fileInfo.isFile()) {
        appendRuntimeLog(QStringLiteral("tree_activated_external_file path=%1 suffix=%2 is_link=%3")
                             .arg(path)
                             .arg(fileInfo.suffix())
                             .arg(fileInfo.isSymLink() ? QStringLiteral("true") : QStringLiteral("false")));
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
    const QStringList extensions = PathUtils::splitExtensionsFilter(m_extensionFilterEdit->text());
    const QString search = m_searchEdit->text().trimmed();

    m_fileModel.setViewMode(m_viewModeController.toFileViewMode(), root);

    if (!ensureDirectoryModelReady()) {
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

    const QueryResult result = m_directoryModel->query(request);
    if (!result.ok) {
        m_scanInProgress = false;
        m_statusLabel->setText(QStringLiteral("Error: %1").arg(result.errorText));
        appendRuntimeLog(QStringLiteral("ui_query_failed error=%1").arg(result.errorText));
        return;
    }

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

    m_statusLabel->setText(QStringLiteral("Complete: %1 items").arg(rows.size()));
    appendRuntimeLog(QStringLiteral("ui_query_complete rows=%1").arg(rows.size()));

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

void MainWindow::navigateToDirectory(const QString& path, bool pushHistory)
{
    const QFileInfo fileInfo(path);
    if (!isInternalNavigableDirectory(fileInfo)) {
        appendRuntimeLog(QStringLiteral("navigate_rejected path=%1 exists=%2 is_dir=%3 is_link=%4")
                             .arg(path)
                             .arg(fileInfo.exists() ? QStringLiteral("true") : QStringLiteral("false"))
                             .arg(fileInfo.isDir() ? QStringLiteral("true") : QStringLiteral("false"))
                             .arg(fileInfo.isSymLink() ? QStringLiteral("true") : QStringLiteral("false")));
        return;
    }

    const QString normalizedPath = fileInfo.absoluteFilePath();
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
        QDir dir(m_rootEdit->text().trimmed());
        const bool canGoUp = dir.exists() && dir.cdUp();
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
