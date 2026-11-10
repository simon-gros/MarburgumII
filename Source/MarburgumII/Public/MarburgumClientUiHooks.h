#pragma once

#include "CoreMinimal.h"

class APlayerController;

/** Narrow event boundary: runtime owns the player; ClientOnly module owns presentation. */
DECLARE_MULTICAST_DELEGATE_OneParam(FMarburgumLocalPlayerUiEvent, APlayerController*);

class MARBURGUMII_API FMarburgumClientUiHooks
{
public:
    static FMarburgumLocalPlayerUiEvent& OnLocalPlayerReady();
    static FMarburgumLocalPlayerUiEvent& OnChatFocusRequested();
    static FMarburgumLocalPlayerUiEvent& OnInventoryToggleRequested();
    static FMarburgumLocalPlayerUiEvent& OnJournalToggleRequested();
};
