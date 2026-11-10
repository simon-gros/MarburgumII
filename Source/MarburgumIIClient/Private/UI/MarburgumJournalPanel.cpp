#include "UI/MarburgumJournalPanel.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/SafeZone.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumPlayerState.h"
#include "UI/MarburgumPinnedObjective.h"
#include "UI/MarburgumUIBuilder.h"

#define LOCTEXT_NAMESPACE "MarburgumJournalPanel"

using namespace MarburgumUI;

namespace
{
    constexpr float PanelWidth = 600.f;

    // Wrapped text is given a fixed width: auto-wrap measures against the previous layout, so blocks
    // rebuilt while the panel was collapsed overlapped their neighbours for the first frames.
    float CardTextWidth(const FTokens& T) { return PanelWidth - 2.f * T.Space.XL - 2.f * T.Space.L; }
}

void UMarburgumJournalPanel::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);

    UVerticalBox* Column = B.Column();
    UHorizontalBox* Header = B.Row();
    UVerticalBox* Titles = B.Column();
    FBuilder::Stack(Titles, B.Text(LOCTEXT("Overline", "JOURNAL"), ETypeRole::Overline, T.Color.Accent), T.Space.XXS);
    FBuilder::Stack(Titles, B.Text(LOCTEXT("Title", "Missions"), ETypeRole::Title), 0.f);
    FBuilder::Inline(Header, Titles, T.Space.M, true);
    FBuilder::Inline(Header, B.Text(LOCTEXT("Close", "[J] Close"), ETypeRole::Caption, T.Color.TextMuted), 0.f);
    FBuilder::Stack(Column, Header, T.Space.M);
    FBuilder::Stack(Column, B.Rule(T.Color.Border, 1.f), T.Space.M);

    Rows = B.Column();
    FBuilder::Stack(Column, Rows, T.Space.M);

    SummaryLabel = B.Text(FText::GetEmpty(), ETypeRole::Caption, T.Color.TextMuted);
    SummaryLabel->SetWrapTextAt(PanelWidth - 2.f * T.Space.XL);
    FBuilder::Stack(Column, SummaryLabel, 0.f);

    UBorder* Panel = B.Surface(ESurfaceKind::Panel, Pad(T.Space.XL), Column);
    USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>();
    Width->SetWidthOverride(PanelWidth);
    Width->SetContent(Panel);

    UBorder* Anchor = B.Surface(ESurfaceKind::Base, Pad(0.f), Width, false);
    Anchor->SetBrushColor(FLinearColor::Transparent);
    FBuilder::Align(Anchor, HAlign_Center, VAlign_Center);

    USafeZone* Safe = WidgetTree->ConstructWidget<USafeZone>();
    Safe->SetContent(Anchor);
    WidgetTree->RootWidget = Safe;
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

void UMarburgumJournalPanel::Toggle()
{
    SetVisibility(IsOpen() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    if (IsOpen())
    {
        Rebuild();
    }
}

bool UMarburgumJournalPanel::IsOpen() const
{
    return GetVisibility() != ESlateVisibility::Collapsed;
}

void UMarburgumJournalPanel::HandleMissionsChanged(AMarburgumPlayerState* State)
{
    const APlayerController* Owner = GetOwningPlayer();
    if (Owner && State == Owner->PlayerState)
    {
        Rebuild();
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

    const APlayerController* Owner = GetOwningPlayer();
    const AMarburgumPlayerState* State = Owner ? Owner->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    if (!State || !State->AreMissionsKnown())
    {
        FBuilder::Stack(Rows, B.StateCard(LOCTEXT("WaitingTitle", "Waiting for the server"),
            LOCTEXT("WaitingBody", "Missions appear once the server has loaded this character's saved record."), ETone::Info), 0.f);
        SummaryLabel->SetText(FText::GetEmpty());
        return;
    }
    if (!State->HasCharacter())
    {
        FBuilder::Stack(Rows, B.StateCard(LOCTEXT("NoCharacterTitle", "No character selected"),
            LOCTEXT("NoCharacterBody", "Join with a development character key to take on missions. People will not trust a stranger without a name."), ETone::Warning), 0.f);
        SummaryLabel->SetText(FText::GetEmpty());
        return;
    }
    if (State->GetMissions().IsEmpty())
    {
        FBuilder::Stack(Rows, B.StateCard(LOCTEXT("EmptyTitle", "No missions yet"),
            LOCTEXT("EmptyBody", "People around the city may ask for help. A mission appears here only after the server has saved it."), ETone::Neutral), 0.f);
        SummaryLabel->SetText(FText::GetEmpty());
        return;
    }

    const FMarburgumMissionState* Pinned = UMarburgumPinnedObjective::FindPinned(*State);
    for (const FMarburgumMissionState& Progress : State->GetMissions())
    {
        const FMissionDefinition* Mission = FindMission(Progress.MissionId.ToString());
        const FMissionStageDefinition* Stage = Mission ? FindStage(*Mission, Progress.StageId.ToString()) : nullptr;
        UVerticalBox* Card = B.Column();

        UHorizontalBox* Header = B.Row();
        FBuilder::Inline(Header, B.Text(Mission && FCString::Strcmp(Mission->Category, TEXT("story")) == 0
            ? LOCTEXT("Story", "STORY") : LOCTEXT("Mission", "MISSION"), ETypeRole::Overline, T.Color.TextSecondary), T.Space.S, true);
        FBuilder::Inline(Header, B.Pill(LOCTEXT("InProgress", "IN PROGRESS"), ETone::Info), T.Space.XS);
        if (&Progress == Pinned)
        {
            FBuilder::Inline(Header, B.Pill(LOCTEXT("Pinned", "PINNED"), ETone::Accent), 0.f);
        }
        FBuilder::Stack(Card, Header, T.Space.S);
        FBuilder::Stack(Card, B.Text(Mission ? FText::FromString(Mission->Name) : FText::FromName(Progress.MissionId), ETypeRole::Heading), T.Space.XS);
        if (Mission)
        {
            UTextBlock* Summary = B.Text(FText::FromString(Mission->Summary), ETypeRole::Caption);
            Summary->SetWrapTextAt(CardTextWidth(T));
            FBuilder::Stack(Card, Summary, T.Space.M);
        }

        FBuilder::Stack(Card, B.Text(LOCTEXT("Current", "CURRENT OBJECTIVE"), ETypeRole::Overline, T.Color.Accent), T.Space.XS);
        UTextBlock* Objective = B.Text(Stage ? FText::FromString(Stage->ObjectiveText)
            : LOCTEXT("UnknownStage", "The next step is not known to this client version."), ETypeRole::Body, T.Color.TextPrimary);
        Objective->SetWrapTextAt(CardTextWidth(T));
        FBuilder::Stack(Card, Objective, T.Space.M);
        FBuilder::Stack(Card, B.Text(LOCTEXT("Saved", "Saved by the server  ·  survives a restart"),
            ETypeRole::Caption, T.Color.TextMuted), 0.f);

        FBuilder::Stack(Rows, B.Surface(ESurfaceKind::Raised, Pad(T.Space.L, T.Space.M), Card), T.Space.S);
    }
    SummaryLabel->SetText(FText::Format(LOCTEXT("Summary", "{0} {0}|plural(one=mission,other=missions) in progress. Completed missions and collected evidence will be listed here as later chapters arrive."),
        FText::AsNumber(State->GetMissions().Num())));
}

#undef LOCTEXT_NAMESPACE
