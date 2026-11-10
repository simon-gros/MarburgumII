#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MarburgumCharacterPanel.generated.h"

class AMarburgumPlayerState;
class UTextBlock;
class UVerticalBox;

/**
 * First UI-04 `character_panel` shell: who the local character is and what the server knows about
 * them: development identity, condition (vitality, stamina, running/winded), whereabouts (the
 * server-derived area), and summaries of the saved custody and missions. Everything comes from
 * owner-only replicated state; each section has a designed waiting state, and equipment, which no
 * system in this build provides, has an explicit "not in this build" card rather than invented
 * slots. Rebuilt on the player-state change events; toggled with C; it does not take input focus yet.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumCharacterPanel : public UUserWidget
{
    GENERATED_BODY()

public:
    void Toggle();
    bool IsOpen() const;

    /** The stamina line as shown (for evidence capture); empty while closed or waiting. */
    FText GetShownStaminaText() const;

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

private:
    void HandleStateChanged(AMarburgumPlayerState* State);
    void Rebuild();

    UPROPERTY(Transient) TObjectPtr<UTextBlock> NameLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> IdentityLabel;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> Sections;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StaminaValue;

    TArray<FDelegateHandle> Handles;
};
