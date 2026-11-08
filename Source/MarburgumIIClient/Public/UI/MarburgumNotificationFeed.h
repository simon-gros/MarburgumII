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
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumNotificationFeed : public UUserWidget
{
    GENERATED_BODY()

public:
    void Push(FName DedupeKey, const FText& Kind, const FText& Message, MarburgumUI::ETone Tone);

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    struct FEntry
    {
        FName Key;
        TWeakObjectPtr<UWidget> Widget;
        double ShownSeconds = 0.0;
    };

    void RemoveEntry(int32 Index);

    UPROPERTY(Transient)
    TObjectPtr<UVerticalBox> Stack;

    TArray<FEntry> Entries;

    static constexpr int32 MaxVisible = 4;
};
