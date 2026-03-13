#include "QueryBarWidget.h"

#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>

QueryBarWidget::QueryBarWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* label = new QLabel(QStringLiteral("Query:"), this);
    m_queryInput = new QLineEdit(this);
    m_queryInput->setObjectName(QStringLiteral("queryInput"));
    m_queryInput->setPlaceholderText(QStringLiteral("ext:.cpp under:src/ name:main references:src/main.cpp usedby:parser.h"));

    m_clearButton = new QPushButton(QStringLiteral("Clear"), this);
    m_clearButton->setObjectName(QStringLiteral("queryClearButton"));
    m_executeButton = new QPushButton(QStringLiteral("Execute"), this);
    m_executeButton->setObjectName(QStringLiteral("queryExecuteButton"));

    layout->addWidget(label);
    layout->addWidget(m_queryInput, 1);
    layout->addWidget(m_clearButton);
    layout->addWidget(m_executeButton);

    connect(m_queryInput, &QLineEdit::returnPressed, this, &QueryBarWidget::onExecutePressed);
    connect(m_executeButton, &QPushButton::clicked, this, &QueryBarWidget::onExecutePressed);
    connect(m_clearButton, &QPushButton::clicked, this, &QueryBarWidget::onClearPressed);

    auto* escapeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), m_queryInput);
    escapeShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escapeShortcut, &QShortcut::activated, this, &QueryBarWidget::onClearPressed);
}

QString QueryBarWidget::queryText() const
{
    return m_queryInput ? m_queryInput->text() : QString();
}

void QueryBarWidget::setQueryText(const QString& text)
{
    if (m_queryInput) {
        m_queryInput->setText(text);
    }
}

void QueryBarWidget::focusInput()
{
    if (!m_queryInput) {
        return;
    }
    m_queryInput->setFocus();
    m_queryInput->selectAll();
}

void QueryBarWidget::clearQuery()
{
    if (m_queryInput) {
        m_queryInput->clear();
    }
    emit queryCleared();
}

void QueryBarWidget::onExecutePressed()
{
    emit querySubmitted(queryText());
}

void QueryBarWidget::onClearPressed()
{
    clearQuery();
}
