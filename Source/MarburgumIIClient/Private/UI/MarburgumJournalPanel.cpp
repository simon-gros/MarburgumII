#include "UI/MarburgumJournalPanel.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumPlayerState.h"
#include "MarburgumMissionLogic.h"
#include "UI/MarburgumEvidenceText.h"
#include "UI/MarburgumPinnedObjective.h"
#include "UI/MarburgumUIBuilder.h"

#define LOCTEXT_NAMESPACE "MarburgumJournalPanel"

using namespace MarburgumUI;

namespace
{
    bool IsStory(const MarburgumII::Gameplay::FMissionDefinition* Mission)
    {
        return Mission && FCString::Strcmp(Mission->Category, TEXT("story")) == 0;
    }

    /** Every item the mission's definition gives, asks for or takes, in catalogue order. */
    TArray<FString> ItemsOf(const MarburgumII::Gameplay::FMissionDefinition& Mission)
    {
        using namespace MarburgumII::Gameplay;
        TArray<FString> Out;
        auto Add = [&Out](const TCHAR* Id) { Out.AddUnique(FString(Id)); };
        for (int32 Index = 0; Index < Mission.StartItemCount; ++Index)
        {
            Add(Mission.StartItems[Index].Id);
        }
        for (int32 Stage = 0; Stage < Mission.StageCount; ++Stage)
        {
            const FMissionStageDefinition& Def = Mission.Stages[Stage];
            for (int32 Index = 0; Index < Def.ObjectiveItemCount; ++Index)
            {
                Add(Def.ObjectiveItems[Index]);
            }
            for (int32 Index = 0; Index < Def.RequiredItemCount; ++Index)
            {
                Add(Def.RequiredItems[Index].Id);
            }
            for (int32 Index = 0; Index < Def.GrantItemCount; ++Index)
            {
                Add(Def.GrantItems[Index].Id);
            }
        }
        return Out;
    }

    int32 StageIndex(const MarburgumII::Gameplay::FMissionDefinition& Mission, FName StageId)
    {
        for (int32 Index = 0; Index < Mission.StageCount; ++Index)
        {
            if (StageId == Mission.Stages[Index].Id)
            {
                return Index;
            }
        }
        return INDEX_NONE;
    }

    UTextBlock* Wrapped(const FBuilder& B, const FText& Value, ETypeRole Role, const FLinearColor& Color)
    {
        UTextBlock* Block = B.Text(Value, Role, Color);
        Block->SetAutoWrapText(true);
        return Block;
    }
}

void UMarburgumJournalPanel::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);

    UHorizontalBox* Columns = B.Row();

    UVerticalBox* ListColumn = B.Column();
    FBuilder::Stack(ListColumn, B.Text(LOCTEXT("Overline", "JOURNAL"), ETypeRole::Overline, T.Color.Accent), T.Space.XXS);
    FBuilder::Stack(ListColumn, B.Text(LOCTEXT("Title", "Cases and errands"), ETypeRole::Title), T.Space.XS);
    SummaryLabel = B.Text(FText::GetEmpty(), ETypeRole::Caption, T.Color.TextSecondary);
    FBuilder::Stack(ListColumn, SummaryLabel, T.Space.L);
    Rows = B.Column();
    ListScroller = B.Scroll(Rows);
    FBuilder::Stack(ListColumn, ListScroller, 0.f, true);
    FBuilder::Inline(Columns, B.Bounded(ListColumn, 520.f, 340.f), T.Space.XXL, false, false);

    // The dossier: an archival paper card, the widest part of the page.
    Dossier = B.Column();
    Scroller = B.Scroll(Dossier);
    UBorder* Paper = B.Surface(ESurfaceKind::Panel, FMargin(T.Space.XXL, T.Space.XL), Scroller);
    Paper->SetBrush(RoundedBrush(T.Color.Paper, T.Radius.Medium, T.Color.Hairline, T.BorderWidth));
    FBuilder::Inline(Columns, B.Bounded(Paper, 900.f, 420.f), 0.f, true, false);

    WidgetTree->RootWidget = Columns;
    SetVisibility(ESlateVisibility::Collapsed);
}

void UMarburgumJournalPanel::NativeConstruct()
{
    Super::NativeConstruct();
    MissionsHandle = AMarburgumPlayerState::OnMissionsChanged().AddUObject(this, &UMarburgumJournalPanel::HandleMissionsChanged);
    Rebuild();
}

void UMarburgumJournalPanel::NativeDestruct()
{
    AMarburgumPlayerState::OnMissionsChanged().Remove(MissionsHandle);
    Super::NativeDestruct();
}

void UMarburgumJournalPanel::SetOpen(bool bOpen)
{
    SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    if (bOpen)
    {
        // Opening shows the pinned mission's dossier (or the first one).
        SelectedMission = NAME_None;
        Rebuild();
    }
}

bool UMarburgumJournalPanel::IsOpen() const
{
    return GetVisibility() != ESlateVisibility::Collapsed;
}

void UMarburgumJournalPanel::Scroll(float Lines)
{
    FBuilder::ScrollBy(Scroller, Lines);
}

UWidget* UMarburgumJournalPanel::GetPreferredFocus() const
{
    const int32 Index = RowMissions.IndexOfByKey(SelectedMission);
    return RowWidgets.IsValidIndex(Index) ? RowWidgets[Index].Get() : RowWidgets.Num() > 0 ? RowWidgets[0].Get() : nullptr;
}

void UMarburgumJournalPanel::HandleMissionsChanged(AMarburgumPlayerState* State)
{
    const APlayerController* Owner = GetOwningPlayer();
    if (IsOpen() && Owner && State == Owner->PlayerState)
    {
        const bool bHadFocus = HasFocusedDescendants();
        Rebuild();
        if (bHadFocus)
        {
            if (UWidget* Focus = GetPreferredFocus())
            {
                Focus->SetFocus();
            }
        }
    }
}

void UMarburgumJournalPanel::Rebuild()
{
    if (!Rows)
    {
        return;
    }
    using namespace MarburgumII::Gameplay;
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);
    Rows->ClearChildren();
    RowWidgets.Reset();
    RowMissions.Reset();

    const APlayerController* Owner = GetOwningPlayer();
    const AMarburgumPlayerState* State = Owner ? Owner->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    auto ShowState = [&](const FText& Title, const FText& Body, ETone Tone)
    {
        FBuilder::Stack(Rows, B.StateCard(Title, Body, Tone), 0.f);
        SummaryLabel->SetText(FText::GetEmpty());
        Dossier->ClearChildren();
        FBuilder::Stack(Dossier, Wrapped(B, LOCTEXT("EmptyDossier", "The archive is empty. A dossier opens here for every mission you take on."),
            ETypeRole::Quote, T.Color.TextSecondary), 0.f);
    };
    if (!State || !State->AreMissionsKnown())
    {
        ShowState(LOCTEXT("WaitingTitle", "Waiting for the server"),
            LOCTEXT("WaitingBody", "Missions appear once the server has loaded this character's saved record."), ETone::Info);
        return;
    }
    if (!State->HasCharacter())
    {
        ShowState(LOCTEXT("NoCharacterTitle", "No character selected"),
            LOCTEXT("NoCharacterBody", "Join with a character to take on missions. People will not trust a stranger without a name."), ETone::Warning);
        return;
    }
    if (State->GetMissions().IsEmpty())
    {
        ShowState(LOCTEXT("EmptyTitle", "No missions yet"),
            LOCTEXT("EmptyBody", "People around the city may ask for help. A mission appears here only after the server has saved it."), ETone::Neutral);
        return;
    }

    const FMarburgumMissionState* Pinned = UMarburgumPinnedObjective::FindPinned(*State);
    TArray<const FMarburgumMissionState*> Ordered;
    for (const FMarburgumMissionState& Progress : State->GetMissions())
    {
        Ordered.Add(&Progress);
    }
    Ordered.StableSort([](const FMarburgumMissionState& A, const FMarburgumMissionState& B)
    {
        return A.bCompleted != B.bCompleted ? !A.bCompleted : A.StartedRevision < B.StartedRevision;
    });

    int32 Active = 0;
    int32 Completed = 0;
    bool bCompletedHeading = false;
    FBuilder::Stack(Rows, B.SectionTitle(LOCTEXT("UnderWay", "UNDER WAY"), T.Color.Accent), T.Space.S);
    for (const FMarburgumMissionState* ProgressPtr : Ordered)
    {
        const FMarburgumMissionState& Progress = *ProgressPtr;
        (Progress.bCompleted ? Completed : Active) += 1;
        if (Progress.bCompleted && !bCompletedHeading)
        {
            if (Active == 0)
            {
                FBuilder::Stack(Rows, B.Text(LOCTEXT("NoneActive", "Nothing under way."), ETypeRole::Caption, T.Color.TextMuted), T.Space.L);
            }
            else
            {
                FBuilder::Stack(Rows, B.Spacer(1.f, T.Space.M), 0.f);
            }
            FBuilder::Stack(Rows, B.SectionTitle(LOCTEXT("Closed", "CLOSED"), T.Color.TextSecondary), T.Space.S);
            bCompletedHeading = true;
        }
        const FMissionDefinition* Mission = FindMission(Progress.MissionId.ToString());
        UHorizontalBox* Line = B.Row();
        const bool bPinned = &Progress == Pinned;
        FBuilder::Inline(Line, B.Glyph(Progress.bCompleted ? EMarburgumGlyph::Check : bPinned ? EMarburgumGlyph::Pin : EMarburgumGlyph::Mission,
            Progress.bCompleted ? T.Color.Success : bPinned ? T.Color.Accent : T.Color.TextSecondary, T.Icon.Medium), T.Space.M);
        UVerticalBox* Names = B.Column();
        FBuilder::Stack(Names, B.Text(IsStory(Mission) ? LOCTEXT("StoryOverline", "STORY") : LOCTEXT("ErrandOverline", "ERRAND"), ETypeRole::Overline,
            T.Color.TextMuted), T.Space.XXS);
        FBuilder::Stack(Names, Wrapped(B, Mission ? FText::FromString(MarburgumII::Gameplay::Localized(Mission->Name)) : FText::FromName(Progress.MissionId), ETypeRole::Label,
            Progress.bCompleted ? T.Color.TextSecondary : T.Color.TextPrimary), 0.f);
        FBuilder::Inline(Line, Names, T.Space.S, true);
        if (bPinned)
        {
            FBuilder::Inline(Line, B.Pill(LOCTEXT("Pinned", "PINNED"), ETone::Accent), 0.f);
        }
        UMarburgumSelectable* Row = B.Selectable(Line, FMargin(T.Space.M, T.Space.S + 2.f));
        const FName MissionId = Progress.MissionId;
        Row->OnFocused.BindWeakLambda(this, [this, MissionId]() { ShowDossier(MissionId); });
        Row->OnActivated.BindWeakLambda(this, [this, MissionId]() { ShowDossier(MissionId); });
        FBuilder::Stack(Rows, Row, T.Space.XS);
        RowWidgets.Add(Row);
        RowMissions.Add(MissionId);
    }
    SummaryLabel->SetText(FText::Format(LOCTEXT("Summary", "{0} under way  ·  {1} closed  ·  saved by the server"),
        FText::AsNumber(Active), FText::AsNumber(Completed)));

    FName Show = SelectedMission;
    if (!RowMissions.Contains(Show))
    {
        Show = Pinned ? Pinned->MissionId : RowMissions[0];
    }
    ShowDossier(Show);
}

void UMarburgumJournalPanel::ShowDossier(FName MissionId)
{
    using namespace MarburgumII::Gameplay;
    SelectedMission = MissionId;
    for (int32 Index = 0; Index < RowWidgets.Num(); ++Index)
    {
        RowWidgets[Index]->SetSelected(RowMissions[Index] == MissionId);
    }
    const APlayerController* Owner = GetOwningPlayer();
    const AMarburgumPlayerState* State = Owner ? Owner->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    const FMarburgumMissionState* Progress = State ? State->GetMissions().FindByPredicate(
        [MissionId](const FMarburgumMissionState& Entry) { return Entry.MissionId == MissionId; }) : nullptr;
    if (!Progress || !Dossier)
    {
        return;
    }
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);
    Dossier->ClearChildren();
    if (Scroller)
    {
        Scroller->ScrollToStart();
    }
    const FMissionDefinition* Mission = FindMission(MissionId.ToString());
    const FLinearColor Ink = T.Color.PaperText;

    UHorizontalBox* Head = B.Row();
    FBuilder::Inline(Head, B.Text(IsStory(Mission) ? LOCTEXT("DossierStory", "DOSSIER  ·  STORY") : LOCTEXT("DossierErrand", "DOSSIER  ·  ERRAND"),
        ETypeRole::Overline, T.Color.Accent), T.Space.M, true);
    FBuilder::Inline(Head, Progress->bCompleted ? B.Chip(EMarburgumGlyph::Check, LOCTEXT("Closed", "CLOSED"), ETone::Success)
        : B.Chip(EMarburgumGlyph::Clock, LOCTEXT("Open", "UNDER WAY"), ETone::Info), 0.f);
    FBuilder::Stack(Dossier, Head, T.Space.M);
    FBuilder::Stack(Dossier, Wrapped(B, Mission ? FText::FromString(MarburgumII::Gameplay::Localized(Mission->Name)) : FText::FromName(MissionId), ETypeRole::PlaceTitle, Ink), T.Space.S);
    if (Mission && *Mission->Summary)
    {
        FBuilder::Stack(Dossier, Wrapped(B, FText::FromString(MarburgumII::Gameplay::Localized(Mission->Summary)), ETypeRole::Quote, T.Color.TextSecondary), T.Space.L);
    }
    FBuilder::Stack(Dossier, B.Rule(T.Color.Hairline, 1.f), T.Space.L);

    const int32 Current = Mission ? StageIndex(*Mission, Progress->StageId) : INDEX_NONE;
    if (!Progress->bCompleted)
    {
        UHorizontalBox* Objective = B.Row();
        FBuilder::Inline(Objective, B.Rule(T.Color.Accent, 3.f, 3.f), T.Space.M, false, false);
        UVerticalBox* ObjectiveText = B.Column();
        FBuilder::Stack(ObjectiveText, B.Text(LOCTEXT("Current", "CURRENT OBJECTIVE"), ETypeRole::Overline, T.Color.Accent), T.Space.XS);
        const FMissionStageDefinition* Stage = Current != INDEX_NONE ? &Mission->Stages[Current] : nullptr;
        FBuilder::Stack(ObjectiveText, Wrapped(B, Stage ? FText::FromString(MarburgumII::Gameplay::Localized(Stage->ObjectiveText))
            : LOCTEXT("UnknownStage", "The next step is not known to this client version."), ETypeRole::BodyStrong, Ink), 0.f);
        FBuilder::Inline(Objective, ObjectiveText, 0.f, true);
        FBuilder::Stack(Dossier, Objective, T.Space.XL);
    }

    // This character's route through the mission (ADR-084): stages for another outcome were never part of it.
    const TMap<FString, MarburgumII::Missions::FProgressView> Views = MarburgumUI::ProgressViews(State);
    const auto OnRoute = [&Views](const FMissionStageDefinition& Stage)
    {
        return MarburgumII::Missions::StageApplies(Stage, [&Views](const FString& Id) { return Views.Find(Id); });
    };
    // Questions the mission leaves open, and what has been established (ADR-084): unresolved first, then settled.
    if (Mission && Mission->QuestionCount > 0)
    {
        TArray<const FMissionQuestion*> Open;
        TArray<const FMissionQuestion*> Settled;
        for (int32 Index = 0; Index < Mission->QuestionCount; ++Index)
        {
            const FMissionQuestion& Question = Mission->Questions[Index];
            const int32 By = *Question.AnsweredBy ? StageIndex(*Mission, FName(Question.AnsweredBy)) : INDEX_NONE;
            const bool bByStage = By != INDEX_NONE && (Progress->bCompleted || By < Current);
            const bool bByItem = *Question.AnsweredByItem && State->GetInventory().ContainsByPredicate(
                [&Question](const FMarburgumItemStack& Stack) { return Stack.ItemId == FName(Question.AnsweredByItem) && Stack.Quantity > 0; });
            (bByStage || bByItem ? Settled : Open).Add(&Question);
        }
        if (!Open.IsEmpty())
        {
            FBuilder::Stack(Dossier, B.SectionTitle(LOCTEXT("OpenQuestions", "OPEN QUESTIONS"), T.Color.Accent), T.Space.S);
            for (const FMissionQuestion* Question : Open)
            {
                FBuilder::Stack(Dossier, B.GlyphText(EMarburgumGlyph::Info, FText::FromString(MarburgumII::Gameplay::Localized(Question->Text)), ETypeRole::BodyStrong, Ink),
                    T.Space.XS);
            }
            FBuilder::Stack(Dossier, B.Spacer(1.f, T.Space.M), 0.f);
        }
        if (!Settled.IsEmpty())
        {
            FBuilder::Stack(Dossier, B.SectionTitle(LOCTEXT("Established", "ESTABLISHED"), T.Color.TextSecondary), T.Space.S);
            for (const FMissionQuestion* Question : Settled)
            {
                UHorizontalBox* Line = B.Row();
                FBuilder::Inline(Line, B.Glyph(EMarburgumGlyph::Check, T.Color.Success, T.Icon.Small), T.Space.S, false, false);
                UVerticalBox* Words = B.Column();
                FBuilder::Stack(Words, Wrapped(B, FText::FromString(MarburgumII::Gameplay::Localized(Question->Text)), ETypeRole::Body, T.Color.TextSecondary), T.Space.XXS);
                FBuilder::Stack(Words, Wrapped(B, FText::FromString(MarburgumII::Gameplay::Localized(Question->Answer)), ETypeRole::BodyStrong, Ink), 0.f);
                FBuilder::Inline(Line, Words, 0.f, true);
                FBuilder::Stack(Dossier, Line, T.Space.S);
            }
            FBuilder::Stack(Dossier, B.Spacer(1.f, T.Space.M), 0.f);
        }
    }

    // Steps already taken and what each turned up; the ones ahead stay unrevealed, as they do in play.
    if (Mission && (Current > 0 || Progress->bCompleted))
    {
        FBuilder::Stack(Dossier, B.SectionTitle(LOCTEXT("Steps", "STEPS TAKEN"), T.Color.TextSecondary), T.Space.S);
        const int32 Done = Progress->bCompleted ? Mission->StageCount : Current;
        for (int32 Index = 0; Index < Done; ++Index)
        {
            const FMissionStageDefinition& Taken = Mission->Stages[Index];
            if (!OnRoute(Taken))
            {
                continue;
            }
            UHorizontalBox* Step = B.Row();
            FBuilder::Inline(Step, B.Glyph(EMarburgumGlyph::Check, T.Color.Success, T.Icon.Small), T.Space.S, false, false);
            UVerticalBox* Words = B.Column();
            FBuilder::Stack(Words, Wrapped(B, FText::FromString(MarburgumII::Gameplay::Localized(Taken.ObjectiveText)), ETypeRole::Body, T.Color.TextSecondary), T.Space.XXS);
            // A decision's words are under YOUR DECISION; a step's own finding is shown here.
            if (Taken.Objective != EObjectiveType::Choice && *Taken.CompletionText && (Index < Mission->StageCount - 1 || !Progress->bCompleted))
            {
                FBuilder::Stack(Words, Wrapped(B, FText::FromString(MarburgumII::Gameplay::Localized(Taken.CompletionText)), ETypeRole::Caption, T.Color.TextMuted), 0.f);
            }
            FBuilder::Inline(Step, Words, 0.f, true);
            FBuilder::Stack(Dossier, Step, T.Space.S);
        }
        if (!Progress->bCompleted)
        {
            FBuilder::Stack(Dossier, B.Text(LOCTEXT("Ahead", "Later steps are revealed as you go."), ETypeRole::Caption, T.Color.TextMuted), T.Space.XS);
        }
        FBuilder::Stack(Dossier, B.Spacer(1.f, T.Space.L), 0.f);
    }

    // Evidence and objects in hand that this mission concerns.
    if (Mission)
    {
        TArray<FText> Held;
        for (const FString& Id : ItemsOf(*Mission))
        {
            const FMarburgumItemStack* Stack = State->GetInventory().FindByPredicate(
                [&Id](const FMarburgumItemStack& Entry) { return Entry.ItemId == FName(*Id); });
            const FItemDefinition* Item = FindItem(Id);
            if (Stack && Item)
            {
                FText Line = Stack->Quantity > 1 ? FText::Format(LOCTEXT("HeldMany", "{0}  ×{1}"), FText::FromString(MarburgumII::Gameplay::Localized(Item->DisplayName)),
                    FText::AsNumber(Stack->Quantity)) : FText::FromString(MarburgumII::Gameplay::Localized(Item->DisplayName));
                // Original, print, copy, testimony, information or inference (ADR-084): never one mistaken for another.
                if (Item->Evidence != EEvidenceClass::None)
                {
                    Line = FText::Format(LOCTEXT("HeldClass", "{0}  ·  {1}"), Caps(MarburgumUI::EvidenceClassName(Item->Evidence)), Line);
                }
                Held.Add(Line);
            }
        }
        if (!Held.IsEmpty())
        {
            FBuilder::Stack(Dossier, B.SectionTitle(LOCTEXT("Evidence", "IN YOUR POSSESSION"), T.Color.TextSecondary), T.Space.S);
            for (const FText& Line : Held)
            {
                FBuilder::Stack(Dossier, B.GlyphText(EMarburgumGlyph::Clue, Line, ETypeRole::Body, Ink), T.Space.XS);
            }
            FBuilder::Stack(Dossier, B.Spacer(1.f, T.Space.L), 0.f);
        }
    }

    if (Progress->bCompleted && Mission)
    {
        if (*Mission->JournalEntryId)
        {
            FBuilder::Stack(Dossier, B.SectionTitle(Caps(FString(MarburgumII::Gameplay::Localized(Mission->JournalEntryTitle))), T.Color.Accent), T.Space.S);
            FBuilder::Stack(Dossier, Wrapped(B, FText::FromString(MarburgumII::Gameplay::Localized(Mission->JournalEntryText)), ETypeRole::Quote, Ink), T.Space.L);
        }
        const FMissionStageDefinition* Stage = Current != INDEX_NONE ? &Mission->Stages[Current] : nullptr;
        const FMissionChoiceDefinition* Choice = Stage && !Progress->Choice.IsNone() ? FindChoice(*Stage, Progress->Choice.ToString()) : nullptr;
        if (Choice)
        {
            FBuilder::Stack(Dossier, B.SectionTitle(LOCTEXT("Decision", "YOUR DECISION"), T.Color.TextSecondary), T.Space.S);
            FBuilder::Stack(Dossier, Wrapped(B, FText::FromString(MarburgumII::Gameplay::Localized(Choice->Label)), ETypeRole::BodyStrong, Ink), T.Space.XS);
            FBuilder::Stack(Dossier, Wrapped(B, FText::FromString(MarburgumII::Gameplay::Localized(Choice->CompletionText)), ETypeRole::Body, T.Color.TextSecondary), T.Space.L);
        }
        else if (Stage && *Stage->CompletionText)
        {
            FBuilder::Stack(Dossier, Wrapped(B, FText::FromString(MarburgumII::Gameplay::Localized(Stage->CompletionText)), ETypeRole::Body, T.Color.TextSecondary), T.Space.L);
        }
        if (Mission->RewardMoney > 0)
        {
            FBuilder::Stack(Dossier, B.GlyphText(EMarburgumGlyph::Coin, FText::Format(LOCTEXT("Reward", "Paid {0} money on completion"),
                FText::AsNumber(Mission->RewardMoney)), ETypeRole::Body, T.Color.TextSecondary), T.Space.L);
        }
    }
    FBuilder::Stack(Dossier, B.Text(LOCTEXT("Saved", "Kept by the server  ·  survives a restart"), ETypeRole::Caption, T.Color.TextMuted), 0.f);
}

#undef LOCTEXT_NAMESPACE
