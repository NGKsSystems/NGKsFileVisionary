#include "QueryBarWidget.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>

#include "StructuralSuggestionModel.h"

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

    m_suggestionModel = new StructuralSuggestionModel(this);
    m_completer = new QCompleter(m_suggestionModel, this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setFilterMode(Qt::MatchStartsWith);
    m_completer->setWrapAround(false);
    m_queryInput->setCompleter(m_completer);

    layout->addWidget(label);
    layout->addWidget(m_queryInput, 1);
    layout->addWidget(m_clearButton);
    layout->addWidget(m_executeButton);

    connect(m_queryInput, &QLineEdit::returnPressed, this, &QueryBarWidget::onReturnPressed);
    connect(m_queryInput, &QLineEdit::textChanged, this, &QueryBarWidget::onQueryTextChanged);
    connect(m_executeButton, &QPushButton::clicked, this, &QueryBarWidget::onExecutePressed);
    connect(m_clearButton, &QPushButton::clicked, this, &QueryBarWidget::onClearPressed);
    connect(m_completer, QOverload<const QString&>::of(&QCompleter::activated), this, &QueryBarWidget::onCompleterActivated);

    auto* escapeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), m_queryInput);
    escapeShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escapeShortcut, &QShortcut::activated, this, [this]() {
        if (m_completer && m_completer->popup() && m_completer->popup()->isVisible()) {
            m_completer->popup()->hide();
            return;
        }
        onClearPressed();
    });
}

QString QueryBarWidget::queryText() const
{
    return m_queryInput ? m_queryInput->text() : QString();
}

void QueryBarWidget::setQueryText(const QString& text)
{
    if (m_queryInput) {
        m_ignoreTextChanges = true;
        m_queryInput->setText(text);
        m_ignoreTextChanges = false;
        updateSuggestions(text, false);
    }
}

void QueryBarWidget::setAutocompleteContext(const StructuralAutocompleteContext& context)
{
    m_autocompleteContext = context;
    updateSuggestions(queryText(), false);
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

void QueryBarWidget::onQueryTextChanged(const QString& text)
{
    if (m_ignoreTextChanges) {
        return;
    }
    updateSuggestions(text, true);
}

void QueryBarWidget::onCompleterActivated(const QString& text)
{
    if (!m_queryInput) {
        return;
    }

    m_ignoreTextChanges = true;
    m_queryInput->setText(text);
    m_queryInput->setCursorPosition(text.size());
    m_ignoreTextChanges = false;
    updateSuggestions(text, false);
}

void QueryBarWidget::onReturnPressed()
{
    if (m_completer && m_completer->popup() && m_completer->popup()->isVisible()) {
        const QModelIndex index = m_completer->popup()->currentIndex();
        const QString selected = index.isValid() ? index.data().toString() : m_completer->currentCompletion();
        if (!selected.isEmpty()) {
            onCompleterActivated(selected);
            return;
        }
    }

    onExecutePressed();
}

QStringList QueryBarWidget::computeSuggestions(const QString& text) const
{
    return StructuralQueryAutocomplete::buildSuggestions(text, m_autocompleteContext);
}

void QueryBarWidget::updateSuggestions(const QString& text, bool showPopup)
{
    m_currentSuggestions = computeSuggestions(text);
    if (m_suggestionModel) {
        m_suggestionModel->setSuggestions(m_currentSuggestions);
    }

    if (!m_completer || !m_completer->popup()) {
        return;
    }

    if (!showPopup) {
        return;
    }

    if (m_currentSuggestions.isEmpty()) {
        m_completer->popup()->hide();
        return;
    }

    if (!text.trimmed().isEmpty()) {
        m_completer->complete();
    }
}

QStringList QueryBarWidget::suggestionsForInputForTesting(const QString& input) const
{
    return StructuralQueryAutocomplete::buildSuggestions(input, m_autocompleteContext);
}

QStringList QueryBarWidget::currentSuggestionsForTesting() const
{
    return m_currentSuggestions;
}

bool QueryBarWidget::acceptFirstSuggestionForTesting()
{
    if (m_currentSuggestions.isEmpty()) {
        return false;
    }
    onCompleterActivated(m_currentSuggestions.first());
    return true;
}

void QueryBarWidget::dismissSuggestionsForTesting()
{
    if (m_completer && m_completer->popup()) {
        m_completer->popup()->hide();
    }
}
