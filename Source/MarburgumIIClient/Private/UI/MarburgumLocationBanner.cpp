#include "UI/MarburgumLocationBanner.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/SafeZone.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "MarburgumArea.h"
#include "MarburgumGameState.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumPlayerState.h"
#include "MarburgumWeather.h"
#include "UI/MarburgumUIBuilder.h"

#define LOCTEXT_NAMESPACE "MarburgumLocationBanner"

using namespace MarburgumUI;

namespace
{
    constexpr float TopOffset = 56.f;
    constexpr float MinWidth = 360.f;
    constexpr float FadeInSeconds = 0.35f;
    constexpr float HoldSeconds = 3.2f;
    constexpr float FadeOutSeconds = 0.8f;

    void Center(UWidget* Child)
    {
        if (UVerticalBoxSlot* Slot = Cast<UVerticalBoxSlot>(Child->Slot))
        {
            Slot->SetHorizontalAlignment(HAlign_Center);
        }
    }
}

void UMarburgumLocationBanner::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);

    UVerticalBox* Column = B.Column();
    KindLabel = B.Text(FText::GetEmpty(), ETypeRole::Overline, T.Color.Accent);
    FBuilder::Stack(Column, KindLabel, T.Space.XS);
    Center(KindLabel);
    NameLabel = B.Text(FText::GetEmpty(), ETypeRole::Title);
    FBuilder::Stack(Column, NameLabel, T.Space.S);
    Center(NameLabel);
    UWidget* Rule = B.Rule(T.Color.Accent, 2.f, 64.f);
    FBuilder::Stack(Column, Rule, T.Space.S);
    Center(Rule);
    NoteLabel = B.Text(FText::GetEmpty(), ETypeRole::Caption);
    NoteLabel->SetJustification(ETextJustify::Center);
    FBuilder::Stack(Column, NoteLabel, T.Space.XS);
    Center(NoteLabel);
    WeatherLabel = B.Text(FText::GetEmpty(), ETypeRole::Caption, T.Color.TextSecondary);
    WeatherLabel->SetJustification(ETextJustify::Center);
    WeatherLabel->SetVisibility(ESlateVisibility::Collapsed);
    FBuilder::Stack(Column, WeatherLabel, 0.f);
    Center(WeatherLabel);

    Card = B.Surface(ESurfaceKind::Overlay, Pad(T.Space.XXL, T.Space.L), Column);
    USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>();
    Width->SetMinDesiredWidth(MinWidth);
    Width->SetContent(Card);

    UBorder* Anchor = B.Surface(ESurfaceKind::Base, FMargin(0.f, TopOffset, 0.f, 0.f), Width, false);
    Anchor->SetBrushColor(FLinearColor::Transparent);
    FBuilder::Align(Anchor, HAlign_Center, VAlign_Top);

    USafeZone* Safe = WidgetTree->ConstructWidget<USafeZone>();
    Safe->SetContent(Anchor);
    WidgetTree->RootWidget = Safe;

    // The widget stays visible so it keeps ticking; only the card collapses between banners.
    SetVisibility(ESlateVisibility::HitTestInvisible);
    Card->SetVisibility(ESlateVisibility::Collapsed);
}

void UMarburgumLocationBanner::NativeConstruct()
{
    Super::NativeConstruct();
    AreaHandle = AMarburgumPlayerState::OnAreaChanged().AddUObject(this, &UMarburgumLocationBanner::HandleAreaChanged);
    WeatherHandle = AMarburgumGameState::OnWeatherChanged().AddUObject(this, &UMarburgumLocationBanner::HandleWeatherChanged);
    if (const FMarburgumWeather* Weather = AMarburgumGameState::WeatherOf(GetWorld()))
    {
        HandleWeatherChanged(*Weather);
    }
    const APlayerController* Owner = GetOwningPlayer();
    if (AMarburgumPlayerState* State = Owner ? Owner->GetPlayerState<AMarburgumPlayerState>() : nullptr)
    {
        HandleAreaChanged(State); // The first value may have arrived before the HUD was mounted.
    }
}

void UMarburgumLocationBanner::NativeDestruct()
{
    AMarburgumPlayerState::OnAreaChanged().Remove(AreaHandle);
    AMarburgumGameState::OnWeatherChanged().Remove(WeatherHandle);
    Super::NativeDestruct();
}

void UMarburgumLocationBanner::HandleAreaChanged(AMarburgumPlayerState* State)
{
    const APlayerController* Owner = GetOwningPlayer();
    if (!State || !Owner || State != Owner->PlayerState || !State->IsLocationKnown())
    {
        return;
    }
    if (bHasShown && State->GetAreaId() == ShownArea)
    {
        return; // Both replicated properties report the same change.
    }
    Show(State->GetAreaId());
}

void UMarburgumLocationBanner::HandleWeatherChanged(const FMarburgumWeather& Weather)
{
    if (!Weather.bKnown)
    {
        return;
    }
    WeatherLabel->SetText(FText::FromString(MarburgumII::Weather::Summary(Weather.State.ToString(), Weather.TemperatureC, Weather.Wetness, Weather.SnowDepthM)));
    WeatherLabel->SetVisibility(ESlateVisibility::HitTestInvisible);
    const bool bTurned = !WeatherState.IsNone() && (Weather.State != WeatherState || Weather.PrecipitationType != WeatherType);
    WeatherState = Weather.State;
    WeatherType = Weather.PrecipitationType;
    if (bTurned && bHasShown && !IsShowing())
    {
        Show(ShownArea);
    }
}

FString UMarburgumLocationBanner::GetWeatherText() const
{
    return WeatherLabel && WeatherLabel->GetVisibility() != ESlateVisibility::Collapsed ? WeatherLabel->GetText().ToString() : FString();
}

void UMarburgumLocationBanner::Show(FName AreaId)
{
    bHasShown = true;
    ShownArea = AreaId;
    const MarburgumII::Gameplay::FAreaDefinition* Area = MarburgumII::Gameplay::FindArea(AreaId.ToString());
    const AMarburgumArea* Placed = AMarburgumArea::Find(GetWorld(), AreaId);

    if (Area)
    {
        KindLabel->SetText(LOCTEXT("District", "DISTRICT"));
        NameLabel->SetText(FText::FromString(Area->DisplayName));
        NoteLabel->SetText(Placed && !Placed->Note.IsEmpty() ? Placed->Note : LOCTEXT("Period", "Maribor  ·  1990s"));
    }
    else if (AreaId.IsNone())
    {
        KindLabel->SetText(LOCTEXT("City", "CITY"));
        NameLabel->SetText(FText::FromString(MarburgumII::Gameplay::OutsideAreaName));
        NoteLabel->SetText(LOCTEXT("Outside", "Outside the named districts"));
    }
    else
    {
        // A server newer than this client: show the id rather than nothing, and say so.
        KindLabel->SetText(LOCTEXT("Unknown", "PLACE"));
        NameLabel->SetText(FText::FromName(AreaId));
        NoteLabel->SetText(LOCTEXT("Unregistered", "Not registered in this client version"));
    }
    Elapsed = 0.f;
    Card->SetRenderOpacity(0.f);
    Card->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UMarburgumLocationBanner::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    if (Elapsed < 0.f)
    {
        return;
    }
    Elapsed += InDeltaTime;
    const float Total = FadeInSeconds + HoldSeconds + FadeOutSeconds;
    if (Elapsed >= Total)
    {
        Elapsed = -1.f;
        Card->SetVisibility(ESlateVisibility::Collapsed);
        return;
    }
    const float In = FMath::Clamp(Elapsed / FadeInSeconds, 0.f, 1.f);
    const float Out = FMath::Clamp((Total - Elapsed) / FadeOutSeconds, 0.f, 1.f);
    Card->SetRenderOpacity(FMath::InterpEaseOut(0.f, 1.f, FMath::Min(In, Out), 2.f));
}

#undef LOCTEXT_NAMESPACE
