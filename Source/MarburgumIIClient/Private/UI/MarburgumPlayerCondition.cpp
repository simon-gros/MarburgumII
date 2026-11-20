#include "UI/MarburgumPlayerCondition.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SafeZone.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "MarburgumCharacterMovement.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumPlayerState.h"
#include "UI/MarburgumUIBuilder.h"
#include "UI/MarburgumUISettings.h"

#define LOCTEXT_NAMESPACE "MarburgumPlayerCondition"

using namespace MarburgumUI;

namespace
{
    constexpr float MeterWidth = 176.f;
    constexpr float ValueWidth = 34.f;
    constexpr float QuietSeconds = 4.f;
    constexpr float FadeSeconds = 0.45f;

    UHorizontalBox* MeterRow(const FBuilder& B, EMarburgumGlyph Icon, const FLinearColor& Tone, UMarburgumGlyph*& OutGlyph,
        UMarburgumMeter*& OutMeter, UTextBlock*& OutValue, int32 Segments)
    {
        const FTokens& T = Tokens();
        UHorizontalBox* Row = B.Row();
        OutGlyph = B.Glyph(Icon, Tone, T.Icon.Small);
        FBuilder::Inline(Row, OutGlyph, T.Space.S);
        OutMeter = B.Bar(ETone::Neutral, 1.f, 7.f, Segments);
        OutMeter->SetFillColor(Tone);
        USizeBox* MeterBox = B.Spacer(MeterWidth, 10.f);
        MeterBox->SetContent(OutMeter);
        FBuilder::Inline(Row, MeterBox, T.Space.S);
        OutValue = B.Text(FText::GetEmpty(), ETypeRole::Numeric, T.Color.TextSecondary);
        OutValue->SetJustification(ETextJustify::Right);
        USizeBox* ValueBox = B.Spacer(ValueWidth * T.TextScale, 0.f);
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

    // The cluster: the state chip, then vitality and stamina, on a shade that fades into the street to the right.
    UVerticalBox* Column = B.Column();
    UTextBlock* ChipLabel = nullptr;
    StateChip = B.Chip(EMarburgumGlyph::Fast, FText::GetEmpty(), ETone::Info, &ChipLabel);
    StateLabel = ChipLabel;
    // The chip's glyph is the first child of its row.
    if (UHorizontalBox* ChipRow = Cast<UHorizontalBox>(StateChip->GetContent()))
    {
        StateGlyph = Cast<UMarburgumGlyph>(ChipRow->GetChildAt(0));
    }
    StateChip->SetVisibility(ESlateVisibility::Collapsed);
    FBuilder::Stack(Column, StateChip, T.Space.S);
    if (UVerticalBoxSlot* ChipSlot = Cast<UVerticalBoxSlot>(StateChip->Slot))
    {
        ChipSlot->SetHorizontalAlignment(HAlign_Left);
    }

    WaitingLabel = B.Text(LOCTEXT("Waiting", "Waiting for the server to restore this character"), ETypeRole::Caption, T.Color.TextSecondary);
    WaitingLabel->SetAutoWrapText(true);
    FBuilder::Stack(Column, WaitingLabel, 0.f);

    Bars = B.Column();
    UMarburgumGlyph* Glyph = nullptr;
    UMarburgumMeter* Meter = nullptr;
    UTextBlock* Value = nullptr;
    FBuilder::Stack(Bars, MeterRow(B, EMarburgumGlyph::Heart, T.Color.Success, Glyph, Meter, Value, 10), T.Space.XS + 2.f);
    VitalityGlyph = Glyph;
    VitalityMeter = Meter;
    VitalityValue = Value;
    FBuilder::Stack(Bars, MeterRow(B, EMarburgumGlyph::Breath, T.Color.Info, Glyph, Meter, Value, 0), 0.f);
    StaminaGlyph = Glyph;
    StaminaMeter = Meter;
    StaminaValue = Value;
    FBuilder::Stack(Column, Bars, 0.f);

    // The shade runs well past the values so they never sit on its faded tail over a bright street.
    UBorder* Content = B.Surface(ESurfaceKind::Base, FMargin(T.Space.L, T.Space.M, T.Space.XXXL * 2.f, T.Space.M), Column, false);
    Content->SetBrushColor(FLinearColor::Transparent);
    UOverlay* ClusterStack = WidgetTree->ConstructWidget<UOverlay>();
    UOverlaySlot* ShadeSlot = ClusterStack->AddChildToOverlay(B.Scrim(EMarburgumScrimShape::SoftFadeRight,
        T.Color.Backdrop.CopyWithNewOpacity(T.bHighContrast ? 0.9f : 0.55f)));
    ShadeSlot->SetHorizontalAlignment(HAlign_Fill);
    ShadeSlot->SetVerticalAlignment(VAlign_Fill);
    ClusterStack->AddChildToOverlay(Content);
    Cluster = ClusterStack;

    UBorder* ClusterAnchor = B.Surface(ESurfaceKind::Base, FMargin(T.Space.XL, 0.f, 0.f, T.Space.XL), ClusterStack, false);
    ClusterAnchor->SetBrushColor(FLinearColor::Transparent);
    FBuilder::Align(ClusterAnchor, HAlign_Left, VAlign_Bottom);

    // Going down is critical: a centred notice, the danger glyph, the cause in words and the time to recovery.
    UVerticalBox* DownColumn = B.Column();
    UMarburgumGlyph* DownGlyph = B.Glyph(EMarburgumGlyph::Warning, T.Color.Danger, T.Icon.Large);
    FBuilder::Stack(DownColumn, DownGlyph, T.Space.S);
    if (UVerticalBoxSlot* GlyphSlot = Cast<UVerticalBoxSlot>(DownGlyph->Slot))
    {
        GlyphSlot->SetHorizontalAlignment(HAlign_Center);
    }
    DownTitle = B.Text(FText::GetEmpty(), ETypeRole::Title, T.Color.TextPrimary);
    DownTitle->SetJustification(ETextJustify::Center);
    FBuilder::Stack(DownColumn, DownTitle, T.Space.XS);
    DownDetail = B.Text(FText::GetEmpty(), ETypeRole::Body, T.Color.TextSecondary);
    DownDetail->SetJustification(ETextJustify::Center);
    FBuilder::Stack(DownColumn, DownDetail, T.Space.M);
    DownMeter = B.Bar(ETone::Danger, 0.f, 4.f);
    USizeBox* DownMeterBox = B.Spacer(280.f, 6.f);
    DownMeterBox->SetContent(DownMeter);
    FBuilder::Stack(DownColumn, DownMeterBox, 0.f);
    if (UVerticalBoxSlot* MeterSlot = Cast<UVerticalBoxSlot>(DownMeterBox->Slot))
    {
        MeterSlot->SetHorizontalAlignment(HAlign_Center);
    }
    UBorder* DownContent = B.Surface(ESurfaceKind::Base, FMargin(T.Space.XXXL * 2.f, T.Space.XL), DownColumn, false);
    DownContent->SetBrushColor(FLinearColor::Transparent);
    UOverlay* DownStack = WidgetTree->ConstructWidget<UOverlay>();
    UOverlaySlot* DownShade = DownStack->AddChildToOverlay(B.Scrim(EMarburgumScrimShape::SoftBand,
        T.Color.Backdrop.CopyWithNewOpacity(T.bHighContrast ? 0.92f : 0.72f)));
    DownShade->SetHorizontalAlignment(HAlign_Fill);
    DownShade->SetVerticalAlignment(VAlign_Fill);
    DownStack->AddChildToOverlay(DownContent);
    DownNotice = DownStack;
    DownNotice->SetVisibility(ESlateVisibility::Collapsed);
    UBorder* DownAnchor = B.Surface(ESurfaceKind::Base, FMargin(0.f, 0.f, 0.f, 0.f), DownStack, false);
    DownAnchor->SetBrushColor(FLinearColor::Transparent);
    FBuilder::Align(DownAnchor, HAlign_Center, VAlign_Center);
    DownAnchor->SetRenderTranslation(FVector2D(0.f, -120.f));

    UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>();
    for (UWidget* Layer : { static_cast<UWidget*>(ClusterAnchor), static_cast<UWidget*>(DownAnchor) })
    {
        UOverlaySlot* LayerSlot = Root->AddChildToOverlay(Layer);
        LayerSlot->SetHorizontalAlignment(HAlign_Fill);
        LayerSlot->SetVerticalAlignment(VAlign_Fill);
    }
    USafeZone* Safe = WidgetTree->ConstructWidget<USafeZone>();
    Safe->SetContent(Root);
    WidgetTree->RootWidget = Safe;
    SetVisibility(ESlateVisibility::HitTestInvisible);
    Refresh(0.f);
}

void UMarburgumPlayerCondition::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    Refresh(InDeltaTime);
}

void UMarburgumPlayerCondition::ShowState(EState State, int32 RecoverySeconds)
{
    if (State == Shown && (RecoverySeconds == ShownRecovery || (State != EState::KnockedDown && State != EState::Drowned)))
    {
        return;
    }
    const bool bEnteredDown = (State == EState::KnockedDown || State == EState::Drowned) && Shown != State;
    Shown = State;
    ShownRecovery = RecoverySeconds;
    const FTokens& T = Tokens();
    FText Text;
    ETone Tone = ETone::Info;
    EMarburgumGlyph Icon = EMarburgumGlyph::Info;
    switch (State)
    {
    case EState::None:
        ShownState.Reset();
        break;
    case EState::Running:
        Text = LOCTEXT("Running", "RUNNING");
        Icon = EMarburgumGlyph::Fast;
        ShownState = TEXT("RUNNING");
        break;
    case EState::Winded:
        Text = LOCTEXT("Winded", "WINDED");
        Tone = ETone::Warning;
        Icon = EMarburgumGlyph::Breath;
        ShownState = TEXT("WINDED");
        break;
    case EState::Swimming:
        Text = LOCTEXT("Swimming", "SWIMMING");
        Icon = EMarburgumGlyph::Swim;
        ShownState = TEXT("SWIMMING");
        break;
    case EState::HoldingBreath:
        Text = LOCTEXT("HoldingBreath", "HOLDING BREATH");
        Tone = ETone::Warning;
        Icon = EMarburgumGlyph::Swim;
        ShownState = TEXT("HOLDING BREATH");
        break;
    case EState::KnockedDown:
    case EState::Drowned:
    {
        const bool bKnocked = State == EState::KnockedDown;
        Text = bKnocked ? FText::Format(LOCTEXT("KnockedDownChip", "KNOCKED DOWN · {0} s"), FText::AsNumber(RecoverySeconds))
                        : FText::Format(LOCTEXT("DrownedChip", "DROWNED · {0} s"), FText::AsNumber(RecoverySeconds));
        Tone = ETone::Danger;
        Icon = EMarburgumGlyph::Warning;
        // The stable capture string stays English whatever the display language.
        ShownState = FString::Printf(TEXT("%s · %d s"), bKnocked ? TEXT("KNOCKED DOWN") : TEXT("DROWNED"), RecoverySeconds);
        if (bEnteredDown)
        {
            DownTotalSeconds = FMath::Max(1, RecoverySeconds);
            DownTitle->SetText(bKnocked ? LOCTEXT("KnockedDownTitle", "Knocked down") : LOCTEXT("DrownedTitle", "Pulled from the water"));
        }
        DownDetail->SetText(RecoverySeconds > 0
            ? FText::Format(LOCTEXT("Recovering", "Back on your feet in {0} s. The server decides when."), FText::AsNumber(RecoverySeconds))
            : LOCTEXT("Getting", "Getting up…"));
        DownMeter->SetPercent(1.f - static_cast<float>(RecoverySeconds) / DownTotalSeconds);
        break;
    }
    }
    const bool bDown = State == EState::KnockedDown || State == EState::Drowned;
    DownNotice->SetVisibility(bDown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    if (State == EState::None)
    {
        StateChip->SetVisibility(ESlateVisibility::Collapsed);
        return;
    }
    const FLinearColor Color = ToneColor(Tone);
    StateLabel->SetText(Text);
    StateLabel->SetColorAndOpacity(FSlateColor(Color));
    if (StateGlyph)
    {
        StateGlyph->SetGlyph(Icon);
        StateGlyph->SetColor(Color);
    }
    StateChip->SetBrush(RoundedBrush(Color.CopyWithNewOpacity(T.bHighContrast ? 0.26f : 0.16f), T.Radius.Pill, Color.CopyWithNewOpacity(0.75f),
        T.BorderWidth));
    StateChip->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UMarburgumPlayerCondition::Refresh(float DeltaTime)
{
    const FTokens& T = Tokens();
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
            ShowState(EState::None, 0);
            Cluster->SetRenderOpacity(bSuppressed ? 0.f : 1.f);
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
    const bool bPredicted = Movement && Movement->IsConditionSynced();
    if (bPredicted && !Movement->IsIgnoringStaminaForTest())
    {
        Stamina = Movement->GetStamina();
        bWinded = Movement->IsWinded();
        bRunning = Movement->IsRunning();
    }

    if (WaitingLabel->IsVisible())
    {
        WaitingLabel->SetVisibility(ESlateVisibility::Collapsed);
        Bars->SetVisibility(ESlateVisibility::HitTestInvisible);
    }
    const float MaxStamina = FMath::Max<float>(1.f, Condition.MaxStamina);
    const float MaxVitality = FMath::Max<float>(1.f, Condition.MaxVitality);
    const float VitalityPercent = FMath::Clamp(Condition.Vitality / MaxVitality, 0.f, 1.f);
    StaminaMeter->SetPercent(FMath::Clamp(Stamina / MaxStamina, 0.f, 1.f));
    VitalityMeter->SetPercent(VitalityPercent);
    // A blow leaves a ghost of the vitality it took, which drains away (none with reduced motion).
    if (VitalityGhost < VitalityPercent || T.bReducedMotion)
    {
        VitalityGhost = VitalityPercent;
    }
    else
    {
        VitalityGhost = FMath::Max(VitalityPercent, VitalityGhost - DeltaTime * 0.6f);
    }
    VitalityMeter->SetGhost(VitalityGhost);

    const int32 WholeStamina = FMath::FloorToInt(Stamina);
    if (WholeStamina != ShownStamina || bWinded != bShowingWinded)
    {
        StaminaValue->SetText(FText::AsNumber(WholeStamina));
        const FLinearColor StaminaTone = bWinded ? T.Color.Warning : T.Color.Info;
        StaminaMeter->SetFillColor(StaminaTone);
        StaminaGlyph->SetColor(StaminaTone);
        ShownStamina = WholeStamina;
    }
    if (Condition.Vitality != ShownVitality)
    {
        VitalityValue->SetText(FText::AsNumber(Condition.Vitality));
        const FLinearColor VitalityTone = Condition.Vitality * 4 < Condition.MaxVitality ? T.Color.Danger
            : Condition.Vitality * 2 < Condition.MaxVitality ? T.Color.Warning : T.Color.Success;
        VitalityMeter->SetFillColor(VitalityTone);
        VitalityGlyph->SetColor(VitalityTone);
        ShownVitality = Condition.Vitality;
    }
    bShowingWinded = bWinded;
    bShowingRunning = bRunning;

    // One state at a time, the most urgent first (ADR-032): text, glyph and tone together, never colour alone.
    const bool bSubmerged = bPredicted ? Movement->IsSubmerged() : Condition.bSubmerged;
    const bool bSwimming = bPredicted ? Movement->IsInWaterBody() : Condition.bSwimming;
    EState Next = EState::None;
    if (Condition.IsDown())
    {
        Next = Condition.bKnockedDown ? EState::KnockedDown : EState::Drowned;
    }
    else if (bSubmerged)
    {
        Next = EState::HoldingBreath;
    }
    else if (bSwimming)
    {
        Next = EState::Swimming;
    }
    else if (bWinded)
    {
        Next = EState::Winded;
    }
    else if (bRunning)
    {
        Next = EState::Running;
    }
    ShowState(Next, Condition.RecoverySeconds);

    // Rested and unhurt for a few seconds: step back out of the street (Settings: always show).
    const bool bNotable = Next != EState::None || WholeStamina < Condition.MaxStamina || Condition.Vitality < Condition.MaxVitality
        || UMarburgumUISettings::Get().ConditionDisplay == 1;
    QuietFor = bNotable ? 0.f : QuietFor + DeltaTime;
    bResting = QuietFor >= QuietSeconds;
    const float Target = bSuppressed ? 0.f : bResting ? 0.f : 1.f;
    Opacity = T.bReducedMotion ? Target : FMath::FInterpConstantTo(Opacity, Target, DeltaTime, 1.f / FadeSeconds);
    Cluster->SetRenderOpacity(Opacity);
}

#undef LOCTEXT_NAMESPACE
