#pragma once

#include <QDialog>

#include "../core/TreeSnapshotService.h"

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QRadioButton;
class QSpinBox;

class TreeSnapshotDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TreeSnapshotDialog(QWidget* parent = nullptr);

    TreeSnapshotService::Options options() const;
    QString outputAction() const;

private slots:
    void onOptionChanged();

private:
    void refreshValidationState();

private:
    QComboBox* m_snapshotTypeCombo = nullptr;
    QComboBox* m_outputFormatCombo = nullptr;
    QCheckBox* m_includeFilesCheck = nullptr;
    QCheckBox* m_includeFoldersCheck = nullptr;
    QCheckBox* m_includeHiddenCheck = nullptr;
    QCheckBox* m_namesOnlyCheck = nullptr;
    QCheckBox* m_fullPathsCheck = nullptr;
    QCheckBox* m_unicodeCheck = nullptr;
    QCheckBox* m_asciiCheck = nullptr;
    QComboBox* m_maxDepthCombo = nullptr;
    QSpinBox* m_maxDepthSpin = nullptr;
    QRadioButton* m_copyClipboardRadio = nullptr;
    QRadioButton* m_saveToFileRadio = nullptr;
    QRadioButton* m_previewRadio = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
};
