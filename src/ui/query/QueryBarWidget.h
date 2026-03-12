#pragma once

#include <QWidget>

class QLineEdit;
class QPushButton;

class QueryBarWidget : public QWidget
{
    Q_OBJECT

public:
    explicit QueryBarWidget(QWidget* parent = nullptr);

    QString queryText() const;
    void setQueryText(const QString& text);
    void focusInput();
    void clearQuery();

signals:
    void querySubmitted(const QString& queryText);
    void queryCleared();

private slots:
    void onExecutePressed();
    void onClearPressed();

private:
    QLineEdit* m_queryInput = nullptr;
    QPushButton* m_clearButton = nullptr;
    QPushButton* m_executeButton = nullptr;
};
