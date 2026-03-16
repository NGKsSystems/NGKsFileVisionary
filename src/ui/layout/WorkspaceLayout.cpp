#include "WorkspaceLayout.h"

#include <QSplitter>

namespace WorkspaceLayout {

void configureNavigationSplitter(QSplitter* splitter)
{
    if (!splitter) {
        return;
    }

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setCollapsible(0, false);
    splitter->setCollapsible(1, false);
}

}
