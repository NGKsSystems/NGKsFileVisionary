#pragma once

#include <QDialog>

class QLabel;
class QPushButton;
class QTextEdit;

class TreeSnapshotPreviewDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TreeSnapshotPreviewDialog(const QString& snapshotText,
                                       const QString& metadataText,
                                       QWidget* parent = nullptr);

private slots:
    void onCopy();
    void onSave();

private:
    QString m_snapshotText;
    QTextEdit* m_textEdit = nullptr;
    QLabel* m_metaLabel = nullptr;
    QPushButton* m_copyButton = nullptr;
    QPushButton* m_saveButton = nullptr;
};
