#include "ArchiveExplorer.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QTemporaryDir>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QGuiApplication>

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

ArchiveExplorer::ArchiveExplorer(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Archive Explorer"));
    setObjectName(QStringLiteral("archiveExplorerWindow"));
    resize(980, 640);

    QToolBar* bar = addToolBar(QStringLiteral("Archive"));
    m_backButton = new QPushButton(QStringLiteral("Back"), this);
    m_backButton->setObjectName(QStringLiteral("archiveBackButton"));
    m_forwardButton = new QPushButton(QStringLiteral("Forward"), this);
    m_forwardButton->setObjectName(QStringLiteral("archiveForwardButton"));
    m_breadcrumbEdit = new QLineEdit(this);
    m_breadcrumbEdit->setObjectName(QStringLiteral("archiveBreadcrumb"));
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName(QStringLiteral("archiveSearchEdit"));
    m_searchEdit->setPlaceholderText(QStringLiteral("Search archive..."));
    m_extractSelectedButton = new QPushButton(QStringLiteral("Extract Selected"), this);
    m_extractAllButton = new QPushButton(QStringLiteral("Extract All"), this);
    m_copyPathButton = new QPushButton(QStringLiteral("Copy Internal Path"), this);
    m_openEntryButton = new QPushButton(QStringLiteral("Open Entry"), this);

    bar->addWidget(m_backButton);
    bar->addWidget(m_forwardButton);
    bar->addWidget(m_breadcrumbEdit);
    bar->addSeparator();
    bar->addWidget(m_searchEdit);
    bar->addSeparator();
    bar->addWidget(m_extractSelectedButton);
    bar->addWidget(m_extractAllButton);
    bar->addWidget(m_copyPathButton);
    bar->addWidget(m_openEntryButton);

    QWidget* central = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(central);
    m_treeView = new QTreeView(central);
    m_treeView->setObjectName(QStringLiteral("archiveTree"));
    layout->addWidget(m_treeView);
    setCentralWidget(central);

    m_proxyModel.setSourceModel(&m_archiveModel);
    m_proxyModel.setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxyModel.setFilterKeyColumn(0);

    m_treeView->setModel(&m_proxyModel);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeView->setSortingEnabled(true);
    m_treeView->header()->setSectionResizeMode(QHeaderView::ResizeToContents);

    connect(m_backButton, &QPushButton::clicked, this, &ArchiveExplorer::onBack);
    connect(m_forwardButton, &QPushButton::clicked, this, &ArchiveExplorer::onForward);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &ArchiveExplorer::onSearchChanged);
    connect(&m_archiveService, &ArchiveService::listReady, this, &ArchiveExplorer::onListReady);
    connect(&m_archiveService, &ArchiveService::operationFinished, this, [this](bool success, const QString& msg) {
        if (!success) {
            QMessageBox::warning(this, QStringLiteral("Archive"), msg);
        }
    });
    connect(m_treeView, &QTreeView::activated, this, &ArchiveExplorer::onItemActivated);
    connect(m_treeView, &QWidget::customContextMenuRequested, this, &ArchiveExplorer::onTreeContextMenu);
    connect(m_extractSelectedButton, &QPushButton::clicked, this, &ArchiveExplorer::onExtractSelected);
    connect(m_extractAllButton, &QPushButton::clicked, this, &ArchiveExplorer::onExtractAll);
    connect(m_copyPathButton, &QPushButton::clicked, this, &ArchiveExplorer::onCopyInternalPath);
    connect(m_openEntryButton, &QPushButton::clicked, this, &ArchiveExplorer::onOpenEntry);
}

void ArchiveExplorer::openArchive(const QString& archivePath)
{
    m_archivePath = archivePath;
    m_history.reset(QString());
    refreshNavButtons();
    m_archiveService.listEntries(archivePath);
}

void ArchiveExplorer::onBack()
{
    navigateTo(m_history.back(), false);
}

void ArchiveExplorer::onForward()
{
    navigateTo(m_history.forward(), false);
}

void ArchiveExplorer::onSearchChanged(const QString& text)
{
    m_proxyModel.setFilterFixedString(text);
}

void ArchiveExplorer::onListReady(const QVector<ArchiveEntry>& entries)
{
    m_archiveModel.setEntries(entries);
    m_treeView->expandAll();
}

void ArchiveExplorer::onItemActivated(const QModelIndex& index)
{
    const QModelIndex sourceIndex = m_proxyModel.mapToSource(index);
    const QString path = m_archiveModel.modelPathForIndex(sourceIndex);
    navigateTo(path, true);
}

void ArchiveExplorer::onExtractSelected()
{
    const QString internalPath = selectedInternalPath();
    if (internalPath.isEmpty()) {
        return;
    }
    const QString dest = QFileDialog::getExistingDirectory(this,
                                                           QStringLiteral("Extract Selected To"),
                                                           QFileInfo(m_archivePath).absolutePath());
    if (dest.isEmpty()) {
        return;
    }
    m_archiveService.extractSelected(m_archivePath, {internalPath}, dest);
}

void ArchiveExplorer::onExtractAll()
{
    const QString dest = QFileDialog::getExistingDirectory(this,
                                                           QStringLiteral("Extract Archive To"),
                                                           QFileInfo(m_archivePath).absolutePath());
    if (dest.isEmpty()) {
        return;
    }
    m_archiveService.extractAll(m_archivePath, dest);
}

void ArchiveExplorer::onCopyInternalPath()
{
    const QString path = selectedInternalPath();
    if (path.isEmpty()) {
        return;
    }
    if (QClipboard* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(path);
    }
}

void ArchiveExplorer::onOpenEntry()
{
    const QString internalPath = selectedInternalPath();
    if (internalPath.isEmpty()) {
        return;
    }

    const QString tempRoot = QDir::temp().filePath(QStringLiteral("ngksfilevisionary_archive_open"));
    QDir().mkpath(tempRoot);
    const QString sevenZip = resolveBundled7zaPath();
    const QStringList args = {
        QStringLiteral("x"),
        m_archivePath,
        internalPath,
        QStringLiteral("-o%1").arg(tempRoot),
        QStringLiteral("-y"),
    };
    const int code = QProcess::execute(sevenZip, args);
    if (code != 0) {
        QMessageBox::warning(this, QStringLiteral("Open Entry"), QStringLiteral("Failed to extract selected entry."));
        return;
    }

    const QString extractedPath = QDir(tempRoot).filePath(internalPath);
    if (QFileInfo::exists(extractedPath)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(extractedPath));
    }
}

void ArchiveExplorer::onTreeContextMenu(const QPoint& pos)
{
    const QModelIndex proxyIndex = m_treeView->indexAt(pos);
    if (!proxyIndex.isValid()) {
        return;
    }

    QMenu menu(this);
    QAction* extractSelectedAction = menu.addAction(QStringLiteral("Extract Selected..."));
    QAction* extractAllAction = menu.addAction(QStringLiteral("Extract All..."));
    QAction* copyInternalPathAction = menu.addAction(QStringLiteral("Copy Internal Path"));
    QAction* openEntryAction = menu.addAction(QStringLiteral("Open Entry"));

    QAction* chosen = menu.exec(m_treeView->viewport()->mapToGlobal(pos));
    if (!chosen) {
        return;
    }
    if (chosen == extractSelectedAction) {
        onExtractSelected();
    } else if (chosen == extractAllAction) {
        onExtractAll();
    } else if (chosen == copyInternalPathAction) {
        onCopyInternalPath();
    } else if (chosen == openEntryAction) {
        onOpenEntry();
    }
}

void ArchiveExplorer::refreshNavButtons()
{
    m_backButton->setEnabled(m_history.canBack());
    m_forwardButton->setEnabled(m_history.canForward());
}

void ArchiveExplorer::navigateTo(const QString& internalPath, bool pushHistory)
{
    if (pushHistory) {
        m_history.enter(internalPath);
    }
    m_archiveModel.setCurrentPath(internalPath);
    m_breadcrumbEdit->setText(internalPath);
    refreshNavButtons();
}

QString ArchiveExplorer::selectedInternalPath() const
{
    if (!m_treeView || !m_treeView->currentIndex().isValid()) {
        return QString();
    }
    const QModelIndex sourceIndex = m_proxyModel.mapToSource(m_treeView->currentIndex());
    return m_archiveModel.modelPathForIndex(sourceIndex);
}
