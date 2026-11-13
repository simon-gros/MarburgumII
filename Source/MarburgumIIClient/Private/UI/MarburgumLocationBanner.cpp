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
#include "MarburgumPlaces.h"
#include "MarburgumPlayerState.h"
#include "MarburgumWeather.h"
#include "MarburgumWorldClock.h"
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
    FBuilder::Stack(Column, NameLabel, T.Space.XS);
    Center(NameLabel);
    // The street or square, then the landmark (ADR-063): collapsed when the player is on none.
    StreetLabel = B.Text(FText::GetEmpty(), ETypeRole::BodyStrong);
    StreetLabel->SetJustification(ETextJustify::Center);
    StreetLabel->SetVisibility(ESlateVisibility::Collapsed);
    FBuilder::Stack(Column, StreetLabel, T.Space.XS);
    Center(StreetLabel);
    LandmarkLabel = B.Text(FText::GetEmpty(), ETypeRole::Body, T.Color.Accent);
    LandmarkLabel->SetJustification(ETextJustify::Center);
    LandmarkLabel->SetVisibility(ESlateVisibility::Collapsed);
    FBuilder::Stack(Column, LandmarkLabel, T.Space.XS);
    Center(LandmarkLabel);
    UWidget* Rule = B.Rule(T.Color.Accent, 2.f, 64.f);
    FBuilder::Stack(Column, Rule, T.Space.S);
    Center(Rule);
    NoteLabel = B.Text(FText::GetEmpty(), ETypeRole::Caption);
    NoteLabel->SetJustification(ETextJustify::Center);
    FBuilder::Stack(Column, NoteLabel, T.Space.XS);
    Center(NoteLabel);
    // The world clock's 1990s date and time (ADR-064), kept current while the banner is up.
    ClockLabel = B.Text(FText::GetEmpty(), ETypeRole::Caption, T.Color.TextSecondary);
    ClockLabel->SetJustification(ETextJustify::Center);
    ClockLabel->SetVisibility(ESlateVisibility::Collapsed);
    FBuilder::Stack(Column, ClockLabel, 0.f);
    Center(ClockLabel);
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

bool FMarburgumPlaceAnnouncer::Observe(const FString& Place, const FString& Street, const FString& Landmark)
{
    if (Place.IsEmpty())
    {
        return false;
    }
    const bool bNewStreet = !Street.IsEmpty() && Street != LastStreet;
    if (!Street.IsEmpty())
    {
        LastStreet = Street;
    }
    const bool bNewLandmark = !Landmark.IsEmpty() && Landmark != LastLandmark;
    if (!Landmark.IsEmpty())
    {
        LastLandmark = Landmark;
    }
    if (Place != LastPlace || bNewStreet || bNewLandmark)
    {
        LastPlace = Place;
        return true;
    }
    return false;
}

void FMarburgumPlaceAnnouncer::Reset()
{
    LastPlace.Reset();
    LastStreet.Reset();
    LastLandmark.Reset();
}

void UMarburgumLocationBanner::HandleAreaChanged(AMarburgumPlayerState* State)
{
    const APlayerController* Owner = GetOwningPlayer();
    if (!State || !Owner || State != Owner->PlayerState || !State->IsLocationKnown())
    {
        return;
    }
    const FName AreaId = State->GetAreaId();
    const FMarburgumPlace& Place = State->GetPlace();
    Describe(AreaId, Place);
    // The place the announcement keys on: the area, else the district, else the city around them. The street is the
    // one shown, so a promenade named like its area ("Lent" on Lent) is not a new street.
    const FString Key = !AreaId.IsNone() ? TEXT("area:") + AreaId.ToString() : Place.District.IsEmpty() ? FString(TEXT("outside")) : Place.District;
    // Both replicated properties report the same change, and the announcer says nothing the second time.
    if (Announcer.Observe(Key, GetStreetText(), Place.Landmark))
    {
        Show();
    }
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
        Show();
    }
}

void UMarburgumLocationBanner::UpdateClock()
{
    FDateTime Utc;
    if (!AMarburgumGameState::WorldUtc(GetWorld(), Utc))
    {
        return;
    }
    const FString Text = MarburgumII::Clock::ShortLabel(MarburgumII::Clock::MarburgumAt(Utc));
    if (ClockLabel->GetText().ToString() != Text)
    {
        ClockLabel->SetText(FText::FromString(Text));
    }
    ClockLabel->SetVisibility(ESlateVisibility::HitTestInvisible);
}

FString UMarburgumLocationBanner::GetClockText() const
{
    return ClockLabel && ClockLabel->GetVisibility() != ESlateVisibility::Collapsed ? ClockLabel->GetText().ToString() : FString();
}

FString UMarburgumLocationBanner::GetWeatherText() const
{
    return WeatherLabel && WeatherLabel->GetVisibility() != ESlateVisibility::Collapsed ? WeatherLabel->GetText().ToString() : FString();
}

FString UMarburgumLocationBanner::GetPlaceText() const
{
    return NameLabel ? NameLabel->GetText().ToString() : FString();
}

FString UMarburgumLocationBanner::GetStreetText() const
{
    return StreetLabel && StreetLabel->GetVisibility() != ESlateVisibility::Collapsed ? StreetLabel->GetText().ToString() : FString();
}

FString UMarburgumLocationBanner::GetLandmarkText() const
{
    return LandmarkLabel && LandmarkLabel->GetVisibility() != ESlateVisibility::Collapsed ? LandmarkLabel->GetText().ToString() : FString();
}

void UMarburgumLocationBanner::Describe(FName AreaId, const FMarburgumPlace& Place)
{
    using namespace MarburgumII;
    ShownArea = AreaId;
    const Places::FCatalogue& Catalogue = Places::Get();
    const Gameplay::FAreaDefinition* Area = AreaId.IsNone() ? nullptr : Gameplay::FindArea(AreaId.ToString());
    const AMarburgumArea* Placed = AreaId.IsNone() ? nullptr : AMarburgumArea::Find(GetWorld(), AreaId);
    const Places::FDistrict* District = Place.District.IsEmpty() ? nullptr : Catalogue.FindDistrictById(Place.District);

    if (Area)
    {
        KindLabel->SetText(LOCTEXT("District", "DISTRICT"));
        NameLabel->SetText(FText::FromString(Area->DisplayName));
        NoteLabel->SetText(Placed && !Placed->Note.IsEmpty() ? Placed->Note : LOCTEXT("Period", "Maribor  ·  1990s"));
    }
    else if (!AreaId.IsNone())
    {
        // A server newer than this client: show the id rather than nothing, and say so.
        KindLabel->SetText(LOCTEXT("Unknown", "PLACE"));
        NameLabel->SetText(FText::FromName(AreaId));
        NoteLabel->SetText(LOCTEXT("Unregistered", "Not registered in this client version"));
    }
    else if (District)
    {
        // The register's units: a city quarter, or a village or local community round the city.
        const bool bQuarter = District->Kind == TEXT("quarter");
        KindLabel->SetText(bQuarter ? LOCTEXT("Quarter", "QUARTER")
            : District->Kind == TEXT("local_community") ? LOCTEXT("LocalCommunity", "LOCAL COMMUNITY") : LOCTEXT("Settlement", "SETTLEMENT"));
        NameLabel->SetText(FText::FromString(District->DisplayName));
        NoteLabel->SetText(bQuarter ? LOCTEXT("Period", "Maribor  ·  1990s") : LOCTEXT("Municipality", "Municipality of Maribor  ·  1990s"));
    }
    else if (!Place.District.IsEmpty())
    {
        KindLabel->SetText(LOCTEXT("Unknown", "PLACE"));
        NameLabel->SetText(FText::FromString(Place.District));
        NoteLabel->SetText(LOCTEXT("Unregistered", "Not registered in this client version"));
    }
    else
    {
        KindLabel->SetText(LOCTEXT("City", "CITY"));
        NameLabel->SetText(FText::FromString(Gameplay::OutsideAreaName));
        NoteLabel->SetText(LOCTEXT("Outside", "Outside the named districts"));
    }

    // A street named like the place it runs through (the Lent promenade on Lent) is said once, as the place.
    const bool bStreet = !Place.Street.IsEmpty() && Place.Street != NameLabel->GetText().ToString();
    StreetLabel->SetText(FText::FromString(bStreet ? Place.Street : FString()));
    StreetLabel->SetVisibility(bStreet ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

    const Places::FLandmark* Landmark = Place.Landmark.IsEmpty() ? nullptr : Catalogue.FindLandmark(Place.Landmark);
    const FString Sight = Landmark ? Landmark->DisplayName : Place.Landmark;
    LandmarkLabel->SetText(FText::FromString(Sight));
    LandmarkLabel->SetVisibility(Sight.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

void UMarburgumLocationBanner::Show()
{
    bHasShown = true;
    ++Announcements;
    if (IsShowing())
    {
        // Already up (a new street while the place is still showing): hold it afresh rather than fade it in again.
        Elapsed = FMath::Min(Elapsed, FadeInSeconds);
        return;
    }
    Elapsed = 0.f;
    UpdateClock();
    Card->SetRenderOpacity(0.f);
    Card->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UMarburgumLocationBanner::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    // Kept current between banners too, so the line is right the moment one comes up.
    UpdateClock();
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
