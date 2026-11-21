#include "UI/MarburgumChoicePanel.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/SafeZone.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/WrapBox.h"
#include "GameFramework/Pawn.h"
#include "InputCoreTypes.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumInteractable.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumPlayerController.h"
#include "MarburgumPlayerState.h"
#include "MarburgumSessionContract.h"
#include "UI/MarburgumUIBuilder.h"
#include "UI/MarburgumUIWidgets.h"

#define LOCTEXT_NAMESPACE "MarburgumChoicePanel"

using namespace MarburgumUI;

namespace
{
    constexpr float PanelWidth = 660.f;

    /** 1-6 on the number row or the keypad; 0 for any other key. */
    int32 NumberOf(const FKey& Key)
    {
        const FKey Row[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six };
        const FKey Pad[] = { EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree, EKeys::NumPadFour, EKeys::NumPadFive, EKeys::NumPadSix };
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Row); ++Index)
        {
            if (Key == Row[Index] || Key == Pad[Index])
            {
                return Index + 1;
            }
        }
        return 0;
    }
}

void UMarburgumChoicePanel::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);

    // A decision card: the mission as an archival heading, the question in the speaker's voice, and numbered answers whose
    // consequences are chips (money, XP), read before choosing. Nothing here decides; the server records the choice.
    UVerticalBox* Column = B.Column();
    Header = B.Row();
    FBuilder::Inline(Header, B.GlyphText(EMarburgumGlyph::Mission, LOCTEXT("Overline", "DECISION"), ETypeRole::Overline, T.Color.Accent, nullptr, 14.f),
        T.Space.M, true);
    FBuilder::Stack(Column, Header, T.Space.S);
    MissionLabel = B.Text(FText::GetEmpty(), ETypeRole::PlaceTitle);
    // Long names wrap inside the card instead of running past its edge.
    MissionLabel->SetAutoWrapText(true);
    FBuilder::Stack(Column, MissionLabel, T.Space.S);
    ObjectiveLabel = B.Text(FText::GetEmpty(), ETypeRole::Quote, T.Color.TextSecondary);
    ObjectiveLabel->SetAutoWrapText(true);
    FBuilder::Stack(Column, ObjectiveLabel, T.Space.L);
    FBuilder::Stack(Column, B.SectionTitle(LOCTEXT("Answers", "YOUR ANSWER"), T.Color.Accent), T.Space.S);
    Rows = B.Column();
    FBuilder::Stack(Column, Rows, T.Space.M);
    // The keys of the device in use, as caps that wrap onto a second line in a longer language.
    KeyHints = WidgetTree->ConstructWidget<UWrapBox>();
    KeyHints->SetInnerSlotPadding(FVector2D(T.Space.L, T.Space.XS));
    FBuilder::Stack(Column, KeyHints, T.Space.S);
    UHorizontalBox* Footer = B.Row();
    FBuilder::Inline(Footer, B.Glyph(EMarburgumGlyph::Lock, T.Color.TextMuted, T.Icon.Small), T.Space.S, false, false);
    UTextBlock* Hint = B.Text(LOCTEXT("RecordedHint", "The server records your choice; it cannot be taken back."), ETypeRole::Caption, T.Color.TextMuted);
    Hint->SetAutoWrapText(true);
    HintLabel = Hint;
    FBuilder::Inline(Footer, Hint, 0.f, true, false);
    FBuilder::Stack(Column, Footer, 0.f);

    UBorder* Panel = B.Surface(ESurfaceKind::Panel, Pad(T.Space.XL), Column);
    UBorder* Anchor = B.Surface(ESurfaceKind::Base, Pad(T.Space.XL), B.Bounded(Panel, PanelWidth), false);
    Anchor->SetBrushColor(FLinearColor::Transparent);
    FBuilder::Align(Anchor, HAlign_Center, VAlign_Center);
    USafeZone* Safe = WidgetTree->ConstructWidget<USafeZone>();
    Safe->SetContent(Anchor);
    WidgetTree->RootWidget = Safe;
    SetVisibility(ESlateVisibility::Collapsed);
}

void UMarburgumChoicePanel::Open(AMarburgumInteractable* InTarget, FName MissionId)
{
    Target = InTarget;
    Mission = MissionId;
    bSubmitted = false;
    FocusedIndex = INDEX_NONE;
    Rebuild();
    RefreshHints();
    // Visible: the whole screen under the card takes the clicks, so a click beside it never reaches the street.
    SetVisibility(ChoiceIds.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
}

void UMarburgumChoicePanel::Close(EMarburgumChoiceClose Why)
{
    const bool bWasOpen = IsOpen();
    SetVisibility(ESlateVisibility::Collapsed);
    Target.Reset();
    Mission = NAME_None;
    Stage = NAME_None;
    ChoiceIds.Reset();
    Affordable.Reset();
    FocusedIndex = INDEX_NONE;
    if (bWasOpen)
    {
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=client event=choice_panel_closed why=%s"), MarburgumII::Session::UiLogPrefix,
            Why == EMarburgumChoiceClose::Chosen ? TEXT("chosen") : Why == EMarburgumChoiceClose::Cancelled ? TEXT("cancelled") : TEXT("dismissed"));
        OnClosed.ExecuteIfBound(Why);
    }
}

bool UMarburgumChoicePanel::IsOpen() const
{
    return GetVisibility() != ESlateVisibility::Collapsed;
}

UMarburgumSelectable* UMarburgumChoicePanel::GetRow(int32 Number) const
{
    return RowWidgets.IsValidIndex(Number - 1) ? RowWidgets[Number - 1].Get() : nullptr;
}

UWidget* UMarburgumChoicePanel::GetPreferredFocus() const
{
    for (int32 Index = 0; Index < RowWidgets.Num(); ++Index)
    {
        if (Affordable.IsValidIndex(Index) && Affordable[Index])
        {
            return RowWidgets[Index];
        }
    }
    return RowWidgets.IsEmpty() ? nullptr : RowWidgets[0].Get();
}

void UMarburgumChoicePanel::Rebuild()
{
    using namespace MarburgumII::Gameplay;
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);
    Rows->ClearChildren();
    RowWidgets.Reset();
    ChoiceIds.Reset();
    Affordable.Reset();

    const APlayerController* Owner = GetOwningPlayer();
    const AMarburgumPlayerState* State = Owner ? Owner->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    const FMarburgumMissionState* Progress = State ? State->FindMission(Mission) : nullptr;
    const FMissionDefinition* Definition = FindMission(Mission.ToString());
    const FMissionStageDefinition* Current = Definition && Progress && !Progress->bCompleted ? FindStage(*Definition, Progress->StageId.ToString()) : nullptr;
    if (!Current || Current->Objective != EObjectiveType::Choice)
    {
        Stage = NAME_None;
        return;
    }
    Stage = FName(Current->Id);
    MissionLabel->SetText(FText::FromString(MarburgumII::Gameplay::Localized(Definition->Name)));
    // A deduction (ADR-084): some answers are wrong. What each would earn is not shown, or it would give the answer away.
    bool bDeduction = false;
    for (int32 Index = 0; Index < Current->ChoiceCount; ++Index)
    {
        bDeduction |= !Current->Choices[Index].bCorrect;
    }
    // A deduction's wrong answer is not recorded (ADR-084): the footer must not say every answer is final.
    HintLabel->SetText(bDeduction
        ? LOCTEXT("DeductionRecordedHint", "Only the right conclusion is recorded; a wrong one changes nothing.")
        : LOCTEXT("RecordedHint", "The server records your choice; it cannot be taken back."));
    ObjectiveSource = bDeduction
        ? FText::Format(LOCTEXT("Deduction", "{0}  A wrong conclusion records nothing; you can look again."),
            FText::FromString(MarburgumII::Gameplay::Localized(Current->ObjectiveText))).ToString()
        : MarburgumII::Gameplay::Localized(Current->ObjectiveText);
    for (int32 Index = 0; Index < Current->ChoiceCount; ++Index)
    {
        const FMissionChoiceDefinition& Choice = Current->Choices[Index];
        const bool bAfford = State->GetMoney() + Choice.MoneyDelta >= 0;
        ChoiceIds.Add(FName(Choice.Id));
        Affordable.Add(bAfford);

        UHorizontalBox* Line = B.Row();
        FBuilder::Inline(Line, B.KeyCap(FText::AsNumber(Index + 1), bAfford), T.Space.M, false, false);
        UVerticalBox* Words = B.Column();
        UTextBlock* Label = B.Text(FText::FromString(MarburgumII::Gameplay::Localized(Choice.Label)), ETypeRole::BodyStrong, bAfford ? T.Color.TextPrimary : T.Color.TextMuted);
        Label->SetAutoWrapText(true);
        FBuilder::Stack(Words, Label, T.Space.XS);
        // What each answer costs or earns, as chips that wrap: never a clipped line of dots.
        UWrapBox* Chips = WidgetTree->ConstructWidget<UWrapBox>();
        Chips->SetInnerSlotPadding(FVector2D(T.Space.XS, T.Space.XS));
        if (bDeduction)
        {
            // Nothing to show: the reasoning is the point.
        }
        else if (Choice.MoneyDelta != 0)
        {
            Chips->AddChildToWrapBox(B.Chip(EMarburgumGlyph::Coin, Choice.MoneyDelta > 0
                ? FText::Format(LOCTEXT("Gain", "+{0} money"), FText::AsNumber(Choice.MoneyDelta))
                : FText::Format(LOCTEXT("Cost", "−{0} money"), FText::AsNumber(-Choice.MoneyDelta)),
                Choice.MoneyDelta > 0 ? ETone::Success : ETone::Warning));
        }
        for (int32 Award = 0; Award < (bDeduction ? 0 : Choice.SkillXpCount); ++Award)
        {
            const FSkillDefinition* Skill = FindSkill(Choice.SkillXp[Award].Skill);
            Chips->AddChildToWrapBox(B.Chip(EMarburgumGlyph::Skill, FText::Format(LOCTEXT("Xp", "{0} +{1} XP"),
                FText::FromString(MarburgumII::Gameplay::Localized(Skill ? Skill->DisplayName : Choice.SkillXp[Award].Skill)), FText::AsNumber(Choice.SkillXp[Award].Xp)), ETone::Info));
        }
        if (!bAfford)
        {
            Chips->AddChildToWrapBox(B.Chip(EMarburgumGlyph::Lock, LOCTEXT("Short", "Not enough money"), ETone::Danger));
        }
        if (Chips->GetChildrenCount() > 0)
        {
            FBuilder::Stack(Words, Chips, 0.f);
        }
        FBuilder::Inline(Line, Words, 0.f, true, false);
        // Each answer is a row the mouse clicks and the keys focus; every row draws the same states, so focus says
        // where you are and nothing about which answer is right.
        UMarburgumSelectable* Row = B.Selectable(Line, Pad(T.Space.L, T.Space.M));
        Row->SetIsEnabled(bAfford);
        const int32 Number = Index + 1;
        Row->OnActivated.BindWeakLambda(this, [this, Number]() { Choose(Number); });
        Row->OnFocused.BindWeakLambda(this, [this, Index]()
        {
            if (FocusedIndex != Index)
            {
                FocusedIndex = Index;
                UE_LOG(LogMarburgumNet, Log, TEXT("%s role=client event=choice_focus number=%d"), MarburgumII::Session::UiLogPrefix, Index + 1);
            }
        });
        RowWidgets.Add(Row);
        FBuilder::Stack(Rows, Row, T.Space.XS);
    }
    // Up and Down (arrows, D-pad, stick) go round the answers and never leave the card; Left and Right do nothing.
    for (int32 Index = 0; Index < RowWidgets.Num(); ++Index)
    {
        UMarburgumSelectable* Row = RowWidgets[Index];
        UMarburgumSelectable* Above = RowWidgets[(Index + RowWidgets.Num() - 1) % RowWidgets.Num()];
        UMarburgumSelectable* Below = RowWidgets[(Index + 1) % RowWidgets.Num()];
        Row->SetNavigationRuleExplicit(EUINavigation::Up, Above);
        Row->SetNavigationRuleExplicit(EUINavigation::Previous, Above);
        Row->SetNavigationRuleExplicit(EUINavigation::Down, Below);
        Row->SetNavigationRuleExplicit(EUINavigation::Next, Below);
        Row->SetNavigationRuleBase(EUINavigation::Left, EUINavigationRule::Stop);
        Row->SetNavigationRuleBase(EUINavigation::Right, EUINavigationRule::Stop);
    }
}

void UMarburgumChoicePanel::RefreshHints()
{
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);
    HintDevice = ActiveInputDevice();
    const bool bPad = HintDevice == EMarburgumInputDevice::Gamepad;
    ObjectiveLabel->SetText(FText::FromString(WithDeviceKeys(ObjectiveSource, HintDevice)));
    if (HeaderHint)
    {
        HeaderHint->RemoveFromParent();
    }
    HeaderHint = B.KeyHint(InputKeyLabel(EMarburgumInputAction::Back, HintDevice), LOCTEXT("Close", "Not now"));
    FBuilder::Inline(Header, HeaderHint, 0.f);

    KeyHints->ClearChildren();
    if (!bPad)
    {
        // The number row picks at once, as it always has; a click does too.
        KeyHints->AddChildToWrapBox(B.KeyHint(ChoiceIds.Num() > 1
            ? FText::Format(LOCTEXT("NumberRange", "1–{0}"), FText::AsNumber(ChoiceIds.Num())) : FText::AsCultureInvariant(TEXT("1")),
            LOCTEXT("NumberAction", "Answer, or click one")));
    }
    KeyHints->AddChildToWrapBox(B.KeyHint(InputKeyLabel(EMarburgumInputAction::Select, HintDevice), LOCTEXT("SelectAction", "Move")));
    KeyHints->AddChildToWrapBox(B.KeyHint(InputKeyLabel(EMarburgumInputAction::Confirm, HintDevice), LOCTEXT("ConfirmAction", "Answer")));
    KeyHints->AddChildToWrapBox(B.KeyHint(InputKeyLabel(EMarburgumInputAction::Back, HintDevice), LOCTEXT("BackAction", "Not now")));
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=client event=choice_hints device=%s"), MarburgumII::Session::UiLogPrefix,
        bPad ? TEXT("gamepad") : TEXT("keyboard_mouse"));
}

void UMarburgumChoicePanel::RefocusRow()
{
    UWidget* Row = RowWidgets.IsValidIndex(FocusedIndex) ? RowWidgets[FocusedIndex].Get() : GetPreferredFocus();
    if (Row)
    {
        Row->SetFocus();
    }
}

bool UMarburgumChoicePanel::Choose(int32 Number)
{
    AMarburgumPlayerController* Controller = Cast<AMarburgumPlayerController>(GetOwningPlayer());
    const int32 Index = Number - 1;
    if (!IsOpen() || bSubmitted || !Controller || !Target.IsValid() || !ChoiceIds.IsValidIndex(Index) || !Affordable[Index])
    {
        return false;
    }
    // Latched before anything else: whatever arrives before the card has gone (a second click, another key) sends nothing.
    bSubmitted = true;
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=client event=choice_submit number=%d choice=%s device=%s"), MarburgumII::Session::UiLogPrefix, Number,
        *ChoiceIds[Index].ToString(), ActiveInputDevice() == EMarburgumInputDevice::Gamepad ? TEXT("gamepad") : TEXT("keyboard_mouse"));
    Controller->SubmitMissionChoice(FGuid::NewGuid(), Target.Get(), Mission, ChoiceIds[Index]);
    Close(EMarburgumChoiceClose::Chosen);
    return true;
}

FReply UMarburgumChoicePanel::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
    const FKey Key = InKeyEvent.GetKey();
    const int32 Number = NumberOf(Key);
    const bool bBack = Key == EKeys::Escape || Key == EKeys::E || Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Gamepad_Special_Right;
    if (Number == 0 && !bBack)
    {
        // Arrows, D-pad and stick fall through to Slate's navigation (the rows' rules); the rows take Accept themselves.
        return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
    }
    // A key held down repeats: only the press itself counts, so the E that opened the card does not close it again.
    if (!InKeyEvent.IsRepeat())
    {
        if (bBack)
        {
            Close(EMarburgumChoiceClose::Cancelled);
        }
        else
        {
            Choose(Number);
        }
    }
    return FReply::Handled();
}

FReply UMarburgumChoicePanel::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    // A click beside the answers: swallowed, and the keys stay on the card.
    RefocusRow();
    return FReply::Handled();
}

void UMarburgumChoicePanel::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    if (!IsOpen())
    {
        return;
    }
    // A device change in the middle of the decision: the hints follow at once, the focus stays where it was.
    if (HintDevice != ActiveInputDevice())
    {
        RefreshHints();
    }
    // Walking away, or the decision being made elsewhere, closes the panel without deciding.
    const APlayerController* Owner = GetOwningPlayer();
    const APawn* Body = Owner ? Owner->GetPawn() : nullptr;
    const AMarburgumInteractable* Subject = Target.Get();
    const AMarburgumPlayerState* State = Owner ? Owner->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    const FMarburgumMissionState* Progress = State ? State->FindMission(Mission) : nullptr;
    if (!Body || !Subject || !Progress || Progress->bCompleted || Progress->StageId != Stage
        || FVector::Dist(Body->GetActorLocation(), Subject->GetActorLocation()) > Subject->InteractionRange + 100.f)
    {
        Close(EMarburgumChoiceClose::Dismissed);
    }
}

#undef LOCTEXT_NAMESPACE
