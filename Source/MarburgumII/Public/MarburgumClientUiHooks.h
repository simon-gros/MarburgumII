#pragma once

#include "CoreMinimal.h"

class AMarburgumInteractable;
class APlayerController;

/** Narrow event boundary: runtime owns the player; ClientOnly module owns presentation. */
DECLARE_MULTICAST_DELEGATE_OneParam(FMarburgumLocalPlayerUiEvent, APlayerController*);
/** A decision is due at Target for Mission: presentation offers the choices (ADR-049). */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FMarburgumChoiceUiEvent, APlayerController*, AMarburgumInteractable* /*Target*/, FName /*MissionId*/);
/** The counter Target keeps should open, or close if it is open (ADR-060). */
DECLARE_MULTICAST_DELEGATE_TwoParams(FMarburgumShopUiEvent, APlayerController*, AMarburgumInteractable* /*Target*/);
/** A number key (1-9) pressed in play: picks an option in an open choice panel, or a line at an open counter. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FMarburgumNumberKeyUiEvent, APlayerController*, int32 /*Number*/);
/** Mouse wheel or Page Up/Down in play: scrolls the open management screen by Lines (negative is up). */
DECLARE_MULTICAST_DELEGATE_TwoParams(FMarburgumScrollUiEvent, APlayerController*, float /*Lines*/);

class MARBURGUMII_API FMarburgumClientUiHooks
{
public:
    static FMarburgumLocalPlayerUiEvent& OnLocalPlayerReady();
    static FMarburgumLocalPlayerUiEvent& OnChatFocusRequested();
    static FMarburgumLocalPlayerUiEvent& OnInventoryToggleRequested();
    static FMarburgumLocalPlayerUiEvent& OnJournalToggleRequested();
    static FMarburgumLocalPlayerUiEvent& OnCharacterToggleRequested();
    static FMarburgumLocalPlayerUiEvent& OnSkillsToggleRequested();
    /** Escape in play: close what is open, or open the system menu. */
    static FMarburgumLocalPlayerUiEvent& OnBackRequested();
    static FMarburgumChoiceUiEvent& OnMissionChoiceRequested();
    static FMarburgumShopUiEvent& OnShopToggleRequested();
    static FMarburgumNumberKeyUiEvent& OnNumberKeyPressed();
    static FMarburgumScrollUiEvent& OnScrollRequested();
    /** The shutter was pressed (ADR-081): presentation flashes; the receipt says what, if anything, was taken. */
    static FMarburgumLocalPlayerUiEvent& OnPhotographTaken();
};
