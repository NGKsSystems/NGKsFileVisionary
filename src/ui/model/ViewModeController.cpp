#include "ViewModeController.h"

void ViewModeController::setModeFromIndex(int index)
{
    switch (index) {
    case 1:
        m_mode = UiViewMode::Hierarchy;
        break;
    case 2:
        m_mode = UiViewMode::Flat;
        break;
    default:
        m_mode = UiViewMode::Standard;
        break;
    }
}

ViewModeController::UiViewMode ViewModeController::mode() const
{
    return m_mode;
}

int ViewModeController::toComboIndex() const
{
    switch (m_mode) {
    case UiViewMode::Standard:
        return 0;
    case UiViewMode::Hierarchy:
        return 1;
    case UiViewMode::Flat:
        return 2;
    }
    return 0;
}

FileViewMode ViewModeController::toFileViewMode() const
{
    switch (m_mode) {
    case UiViewMode::Standard:
        return FileViewMode::Standard;
    case UiViewMode::Hierarchy:
        return FileViewMode::FullHierarchy;
    case UiViewMode::Flat:
        return FileViewMode::FlatFiles;
    }
    return FileViewMode::Standard;
}
