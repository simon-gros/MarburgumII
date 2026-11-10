#include "UI/MarburgumVehicleStatus.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/SafeZone.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "GameFramework/PlayerController.h"
#include "MarburgumCharacter.h"
#include "MarburgumVehicle.h"
#include "UI/MarburgumUIBuilder.h"

#define LOCTEXT_NAMESPACE "MarburgumVehicleStatus"

using namespace MarburgumUI;

namespace
{
    constexpr float CardWidth = 260.f;
    constexpr float BottomOffset = 32.f;
    /** The original showed REV once the car rolled backward faster than this (m/s). */
    constexpr float ReverseShownBelow = -0.5f;
}

void UMarburgumVehicleStatus::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);

    UVerticalBox* Column = B.Column();
    FBuilder::Stack(Column, B.Text(LOCTEXT("Overline", "VEHICLE"), ETypeRole::Overline, T.Color.Accent), T.Space.XS);
    NameLabel = B.Text(FText::GetEmpty(), ETypeRole::Heading);
    FBuilder::Stack(Column, NameLabel, T.Space.XXS);
    SeatLabel = B.Text(FText::GetEmpty(), ETypeRole::Caption, T.Color.TextMuted);
    FBuilder::Stack(Column, SeatLabel, T.Space.S);
    SpeedLabel = B.Text(FText::GetEmpty(), ETypeRole::Title, T.Color.TextPrimary);
    FBuilder::Stack(Column, SpeedLabel, 0.f);

    Card = B.Surface(ESurfaceKind::Overlay, Pad(T.Space.L, T.Space.M), Column);
    USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>();
    Width->SetWidthOverride(CardWidth);
    Width->SetContent(Card);

    UBorder* Anchor = B.Surface(ESurfaceKind::Base, FMargin(0.f, 0.f, T.Space.XXL, BottomOffset), Width, false);
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
    const FString Name = Vehicle->GetDisplayName().ToString();
    if (Name != ShownName)
    {
        ShownName = Name;
        NameLabel->SetText(FText::FromString(Name));
        Card->SetVisibility(ESlateVisibility::HitTestInvisible);
    }
    const FString Seat = Body->IsDriving() ? LOCTEXT("Driving", "Driving").ToString() : LOCTEXT("Passenger", "Passenger").ToString();
    if (Seat != ShownSeat)
    {
        ShownSeat = Seat;
        SeatLabel->SetText(FText::FromString(Seat));
    }
    const float Speed = Vehicle->GetForwardSpeed();
    const int32 Kmh = FMath::RoundToInt(FMath::Abs(Speed) * 3.6f);
    const FString Text = FString::Printf(TEXT("%s%d km/h"), Speed < ReverseShownBelow ? TEXT("REV · ") : TEXT(""), Kmh);
    if (Text != ShownSpeed)
    {
        ShownSpeed = Text;
        SpeedLabel->SetText(FText::FromString(Text));
    }
}

#undef LOCTEXT_NAMESPACE
