#pragma once

#include <QStringListModel>

class StructuralSuggestionModel : public QStringListModel
{
    Q_OBJECT

public:
    explicit StructuralSuggestionModel(QObject* parent = nullptr);

    void setSuggestions(const QStringList& suggestions);
    QStringList suggestions() const;
};
