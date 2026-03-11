#include "TreeSnapshotPreviewDialog.h"

#include <QClipboard>
#include <QFile>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QTextStream>
#include <QVBoxLayout>

TreeSnapshotPreviewDialog::TreeSnapshotPreviewDialog(const QString& snapshotText,
                                                     const QString& metadataText,
                                                     QWidget* parent)
    : QDialog(parent)
    , m_snapshotText(snapshotText)
{
    setWindowTitle(QStringLiteral("Tree Snapshot Preview"));
    resize(900, 650);

    auto* layout = new QVBoxLayout(this);

    m_metaLabel = new QLabel(metadataText, this);
    m_metaLabel->setWordWrap(true);
    layout->addWidget(m_metaLabel);

    m_textEdit = new QTextEdit(this);
    m_textEdit->setReadOnly(true);
    m_textEdit->setPlainText(m_snapshotText);
    layout->addWidget(m_textEdit, 1);

    auto* buttonRow = new QHBoxLayout();
    m_copyButton = new QPushButton(QStringLiteral("Copy"), this);
    m_saveButton = new QPushButton(QStringLiteral("Save"), this);
    auto* closeButton = new QPushButton(QStringLiteral("Close"), this);

    buttonRow->addWidget(m_copyButton);
    buttonRow->addWidget(m_saveButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(closeButton);

    layout->addLayout(buttonRow);

    connect(m_copyButton, &QPushButton::clicked, this, &TreeSnapshotPreviewDialog::onCopy);
    connect(m_saveButton, &QPushButton::clicked, this, &TreeSnapshotPreviewDialog::onSave);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

void TreeSnapshotPreviewDialog::onCopy()
{
    if (QClipboard* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(m_snapshotText);
    }
}

void TreeSnapshotPreviewDialog::onSave()
{
    const QString path = QFileDialog::getSaveFileName(this,
                                                      QStringLiteral("Save Tree Snapshot"),
                                                      QStringLiteral("tree_snapshot.txt"),
                                                      QStringLiteral("Text Files (*.txt);;Markdown Files (*.md);;All Files (*.*)"));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    QTextStream stream(&file);
    stream << m_snapshotText;
}
