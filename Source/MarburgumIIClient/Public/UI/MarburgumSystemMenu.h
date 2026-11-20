#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MarburgumSystemMenu.generated.h"

class UMarburgumSelectable;
class UTextBlock;
class UVerticalBox;
class UWidgetSwitcher;

DECLARE_DELEGATE(FMarburgumSystemMenuAction);

/**
 * UI-05 `system_menu` (Escape in play): back to the street, Settings, leave the world, quit. Marburgum has no pause;
 * the menu says so, and the city keeps running behind it. Leaving and quitting ask once more (a confirmation card;
 * Escape or gamepad B steps back), because both end the session. Keyboard, mouse and gamepad reach every entry.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumSystemMenu : public UUserWidget
{
    GENERATED_BODY()

public:
    bool IsOpen() const;
    void Open();
    void Close();
    /** The entry keyboard focus starts on. */
    UWidget* GetPreferredFocus() const;
    /** True while the leave/quit confirmation card shows (for evidence capture). */
    bool IsConfirming() const;

    FMarburgumSystemMenuAction OnResume;
    FMarburgumSystemMenuAction OnSettings;
    FMarburgumSystemMenuAction OnLeave;
    FMarburgumSystemMenuAction OnQuit;

    /** Shows the confirmation for leaving (bQuit false) or quitting, as the entry would. */
    void AskToConfirm(bool bQuit);

protected:
    virtual void NativeOnInitialized() override;
    virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
    void CancelConfirm();

    UPROPERTY(Transient) TObjectPtr<UWidgetSwitcher> Pages;
    UPROPERTY(Transient) TObjectPtr<UMarburgumSelectable> ResumeEntry;
    UPROPERTY(Transient) TObjectPtr<UMarburgumSelectable> ConfirmEntry;
    UPROPERTY(Transient) TObjectPtr<UMarburgumSelectable> StayEntry;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ConfirmTitle;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ConfirmBody;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ConfirmLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ClockLabel;

    bool bConfirmQuit = false;
};
