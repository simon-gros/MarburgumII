#include "UI/MarburgumNotificationFeed.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SafeZone.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "HAL/PlatformTime.h"
#include "UI/MarburgumUIBuilder.h"

using namespace MarburgumUI;

namespace
{
    constexpr float FeedWidth = 360.f;
    constexpr float SubtitleWidth = 860.f;
    constexpr float SubtitleBottom = 132.f;
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
    Width->SetWidthOverride(FeedWidth * T.TextScale);
    Width->SetContent(Stack);

    // Right edge, top: never centred over the street.
    UBorder* Anchor = B.Surface(ESurfaceKind::Base, FMargin(0.f, T.Space.XL, T.Space.XL, 0.f), Width, false);
    Anchor->SetBrushColor(FLinearColor::Transparent);
    FBuilder::Align(Anchor, HAlign_Right, VAlign_Top);

    // Subtitles: lower centre, the speaker in spaced capitals over the line in the serif italic.
    UVerticalBox* Said = B.Column();
    SpeakerLabel = B.Text(FText::GetEmpty(), ETypeRole::Overline, T.Color.Accent);
    SpeakerLabel->SetJustification(ETextJustify::Center);
    FBuilder::Stack(Said, SpeakerLabel, T.Space.XS);
    if (UVerticalBoxSlot* SpeakerSlot = Cast<UVerticalBoxSlot>(SpeakerLabel->Slot))
    {
        SpeakerSlot->SetHorizontalAlignment(HAlign_Center);
    }
    LineLabel = B.Text(FText::GetEmpty(), ETypeRole::Quote, T.Color.TextPrimary);
    LineLabel->SetJustification(ETextJustify::Center);
    LineLabel->SetAutoWrapText(true);
    LineLabel->SetShadowOffset(FVector2D(0.f, 1.f));
    LineLabel->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.6f));
    FBuilder::Stack(Said, LineLabel, 0.f);
    UBorder* SaidPad = B.Surface(ESurfaceKind::Base, FMargin(T.Space.XXXL * 1.5f, T.Space.M), Said, false);
    SaidPad->SetBrushColor(FLinearColor::Transparent);
    UOverlay* SaidStack = WidgetTree->ConstructWidget<UOverlay>();
    UOverlaySlot* ShadeSlot = SaidStack->AddChildToOverlay(B.Scrim(EMarburgumScrimShape::HorizontalBand,
        T.Color.Backdrop.CopyWithNewOpacity(T.bHighContrast ? 0.9f : 0.62f)));
    ShadeSlot->SetHorizontalAlignment(HAlign_Fill);
    ShadeSlot->SetVerticalAlignment(VAlign_Fill);
    SaidStack->AddChildToOverlay(SaidPad);
    USizeBox* SaidWidth = B.Bounded(SaidStack, SubtitleWidth * T.TextScale);
    Subtitle = SaidWidth;
    Subtitle->SetVisibility(ESlateVisibility::Collapsed);
    UBorder* SaidAnchor = B.Surface(ESurfaceKind::Base, FMargin(T.Space.XXL, 0.f, T.Space.XXL, SubtitleBottom), SaidWidth, false);
    SaidAnchor->SetBrushColor(FLinearColor::Transparent);
    FBuilder::Align(SaidAnchor, HAlign_Center, VAlign_Bottom);

    UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>();
    for (UWidget* Layer : { static_cast<UWidget*>(Anchor), static_cast<UWidget*>(SaidAnchor) })
    {
        UOverlaySlot* LayerSlot = Root->AddChildToOverlay(Layer);
        LayerSlot->SetHorizontalAlignment(HAlign_Fill);
        LayerSlot->SetVerticalAlignment(VAlign_Fill);
    }
    USafeZone* Safe = WidgetTree->ConstructWidget<USafeZone>();
    Safe->SetContent(Root);
    WidgetTree->RootWidget = Safe;
    SetVisibility(ESlateVisibility::HitTestInvisible);
}

double UMarburgumNotificationFeed::ReadingSeconds(const FQueued& Item) const
{
    const FTokens& T = Tokens();
    double Reading = GlanceSeconds + SecondsPerCharacter * (Item.Kind.ToString().Len() + Item.Message.ToString().Len());
    if (Item.Tone == ETone::Danger || Item.Tone == ETone::Warning)
    {
        Reading *= 1.3; // A refusal is worth reading twice.
    }
    return FMath::Clamp(Reading, static_cast<double>(T.ToastSeconds), LongestSeconds) * T.NotificationTimeScale;
}

void UMarburgumNotificationFeed::Push(FName DedupeKey, const FText& Kind, const FText& Message, ETone Tone, bool bStory, EMarburgumGlyph Icon)
{
    if (!Stack)
    {
        return;
    }
    const FQueued Item{ DedupeKey, Kind, Message, Tone, Icon };
    if (bStory)
    {
        // A replayed line replaces its earlier self; otherwise lines wait their turn.
        Lines.RemoveAll([DedupeKey](const FQueued& Queued) { return Queued.Key == DedupeKey; });
        if (SubtitleKey == DedupeKey && SubtitleShown >= 0.0)
        {
            ShowSubtitle(Item);
            return;
        }
        Lines.Add(Item);
        if (Lines.Num() > MaxLines)
        {
            Lines.RemoveAt(0);
        }
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
    if (Entries.Num() >= MaxVisible)
    {
        Waiting.Add(Item);
        if (Waiting.Num() > MaxWaiting)
        {
            Waiting.RemoveAt(0);
        }
        return;
    }
    Show(Item);
}

void UMarburgumNotificationFeed::Show(const FQueued& Queued)
{
    FBuilder B(*WidgetTree);
    UBorder* Toast = B.Toast(Queued.Kind, Queued.Message, Queued.Tone, Queued.Icon);
    FBuilder::Stack(Stack, Toast, Tokens().Space.S);
    Entries.Add({ Queued.Key, Toast, FPlatformTime::Seconds(), ReadingSeconds(Queued) });
}

void UMarburgumNotificationFeed::ShowSubtitle(const FQueued& Line)
{
    SubtitleKey = Line.Key;
    SpeakerLabel->SetText(Line.Kind);
    SpeakerLabel->SetColorAndOpacity(FSlateColor(Line.Tone == ETone::Info ? Tokens().Color.Info : Tokens().Color.Accent));
    LineLabel->SetText(Line.Message);
    SubtitleShown = FPlatformTime::Seconds();
    SubtitleLife = ReadingSeconds(Line) + 1.0;
    Subtitle->SetRenderOpacity(Tokens().bReducedMotion ? 1.f : 0.f);
    Subtitle->SetVisibility(ESlateVisibility::HitTestInvisible);
}

FString UMarburgumNotificationFeed::GetShownSubtitle() const
{
    return SubtitleShown >= 0.0 && LineLabel ? LineLabel->GetText().ToString() : FString();
}

void UMarburgumNotificationFeed::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    const FTokens& T = Tokens();
    const double Now = FPlatformTime::Seconds();
    const float Fade = FMath::Max(T.FadeSeconds, 0.001f);
    for (int32 Index = Entries.Num() - 1; Index >= 0; --Index)
    {
        UWidget* Widget = Entries[Index].Widget.Get();
        const double Age = Now - Entries[Index].ShownSeconds;
        if (!Widget || Age >= Entries[Index].LifeSeconds)
        {
            RemoveEntry(Index);
            continue;
        }
        if (T.bReducedMotion)
        {
            Widget->SetRenderOpacity(1.f);
            continue;
        }
        const double Remaining = Entries[Index].LifeSeconds - Age;
        const float FadeIn = FMath::Clamp(static_cast<float>(Age / Fade), 0.f, 1.f);
        const float FadeOut = FMath::Clamp(static_cast<float>(Remaining / (Fade * 2.0)), 0.f, 1.f);
        Widget->SetRenderOpacity(FMath::Min(FadeIn, FadeOut));
        // New toasts slide in from the edge they live on.
        Widget->SetRenderTranslation(FVector2D(16.f * (1.f - FMath::InterpEaseOut(0.f, 1.f, FadeIn, 3.f)), 0.f));
    }
    while (Entries.Num() < MaxVisible && Waiting.Num() > 0)
    {
        const FQueued Next = Waiting[0];
        Waiting.RemoveAt(0);
        Show(Next);
    }

    if (SubtitleShown >= 0.0)
    {
        const double Age = Now - SubtitleShown;
        if (Age >= SubtitleLife)
        {
            SubtitleShown = -1.0;
            SubtitleKey = NAME_None;
            Subtitle->SetVisibility(ESlateVisibility::Collapsed);
        }
        else if (!T.bReducedMotion)
        {
            const float In = FMath::Clamp(static_cast<float>(Age / 0.25), 0.f, 1.f);
            const float Out = FMath::Clamp(static_cast<float>((SubtitleLife - Age) / 0.4), 0.f, 1.f);
            Subtitle->SetRenderOpacity(FMath::Min(In, Out));
        }
    }
    if (SubtitleShown < 0.0 && Lines.Num() > 0)
    {
        const FQueued Next = Lines[0];
        Lines.RemoveAt(0);
        ShowSubtitle(Next);
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
