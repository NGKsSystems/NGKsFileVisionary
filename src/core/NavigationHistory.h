#pragma once

#include <QString>
#include <QStringList>

class NavigationHistory
{
public:
    void reset(const QString& path = QString());
    void enter(const QString& path);
    QString back();
    QString forward();
    QString jumpBreadcrumb(const QString& path);

    bool canBack() const;
    bool canForward() const;
    QString current() const;

private:
    QStringList m_backStack;
    QStringList m_forwardStack;
    QString m_current;
};
