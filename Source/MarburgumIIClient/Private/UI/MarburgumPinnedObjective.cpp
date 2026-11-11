#include "UI/MarburgumPinnedObjective.h"

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
#include "UI/MarburgumUIBuilder.h"

#define LOCTEXT_NAMESPACE "MarburgumPinnedObjective"

using namespace MarburgumUI;

namespace
{
    constexpr float CardWidth = 400.f;
    // Level with the top of the notification stack on the opposite edge.
    constexpr float TopOffset = 96.f;
}

const FMarburgumMissionState* UMarburgumPinnedObjective::FindPinned(const AMarburgumPlayerState& State)
{
    const FMarburgumMissionState* Pinned = nullptr;
    for (const FMarburgumMissionState& Mission : State.GetMissions())
    {
        // A completed mission has no objective left to pin (ADR-049).
        if (Mission.bCompleted)
        {
            continue;
        }
        if (!Pinned || Mission.StartedRevision < Pinned->StartedRevision)
        {
            Pinned = &Mission;
        }
    }
    return Pinned;
}

void UMarburgumPinnedObjective::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);

    UVerticalBox* Column = B.Column();
    UHorizontalBox* Header = B.Row();
    KindLabel = B.Text(FText::GetEmpty(), ETypeRole::Overline, T.Color.Accent);
    FBuilder::Inline(Header, KindLabel, T.Space.S, true);
    FBuilder::Inline(Header, B.Text(LOCTEXT("Journal", "[J] Journal"), ETypeRole::Caption, T.Color.TextMuted), 0.f);
    FBuilder::Stack(Column, Header, T.Space.XS);
    NameLabel = B.Text(FText::GetEmpty(), ETypeRole::Heading);
    NameLabel->SetAutoWrapText(true);
    FBuilder::Stack(Column, NameLabel, T.Space.S);
    ObjectiveLabel = B.Text(FText::GetEmpty(), ETypeRole::Body, T.Color.TextPrimary);
    ObjectiveLabel->SetAutoWrapText(true);
    FBuilder::Stack(Column, ObjectiveLabel, 0.f);

    // A copper bar on the leading edge marks the one pinned objective.
    UHorizontalBox* Line = B.Row();
    FBuilder::Inline(Line, B.Rule(T.Color.Accent, 1.f, 3.f), T.Space.M, false, false);
    FBuilder::Inline(Line, Column, 0.f, true, false);

    Card = B.Surface(ESurfaceKind::Overlay, Pad(T.Space.L, T.Space.M), Line);
    USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>();
    Width->SetWidthOverride(CardWidth);
    Width->SetContent(Card);

    UBorder* Anchor = B.Surface(ESurfaceKind::Base, FMargin(T.Space.XXL, TopOffset, 0.f, 0.f), Width, false);
    Anchor->SetBrushColor(FLinearColor::Transparent);
    FBuilder::Align(Anchor, HAlign_Left, VAlign_Top);

    USafeZone* Safe = WidgetTree->ConstructWidget<USafeZone>();
    Safe->SetContent(Anchor);
    WidgetTree->RootWidget = Safe;
    SetVisibility(ESlateVisibility::HitTestInvisible);
    Card->SetVisibility(ESlateVisibility::Collapsed);
}

void UMarburgumPinnedObjective::NativeConstruct()
{
    Super::NativeConstruct();
    MissionsHandle = AMarburgumPlayerState::OnMissionsChanged().AddUObject(this, &UMarburgumPinnedObjective::HandleMissionsChanged);
    Refresh();
}

void UMarburgumPinnedObjective::NativeDestruct()
{
    AMarburgumPlayerState::OnMissionsChanged().Remove(MissionsHandle);
    Super::NativeDestruct();
}

void UMarburgumPinnedObjective::HandleMissionsChanged(AMarburgumPlayerState* State)
{
    const APlayerController* Owner = GetOwningPlayer();
    if (Owner && State == Owner->PlayerState)
    {
        Refresh();
    }
}

void UMarburgumPinnedObjective::Refresh()
{
    const APlayerController* Owner = GetOwningPlayer();
    const AMarburgumPlayerState* State = Owner ? Owner->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    const FMarburgumMissionState* Pinned = State && State->AreMissionsKnown() ? FindPinned(*State) : nullptr;
    if (!Pinned)
    {
        ShownMission = NAME_None;
        ShownStage = NAME_None;
        Card->SetVisibility(ESlateVisibility::Collapsed);
        return;
    }
    ShownMission = Pinned->MissionId;
    ShownStage = Pinned->StageId;

    using namespace MarburgumII::Gameplay;
    const FMissionDefinition* Mission = FindMission(Pinned->MissionId.ToString());
    const FMissionStageDefinition* Stage = Mission ? FindStage(*Mission, Pinned->StageId.ToString()) : nullptr;
    KindLabel->SetText(Mission && FCString::Strcmp(Mission->Category, TEXT("story")) == 0
        ? LOCTEXT("Story", "STORY MISSION  ·  PINNED") : LOCTEXT("Mission", "MISSION  ·  PINNED"));
    NameLabel->SetText(Mission ? FText::FromString(Mission->Name) : FText::FromName(Pinned->MissionId));
    ObjectiveLabel->SetText(Stage ? FText::FromString(Stage->ObjectiveText)
        : LOCTEXT("UnknownStage", "The next step is not known to this client version."));
    Card->SetVisibility(ESlateVisibility::HitTestInvisible);
}

#undef LOCTEXT_NAMESPACE
