#include "UI/MarburgumLocationBanner.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
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
    constexpr float TopOffset = 40.f;
    constexpr float BandWidth = 880.f;
    constexpr float FadeInSeconds = 0.45f;
    constexpr float HoldSeconds = 3.4f;
    constexpr float FadeOutSeconds = 0.9f;
    /** The title settles from slightly above as it fades in (none with reduced motion). */
    constexpr float SettleOffset = 10.f;

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

    // A title card in the manner of a documentary's place caption: the kind between two copper hairlines, the name in
    // the serif, the street and landmark under it, and one quiet line of date, time and weather. No box: a soft band
    // of shade keeps it legible over a bright sky without walling off the street.
    UVerticalBox* Column = B.Column();
    UHorizontalBox* KindLine = B.Row();
    FBuilder::Inline(KindLine, B.Rule(T.Color.Hairline, 1.f, 36.f), T.Space.M);
    KindLabel = B.Text(FText::GetEmpty(), ETypeRole::Overline, T.Color.Accent);
    FBuilder::Inline(KindLine, KindLabel, T.Space.M);
    FBuilder::Inline(KindLine, B.Rule(T.Color.Hairline, 1.f, 36.f), 0.f);
    FBuilder::Stack(Column, KindLine, T.Space.XS);
    Center(KindLine);
    NameLabel = B.Text(FText::GetEmpty(), ETypeRole::PlaceTitle);
    NameLabel->SetShadowOffset(FVector2D(0.f, 1.f));
    NameLabel->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.55f));
    FBuilder::Stack(Column, NameLabel, T.Space.XXS);
    Center(NameLabel);
    // The street or square, then the landmark (ADR-063): collapsed when the player is on none.
    StreetLabel = B.Text(FText::GetEmpty(), ETypeRole::Subheading, T.Color.TextPrimary);
    StreetLabel->SetJustification(ETextJustify::Center);
    StreetLabel->SetVisibility(ESlateVisibility::Collapsed);
    FBuilder::Stack(Column, StreetLabel, T.Space.XXS);
    Center(StreetLabel);
    LandmarkLabel = B.Text(FText::GetEmpty(), ETypeRole::Quote, T.Color.Accent);
    LandmarkLabel->SetJustification(ETextJustify::Center);
    LandmarkLabel->SetVisibility(ESlateVisibility::Collapsed);
    FBuilder::Stack(Column, LandmarkLabel, T.Space.XS);
    Center(LandmarkLabel);
    FBuilder::Stack(Column, B.Spacer(1.f, T.Space.XS), 0.f);
    // The world clock's 1990s date and time (ADR-064) and the server's weather (ADR-061) on one line.
    UHorizontalBox* Meta = B.Row();
    UTextBlock* ClockText = nullptr;
    FBuilder::Inline(Meta, B.GlyphText(EMarburgumGlyph::Clock, FText::GetEmpty(), ETypeRole::Caption, T.Color.TextSecondary, &ClockText, 14.f),
        T.Space.L);
    ClockLabel = ClockText;
    ClockLabel->SetVisibility(ESlateVisibility::Collapsed);
    WeatherLabel = B.Text(FText::GetEmpty(), ETypeRole::Caption, T.Color.TextSecondary);
    WeatherLabel->SetVisibility(ESlateVisibility::Collapsed);
    FBuilder::Inline(Meta, WeatherLabel, 0.f);
    FBuilder::Stack(Column, Meta, T.Space.XXS);
    Center(Meta);
    // The area's provenance note (e.g. fictional route names), quietly, so a label is never passed off as history.
    NoteLabel = B.Text(FText::GetEmpty(), ETypeRole::Caption, T.Color.TextMuted);
    NoteLabel->SetJustification(ETextJustify::Center);
    NoteLabel->SetAutoWrapText(true);
    FBuilder::Stack(Column, NoteLabel, 0.f);
    Center(NoteLabel);

    UBorder* Content = B.Surface(ESurfaceKind::Base, FMargin(T.Space.XXXL, T.Space.L, T.Space.XXXL, T.Space.L), Column, false);
    Content->SetBrushColor(FLinearColor::Transparent);
    FBuilder::Align(Content, HAlign_Center, VAlign_Center);

    UOverlay* Stackup = WidgetTree->ConstructWidget<UOverlay>();
    UOverlaySlot* ShadeSlot = Stackup->AddChildToOverlay(B.Scrim(EMarburgumScrimShape::HorizontalBand,
        T.Color.Backdrop.CopyWithNewOpacity(T.bHighContrast ? 0.88f : 0.62f)));
    ShadeSlot->SetHorizontalAlignment(HAlign_Fill);
    ShadeSlot->SetVerticalAlignment(VAlign_Fill);
    UOverlaySlot* ContentSlot = Stackup->AddChildToOverlay(Content);
    ContentSlot->SetHorizontalAlignment(HAlign_Fill);
    ContentSlot->SetVerticalAlignment(VAlign_Center);

    USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>();
    Width->SetMaxDesiredWidth(BandWidth);
    Width->SetMinDesiredWidth(BandWidth * 0.6f);
    Width->SetContent(Stackup);
    Card = B.Surface(ESurfaceKind::Base, Pad(0.f), Width, false);
    Card->SetBrushColor(FLinearColor::Transparent);

    UBorder* Anchor = B.Surface(ESurfaceKind::Base, FMargin(0.f, TopOffset, 0.f, 0.f), Card, false);
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
    // Kept current between banners too, so the line is right the moment one comes up; the clock reads in minutes,
    // so once a second is plenty (QA U14: no string work every frame).
    SinceClock += InDeltaTime;
    if (SinceClock >= 1.f || ClockLabel->GetVisibility() == ESlateVisibility::Collapsed)
    {
        SinceClock = 0.f;
        UpdateClock();
    }
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
    if (Tokens().bReducedMotion)
    {
        // Reduced motion: on and off without a fade or slide; the hold is unchanged.
        Card->SetRenderOpacity(1.f);
        Card->SetRenderTranslation(FVector2D::ZeroVector);
        return;
    }
    const float In = FMath::Clamp(Elapsed / FadeInSeconds, 0.f, 1.f);
    const float Out = FMath::Clamp((Total - Elapsed) / FadeOutSeconds, 0.f, 1.f);
    Card->SetRenderOpacity(FMath::InterpEaseOut(0.f, 1.f, FMath::Min(In, Out), 2.f));
    Card->SetRenderTranslation(FVector2D(0.f, -SettleOffset * (1.f - FMath::InterpEaseOut(0.f, 1.f, In, 3.f))));
}

#undef LOCTEXT_NAMESPACE
