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
