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
#include "MarburgumGameplayContract.h"
#include "MarburgumInteractable.h"
#include "MarburgumPlayerController.h"
#include "MarburgumPlayerState.h"
#include "UI/MarburgumUIBuilder.h"

#define LOCTEXT_NAMESPACE "MarburgumChoicePanel"

using namespace MarburgumUI;

namespace
{
    constexpr float PanelWidth = 660.f;
}

void UMarburgumChoicePanel::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);

    // A decision card: the mission as an archival heading, the question in the speaker's voice, and numbered answers whose
    // consequences are chips (money, XP), read before choosing. Nothing here decides; the server records the choice.
    UVerticalBox* Column = B.Column();
    UHorizontalBox* Header = B.Row();
    FBuilder::Inline(Header, B.GlyphText(EMarburgumGlyph::Mission, LOCTEXT("Overline", "DECISION"), ETypeRole::Overline, T.Color.Accent, nullptr, 14.f),
        T.Space.M, true);
    FBuilder::Inline(Header, B.KeyHint(FText::AsCultureInvariant(TEXT("E")), LOCTEXT("Close", "Not now")), 0.f);
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
    UHorizontalBox* Footer = B.Row();
    FBuilder::Inline(Footer, B.Glyph(EMarburgumGlyph::Lock, T.Color.TextMuted, T.Icon.Small), T.Space.S, false, false);
    UTextBlock* Hint = B.Text(LOCTEXT("Hint", "Press a number to decide. The server records your choice; it cannot be taken back."),
        ETypeRole::Caption, T.Color.TextMuted);
    Hint->SetAutoWrapText(true);
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
    Rebuild();
    SetVisibility(ChoiceIds.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

void UMarburgumChoicePanel::Close()
{
    SetVisibility(ESlateVisibility::Collapsed);
    Target.Reset();
    Mission = NAME_None;
    Stage = NAME_None;
    ChoiceIds.Reset();
    Affordable.Reset();
}

bool UMarburgumChoicePanel::IsOpen() const
{
    return GetVisibility() != ESlateVisibility::Collapsed;
}

void UMarburgumChoicePanel::Rebuild()
{
    using namespace MarburgumII::Gameplay;
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);
    Rows->ClearChildren();
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
    MissionLabel->SetText(FText::FromString(Definition->Name));
    ObjectiveLabel->SetText(FText::FromString(Current->ObjectiveText));
    for (int32 Index = 0; Index < Current->ChoiceCount; ++Index)
    {
        const FMissionChoiceDefinition& Choice = Current->Choices[Index];
        const bool bAfford = State->GetMoney() + Choice.MoneyDelta >= 0;
        ChoiceIds.Add(FName(Choice.Id));
        Affordable.Add(bAfford);

        UHorizontalBox* Line = B.Row();
        FBuilder::Inline(Line, B.KeyCap(FText::AsNumber(Index + 1), bAfford), T.Space.M, false, false);
        UVerticalBox* Words = B.Column();
        UTextBlock* Label = B.Text(FText::FromString(Choice.Label), ETypeRole::BodyStrong, bAfford ? T.Color.TextPrimary : T.Color.TextMuted);
        Label->SetAutoWrapText(true);
        FBuilder::Stack(Words, Label, T.Space.XS);
        // What each answer costs or earns, as chips that wrap: never a clipped line of dots.
        UWrapBox* Chips = WidgetTree->ConstructWidget<UWrapBox>();
        Chips->SetInnerSlotPadding(FVector2D(T.Space.XS, T.Space.XS));
        if (Choice.MoneyDelta != 0)
        {
            Chips->AddChildToWrapBox(B.Chip(EMarburgumGlyph::Coin, Choice.MoneyDelta > 0
                ? FText::Format(LOCTEXT("Gain", "+{0} money"), FText::AsNumber(Choice.MoneyDelta))
                : FText::Format(LOCTEXT("Cost", "−{0} money"), FText::AsNumber(-Choice.MoneyDelta)),
                Choice.MoneyDelta > 0 ? ETone::Success : ETone::Warning));
        }
        for (int32 Award = 0; Award < Choice.SkillXpCount; ++Award)
        {
            const FSkillDefinition* Skill = FindSkill(Choice.SkillXp[Award].Skill);
            Chips->AddChildToWrapBox(B.Chip(EMarburgumGlyph::Skill, FText::Format(LOCTEXT("Xp", "{0} +{1} XP"),
                FText::FromString(Skill ? Skill->DisplayName : Choice.SkillXp[Award].Skill), FText::AsNumber(Choice.SkillXp[Award].Xp)), ETone::Info));
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
        FBuilder::Stack(Rows, B.Surface(bAfford ? ESurfaceKind::Raised : ESurfaceKind::Sunken, Pad(T.Space.L, T.Space.M), Line), T.Space.XS);
    }
}

bool UMarburgumChoicePanel::Choose(int32 Number)
{
    AMarburgumPlayerController* Controller = Cast<AMarburgumPlayerController>(GetOwningPlayer());
    const int32 Index = Number - 1;
    if (!IsOpen() || !Controller || !Target.IsValid() || !ChoiceIds.IsValidIndex(Index) || !Affordable[Index])
    {
        return false;
    }
    Controller->SubmitMissionChoice(FGuid::NewGuid(), Target.Get(), Mission, ChoiceIds[Index]);
    Close();
    return true;
}

void UMarburgumChoicePanel::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    if (!IsOpen())
    {
        return;
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
        Close();
    }
}

#undef LOCTEXT_NAMESPACE
