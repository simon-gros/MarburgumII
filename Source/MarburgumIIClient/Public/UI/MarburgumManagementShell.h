#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/MarburgumInputDevice.h"
#include "UI/MarburgumUIWidgets.h"
#include "MarburgumManagementShell.generated.h"

class UBorder;
class UOverlay;
class UTextBlock;
class UMarburgumSelectable;

/** The management screens, in tab order (ontology `ui_surface` management_screen). */
UENUM()
enum class EMarburgumScreen : uint8
{
    None,
    Character,
    Skills,
    Inventory,
    Journal,
    Settings
};

DECLARE_DELEGATE_OneParam(FMarburgumScreenRequest, EMarburgumScreen);
DECLARE_DELEGATE(FMarburgumShellClose);
DECLARE_DELEGATE_OneParam(FMarburgumShellScroll, float);

/**
 * UI-04 management shell: the one frame Character, Skills, Inventory, Journal and Settings open in. It dims the street,
 * hides the exploration HUD (the subsystem collapses that layer), carries the tab bar with each screen's key and the
 * footer's key hints, and owns the keys while it is open: Esc / gamepad B closes it, C K I J O jump to a screen (its own
 * key closes it), Tab, Q/E and the shoulder buttons step between screens, and arrows / the d-pad move between rows.
 * Opening and closing go through UMarburgumUISubsystem, which also hands the input mode back to the street.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumManagementShell : public UUserWidget
{
    GENERATED_BODY()

public:
    FMarburgumScreenRequest OnScreenRequested;
    FMarburgumShellClose OnCloseRequested;
    FMarburgumShellScroll OnScrollRequested;

    /** Hosts Page for Screen (added once; the page collapses itself while another screen shows). */
    void AddPage(EMarburgumScreen Screen, UWidget* Page);
    /** Shows the frame on Screen: tab selection, the character line and the page area. */
    void Show(EMarburgumScreen Screen);
    void Hide();
    bool IsOpen() const { return Shown != EMarburgumScreen::None; }
    EMarburgumScreen GetShown() const { return Shown; }

    /** The screen Direction tabs away from From, wrapping. */
    static EMarburgumScreen Step(EMarburgumScreen From, int32 Direction);
    static FText ScreenName(EMarburgumScreen Screen);
    static FText ScreenKey(EMarburgumScreen Screen);
    static EMarburgumGlyph ScreenGlyph(EMarburgumScreen Screen);

protected:
    virtual void NativeOnInitialized() override;
    virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
    virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    /** Keeps the page area at the screen's width up to MaxPageWidth (a centred size box alone would shrink to its
     *  content), so pages lay out the same at 720p, ultrawide and 5:4 at any interface scale. */
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    void RefreshIdentity();

    UPROPERTY(Transient) TObjectPtr<UOverlay> Pages;
    /** The key caps of the header's Close and the footer's hints, named for the device in use. */
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> HintKeys;
    EMarburgumInputDevice HintDevice = EMarburgumInputDevice::KeyboardMouse;
    void RefreshHintKeys();
    UPROPERTY(Transient) TObjectPtr<class USizeBox> PageWidth;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> NameLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> PlaceLabel;
    UPROPERTY(Transient) TArray<TObjectPtr<UMarburgumSelectable>> Tabs;
    UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> TabUnderlines;
    UPROPERTY(Transient) TMap<EMarburgumScreen, TObjectPtr<UWidget>> PageWidgets;

    EMarburgumScreen Shown = EMarburgumScreen::None;
};
