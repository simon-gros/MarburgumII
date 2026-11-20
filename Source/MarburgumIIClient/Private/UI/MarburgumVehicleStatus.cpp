#include "UI/MarburgumVehicleStatus.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SafeZone.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GameFramework/PlayerController.h"
#include "MarburgumCharacter.h"
#include "MarburgumVehicle.h"
#include "UI/MarburgumUIBuilder.h"

#define LOCTEXT_NAMESPACE "MarburgumVehicleStatus"

using namespace MarburgumUI;

namespace
{
    /** The original showed REV once the car rolled backward faster than this (m/s). */
    constexpr float ReverseShownBelow = -0.5f;
    /** The dial's scale: a 1990s city hatchback, not a sports car. */
    constexpr float DialKmh = 120.f;
}

void UMarburgumVehicleStatus::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);

    // A driver's readout: big numerals, the unit and the gear beside them, a thin speed bar, then the car and seat.
    UVerticalBox* Column = B.Column();
    UHorizontalBox* Readout = B.Row();
    SpeedLabel = B.Text(FText::GetEmpty(), ETypeRole::NumericLarge, T.Color.TextPrimary);
    SpeedLabel->SetShadowOffset(FVector2D(0.f, 1.f));
    SpeedLabel->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.5f));
    FBuilder::Inline(Readout, SpeedLabel, T.Space.S, false, false);
    UVerticalBox* Units = B.Column();
    FBuilder::Stack(Units, B.Text(LOCTEXT("Unit", "KM/H"), ETypeRole::Overline, T.Color.TextSecondary), T.Space.XS);
    UTextBlock* GearText = nullptr;
    GearChip = B.Chip(EMarburgumGlyph::ChevronUp, LOCTEXT("Drive", "DRIVE"), ETone::Neutral, &GearText);
    GearLabel = GearText;
    FBuilder::Stack(Units, GearChip, 0.f);
    if (UVerticalBoxSlot* GearSlot = Cast<UVerticalBoxSlot>(GearChip->Slot))
    {
        GearSlot->SetHorizontalAlignment(HAlign_Left);
    }
    UHorizontalBoxSlot* UnitsSlot = Readout->AddChildToHorizontalBox(Units);
    UnitsSlot->SetVerticalAlignment(VAlign_Bottom);
    UnitsSlot->SetPadding(FMargin(0.f, 0.f, 0.f, T.Space.S));
    FBuilder::Stack(Column, Readout, T.Space.XS);
    SpeedBar = B.Bar(ETone::Accent, 0.f, 4.f, 6);
    USizeBox* BarBox = B.Spacer(220.f, 6.f);
    BarBox->SetContent(SpeedBar);
    FBuilder::Stack(Column, BarBox, T.Space.S);
    UHorizontalBox* Identity = B.Row();
    UTextBlock* Name = nullptr;
    FBuilder::Inline(Identity, B.GlyphText(EMarburgumGlyph::Car, FText::GetEmpty(), ETypeRole::Label, T.Color.TextPrimary, &Name, 16.f), T.Space.S);
    NameLabel = Name;
    SeatLabel = B.Text(FText::GetEmpty(), ETypeRole::Caption, T.Color.TextSecondary);
    FBuilder::Inline(Identity, SeatLabel, 0.f);
    FBuilder::Stack(Column, Identity, 0.f);

    UBorder* Content = B.Surface(ESurfaceKind::Base, FMargin(T.Space.XXL, T.Space.M, T.Space.L, T.Space.M), Column, false);
    Content->SetBrushColor(FLinearColor::Transparent);
    UOverlay* Stack = WidgetTree->ConstructWidget<UOverlay>();
    UOverlaySlot* ShadeSlot = Stack->AddChildToOverlay(B.Scrim(EMarburgumScrimShape::FadeLeft,
        T.Color.Backdrop.CopyWithNewOpacity(T.bHighContrast ? 0.9f : 0.58f)));
    ShadeSlot->SetHorizontalAlignment(HAlign_Fill);
    ShadeSlot->SetVerticalAlignment(VAlign_Fill);
    Stack->AddChildToOverlay(Content);
    Card = B.Surface(ESurfaceKind::Base, Pad(0.f), Stack, false);
    Card->SetBrushColor(FLinearColor::Transparent);

    UBorder* Anchor = B.Surface(ESurfaceKind::Base, FMargin(0.f, 0.f, T.Space.XL, T.Space.XL), Card, false);
    Anchor->SetBrushColor(FLinearColor::Transparent);
    FBuilder::Align(Anchor, HAlign_Right, VAlign_Bottom);

    USafeZone* Safe = WidgetTree->ConstructWidget<USafeZone>();
    Safe->SetContent(Anchor);
    WidgetTree->RootWidget = Safe;
    SetVisibility(ESlateVisibility::HitTestInvisible);
    Card->SetVisibility(ESlateVisibility::Collapsed);
}

void UMarburgumVehicleStatus::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    Refresh();
}

bool UMarburgumVehicleStatus::IsSeated() const
{
    return !ShownName.IsEmpty();
}

void UMarburgumVehicleStatus::Refresh()
{
    const APlayerController* Owner = GetOwningPlayer();
    const AMarburgumCharacter* Body = Owner ? Cast<AMarburgumCharacter>(Owner->GetPawn()) : nullptr;
    const AMarburgumVehicle* Vehicle = Body ? Body->GetSeatedVehicle() : nullptr;
    if (!Vehicle)
    {
        if (!ShownName.IsEmpty())
        {
            ShownName.Reset();
            ShownSpeed.Reset();
            ShownSeat.Reset();
            Card->SetVisibility(ESlateVisibility::Collapsed);
        }
        return;
    }
    const FTokens& T = Tokens();
    const FString Name = Vehicle->GetDisplayName().ToString();
    if (Name != ShownName)
    {
        ShownName = Name;
        NameLabel->SetText(FText::FromString(Name));
        Card->SetVisibility(ESlateVisibility::HitTestInvisible);
    }
    const bool bDriving = Body->IsDriving();
    const FString Seat = bDriving ? TEXT("driver") : TEXT("passenger");
    if (Seat != ShownSeat)
    {
        ShownSeat = Seat;
        SeatLabel->SetText(bDriving ? LOCTEXT("Driving", "Driver's seat") : LOCTEXT("Passenger", "Passenger seat"));
    }
    const float Speed = Vehicle->GetForwardSpeed();
    const int32 Kmh = FMath::RoundToInt(FMath::Abs(Speed) * 3.6f);
    const bool bReverse = Speed < ReverseShownBelow;
    // The capture string keeps the original HUD's wording ("REV · 12 km/h"); the readout splits it into its parts.
    const FString Text = FString::Printf(TEXT("%s%d km/h"), bReverse ? TEXT("REV · ") : TEXT(""), Kmh);
    if (Text != ShownSpeed)
    {
        ShownSpeed = Text;
        SpeedLabel->SetText(FText::AsNumber(Kmh));
        GearLabel->SetText(bReverse ? LOCTEXT("Reverse", "REVERSE") : Kmh == 0 ? LOCTEXT("Stopped", "STOPPED") : LOCTEXT("Forward", "DRIVE"));
        const FLinearColor GearColor = bReverse ? T.Color.Warning : T.Color.TextSecondary;
        GearLabel->SetColorAndOpacity(FSlateColor(GearColor));
        if (UHorizontalBox* ChipRow = Cast<UHorizontalBox>(GearChip->GetContent()))
        {
            if (UMarburgumGlyph* Mark = Cast<UMarburgumGlyph>(ChipRow->GetChildAt(0)))
            {
                Mark->SetGlyph(bReverse ? EMarburgumGlyph::ChevronDown : EMarburgumGlyph::ChevronUp);
                Mark->SetColor(GearColor);
            }
        }
        GearChip->SetBrush(RoundedBrush(GearColor.CopyWithNewOpacity(0.14f), T.Radius.Pill, GearColor.CopyWithNewOpacity(0.6f), T.BorderWidth));
        SpeedBar->SetFillColor(bReverse ? T.Color.Warning : T.Color.Accent);
    }
    SpeedBar->SetPercent(FMath::Clamp(Kmh / DialKmh, 0.f, 1.f));
}

#undef LOCTEXT_NAMESPACE
