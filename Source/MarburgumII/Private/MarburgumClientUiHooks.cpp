#include "MarburgumClientUiHooks.h"

FMarburgumLocalPlayerUiEvent& FMarburgumClientUiHooks::OnLocalPlayerReady()
{
    static FMarburgumLocalPlayerUiEvent Event;
    return Event;
}

FMarburgumLocalPlayerUiEvent& FMarburgumClientUiHooks::OnChatFocusRequested()
{
    static FMarburgumLocalPlayerUiEvent Event;
    return Event;
}

FMarburgumLocalPlayerUiEvent& FMarburgumClientUiHooks::OnInventoryToggleRequested()
{
    static FMarburgumLocalPlayerUiEvent Event;
    return Event;
}

FMarburgumLocalPlayerUiEvent& FMarburgumClientUiHooks::OnJournalToggleRequested()
{
    static FMarburgumLocalPlayerUiEvent Event;
    return Event;
}

FMarburgumLocalPlayerUiEvent& FMarburgumClientUiHooks::OnCharacterToggleRequested()
{
    static FMarburgumLocalPlayerUiEvent Event;
    return Event;
}

FMarburgumLocalPlayerUiEvent& FMarburgumClientUiHooks::OnSkillsToggleRequested()
{
    static FMarburgumLocalPlayerUiEvent Event;
    return Event;
}

FMarburgumChoiceUiEvent& FMarburgumClientUiHooks::OnMissionChoiceRequested()
{
    static FMarburgumChoiceUiEvent Event;
    return Event;
}

FMarburgumNumberKeyUiEvent& FMarburgumClientUiHooks::OnNumberKeyPressed()
{
    static FMarburgumNumberKeyUiEvent Event;
    return Event;
}
