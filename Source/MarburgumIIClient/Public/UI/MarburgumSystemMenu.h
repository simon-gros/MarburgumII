#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MarburgumSystemMenu.generated.h"

class UButton;

DECLARE_DELEGATE(FMarburgumSystemMenuAction);

/**
 * The in-world system menu (Escape, UI_HUD_ROADMAP "Escape/back"): back to the street, leave the world for the
 * title screen, or quit the game. The world does not stop while it is open; it only takes the mouse and keys.
 * Escape, or "Back to the street", closes it.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumSystemMenu : public UUserWidget
{
    GENERATED_BODY()

public:
    bool IsOpen() const;
    void Open();
    void Close();

    /** The button keyboard focus starts on. */
    UWidget* GetPreferredFocus() const;

    FMarburgumSystemMenuAction OnResume;
    FMarburgumSystemMenuAction OnLeave;
    FMarburgumSystemMenuAction OnQuit;

protected:
    virtual void NativeOnInitialized() override;
    virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
    UFUNCTION() void HandleResume();
    UFUNCTION() void HandleLeave();
    UFUNCTION() void HandleQuit();

    UPROPERTY(Transient) TObjectPtr<UButton> ResumeButton;
};
