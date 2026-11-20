#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/EditableTextBox.h"
#include "MarburgumChatTypes.h"
#include "MarburgumChatWidget.generated.h"

class AMarburgumPlayerController;
class UBorder;
class UScrollBox;
class UTextBlock;
class UVerticalBox;
class UWidgetSwitcher;

/**
 * `chat_panel` HUD module (UI-03, CHAT-001): general and one-to-one private text, lower-left, above the condition.
 * Quiet by default: the last few lines float over the street on a soft shade and fade after a while; anything that
 * arrives unseen leaves a small "new" marker. Enter opens the full panel (history, channel, who is online) with the
 * composer; Tab switches General and Private, Esc or a click on the street leaves it. Sender names are the characters'
 * own names where the session knows them. Server data reaches it only via owned RPCs; it never marks a message
 * accepted itself, and a private line is only ever shown to its two ends (the server routes it).
 */
UCLASS()
class UMarburgumChatWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
    /** Escape while typing leaves the composer; Tab switches the channel. */
    virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

    void FocusComposer();
    /** Closes the composer (keeping the draft) without touching the input mode; a screen opening takes the keys. */
    void LeaveComposer();
    bool IsComposing() const { return bComposing; }
    int32 GetUnreadCount() const { return Unread; }

private:
    struct FEntry
    {
        EMarburgumChatChannel Channel = EMarburgumChatChannel::General;
        FText Heading;
        FText Body;
        FDateTime Received;
        bool bOwn = false;
    };

    UPROPERTY(Transient) TObjectPtr<UScrollBox> History;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> RecentLines;
    UPROPERTY(Transient) TObjectPtr<UWidget> RecentFade;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> OnlineList;
    UPROPERTY(Transient) TObjectPtr<UWidgetSwitcher> Body;
    UPROPERTY(Transient) TObjectPtr<UWidget> IdleView;
    UPROPERTY(Transient) TObjectPtr<UWidget> OpenView;
    UPROPERTY(Transient) TObjectPtr<UEditableTextBox> Composer;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ChannelLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> OnlineLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> UnreadLabel;
    UPROPERTY(Transient) TObjectPtr<UBorder> UnreadChip;
    UPROPERTY(Transient) TObjectPtr<AMarburgumPlayerController> ChatController;

    UFUNCTION()
    void HandleMessage(const FMarburgumChatMessage& Message);

    UFUNCTION()
    void HandleTextCommitted(const FText& Text, ETextCommit::Type Method);

    void BuildPanel();
    void SubmitComposer();
    void AddEntry(const FEntry& Entry);
    void AddNotice(const FText& Text, bool bWarning);
    UWidget* MakeLine(const FEntry& Entry, bool bCompact);
    void ReturnFocusToWorld();
    void SetComposing(bool bInComposing);
    void CycleChannel();
    void RefreshChannel();
    void ToggleOnline();
    void RebuildOnline();
    /** The character's name for a session player id when the session knows it, else Fallback. */
    FString DisplayName(int32 PlayerId, const FString& Fallback) const;

    bool bComposing = false;
    bool bPrivate = false;
    bool bShowingOnline = false;
    int32 PrivateTarget = INDEX_NONE;
    int32 Unread = 0;
    double LastActivity = 0.0;
    float RecentOpacity = 1.f;
};
