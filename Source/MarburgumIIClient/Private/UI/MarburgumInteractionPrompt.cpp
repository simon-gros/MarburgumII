#include "UI/MarburgumInteractionPrompt.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumInteractable.h"
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
    const AMarburgumInteractable* Target = Shown.Get();
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
    AMarburgumInteractable* Target = Controller && UMarburgumSessionSubsystem::IsAdmitted(Controller->GetWorld())
        ? Controller->FindInteractionCandidate() : nullptr;
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
    if (const AMarburgumNpc* Npc = Cast<AMarburgumNpc>(Target))
    {
        RefreshNpc(*Npc, State, bWaiting);
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

void UMarburgumInteractionPrompt::RefreshNpc(const AMarburgumNpc& Npc, const AMarburgumPlayerState* State, bool bWaiting)
{
    using namespace MarburgumII::Gameplay;
    // What the server would decide is mirrored here only to choose words; the server still decides.
    const FMissionDefinition* Offer = FindMissionOfferedBy(Npc.InteractableId.ToString());
    const FMissionDefinition* StageMission = nullptr;
    const FMissionStageDefinition* Stage = nullptr;
    bool bHasRequired = true;
    if (State && State->AreMissionsKnown())
    {
        for (const FMarburgumMissionState& Progress : State->GetMissions())
        {
            const FMissionDefinition* Mission = FindMission(Progress.MissionId.ToString());
            const FMissionStageDefinition* Current = Mission ? FindStage(*Mission, Progress.StageId.ToString()) : nullptr;
            if (Current && Npc.InteractableId.ToString() == Current->CompleteTarget)
            {
                StageMission = Mission;
                Stage = Current;
                break;
            }
        }
        if (Offer && State->FindMission(FName(Offer->Id)))
        {
            Offer = nullptr; // Already taken up.
        }
    }
    if (Stage && Stage->RequiredAnyItemCount > 0)
    {
        bHasRequired = false;
        for (int32 Index = 0; Index < Stage->RequiredAnyItemCount && !bHasRequired; ++Index)
        {
            bHasRequired = State->CountItem(FName(Stage->RequiredAnyItems[Index])) > 0;
        }
    }

    enum class EState : uint8 { Waiting, NoCharacter, Unknown, Offer, Stage, StageBlocked, Idle };
    const EState Mode = bWaiting ? EState::Waiting
        : !(State && State->HasCharacter()) ? EState::NoCharacter
        : !State->AreMissionsKnown() ? EState::Unknown
        : Offer ? EState::Offer
        : Stage ? (bHasRequired ? EState::Stage : EState::StageBlocked)
        : EState::Idle;

    const FString Signature = FString::Printf(TEXT("npc|%s|%d|%s"), *Npc.InteractableId.ToString(),
        static_cast<int32>(Mode), Stage ? Stage->Id : TEXT("-"));
    if (Signature == LastSignature)
    {
        return;
    }
    LastSignature = Signature;
    Shown = const_cast<AMarburgumNpc*>(&Npc);

    FText Verb = Npc.Verb.IsEmpty() ? LOCTEXT("TalkVerb", "Talk") : Npc.Verb;
    FText Detail;
    ETone Tone = ETone::Accent;
    bool bKeyActive = true;
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
    case EState::Offer:
        Detail = FText::Format(LOCTEXT("Offer", "Has a job for you  \u00B7  {0}"), FText::FromString(Offer->Name));
        break;
    case EState::Stage:
        Verb = FText::FromString(Stage->Action);
        Detail = FText::Format(LOCTEXT("StageReady", "{0}  \u00B7  next step"), FText::FromString(StageMission->Name));
        break;
    case EState::StageBlocked:
    {
        Verb = FText::FromString(Stage->Action);
        TArray<FString> Names;
        for (int32 Index = 0; Index < Stage->RequiredAnyItemCount; ++Index)
        {
            const FItemDefinition* Item = FindItem(Stage->RequiredAnyItems[Index]);
            Names.Add(Item ? Item->DisplayName : Stage->RequiredAnyItems[Index]);
        }
        Detail = FText::Format(LOCTEXT("StageBlocked", "Needs one fish: {0}. Fishing is not in this build yet."),
            FText::FromString(FString::Join(Names, TEXT(", "))));
        Tone = ETone::Neutral;
        bKeyActive = false;
        break;
    }
    case EState::Idle:
        Detail = LOCTEXT("Idle", "Nothing more to discuss for now.");
        Tone = ETone::Neutral;
        bKeyActive = false;
        break;
    }
    Apply(Npc, Verb, Detail, Tone, bKeyActive);
}

void UMarburgumInteractionPrompt::Apply(const AMarburgumInteractable& Target, const FText& Verb, const FText& Detail, ETone Tone, bool bKeyActive)
{
    const FTokens& T = Tokens();
    VerbLabel->SetText(FText::FromString(Verb.ToString().ToUpper()));
    VerbLabel->SetColorAndOpacity(FSlateColor(Tone == ETone::Neutral ? T.Color.TextSecondary : ToneColor(Tone)));
    SubjectLabel->SetText(Target.DisplayName.IsEmpty() ? FText::FromName(Target.InteractableId) : Target.DisplayName);
    DetailLabel->SetText(Detail);
    FixturePill->SetVisibility(Target.bTestFixture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    KeyCap->SetBrush(RoundedBrush(bKeyActive ? T.Color.Accent : T.Color.Raised, T.Radius.Medium,
        bKeyActive ? T.Color.AccentPressed : T.Color.BorderStrong, T.BorderWidth));
    KeyLabel->SetColorAndOpacity(FSlateColor(bKeyActive ? T.Color.TextOnAccent : T.Color.TextMuted));
    Card->SetVisibility(ESlateVisibility::HitTestInvisible);
}

#undef LOCTEXT_NAMESPACE
