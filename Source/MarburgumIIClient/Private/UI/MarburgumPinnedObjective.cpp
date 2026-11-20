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
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumInteractable.h"
#include "MarburgumPhotography.h"
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
    FBuilder::Inline(Header, B.KeyHint(LOCTEXT("JournalKey", "J"), LOCTEXT("Journal", "Journal")), 0.f);
    FBuilder::Stack(Column, Header, T.Space.XS);
    // The news chip (UPDATED, COMPLETED) on its own line: beside a longer kind ("ZGODBENA NALOGA") it ran over the label.
    FBuilder::Stack(Column, NewsChip, T.Space.XS);
    if (UVerticalBoxSlot* ChipSlot = Cast<UVerticalBoxSlot>(Column->GetSlots().Last()))
    {
        ChipSlot->SetHorizontalAlignment(HAlign_Left);
    }
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
    GuideLabel = B.Text(FText::GetEmpty(), ETypeRole::Caption, T.Color.TextSecondary);
    GuideLabel->SetAutoWrapText(true);
    GuideLabel->SetVisibility(ESlateVisibility::Collapsed);
    FBuilder::Stack(Column, GuideLabel, 0.f);

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
    SinceGuide += InDeltaTime;
    if (SinceGuide >= 0.5f)
    {
        SinceGuide = 0.f;
        RefreshGuide();
    }
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
                NameLabel->SetText(Done ? FText::FromString(MarburgumII::Gameplay::Localized(Done->Name)) : FText::FromName(Mission.MissionId));
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
    NameLabel->SetText(Mission ? FText::FromString(MarburgumII::Gameplay::Localized(Mission->Name)) : FText::FromName(Pinned->MissionId));
    ObjectiveLabel->SetText(Stage ? FText::FromString(MarburgumII::Gameplay::Localized(Stage->ObjectiveText))
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
    GuideTarget = Stage ? Stage->CompleteTarget : TEXT("");
    GuideActor.Reset();
    RefreshGuide();
    Card->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UMarburgumPinnedObjective::RefreshGuide()
{
    using namespace MarburgumII::Gameplay;
    const APlayerController* Owner = GetOwningPlayer();
    const APawn* Body = Owner ? Owner->GetPawn() : nullptr;
    UWorld* World = Owner ? Owner->GetWorld() : nullptr;
    if (GuideTarget.IsEmpty() || !Body || !World || bShowingCompletion)
    {
        GuideLabel->SetVisibility(ESlateVisibility::Collapsed);
        return;
    }
    // Where the step is done: a photo subject's aim point, or the person or place standing in this world.
    FText Where;
    FVector At;
    bool bFound = false;
    bool bTemporary = false;
    if (const FPhotoSubjectDefinition* Subject = FindPhotoSubject(GuideTarget))
    {
        const UMarburgumPhotographySubsystem* Photography = World->GetSubsystem<UMarburgumPhotographySubsystem>();
        bFound = Photography && Photography->GetAimPoint(GuideTarget, At);
        Where = FText::FromString(MarburgumII::Gameplay::Localized(Subject->DisplayName));
    }
    else
    {
        if (!GuideActor.IsValid())
        {
            for (TActorIterator<AMarburgumInteractable> It(World); It; ++It)
            {
                if (It->InteractableId == FName(*GuideTarget))
                {
                    GuideActor = *It;
                    break;
                }
            }
        }
        const AMarburgumInteractable* Placed = Cast<AMarburgumInteractable>(GuideActor.Get());
        const FInteractableDefinition* Entry = FindInteractable(GuideTarget);
        bFound = Placed != nullptr;
        At = Placed ? Placed->GetActorLocation() : FVector::ZeroVector;
        Where = Placed && !Placed->DisplayName.IsEmpty() ? Placed->DisplayName : Entry ? FText::FromString(MarburgumII::Gameplay::Localized(Entry->DisplayName)) : FText::GetEmpty();
        bTemporary = Entry && Entry->Placement == EPlacement::Pad;
    }
    if (!bFound)
    {
        // Not in this world (yet): say so rather than point somewhere it is not.
        GuideLabel->SetText(Where.IsEmpty() ? FText::GetEmpty()
            : FText::Format(LOCTEXT("GuideAbsent", "{0} \u00B7 not in this part of the city yet"), Where));
        GuideLabel->SetVisibility(Where.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
        return;
    }
    // Unreal +X is north and +Y east on the Marburgum maps (the original's -z and x).
    const FVector Toward = At - Body->GetActorLocation();
    const double Metres = Toward.Size2D() / 100.0;
    static const FText Compass[] = { LOCTEXT("N", "north"), LOCTEXT("NE", "north-east"), LOCTEXT("E", "east"), LOCTEXT("SE", "south-east"),
        LOCTEXT("S", "south"), LOCTEXT("SW", "south-west"), LOCTEXT("W", "west"), LOCTEXT("NW", "north-west") };
    const double Bearing = FMath::Fmod(FMath::RadiansToDegrees(FMath::Atan2(Toward.Y, Toward.X)) + 360.0 + 22.5, 360.0);
    const FText Direction = Compass[FMath::Clamp(FMath::FloorToInt32(Bearing / 45.0), 0, 7)];
    FText Distance = Metres < 8.0 ? LOCTEXT("Here", "here")
        : Metres < 1000.0 ? FText::Format(LOCTEXT("Metres", "{0} m {1}"), FText::AsNumber(FMath::RoundToInt32(Metres / 5.0) * 5), Direction)
        : FText::Format(LOCTEXT("Kilometres", "{0} km {1}"), FText::AsNumber(FMath::RoundToDouble(Metres / 100.0) / 10.0), Direction);
    GuideLabel->SetText(bTemporary
        ? FText::Format(LOCTEXT("GuideTemporary", "{0} \u00B7 {1} \u00B7 on a temporary floor until that district is built"), Where, Distance)
        : FText::Format(LOCTEXT("Guide", "{0} \u00B7 {1}"), Where, Distance));
    GuideLabel->SetVisibility(ESlateVisibility::HitTestInvisible);
}

#undef LOCTEXT_NAMESPACE
