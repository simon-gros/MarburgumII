#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/MarburgumUITheme.h"
#include "MarburgumNotificationFeed.generated.h"

class UVerticalBox;
class UWidget;

/**
 * `notification_feed` module (right_notification_stack region): bounded, deduplicated toasts.
 * A key that is already visible refreshes that toast instead of stacking a duplicate, so one
 * server event reported twice (state change + receipt) is shown once.
 *
 * A toast stays long enough to be read (longer text, longer time). When the stack is full, a new
 * toast waits for a free place rather than pushing one out, except a story toast (what somebody
 * said, what a mission step told), which takes the place of the oldest toast that is not one.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumNotificationFeed : public UUserWidget
{
    GENERATED_BODY()

public:
    void Push(FName DedupeKey, const FText& Kind, const FText& Message, MarburgumUI::ETone Tone, bool bStory = false);

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    struct FEntry
    {
        FName Key;
        TWeakObjectPtr<UWidget> Widget;
        double ShownSeconds = 0.0;
        double LifeSeconds = 0.0;
        bool bStory = false;
    };

    struct FQueued
    {
        FName Key;
        FText Kind;
        FText Message;
        MarburgumUI::ETone Tone;
        bool bStory = false;
    };

    void Show(const FQueued& Toast);
    void RemoveEntry(int32 Index);

    UPROPERTY(Transient)
    TObjectPtr<UVerticalBox> Stack;

    TArray<FEntry> Entries;
    /** Toasts waiting for a free place, oldest first. */
    TArray<FQueued> Waiting;

    static constexpr int32 MaxVisible = 4;
    static constexpr int32 MaxWaiting = 8;
};
