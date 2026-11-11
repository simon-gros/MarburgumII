#include "UI/MarburgumChoicePanel.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/SafeZone.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
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
    constexpr float PanelWidth = 620.f;
    float TextWidth(const FTokens& T) { return PanelWidth - 2.f * T.Space.XL - 2.f * T.Space.L - 48.f; }
}

void UMarburgumChoicePanel::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);

    UVerticalBox* Column = B.Column();
    UHorizontalBox* Header = B.Row();
    UVerticalBox* Titles = B.Column();
    FBuilder::Stack(Titles, B.Text(LOCTEXT("Overline", "DECISION"), ETypeRole::Overline, T.Color.Accent), T.Space.XXS);
    MissionLabel = B.Text(FText::GetEmpty(), ETypeRole::Title);
    FBuilder::Stack(Titles, MissionLabel, 0.f);
    FBuilder::Inline(Header, Titles, T.Space.M, true);
    FBuilder::Inline(Header, B.Text(LOCTEXT("Close", "[E] Not now"), ETypeRole::Caption, T.Color.TextMuted), 0.f, false, false);
    FBuilder::Stack(Column, Header, T.Space.S);
    ObjectiveLabel = B.Text(FText::GetEmpty(), ETypeRole::Body);
    ObjectiveLabel->SetWrapTextAt(PanelWidth - 2.f * T.Space.XL);
    FBuilder::Stack(Column, ObjectiveLabel, T.Space.M);
    FBuilder::Stack(Column, B.Rule(T.Color.Border, 1.f), T.Space.M);
    Rows = B.Column();
    FBuilder::Stack(Column, Rows, T.Space.S);
    FBuilder::Stack(Column, B.Text(LOCTEXT("Hint", "Press a number to decide. The server records your choice; it cannot be taken back."),
        ETypeRole::Caption, T.Color.TextMuted), 0.f);

    UBorder* Panel = B.Surface(ESurfaceKind::Panel, Pad(T.Space.XL), Column);
    USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>();
    Width->SetWidthOverride(PanelWidth);
    Width->SetContent(Panel);
    UBorder* Anchor = B.Surface(ESurfaceKind::Base, FMargin(0.f), Width, false);
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

        TArray<FString> Consequences;
        if (Choice.MoneyDelta != 0)
        {
            Consequences.Add(Choice.MoneyDelta > 0 ? FString::Printf(TEXT("+%d money"), Choice.MoneyDelta)
                                                   : FString::Printf(TEXT("−%d money"), -Choice.MoneyDelta));
        }
        for (int32 Award = 0; Award < Choice.SkillXpCount; ++Award)
        {
            const FSkillDefinition* Skill = FindSkill(Choice.SkillXp[Award].Skill);
            Consequences.Add(FString::Printf(TEXT("%s +%d XP"), Skill ? Skill->DisplayName : Choice.SkillXp[Award].Skill, Choice.SkillXp[Award].Xp));
        }
        if (!bAfford)
        {
            Consequences.Add(TEXT("not enough money"));
        }

        UHorizontalBox* Line = B.Row();
        UBorder* Key = B.Surface(ESurfaceKind::Raised, Pad(T.Space.S, T.Space.XXS),
            B.Text(FText::AsNumber(Index + 1), ETypeRole::Heading, bAfford ? T.Color.Accent : T.Color.TextMuted));
        FBuilder::Inline(Line, Key, T.Space.M);
        UVerticalBox* Words = B.Column();
        UTextBlock* Label = B.Text(FText::FromString(Choice.Label), ETypeRole::Body, bAfford ? T.Color.TextPrimary : T.Color.TextMuted);
        Label->SetWrapTextAt(TextWidth(T));
        FBuilder::Stack(Words, Label, T.Space.XXS);
        if (!Consequences.IsEmpty())
        {
            FBuilder::Stack(Words, B.Text(FText::FromString(FString::Join(Consequences, TEXT("  ·  "))), ETypeRole::Caption), 0.f);
        }
        FBuilder::Inline(Line, Words, 0.f, true);
        FBuilder::Stack(Rows, B.Surface(ESurfaceKind::Raised, Pad(T.Space.L, T.Space.S), Line), T.Space.XS);
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
