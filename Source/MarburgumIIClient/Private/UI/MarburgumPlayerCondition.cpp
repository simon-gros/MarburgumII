#include "UI/MarburgumPlayerCondition.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/ProgressBar.h"
#include "Components/SafeZone.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "MarburgumCharacterMovement.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumPlayerState.h"
#include "UI/MarburgumUIBuilder.h"

#define LOCTEXT_NAMESPACE "MarburgumPlayerCondition"

using namespace MarburgumUI;

namespace
{
    constexpr float CardWidth = 300.f;
    constexpr float LabelWidth = 72.f;
    constexpr float ValueWidth = 36.f;
    constexpr float MeterHeight = 6.f;
    // Above the chat panel (320 high at the same left gutter).
    constexpr float BottomOffset = 32.f + 320.f + 16.f;
    // A rested body is not news; the card steps back until something changes.
    constexpr float RestedOpacity = 0.6f;

    UHorizontalBox* MeterRow(const FBuilder& B, const FText& Label, ETone Tone, UProgressBar*& OutMeter, UTextBlock*& OutValue)
    {
        const FTokens& T = Tokens();
        UHorizontalBox* Row = B.Row();
        USizeBox* LabelBox = B.Spacer(LabelWidth, 0.f);
        LabelBox->ClearHeightOverride();
        LabelBox->SetContent(B.Text(Label, ETypeRole::Caption));
        FBuilder::Inline(Row, LabelBox, T.Space.S);
        OutMeter = B.Meter(Tone, 1.f);
        USizeBox* MeterBox = B.Spacer(0.f, MeterHeight);
        MeterBox->ClearWidthOverride();
        MeterBox->SetContent(OutMeter);
        FBuilder::Inline(Row, MeterBox, T.Space.S, true);
        OutValue = B.Text(FText::GetEmpty(), ETypeRole::Numeric, T.Color.TextPrimary);
        OutValue->SetJustification(ETextJustify::Right);
        USizeBox* ValueBox = B.Spacer(ValueWidth, 0.f);
        ValueBox->ClearHeightOverride();
        ValueBox->SetContent(OutValue);
        FBuilder::Inline(Row, ValueBox, 0.f);
        return Row;
    }
}

void UMarburgumPlayerCondition::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);

    UVerticalBox* Column = B.Column();
    UHorizontalBox* Header = B.Row();
    FBuilder::Inline(Header, B.Text(LOCTEXT("Overline", "CONDITION"), ETypeRole::Overline, T.Color.Accent), T.Space.S, true);
    UTextBlock* PillLabel = nullptr;
    StatePill = B.Pill(FText::GetEmpty(), ETone::Info, &PillLabel);
    StateLabel = PillLabel;
    StatePill->SetVisibility(ESlateVisibility::Collapsed);
    FBuilder::Inline(Header, StatePill, 0.f);
    FBuilder::Stack(Column, Header, T.Space.S);

    WaitingLabel = B.Text(LOCTEXT("Waiting", "Waiting for the server to restore this character"), ETypeRole::Caption, T.Color.TextMuted);
    WaitingLabel->SetAutoWrapText(true);
    FBuilder::Stack(Column, WaitingLabel, 0.f);

    Bars = B.Column();
    UProgressBar* Meter = nullptr;
    UTextBlock* Value = nullptr;
    FBuilder::Stack(Bars, MeterRow(B, LOCTEXT("Vitality", "Vitality"), ETone::Success, Meter, Value), T.Space.XS);
    VitalityMeter = Meter;
    VitalityValue = Value;
    FBuilder::Stack(Bars, MeterRow(B, LOCTEXT("Stamina", "Stamina"), ETone::Info, Meter, Value), T.Space.XS);
    StaminaMeter = Meter;
    StaminaValue = Value;
    FBuilder::Stack(Bars, B.Text(LOCTEXT("Hint", "[Shift] Run  ·  [C] Character"), ETypeRole::Caption, T.Color.TextMuted), 0.f);
    FBuilder::Stack(Column, Bars, 0.f);

    Card = B.Surface(ESurfaceKind::Overlay, Pad(T.Space.L, T.Space.M), Column);
    USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>();
    Width->SetWidthOverride(CardWidth);
    Width->SetContent(Card);

    UBorder* Anchor = B.Surface(ESurfaceKind::Base, FMargin(T.Space.XXL, 0.f, 0.f, BottomOffset), Width, false);
    Anchor->SetBrushColor(FLinearColor::Transparent);
    FBuilder::Align(Anchor, HAlign_Left, VAlign_Bottom);

    USafeZone* Safe = WidgetTree->ConstructWidget<USafeZone>();
    Safe->SetContent(Anchor);
    WidgetTree->RootWidget = Safe;
    SetVisibility(ESlateVisibility::HitTestInvisible);
    Refresh();
}

void UMarburgumPlayerCondition::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    Refresh();
}

void UMarburgumPlayerCondition::Refresh()
{
    const APlayerController* Owner = GetOwningPlayer();
    const AMarburgumPlayerState* State = Owner ? Owner->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    if (!State || !State->IsConditionKnown())
    {
        if (ShownStamina != -1 || !WaitingLabel->IsVisible())
        {
            ShownStamina = ShownVitality = -1;
            bShowingWinded = bShowingRunning = false;
            WaitingLabel->SetVisibility(ESlateVisibility::HitTestInvisible);
            Bars->SetVisibility(ESlateVisibility::Collapsed);
            StatePill->SetVisibility(ESlateVisibility::Collapsed);
            Card->SetRenderOpacity(1.f);
        }
        return;
    }

    const FMarburgumCondition& Condition = State->GetCondition();
    float Stamina = Condition.Stamina;
    bool bWinded = Condition.bWinded;
    bool bRunning = Condition.bRunning;
    // The local prediction, once it has adopted the server's value, is smoother and as authoritative:
    // the server corrects it whenever the two disagree.
    const ACharacter* Body = Cast<ACharacter>(Owner->GetPawn());
    const UMarburgumCharacterMovement* Movement = Body ? Cast<UMarburgumCharacterMovement>(Body->GetCharacterMovement()) : nullptr;
    if (Movement && Movement->IsConditionSynced() && !Movement->IsIgnoringStaminaForTest())
    {
        Stamina = Movement->GetStamina();
        bWinded = Movement->IsWinded();
        bRunning = Movement->IsRunning();
    }

    WaitingLabel->SetVisibility(ESlateVisibility::Collapsed);
    Bars->SetVisibility(ESlateVisibility::HitTestInvisible);
    const float MaxStamina = FMath::Max<float>(1.f, Condition.MaxStamina);
    const float MaxVitality = FMath::Max<float>(1.f, Condition.MaxVitality);
    StaminaMeter->SetPercent(FMath::Clamp(Stamina / MaxStamina, 0.f, 1.f));
    StaminaMeter->SetFillColorAndOpacity(ToneColor(bWinded ? ETone::Warning : ETone::Info));
    VitalityMeter->SetPercent(FMath::Clamp(Condition.Vitality / MaxVitality, 0.f, 1.f));
    VitalityMeter->SetFillColorAndOpacity(ToneColor(Condition.Vitality * 4 < Condition.MaxVitality ? ETone::Danger : ETone::Success));

    const int32 WholeStamina = FMath::FloorToInt(Stamina);
    if (WholeStamina != ShownStamina)
    {
        StaminaValue->SetText(FText::AsNumber(WholeStamina));
        ShownStamina = WholeStamina;
    }
    if (Condition.Vitality != ShownVitality)
    {
        VitalityValue->SetText(FText::AsNumber(Condition.Vitality));
        ShownVitality = Condition.Vitality;
    }
    if (bWinded != bShowingWinded || bRunning != bShowingRunning)
    {
        bShowingWinded = bWinded;
        bShowingRunning = bRunning;
        // Text and tone together: the state never rests on colour alone.
        if (bWinded)
        {
            StateLabel->SetText(LOCTEXT("Winded", "WINDED"));
            FBuilder::SetPillTone(StatePill, StateLabel, ETone::Warning);
        }
        else if (bRunning)
        {
            StateLabel->SetText(LOCTEXT("Running", "RUNNING"));
            FBuilder::SetPillTone(StatePill, StateLabel, ETone::Info);
        }
        StatePill->SetVisibility(bWinded || bRunning ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    }
    const bool bRested = !bWinded && !bRunning && WholeStamina >= Condition.MaxStamina && Condition.Vitality >= Condition.MaxVitality;
    Card->SetRenderOpacity(bRested ? RestedOpacity : 1.f);
}

#undef LOCTEXT_NAMESPACE
