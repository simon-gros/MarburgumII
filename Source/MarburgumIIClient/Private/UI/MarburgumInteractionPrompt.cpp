#include "UI/MarburgumInteractionPrompt.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "MarburgumCharacter.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumMissionLogic.h"
#include "MarburgumInteractable.h"
#include "MarburgumVehicle.h"
#include "MarburgumVehicleContract.h"
#include "MarburgumNpc.h"
#include "MarburgumPlayerController.h"
#include "MarburgumPlayerState.h"
#include "Session/MarburgumSessionSubsystem.h"
#include "UI/MarburgumUIBuilder.h"

#define LOCTEXT_NAMESPACE "MarburgumInteractionPrompt"

using namespace MarburgumUI;

namespace
{
    constexpr float RefreshSeconds = 0.1f;
    constexpr float CardWidth = 440.f;
    constexpr float KeyCapSize = 44.f;
    constexpr float GapAboveTarget = 24.f;
    constexpr float EdgeMargin = 24.f;
    // Fallback position when the object's top is not on screen: above the lower edge, clear of chat.
    constexpr float FallbackBottomOffset = 168.f;
}

void UMarburgumInteractionPrompt::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);

    KeyLabel = B.Text(LOCTEXT("Key", "E"), ETypeRole::Heading, T.Color.TextPrimary);
    KeyCap = B.Surface(ESurfaceKind::Raised, Pad(0.f), KeyLabel);
    FBuilder::Align(KeyCap, HAlign_Center, VAlign_Center);
    USizeBox* KeySize = WidgetTree->ConstructWidget<USizeBox>();
    KeySize->SetWidthOverride(KeyCapSize);
    KeySize->SetHeightOverride(KeyCapSize);
    KeySize->SetContent(KeyCap);

    UVerticalBox* Text = B.Column();
    UHorizontalBox* VerbLine = B.Row();
    VerbLabel = B.Text(FText::GetEmpty(), ETypeRole::Overline, T.Color.Accent);
    FBuilder::Inline(VerbLine, VerbLabel, T.Space.S, true);
    FixturePill = B.Pill(LOCTEXT("Fixture", "TEST FIXTURE"), ETone::Warning);
    FBuilder::Inline(VerbLine, FixturePill, 0.f);
    FBuilder::Stack(Text, VerbLine, T.Space.XXS);
    SubjectLabel = B.Text(FText::GetEmpty(), ETypeRole::Heading);
    FBuilder::Stack(Text, SubjectLabel, T.Space.XXS);
    DetailLabel = B.Text(FText::GetEmpty(), ETypeRole::Caption);
    DetailLabel->SetAutoWrapText(true);
    FBuilder::Stack(Text, DetailLabel, 0.f);

    UHorizontalBox* Line = B.Row();
    FBuilder::Inline(Line, KeySize, T.Space.L);
    FBuilder::Inline(Line, Text, 0.f, true);

    Card = B.Surface(ESurfaceKind::Overlay, Pad(T.Space.L, T.Space.M), Line);
    USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>();
    Width->SetWidthOverride(CardWidth);
    Width->SetContent(Card);
    Placement = Width;

    // Positioned each frame by render translation from the viewport's top-left corner.
    UBorder* Anchor = B.Surface(ESurfaceKind::Base, Pad(0.f), Width, false);
    Anchor->SetBrushColor(FLinearColor::Transparent);
    FBuilder::Align(Anchor, HAlign_Left, VAlign_Top);
    WidgetTree->RootWidget = Anchor;

    // The widget itself stays visible so it keeps ticking (collapsed widgets do not tick); only the
    // card collapses while there is nothing to offer.
    SetVisibility(ESlateVisibility::HitTestInvisible);
    Card->SetVisibility(ESlateVisibility::Collapsed);
}

void UMarburgumInteractionPrompt::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    SinceRefresh += InDeltaTime;
    if (SinceRefresh >= RefreshSeconds)
    {
        SinceRefresh = 0.f;
        Refresh();
    }
    Place(); // Every frame, so the card follows the object while the camera moves.
}

void UMarburgumInteractionPrompt::Place()
{
    const AActor* Target = Shown.Get();
    APlayerController* Controller = GetOwningPlayer();
    if (!Target || !Controller || !Placement || Card->GetVisibility() == ESlateVisibility::Collapsed)
    {
        return;
    }
    const float Scale = UWidgetLayoutLibrary::GetViewportScale(this);
    const FVector2D Viewport = UWidgetLayoutLibrary::GetViewportSize(this) / FMath::Max(Scale, KINDA_SMALL_NUMBER);
    const FVector2D Size(CardWidth, Card->GetDesiredSize().Y);

    FVector Origin;
    FVector Extent;
    Target->GetActorBounds(true, Origin, Extent);
    FVector2D Top;
    const bool bProjected = UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
        Controller, Origin + FVector(0.f, 0.f, Extent.Z), Top, true);

    FVector2D Position;
    if (bProjected && Top.Y - GapAboveTarget - Size.Y >= EdgeMargin && Top.Y <= Viewport.Y)
    {
        Position = FVector2D(Top.X - Size.X * 0.5f, Top.Y - GapAboveTarget - Size.Y);
    }
    else
    {
        Position = FVector2D((Viewport.X - Size.X) * 0.5f, Viewport.Y - FallbackBottomOffset - Size.Y);
    }
    Position.X = FMath::Clamp(Position.X, EdgeMargin, FMath::Max(EdgeMargin, Viewport.X - Size.X - EdgeMargin));
    Position.Y = FMath::Clamp(Position.Y, EdgeMargin, FMath::Max(EdgeMargin, Viewport.Y - Size.Y - EdgeMargin));
    Placement->SetRenderTranslation(Position);
}

void UMarburgumInteractionPrompt::Refresh()
{
    AMarburgumPlayerController* Controller = Cast<AMarburgumPlayerController>(GetOwningPlayer());
    const bool bAdmitted = Controller && UMarburgumSessionSubsystem::IsAdmitted(Controller->GetWorld());
    if (bAdmitted)
    {
        // Cars first: the same key gets in and out of them.
        const AMarburgumCharacter* Body = Cast<AMarburgumCharacter>(Controller->GetPawn());
        FName Seat;
        if (const AMarburgumVehicle* Ridden = Body ? Body->GetSeatedVehicle() : nullptr)
        {
            RefreshVehicle(*Ridden, Body->GetSeatId(), true, Controller->IsVehicleRequestPending());
            return;
        }
        if (const AMarburgumVehicle* Car = Controller->FindVehicleCandidate(Seat))
        {
            RefreshVehicle(*Car, Seat, false, Controller->IsVehicleRequestPending());
            return;
        }
    }
    AMarburgumInteractable* Target = bAdmitted ? Controller->FindInteractionCandidate() : nullptr;
    if (!Target)
    {
        if (!LastSignature.IsEmpty())
        {
            LastSignature.Reset();
            Shown.Reset();
            Card->SetVisibility(ESlateVisibility::Collapsed);
        }
        return;
    }

    FGuid PendingId;
    TWeakObjectPtr<AMarburgumInteractable> PendingTarget;
    const bool bWaiting = Controller->GetPendingInteraction(PendingId, PendingTarget) && PendingTarget.Get() == Target;
    const AMarburgumPlayerState* State = Controller->GetPlayerState<AMarburgumPlayerState>();
    const bool bHasCharacter = State && State->HasCharacter();
    if (MarburgumII::Gameplay::FindFishingSpot(Target->InteractableId.ToString()))
    {
        RefreshFishing(*Target, State, bWaiting);
        return;
    }
    if (!Target->IsItemSource())
    {
        RefreshAuthored(*Target, *Controller, State, bWaiting);
        return;
    }
    const int32 Stock = Target->GetRemainingStock();

    enum class EState : uint8 { Available, Waiting, Empty, Unknown, NoCharacter };
    const EState Mode = bWaiting ? EState::Waiting
        : !bHasCharacter ? EState::NoCharacter
        : !Target->IsStockKnown() ? EState::Unknown
        : Stock <= 0 ? EState::Empty
        : EState::Available;

    const FString Signature = FString::Printf(TEXT("%s|%d|%d|%d"), *Target->InteractableId.ToString(),
        static_cast<int32>(Mode), Stock, Target->bTestFixture ? 1 : 0);
    if (Signature == LastSignature)
    {
        return;
    }
    LastSignature = Signature;
    Shown = Target;

    FText Verb = Target->Verb.IsEmpty() ? LOCTEXT("DefaultVerb", "Use") : Target->Verb;
    FText Detail;
    ETone Tone = ETone::Accent;
    bool bKeyActive = true;
    switch (Mode)
    {
    case EState::Available:
        Detail = FText::Format(LOCTEXT("Stock", "{0} left  ·  shared by everyone on this server"), FText::AsNumber(Stock));
        break;
    case EState::Waiting:
        Verb = LOCTEXT("WaitingVerb", "Waiting for the server");
        Detail = LOCTEXT("Waiting", "Request sent. Nothing changes until the server confirms it.");
        Tone = ETone::Info;
        bKeyActive = false;
        break;
    case EState::Empty:
        Verb = LOCTEXT("EmptyVerb", "Empty");
        Detail = LOCTEXT("Empty", "Nothing left to take. The server keeps this count for every player.");
        Tone = ETone::Neutral;
        bKeyActive = false;
        break;
    case EState::Unknown:
        Detail = LOCTEXT("Unknown", "Waiting for the server's count.");
        Tone = ETone::Neutral;
        bKeyActive = false;
        break;
    case EState::NoCharacter:
        Detail = LOCTEXT("NoCharacter", "No character selected for this connection; the server will refuse.");
        Tone = ETone::Warning;
        break;
    }

    Apply(*Target, Verb, Detail, Tone, bKeyActive);
}

void UMarburgumInteractionPrompt::RefreshAuthored(const AMarburgumInteractable& Target, const AMarburgumPlayerController& Controller,
    const AMarburgumPlayerState* State, bool bWaiting)
{
    using namespace MarburgumII::Gameplay;
    using MarburgumII::Missions::EPlanAction;
    // What the server would decide is mirrored here only to choose words; the server still decides.
    FMarburgumMissionPreview Preview;
    const bool bKnown = Controller.PlanMissionInteraction(Target, Preview);
    const FMissionDefinition* Mission = Preview.MissionId.IsNone() ? nullptr : FindMission(Preview.MissionId.ToString());
    const FMissionStageDefinition* Stage = Mission && !Preview.StageId.IsNone() ? FindStage(*Mission, Preview.StageId.ToString()) : nullptr;
    const EPlanAction Action = static_cast<EPlanAction>(Preview.Action);

    enum class EState : uint8 { Waiting, NoCharacter, Unknown, Plan };
    const EState Mode = bWaiting ? EState::Waiting
        : !(State && State->HasCharacter()) ? EState::NoCharacter
        : !bKnown ? EState::Unknown
        : EState::Plan;

    const FString Signature = FString::Printf(TEXT("authored|%s|%d|%d|%s|%s|%s"), *Target.InteractableId.ToString(), static_cast<int32>(Mode),
        Preview.Action, *Preview.MissionId.ToString(), *Preview.StageId.ToString(), *Preview.MissingItem.ToString());
    if (Signature == LastSignature)
    {
        return;
    }
    LastSignature = Signature;
    Shown = const_cast<AMarburgumInteractable*>(&Target);

    const bool bPerson = Target.IsA<AMarburgumNpc>();
    FText Verb = Target.Verb.IsEmpty() ? (bPerson ? LOCTEXT("TalkVerb", "Talk") : LOCTEXT("InspectVerb", "Inspect")) : Target.Verb;
    FText Detail;
    ETone Tone = ETone::Accent;
    bool bKeyActive = true;
    auto ItemName = [](const FName& Item)
    {
        const FItemDefinition* Definition = FindItem(Item.ToString());
        return FText::FromString(Definition ? Definition->DisplayName : Item.ToString());
    };
    switch (Mode)
    {
    case EState::Waiting:
        Verb = LOCTEXT("WaitingVerb", "Waiting for the server");
        Detail = LOCTEXT("Waiting", "Request sent. Nothing changes until the server confirms it.");
        Tone = ETone::Info;
        bKeyActive = false;
        break;
    case EState::NoCharacter:
        Detail = LOCTEXT("NpcNoCharacter", "No character selected for this connection; the server will refuse.");
        Tone = ETone::Warning;
        break;
    case EState::Unknown:
        Detail = LOCTEXT("NpcUnknown", "Waiting for the server's record of your missions.");
        Tone = ETone::Neutral;
        bKeyActive = false;
        break;
    case EState::Plan:
        switch (Action)
        {
        case EPlanAction::StartMission:
            Detail = FText::Format(LOCTEXT("Offer", "Has a job for you  \u00B7  {0}"), FText::FromString(Mission->Name));
            break;
        case EPlanAction::CompleteStage:
            Verb = FText::FromString(Stage->Action);
            Detail = FText::Format(LOCTEXT("StageReady", "{0}  \u00B7  next step"), FText::FromString(Mission->Name));
            break;
        case EPlanAction::ChoiceRequired:
            Verb = FText::FromString(Stage->Action);
            Detail = FText::Format(LOCTEXT("ChoiceDue", "{0}  \u00B7  a decision is yours to make"), FText::FromString(Mission->Name));
            break;
        case EPlanAction::RequirementNotMet:
            Verb = FText::FromString(Stage->Action);
            Detail = Stage->Objective == EObjectiveType::InventoryAny
                ? FText::Format(LOCTEXT("NeedsAny", "Bring {0} first."), ItemName(Preview.MissingItem))
                : FText::Format(LOCTEXT("Needs", "You need {0} for this."), ItemName(Preview.MissingItem));
            Tone = ETone::Neutral;
            bKeyActive = false;
            break;
        case EPlanAction::PrerequisiteIncomplete:
        {
            const FMissionDefinition* Before = FindMission(Preview.Prerequisite.ToString());
            Detail = FText::Format(LOCTEXT("Later", "Not yet. Finish \u201C{0}\u201D first."),
                FText::FromString(Before ? Before->Name : Preview.Prerequisite.ToString()));
            Tone = ETone::Neutral;
            break;
        }
        case EPlanAction::NothingToDo:
            Detail = bPerson ? LOCTEXT("Idle", "Nothing more to discuss for now.") : LOCTEXT("IdlePlace", "Nothing more to find here for now.");
            Tone = ETone::Neutral;
            break;
        }
        break;
    }
    Apply(Target, Verb, Detail, Tone, bKeyActive);
}

void UMarburgumInteractionPrompt::RefreshFishing(const AMarburgumInteractable& Spot, const AMarburgumPlayerState* State, bool bWaiting)
{
    using namespace MarburgumII::Gameplay;
    const FMarburgumFishingStatus Fishing = State ? State->GetFishing() : FMarburgumFishingStatus();
    const bool bRod = State && State->IsInventoryKnown() && State->CountItem(FName(FishingRodItem)) > 0;
    const FString Signature = FString::Printf(TEXT("fishing|%s|%d|%d|%d|%d"), *Spot.InteractableId.ToString(), bWaiting ? 1 : 0,
        static_cast<int32>(Fishing.Phase), Fishing.Attempt, bRod ? 1 : 0);
    if (Signature == LastSignature)
    {
        return;
    }
    LastSignature = Signature;
    Shown = const_cast<AMarburgumInteractable*>(&Spot);

    FText Verb;
    FText Detail;
    ETone Tone = ETone::Accent;
    bool bKeyActive = true;
    if (bWaiting)
    {
        Verb = LOCTEXT("WaitingVerb", "Waiting for the server");
        Detail = LOCTEXT("Waiting", "Request sent. Nothing changes until the server confirms it.");
        Tone = ETone::Info;
        bKeyActive = false;
    }
    else if (Fishing.Phase == EMarburgumFishingPhase::Bite)
    {
        Verb = LOCTEXT("StrikeVerb", "Strike!");
        Detail = LOCTEXT("Bite", "Something has taken the bait. Strike now.");
        Tone = ETone::Success;
    }
    else if (Fishing.Phase == EMarburgumFishingPhase::Waiting)
    {
        Verb = LOCTEXT("WaitVerb", "Wait for a bite");
        Detail = LOCTEXT("LineIn", "Line in the water. Striking before a bite loses the cast; walking off ends it.");
        Tone = ETone::Neutral;
    }
    else if (!bRod)
    {
        Verb = LOCTEXT("FishVerb", "Fish");
        Detail = LOCTEXT("NoRod", "You need a fishing rod.");
        Tone = ETone::Neutral;
        bKeyActive = false;
    }
    else
    {
        Verb = LOCTEXT("CastVerb", "Cast");
        Detail = LOCTEXT("Cast", "Cast your line and wait for a bite, then strike.");
    }
    Apply(Spot, Verb, Detail, Tone, bKeyActive);
}

void UMarburgumInteractionPrompt::RefreshVehicle(const AMarburgumVehicle& Vehicle, FName Seat, bool bSeated, bool bWaiting)
{
    const bool bDriver = Seat == Vehicle.GetDriverSeatId();
    const FString Signature = FString::Printf(TEXT("vehicle|%s|%s|%d|%d"), *Vehicle.GetName(), *Seat.ToString(), bSeated ? 1 : 0, bWaiting ? 1 : 0);
    if (Signature == LastSignature)
    {
        return;
    }
    LastSignature = Signature;
    Shown = const_cast<AMarburgumVehicle*>(&Vehicle);

    FText Verb;
    FText Detail;
    ETone Tone = ETone::Accent;
    bool bKeyActive = true;
    if (bWaiting)
    {
        Verb = LOCTEXT("WaitingVerb", "Waiting for the server");
        Detail = LOCTEXT("VehicleWaiting", "Request sent. You move only when the server confirms it.");
        Tone = ETone::Info;
        bKeyActive = false;
    }
    else if (bSeated)
    {
        Verb = LOCTEXT("ExitVerb", "Get out");
        Detail = bDriver
            ? LOCTEXT("Driving", "[W] Throttle  ·  [S] Brake / reverse  ·  [A][D] Steer  ·  [Shift] Handbrake")
            : LOCTEXT("Riding", "Riding as a passenger. The driver has the wheel.");
        Tone = ETone::Neutral;
    }
    else
    {
        Verb = bDriver ? LOCTEXT("DriveVerb", "Drive") : LOCTEXT("RideVerb", "Get in");
        Detail = bDriver ? LOCTEXT("DriverSeat", "Driver's seat") : LOCTEXT("PassengerSeat", "Passenger seat");
    }
    ApplyTo(Vehicle, Vehicle.GetDisplayName(), false, Verb, Detail, Tone, bKeyActive);
}

void UMarburgumInteractionPrompt::Apply(const AMarburgumInteractable& Target, const FText& Verb, const FText& Detail, ETone Tone, bool bKeyActive)
{
    ApplyTo(Target, Target.DisplayName.IsEmpty() ? FText::FromName(Target.InteractableId) : Target.DisplayName, Target.bTestFixture, Verb,
        Detail, Tone, bKeyActive);
}

void UMarburgumInteractionPrompt::ApplyTo(const AActor& Target, const FText& Subject, bool bFixture, const FText& Verb, const FText& Detail,
    ETone Tone, bool bKeyActive)
{
    const FTokens& T = Tokens();
    VerbLabel->SetText(FText::FromString(Verb.ToString().ToUpper()));
    VerbLabel->SetColorAndOpacity(FSlateColor(Tone == ETone::Neutral ? T.Color.TextSecondary : ToneColor(Tone)));
    SubjectLabel->SetText(Subject);
    DetailLabel->SetText(Detail);
    FixturePill->SetVisibility(bFixture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    KeyCap->SetBrush(RoundedBrush(bKeyActive ? T.Color.Accent : T.Color.Raised, T.Radius.Medium,
        bKeyActive ? T.Color.AccentPressed : T.Color.BorderStrong, T.BorderWidth));
    KeyLabel->SetColorAndOpacity(FSlateColor(bKeyActive ? T.Color.TextOnAccent : T.Color.TextMuted));
    Card->SetVisibility(ESlateVisibility::HitTestInvisible);
}

#undef LOCTEXT_NAMESPACE
