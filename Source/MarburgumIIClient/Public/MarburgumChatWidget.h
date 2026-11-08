#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/EditableTextBox.h"
#include "MarburgumChatTypes.h"
#include "MarburgumChatWidget.generated.h"

class AMarburgumPlayerController;
class UScrollBox;
class UTextBlock;

/**
 * Early native UMG chat panel. This is a working UI foundation, not the final
 * approved Marburgum visual design. Server data reaches it only via owned RPCs.
 */
UCLASS()
class UMarburgumChatWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

    void FocusComposer();

private:
    UPROPERTY(Transient)
    TObjectPtr<UScrollBox> MessageFeed;

    UPROPERTY(Transient)
    TObjectPtr<UEditableTextBox> Composer;

    UPROPERTY(Transient)
    TObjectPtr<AMarburgumPlayerController> ChatController;

    UFUNCTION()
    void HandleMessage(const FMarburgumChatMessage& Message);

    UFUNCTION()
    void HandleTextCommitted(const FText& Text, ETextCommit::Type Method);

    UFUNCTION()
    void HandleSendButton();

    UFUNCTION()
    void ShowConnectedPlayers();

    void BuildPanel();
    void SubmitComposer();
    void AddLine(const FString& Line, const FLinearColor& Color);
    void ReturnFocusToWorld();
};
