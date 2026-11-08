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

void UMarburgumNotificationFeed::Push(FName DedupeKey, const FText& Kind, const FText& Message, ETone Tone)
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
    while (Entries.Num() >= MaxVisible)
    {
        RemoveEntry(0);
    }

    FBuilder B(*WidgetTree);
    UBorder* Toast = B.Toast(Kind, Message, Tone);
    FBuilder::Stack(Stack, Toast, Tokens().Space.S);
    Entries.Add({ DedupeKey, Toast, FPlatformTime::Seconds() });
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
        if (!Widget || Age >= T.ToastSeconds)
        {
            RemoveEntry(Index);
            continue;
        }
        const double Remaining = T.ToastSeconds - Age;
        const float FadeIn = FMath::Clamp(static_cast<float>(Age / T.FadeSeconds), 0.f, 1.f);
        const float FadeOut = FMath::Clamp(static_cast<float>(Remaining / (T.FadeSeconds * 2.0)), 0.f, 1.f);
        Widget->SetRenderOpacity(FMath::Min(FadeIn, FadeOut));
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
