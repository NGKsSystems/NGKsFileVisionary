#include "NavigationHistory.h"

void NavigationHistory::reset(const QString& path)
{
    m_backStack.clear();
    m_forwardStack.clear();
    m_current = path;
}

void NavigationHistory::enter(const QString& path)
{
    if (path == m_current) {
        return;
    }
    if (!m_current.isEmpty()) {
        m_backStack.push_back(m_current);
    }
    m_current = path;
    m_forwardStack.clear();
}

QString NavigationHistory::back()
{
    if (m_backStack.isEmpty()) {
        return m_current;
    }
    if (!m_current.isEmpty()) {
        m_forwardStack.push_back(m_current);
    }
    m_current = m_backStack.takeLast();
    return m_current;
}

QString NavigationHistory::forward()
{
    if (m_forwardStack.isEmpty()) {
        return m_current;
    }
    if (!m_current.isEmpty()) {
        m_backStack.push_back(m_current);
    }
    m_current = m_forwardStack.takeLast();
    return m_current;
}

QString NavigationHistory::jumpBreadcrumb(const QString& path)
{
    enter(path);
    return m_current;
}

bool NavigationHistory::canBack() const
{
    return !m_backStack.isEmpty();
}

bool NavigationHistory::canForward() const
{
    return !m_forwardStack.isEmpty();
}

QString NavigationHistory::current() const
{
    return m_current;
}
