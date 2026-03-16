#pragma once

#include <QWidget>

#include "StructuralQueryAutocomplete.h"

class QLineEdit;
class QPushButton;
class QCompleter;
class QLabel;
class StructuralSuggestionModel;

class QueryBarWidget : public QWidget
{
    Q_OBJECT

public:
    explicit QueryBarWidget(QWidget* parent = nullptr);

    QString queryText() const;
    void setQueryText(const QString& text);
    void setAutocompleteContext(const StructuralAutocompleteContext& context);
    void focusInput();
    void clearQuery();
    QStringList suggestionsForInputForTesting(const QString& input) const;
    QStringList currentSuggestionsForTesting() const;
    bool acceptFirstSuggestionForTesting();
    void dismissSuggestionsForTesting();

signals:
    void querySubmitted(const QString& queryText);
    void queryCleared();

private slots:
    void onExecutePressed();
    void onClearPressed();
    void onQueryTextChanged(const QString& text);
    void onCompleterActivated(const QString& text);
    void onReturnPressed();

private:
    QString replaceCurrentToken(const QString& selectedSuggestion) const;
    QStringList computeSuggestions(const QString& text) const;
    void updateSuggestions(const QString& text, bool showPopup);

    QLineEdit* m_queryInput = nullptr;
    QLabel* m_hintLabel = nullptr;
    QPushButton* m_clearButton = nullptr;
    QPushButton* m_executeButton = nullptr;
    QCompleter* m_completer = nullptr;
    StructuralSuggestionModel* m_suggestionModel = nullptr;
    StructuralAutocompleteContext m_autocompleteContext;
    QStringList m_currentSuggestions;
    bool m_ignoreTextChanges = false;
};
