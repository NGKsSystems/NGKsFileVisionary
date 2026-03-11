#pragma once

#include "core/FileScanner.h"

class ViewModeController
{
public:
    enum class UiViewMode
    {
        Standard = 0,
        Hierarchy = 1,
        Flat = 2,
    };

    void setModeFromIndex(int index);
    UiViewMode mode() const;
    int toComboIndex() const;
    FileViewMode toFileViewMode() const;

private:
    UiViewMode m_mode = UiViewMode::Standard;
};
