#include "UI/MarburgumPinnedObjective.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SafeZone.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumPlayerState.h"
#include "UI/MarburgumUIBuilder.h"

#define LOCTEXT_NAMESPACE "MarburgumPinnedObjective"

using namespace MarburgumUI;

namespace
{
    constexpr float MaxWidth = 420.f;
    /** How long "updated" and "completed" stay on the tracker. */
    constexpr float NewsSeconds = 4.5f;
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

    // The tracker: a flag and the mission's kind, the mission, and its one actionable line behind a check box. No card:
    // a shade fading into the street keeps it legible.
    UVerticalBox* Column = B.Column();
    UHorizontalBox* Header = B.Row();
    UTextBlock* Kind = nullptr;
    FBuilder::Inline(Header, B.GlyphText(EMarburgumGlyph::Mission, FText::GetEmpty(), ETypeRole::Overline, T.Color.Accent, &Kind, 14.f),
        T.Space.S, true);
    KindLabel = Kind;
    UTextBlock* NewsText = nullptr;
    NewsChip = B.Chip(EMarburgumGlyph::Check, FText::GetEmpty(), ETone::Success, &NewsText);
    NewsLabel = NewsText;
    NewsChip->SetVisibility(ESlateVisibility::Collapsed);
    FBuilder::Inline(Header, NewsChip, T.Space.S);
    FBuilder::Inline(Header, B.KeyHint(LOCTEXT("JournalKey", "J"), LOCTEXT("Journal", "Journal")), 0.f);
    FBuilder::Stack(Column, Header, T.Space.XS);
    NameLabel = B.Text(FText::GetEmpty(), ETypeRole::Heading);
    NameLabel->SetAutoWrapText(true);
    FBuilder::Stack(Column, NameLabel, T.Space.XS);

    UHorizontalBox* Step = B.Row();
    StepGlyph = B.Glyph(EMarburgumGlyph::ChevronRight, T.Color.Accent, T.Icon.Small);
    FBuilder::Inline(Step, StepGlyph, T.Space.S, false, false);
    ObjectiveLabel = B.Text(FText::GetEmpty(), ETypeRole::Body, T.Color.TextPrimary);
    ObjectiveLabel->SetAutoWrapText(true);
    FBuilder::Inline(Step, ObjectiveLabel, 0.f, true, false);
    FBuilder::Stack(Column, Step, 0.f);

    UHorizontalBox* Line = B.Row();
    FBuilder::Inline(Line, B.Rule(T.Color.Accent, 1.f, 2.f), T.Space.M, false, false);
    FBuilder::Inline(Line, Column, 0.f, true, false);

    UBorder* Content = B.Surface(ESurfaceKind::Base, FMargin(T.Space.L, T.Space.XL, T.Space.XXXL, T.Space.XL), Line, false);
    Content->SetBrushColor(FLinearColor::Transparent);
    UOverlay* Stack = WidgetTree->ConstructWidget<UOverlay>();
    UOverlaySlot* ShadeSlot = Stack->AddChildToOverlay(B.Scrim(EMarburgumScrimShape::SoftFadeRight,
        T.Color.Backdrop.CopyWithNewOpacity(T.bHighContrast ? 0.9f : 0.58f)));
    ShadeSlot->SetHorizontalAlignment(HAlign_Fill);
    ShadeSlot->SetVerticalAlignment(VAlign_Fill);
    Stack->AddChildToOverlay(Content);
    Card = B.Surface(ESurfaceKind::Base, Pad(0.f), B.Bounded(Stack, MaxWidth * T.TextScale), false);
    Card->SetBrushColor(FLinearColor::Transparent);

    UBorder* Anchor = B.Surface(ESurfaceKind::Base, FMargin(T.Space.XL, T.Space.XL, 0.f, 0.f), Card, false);
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

void UMarburgumPinnedObjective::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    if (NewsLeft <= 0.f)
    {
        return;
    }
    NewsLeft -= InDeltaTime;
    if (NewsLeft > 0.f)
    {
        return;
    }
    NewsChip->SetVisibility(ESlateVisibility::Collapsed);
    if (bShowingCompletion)
    {
        // The completed mission's moment is over: show the next pinned one, or nothing.
        bShowingCompletion = false;
        Refresh();
    }
}

void UMarburgumPinnedObjective::HandleMissionsChanged(AMarburgumPlayerState* State)
{
    const APlayerController* Owner = GetOwningPlayer();
    if (Owner && State == Owner->PlayerState)
    {
        Refresh();
    }
}

void UMarburgumPinnedObjective::ShowNews(const FText& Label, ETone Tone, EMarburgumGlyph Icon)
{
    const FTokens& T = Tokens();
    const FLinearColor Color = ToneColor(Tone);
    NewsLabel->SetText(Label);
    NewsLabel->SetColorAndOpacity(FSlateColor(Color));
    if (UHorizontalBox* ChipRow = Cast<UHorizontalBox>(NewsChip->GetContent()))
    {
        if (UMarburgumGlyph* Mark = Cast<UMarburgumGlyph>(ChipRow->GetChildAt(0)))
        {
            Mark->SetGlyph(Icon);
            Mark->SetColor(Color);
        }
    }
    NewsChip->SetBrush(RoundedBrush(Color.CopyWithNewOpacity(T.bHighContrast ? 0.26f : 0.14f), T.Radius.Pill, Color.CopyWithNewOpacity(0.7f),
        T.BorderWidth));
    NewsChip->SetVisibility(ESlateVisibility::HitTestInvisible);
    NewsLeft = NewsSeconds * T.NotificationTimeScale;
}

void UMarburgumPinnedObjective::Refresh()
{
    using namespace MarburgumII::Gameplay;
    const APlayerController* Owner = GetOwningPlayer();
    const AMarburgumPlayerState* State = Owner ? Owner->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    const FMarburgumMissionState* Pinned = State && State->AreMissionsKnown() ? FindPinned(*State) : nullptr;
    const FTokens& T = Tokens();

    // The mission on the tracker has just been completed: it stays a moment, ticked off, before the next one.
    if (!ShownMission.IsNone() && State && (!Pinned || Pinned->MissionId != ShownMission))
    {
        for (const FMarburgumMissionState& Mission : State->GetMissions())
        {
            if (Mission.MissionId == ShownMission && Mission.bCompleted)
            {
                const FMissionDefinition* Done = FindMission(ShownMission.ToString());
                ShownMission = NAME_None;
                ShownStage = NAME_None;
                bShowingCompletion = true;
                NameLabel->SetText(Done ? FText::FromString(Done->Name) : FText::FromName(Mission.MissionId));
                ObjectiveLabel->SetText(LOCTEXT("CompletedLine", "Completed and saved by the server. [J] has the journal entry."));
                ObjectiveLabel->SetColorAndOpacity(FSlateColor(T.Color.TextSecondary));
                StepGlyph->SetGlyph(EMarburgumGlyph::Check);
                StepGlyph->SetColor(T.Color.Success);
                ShowNews(LOCTEXT("Completed", "COMPLETED"), ETone::Success, EMarburgumGlyph::Check);
                Card->SetVisibility(ESlateVisibility::HitTestInvisible);
                return;
            }
        }
    }
    if (bShowingCompletion)
    {
        return; // The completion moment finishes in NativeTick.
    }
    if (!Pinned)
    {
        ShownMission = NAME_None;
        ShownStage = NAME_None;
        Card->SetVisibility(ESlateVisibility::Collapsed);
        // Missions known and none pinned: the next one to appear is news.
        bHasShownOnce = bHasShownOnce || (State && State->AreMissionsKnown());
        return;
    }
    const bool bNewStep = Pinned->MissionId == ShownMission && Pinned->StageId != ShownStage;
    const bool bNewMission = Pinned->MissionId != ShownMission;
    ShownMission = Pinned->MissionId;
    ShownStage = Pinned->StageId;

    const FMissionDefinition* Mission = FindMission(Pinned->MissionId.ToString());
    const FMissionStageDefinition* Stage = Mission ? FindStage(*Mission, Pinned->StageId.ToString()) : nullptr;
    KindLabel->SetText(Mission && FCString::Strcmp(Mission->Category, TEXT("story")) == 0
        ? LOCTEXT("Story", "STORY MISSION") : LOCTEXT("Mission", "MISSION"));
    NameLabel->SetText(Mission ? FText::FromString(Mission->Name) : FText::FromName(Pinned->MissionId));
    ObjectiveLabel->SetText(Stage ? FText::FromString(Stage->ObjectiveText)
        : LOCTEXT("UnknownStage", "The next step is not known to this client version."));
    ObjectiveLabel->SetColorAndOpacity(FSlateColor(Stage ? T.Color.TextPrimary : T.Color.TextSecondary));
    StepGlyph->SetGlyph(Stage ? EMarburgumGlyph::ChevronRight : EMarburgumGlyph::Info);
    StepGlyph->SetColor(Stage ? T.Color.Accent : T.Color.TextMuted);
    // The server has moved the mission on: say so for a moment, with a shape and a word, not only a colour.
    if (bNewStep)
    {
        ShowNews(LOCTEXT("Updated", "UPDATED"), ETone::Accent, EMarburgumGlyph::Star);
    }
    else if (bNewMission && bHasShownOnce)
    {
        ShowNews(LOCTEXT("New", "NEW"), ETone::Accent, EMarburgumGlyph::Mission);
    }
    bHasShownOnce = true;
    Card->SetVisibility(ESlateVisibility::HitTestInvisible);
}

#undef LOCTEXT_NAMESPACE
