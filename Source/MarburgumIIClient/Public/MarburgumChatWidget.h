#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/EditableTextBox.h"
#include "MarburgumChatTypes.h"
#include "MarburgumChatWidget.generated.h"

class AMarburgumPlayerController;
class UScrollBox;
class UTextBlock;
class UVerticalBox;

/**
 * `chat_panel` HUD module (UI-03, CHAT-001): general and one-to-one private text, lower-left.
 * Built from the UI-01 tokens and mounted in the UI root's HUD layer by UMarburgumUISubsystem.
 * Server data reaches it only via owned RPCs; it never marks a message accepted itself.
 */
UCLASS()
class UMarburgumChatWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    virtual void NativeOnInitialized() override;
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
    /** One feed entry: a toned kind/address line above the body text. */
    void AddEntry(const FString& Heading, const FString& Body, const FLinearColor& HeadingColor, const FLinearColor& BodyColor);
    void AddLine(const FString& Line, const FLinearColor& Color);
    void ReturnFocusToWorld();
};
