#include "ApplicationShell.h"

#include <QToolBar>
#include <QWidget>

namespace {

void applyCardLook(QWidget* container, const QString& borderColor, const QString& backgroundColor)
{
    if (!container) {
        return;
    }
    container->setStyleSheet(QStringLiteral("QWidget#%1 { border: 1px solid %2; border-radius: 8px; background: %3; }")
                                 .arg(container->objectName(), borderColor, backgroundColor));
}

}

namespace ApplicationShell {

void styleTopToolbar(QToolBar* toolbar)
{
    if (!toolbar) {
        return;
    }
    toolbar->setObjectName(QStringLiteral("topToolbarZone"));
}

void styleQueryCommandBar(QWidget* container)
{
    applyCardLook(container, QStringLiteral("#1c7ed6"), QStringLiteral("#edf4ff"));
}

void styleNavigationRail(QWidget* container)
{
    applyCardLook(container, QStringLiteral("#94a3b8"), QStringLiteral("#f2f4f7"));
}

void styleMainWorkspace(QWidget* container)
{
    applyCardLook(container, QStringLiteral("#c4cdd8"), QStringLiteral("#f7f8fa"));
}

}
