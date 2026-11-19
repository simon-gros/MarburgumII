#include "UI/MarburgumNotificationFeed.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/SafeZone.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "HAL/PlatformTime.h"
#include "UI/MarburgumUIBuilder.h"

using namespace MarburgumUI;

namespace
{
    constexpr float FeedWidth = 380.f;
    constexpr float TopOffset = 96.f;
    /** About 15 characters a second of reading on top of a glance, never past 12 s. */
    constexpr double GlanceSeconds = 2.0;
    constexpr double SecondsPerCharacter = 1.0 / 15.0;
    constexpr double LongestSeconds = 12.0;
}

void UMarburgumNotificationFeed::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);

    Stack = B.Column();
    USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>();
    Width->SetWidthOverride(FeedWidth);
    Width->SetContent(Stack);

    // Right edge, below the objective region; never centered over the street.
    UBorder* Anchor = B.Surface(ESurfaceKind::Base, FMargin(0.f, TopOffset, T.Space.XXL, 0.f), Width, false);
    Anchor->SetBrushColor(FLinearColor::Transparent);
    FBuilder::Align(Anchor, HAlign_Right, VAlign_Top);

    USafeZone* Safe = WidgetTree->ConstructWidget<USafeZone>();
    Safe->SetContent(Anchor);
    WidgetTree->RootWidget = Safe;
    SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UMarburgumNotificationFeed::Push(FName DedupeKey, const FText& Kind, const FText& Message, ETone Tone, bool bStory)
{
    if (!Stack)
    {
        return;
    }
    for (int32 Index = Entries.Num() - 1; Index >= 0; --Index)
    {
        if (Entries[Index].Key == DedupeKey)
        {
            RemoveEntry(Index);
        }
    }
    Waiting.RemoveAll([DedupeKey](const FQueued& Queued) { return Queued.Key == DedupeKey; });

    const FQueued Toast{ DedupeKey, Kind, Message, Tone, bStory };
    if (Entries.Num() >= MaxVisible)
    {
        // A story line is not pushed out by the rewards that came with it: it takes the oldest other toast's place.
        const int32 Other = bStory ? Entries.IndexOfByPredicate([](const FEntry& Entry) { return !Entry.bStory; }) : INDEX_NONE;
        if (Other == INDEX_NONE)
        {
            Waiting.Add(Toast);
            if (Waiting.Num() > MaxWaiting)
            {
                Waiting.RemoveAt(0);
            }
            return;
        }
        RemoveEntry(Other);
    }
    Show(Toast);
}

void UMarburgumNotificationFeed::Show(const FQueued& Queued)
{
    FBuilder B(*WidgetTree);
    UBorder* Toast = B.Toast(Queued.Kind, Queued.Message, Queued.Tone);
    FBuilder::Stack(Stack, Toast, Tokens().Space.S);
    const double Reading = GlanceSeconds + SecondsPerCharacter * (Queued.Kind.ToString().Len() + Queued.Message.ToString().Len());
    const double Life = FMath::Clamp(Reading, static_cast<double>(Tokens().ToastSeconds), LongestSeconds);
    Entries.Add({ Queued.Key, Toast, FPlatformTime::Seconds(), Life, Queued.bStory });
}

void UMarburgumNotificationFeed::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    const FTokens& T = Tokens();
    const double Now = FPlatformTime::Seconds();
    for (int32 Index = Entries.Num() - 1; Index >= 0; --Index)
    {
        UWidget* Widget = Entries[Index].Widget.Get();
        const double Age = Now - Entries[Index].ShownSeconds;
        if (!Widget || Age >= Entries[Index].LifeSeconds)
        {
            RemoveEntry(Index);
            continue;
        }
        const double Remaining = Entries[Index].LifeSeconds - Age;
        const float FadeIn = FMath::Clamp(static_cast<float>(Age / T.FadeSeconds), 0.f, 1.f);
        const float FadeOut = FMath::Clamp(static_cast<float>(Remaining / (T.FadeSeconds * 2.0)), 0.f, 1.f);
        Widget->SetRenderOpacity(FMath::Min(FadeIn, FadeOut));
    }
    while (Entries.Num() < MaxVisible && Waiting.Num() > 0)
    {
        const FQueued Next = Waiting[0];
        Waiting.RemoveAt(0);
        Show(Next);
    }
}

void UMarburgumNotificationFeed::RemoveEntry(int32 Index)
{
    if (UWidget* Widget = Entries[Index].Widget.Get())
    {
        Widget->RemoveFromParent();
    }
    Entries.RemoveAt(Index);
}
