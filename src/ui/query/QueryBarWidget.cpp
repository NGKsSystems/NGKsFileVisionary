#include "QueryBarWidget.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>

#include "StructuralSuggestionModel.h"

QueryBarWidget::QueryBarWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    m_queryInput = new QLineEdit(this);
    m_queryInput->setObjectName(QStringLiteral("queryInput"));
    m_queryInput->setPlaceholderText(QStringLiteral("Example: ext:.cpp under:src name:main references:parser"));
    m_queryInput->setMinimumHeight(40);
    m_queryInput->setStyleSheet(QStringLiteral("QLineEdit#queryInput { font-size: 14px; padding: 8px 10px; }"));

    m_executeButton = new QPushButton(QStringLiteral("Execute"), this);
    m_executeButton->setObjectName(QStringLiteral("queryExecuteButton"));
    m_executeButton->setStyleSheet(QStringLiteral("QPushButton#queryExecuteButton { background: #1d4ed8; color: #ffffff; border: 1px solid #1e40af; border-radius: 5px; padding: 5px 14px; font-weight: 700; } QPushButton#queryExecuteButton:hover { background: #1e40af; }"));
    m_clearButton = new QPushButton(QStringLiteral("Clear"), this);
    m_clearButton->setObjectName(QStringLiteral("queryClearButton"));
    m_clearButton->setStyleSheet(QStringLiteral("QPushButton#queryClearButton { background: #f3f4f6; color: #1f2937; border: 1px solid #cbd5e1; border-radius: 5px; padding: 5px 14px; font-weight: 600; } QPushButton#queryClearButton:hover { background: #e5e7eb; }"));
    m_hintLabel = new QLabel(QStringLiteral("Query tokens: ext:  under:  name:  references:  usedby:"), this);
    m_hintLabel->setObjectName(QStringLiteral("queryTokensHint"));
    m_hintLabel->setStyleSheet(QStringLiteral("QLabel#queryTokensHint { color: #6b7280; font-size: 11px; font-weight: 500; }"));

    m_suggestionModel = new StructuralSuggestionModel(this);
    m_completer = new QCompleter(m_suggestionModel, this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setFilterMode(Qt::MatchStartsWith);
    m_completer->setWrapAround(false);
    m_queryInput->setCompleter(m_completer);

    layout->addWidget(m_queryInput, 1);
    auto* buttonRow = new QHBoxLayout();
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(8);
    buttonRow->addWidget(m_executeButton);
    buttonRow->addWidget(m_clearButton);
    buttonRow->addStretch(1);
    layout->addLayout(buttonRow);
    layout->addWidget(m_hintLabel);

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

    const QString replaced = replaceCurrentToken(text);
    m_ignoreTextChanges = true;
    m_queryInput->setText(replaced);
    m_queryInput->setCursorPosition(replaced.size());
    m_ignoreTextChanges = false;
    updateSuggestions(replaced, false);
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

QString QueryBarWidget::replaceCurrentToken(const QString& selectedSuggestion) const
{
    if (!m_queryInput) {
        return selectedSuggestion;
    }

    const QString current = m_queryInput->text();
    const int cursorPos = m_queryInput->cursorPosition();
    const QString beforeCursor = current.left(cursorPos);
    const QString afterCursor = current.mid(cursorPos);

    int tokenStart = beforeCursor.size() - 1;
    while (tokenStart >= 0 && !beforeCursor.at(tokenStart).isSpace()) {
        --tokenStart;
    }
    tokenStart += 1;

    int tokenEnd = 0;
    while (tokenEnd < afterCursor.size() && !afterCursor.at(tokenEnd).isSpace()) {
        ++tokenEnd;
    }

    const QString left = beforeCursor.left(tokenStart);
    const QString right = afterCursor.mid(tokenEnd);

    QString rebuilt = left;
    if (!rebuilt.isEmpty() && !rebuilt.endsWith(QLatin1Char(' '))) {
        rebuilt += QLatin1Char(' ');
    }
    rebuilt += selectedSuggestion;
    if (!right.isEmpty() && !right.startsWith(QLatin1Char(' '))) {
        rebuilt += QLatin1Char(' ');
    }
    rebuilt += right;
    return rebuilt.trimmed();
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
