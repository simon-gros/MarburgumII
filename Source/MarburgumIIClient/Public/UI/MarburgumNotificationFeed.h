#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/MarburgumUITheme.h"
#include "UI/MarburgumUIWidgets.h"
#include "MarburgumNotificationFeed.generated.h"

class UTextBlock;
class UVerticalBox;
class UWidget;

/**
 * `notification_feed` module (right_notification_stack region): bounded, deduplicated toasts, each with a glyph for
 * its kind (item, money, skill, mission, receipt, refusal, fishing, fight, connection) beside the words, so success and
 * refusal never differ by colour alone. A key that is already visible refreshes that toast instead of stacking a
 * duplicate, so one server event reported twice (state change + receipt) is shown once.
 *
 * A toast stays long enough to be read (longer text, longer time; refusals a little longer; Settings scales it). When
 * the stack is full, a new toast waits for a free place rather than pushing one out.
 *
 * What somebody says and what a mission step tells (story lines) are not toasts: they appear as subtitles in the
 * lower centre, speaker above, one at a time in order, so rewards arriving with them never push them away (QA U10).
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumNotificationFeed : public UUserWidget
{
    GENERATED_BODY()

public:
    void Push(FName DedupeKey, const FText& Kind, const FText& Message, MarburgumUI::ETone Tone, bool bStory = false,
        EMarburgumGlyph Icon = EMarburgumGlyph::None);

    /** The subtitle on screen ("" when none), for evidence capture. */
    FString GetShownSubtitle() const;
    /** Toasts on screen, for evidence capture. */
    int32 GetShownToastCount() const { return Entries.Num(); }

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
    };

    struct FQueued
    {
        FName Key;
        FText Kind;
        FText Message;
        MarburgumUI::ETone Tone;
        EMarburgumGlyph Icon = EMarburgumGlyph::None;
    };

    void Show(const FQueued& Toast);
    void RemoveEntry(int32 Index);
    void ShowSubtitle(const FQueued& Line);
    double ReadingSeconds(const FQueued& Item) const;

    UPROPERTY(Transient) TObjectPtr<UVerticalBox> Stack;
    UPROPERTY(Transient) TObjectPtr<UWidget> Subtitle;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> SpeakerLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> LineLabel;

    TArray<FEntry> Entries;
    /** Toasts waiting for a free place, oldest first. */
    TArray<FQueued> Waiting;
    /** Story lines waiting their turn as subtitles, oldest first. */
    TArray<FQueued> Lines;
    FName SubtitleKey;
    double SubtitleShown = -1.0;
    double SubtitleLife = 0.0;

    static constexpr int32 MaxVisible = 4;
    static constexpr int32 MaxWaiting = 8;
    static constexpr int32 MaxLines = 6;
};
