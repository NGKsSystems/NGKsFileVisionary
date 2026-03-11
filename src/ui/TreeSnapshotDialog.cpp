#include "TreeSnapshotDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

TreeSnapshotDialog::TreeSnapshotDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Tree Snapshot"));
    setObjectName(QStringLiteral("treeSnapshotDialog"));
    resize(460, 380);

    auto* rootLayout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    m_snapshotTypeCombo = new QComboBox(this);
    m_snapshotTypeCombo->setObjectName(QStringLiteral("snapshotTypeCombo"));
    m_snapshotTypeCombo->addItem(QStringLiteral("Full Recursive Tree"));
    m_snapshotTypeCombo->addItem(QStringLiteral("Visible View Tree"));

    m_outputFormatCombo = new QComboBox(this);
    m_outputFormatCombo->setObjectName(QStringLiteral("outputFormatCombo"));
    m_outputFormatCombo->addItem(QStringLiteral("Plain Text (.txt)"));
    m_outputFormatCombo->addItem(QStringLiteral("Markdown (.md)"));

    m_includeFilesCheck = new QCheckBox(QStringLiteral("Include files"), this);
    m_includeFilesCheck->setObjectName(QStringLiteral("includeFilesCheck"));
    m_includeFilesCheck->setChecked(true);
    m_includeFoldersCheck = new QCheckBox(QStringLiteral("Include folders"), this);
    m_includeFoldersCheck->setObjectName(QStringLiteral("includeFoldersCheck"));
    m_includeFoldersCheck->setChecked(true);
    m_includeHiddenCheck = new QCheckBox(QStringLiteral("Include hidden items"), this);
    m_includeHiddenCheck->setObjectName(QStringLiteral("includeHiddenCheck"));

    m_namesOnlyCheck = new QCheckBox(QStringLiteral("Names only"), this);
    m_namesOnlyCheck->setObjectName(QStringLiteral("namesOnlyRadio"));
    m_namesOnlyCheck->setChecked(true);
    m_fullPathsCheck = new QCheckBox(QStringLiteral("Full paths"), this);
    m_fullPathsCheck->setObjectName(QStringLiteral("fullPathsRadio"));

    m_unicodeCheck = new QCheckBox(QStringLiteral("Unicode tree characters"), this);
    m_unicodeCheck->setObjectName(QStringLiteral("unicodeRadio"));
    m_unicodeCheck->setChecked(true);
    m_asciiCheck = new QCheckBox(QStringLiteral("ASCII tree characters"), this);
    m_asciiCheck->setObjectName(QStringLiteral("asciiRadio"));

    m_maxDepthCombo = new QComboBox(this);
    m_maxDepthCombo->setObjectName(QStringLiteral("maxDepthModeCombo"));
    m_maxDepthCombo->addItem(QStringLiteral("Unlimited"));
    m_maxDepthCombo->addItem(QStringLiteral("Limit depth"));

    m_maxDepthSpin = new QSpinBox(this);
    m_maxDepthSpin->setObjectName(QStringLiteral("maxDepthSpin"));
    m_maxDepthSpin->setRange(1, 128);
    m_maxDepthSpin->setValue(6);
    m_maxDepthSpin->setEnabled(false);

    auto* outputActionRow = new QWidget(this);
    auto* outputActionLayout = new QHBoxLayout(outputActionRow);
    outputActionLayout->setContentsMargins(0, 0, 0, 0);
    m_copyClipboardRadio = new QRadioButton(QStringLiteral("Copy to clipboard"), this);
    m_copyClipboardRadio->setObjectName(QStringLiteral("copyClipboardButton"));
    m_saveToFileRadio = new QRadioButton(QStringLiteral("Save to file"), this);
    m_saveToFileRadio->setObjectName(QStringLiteral("saveToFileButton"));
    m_previewRadio = new QRadioButton(QStringLiteral("Open preview dialog"), this);
    m_previewRadio->setObjectName(QStringLiteral("previewButton"));
    m_copyClipboardRadio->setChecked(true);
    outputActionLayout->addWidget(m_copyClipboardRadio);
    outputActionLayout->addWidget(m_saveToFileRadio);
    outputActionLayout->addWidget(m_previewRadio);

    auto* includeRow = new QWidget(this);
    auto* includeLayout = new QHBoxLayout(includeRow);
    includeLayout->setContentsMargins(0, 0, 0, 0);
    includeLayout->addWidget(m_includeFilesCheck);
    includeLayout->addWidget(m_includeFoldersCheck);
    includeLayout->addWidget(m_includeHiddenCheck);

    auto* namingRow = new QWidget(this);
    auto* namingLayout = new QHBoxLayout(namingRow);
    namingLayout->setContentsMargins(0, 0, 0, 0);
    namingLayout->addWidget(m_namesOnlyCheck);
    namingLayout->addWidget(m_fullPathsCheck);

    auto* charsetRow = new QWidget(this);
    auto* charsetLayout = new QHBoxLayout(charsetRow);
    charsetLayout->setContentsMargins(0, 0, 0, 0);
    charsetLayout->addWidget(m_unicodeCheck);
    charsetLayout->addWidget(m_asciiCheck);

    auto* depthRow = new QWidget(this);
    auto* depthLayout = new QHBoxLayout(depthRow);
    depthLayout->setContentsMargins(0, 0, 0, 0);
    depthLayout->addWidget(m_maxDepthCombo);
    depthLayout->addWidget(m_maxDepthSpin);

    form->addRow(QStringLiteral("Snapshot Type"), m_snapshotTypeCombo);
    form->addRow(QStringLiteral("Output Format"), m_outputFormatCombo);
    form->addRow(QStringLiteral("Options"), includeRow);
    form->addRow(QStringLiteral("Path Style"), namingRow);
    form->addRow(QStringLiteral("Tree Charset"), charsetRow);
    form->addRow(QStringLiteral("Max Depth"), depthRow);
    form->addRow(QStringLiteral("Output Action"), outputActionRow);

    rootLayout->addLayout(form);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    if (QPushButton* okButton = m_buttons->button(QDialogButtonBox::Ok)) {
        okButton->setObjectName(QStringLiteral("snapshotOkButton"));
    }
    if (QPushButton* cancelButton = m_buttons->button(QDialogButtonBox::Cancel)) {
        cancelButton->setObjectName(QStringLiteral("snapshotCancelButton"));
    }
    rootLayout->addWidget(m_buttons);

    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_includeFilesCheck, &QCheckBox::toggled, this, &TreeSnapshotDialog::onOptionChanged);
    connect(m_includeFoldersCheck, &QCheckBox::toggled, this, &TreeSnapshotDialog::onOptionChanged);
    connect(m_namesOnlyCheck, &QCheckBox::toggled, this, &TreeSnapshotDialog::onOptionChanged);
    connect(m_fullPathsCheck, &QCheckBox::toggled, this, &TreeSnapshotDialog::onOptionChanged);
    connect(m_unicodeCheck, &QCheckBox::toggled, this, &TreeSnapshotDialog::onOptionChanged);
    connect(m_asciiCheck, &QCheckBox::toggled, this, &TreeSnapshotDialog::onOptionChanged);
    connect(m_maxDepthCombo, &QComboBox::currentIndexChanged, this, &TreeSnapshotDialog::onOptionChanged);

    refreshValidationState();
}

TreeSnapshotService::Options TreeSnapshotDialog::options() const
{
    TreeSnapshotService::Options opts;
    opts.snapshotType = static_cast<TreeSnapshotService::SnapshotType>(m_snapshotTypeCombo->currentIndex());
    opts.outputFormat = static_cast<TreeSnapshotService::OutputFormat>(m_outputFormatCombo->currentIndex());
    opts.includeFiles = m_includeFilesCheck->isChecked();
    opts.includeFolders = m_includeFoldersCheck->isChecked();
    opts.includeHidden = m_includeHiddenCheck->isChecked();
    opts.namesOnly = m_namesOnlyCheck->isChecked();
    opts.fullPaths = m_fullPathsCheck->isChecked();
    opts.useUnicode = m_unicodeCheck->isChecked();
    opts.maxDepth = (m_maxDepthCombo->currentIndex() == 0) ? -1 : m_maxDepthSpin->value();
    return opts;
}

QString TreeSnapshotDialog::outputAction() const
{
    if (m_saveToFileRadio && m_saveToFileRadio->isChecked()) {
        return QStringLiteral("Save to file");
    }
    if (m_previewRadio && m_previewRadio->isChecked()) {
        return QStringLiteral("Open preview dialog");
    }
    return QStringLiteral("Copy to clipboard");
}

void TreeSnapshotDialog::onOptionChanged()
{
    refreshValidationState();
}

void TreeSnapshotDialog::refreshValidationState()
{
    if (sender() == m_namesOnlyCheck && m_namesOnlyCheck->isChecked()) {
        m_fullPathsCheck->setChecked(false);
    }
    if (sender() == m_fullPathsCheck && m_fullPathsCheck->isChecked()) {
        m_namesOnlyCheck->setChecked(false);
    }
    if (!m_namesOnlyCheck->isChecked() && !m_fullPathsCheck->isChecked()) {
        m_namesOnlyCheck->setChecked(true);
    }

    if (sender() == m_unicodeCheck && m_unicodeCheck->isChecked()) {
        m_asciiCheck->setChecked(false);
    }
    if (sender() == m_asciiCheck && m_asciiCheck->isChecked()) {
        m_unicodeCheck->setChecked(false);
    }
    if (!m_unicodeCheck->isChecked() && !m_asciiCheck->isChecked()) {
        m_unicodeCheck->setChecked(true);
    }

    m_maxDepthSpin->setEnabled(m_maxDepthCombo->currentIndex() == 1);

    const bool validIncludes = m_includeFilesCheck->isChecked() || m_includeFoldersCheck->isChecked();
    if (m_buttons && m_buttons->button(QDialogButtonBox::Ok)) {
        m_buttons->button(QDialogButtonBox::Ok)->setEnabled(validIncludes);
    }
}
