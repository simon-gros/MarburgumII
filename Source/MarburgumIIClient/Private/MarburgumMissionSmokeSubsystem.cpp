#include "MarburgumMissionSmokeSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumCharacter.h"
#include "MarburgumInteractable.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumNpc.h"
#include "MarburgumPhotography.h"
#include "MarburgumPlayerController.h"
#include "MarburgumPlayerState.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Session/MarburgumSessionSubsystem.h"
#include "UI/MarburgumUISubsystem.h"
#include "UnrealClient.h"

#define MB2_MISSION_LOG(Format, ...) \
    UE_LOG(LogMarburgumNet, Log, TEXT("MB2MISSION role=client tag=%s " Format), *Role, ##__VA_ARGS__)

namespace MarburgumMissionSmoke
{
    constexpr double SettleSeconds = 1.5;
    constexpr double ToastSettleSeconds = 0.8;
    constexpr double ShotTimeoutSeconds = 10.0;
    // Longer than the original's longest wait (4 s) plus its bite window (1.8 s), with slack for replication.
    constexpr double FishingPhaseTimeoutSeconds = 9.0;
    const TCHAR* const Rod = TEXT("fishing_rod");
    const TCHAR* const FirstCatch = TEXT("lent_first_catch");
    const FName Angler(TEXT("lent_angler"));
    const FName Spot(TEXT("drava_fishing_spot"));
}

using namespace MarburgumMissionSmoke;
using MarburgumII::Gameplay::ToString;

bool UMarburgumMissionSmokeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
    static_cast<void>(Outer);
    return false; // Test harnesses and capture tools never run in a player build.
#else
    const UGameInstance* GameInstance = Cast<UGameInstance>(Outer);
    return GameInstance && !GameInstance->IsDedicatedServerInstance() && FParse::Param(FCommandLine::Get(), TEXT("MB2MissionSmoke"));
#endif
}

void UMarburgumMissionSmokeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    const TCHAR* CommandLine = FCommandLine::Get();
    FParse::Value(CommandLine, TEXT("MB2MissionRole="), Role);
    FParse::Value(CommandLine, TEXT("MB2MissionShot="), ShotPrefix);
    FParse::Value(CommandLine, TEXT("MB2MissionTimeout="), TimeoutSeconds);
    FString Text;
    if (FParse::Value(CommandLine, TEXT("MB2MissionTarget="), Text)) TargetId = FName(*Text);
    if (FParse::Value(CommandLine, TEXT("MB2MissionExpect="), Text)) ExpectEffect = FName(*Text);
    if (FParse::Value(CommandLine, TEXT("MB2MissionId="), Text)) MissionId = FName(*Text);
    if (FParse::Value(CommandLine, TEXT("MB2MissionChoice="), Text)) ChoiceId = FName(*Text);
    Role = Role.ToUpper();
    StartSeconds = FPlatformTime::Seconds();
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UMarburgumMissionSmokeSubsystem::Tick), 0.f);
    MB2_MISSION_LOG("event=start target=%s expect=%s mission=%s choice=%s shot=%d", *TargetId.ToString(), *ExpectEffect.ToString(),
        *MissionId.ToString(), *ChoiceId.ToString(), ShotPrefix.IsEmpty() ? 0 : 1);
}

void UMarburgumMissionSmokeSubsystem::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
    if (AMarburgumPlayerController* Bound = Controller.Get())
    {
        Bound->OnInteractionReceipt.RemoveAll(this);
    }
    Super::Deinitialize();
}

bool UMarburgumMissionSmokeSubsystem::Bind()
{
    UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    if (!UMarburgumSessionSubsystem::IsAdmitted(World))
    {
        return false;
    }
    AMarburgumPlayerController* Local = Cast<AMarburgumPlayerController>(World->GetFirstPlayerController());
    const AMarburgumPlayerState* State = Local ? Local->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    if (!State || !State->IsInventoryKnown() || !State->AreMissionsKnown() || !State->IsProgressKnown() || !Local->GetPawn())
    {
        return false;
    }
    if (Controller.Get() != Local)
    {
        Local->OnInteractionReceipt.AddUObject(this, &UMarburgumMissionSmokeSubsystem::HandleReceipt);
        Controller = Local;
        StepSeconds = FPlatformTime::Seconds();
        MB2_MISSION_LOG("event=ready player=%d missions=%d money=%lld fishing_xp=%lld", State->GetPlayerId(), State->GetMissions().Num(),
            State->GetMoney(), State->GetSkillXp(TEXT("fishing")));
    }
    return true;
}

void UMarburgumMissionSmokeSubsystem::HandleReceipt(const FMarburgumInteractionReceipt& Receipt)
{
    Receipts.Add(Receipt.OperationId, Receipt);
    MB2_MISSION_LOG("event=receipt op=%s target=%s outcome=%s reason=%s effect=%s mission=%s stage=%s item=%s granted=%d choice=%s outcome_id=%s money=%lld",
        *Receipt.OperationId.ToString(EGuidFormats::Digits), *Receipt.InteractableId.ToString(), ToString(Receipt.Outcome), ToString(Receipt.Reason),
        ToString(Receipt.Effect), *Receipt.MissionId.ToString(), *Receipt.MissionStage.ToString(), *Receipt.ItemId.ToString(),
        Receipt.ItemsGranted.Num(), *Receipt.Choice.ToString(), *Receipt.ChoiceOutcome.ToString(), Receipt.MoneyDelta);
}

const FMarburgumInteractionReceipt* UMarburgumMissionSmokeSubsystem::ReceiptFor(const FGuid& OperationId) const
{
    return OperationId.IsValid() ? Receipts.Find(OperationId) : nullptr;
}

AMarburgumInteractable* UMarburgumMissionSmokeSubsystem::Find(FName Id) const
{
    const UWorld* World = Controller.IsValid() ? Controller->GetWorld() : nullptr;
    for (TActorIterator<AMarburgumInteractable> It(World); World && It; ++It)
    {
        if (It->InteractableId == Id)
        {
            return *It;
        }
    }
    return nullptr;
}

FGuid UMarburgumMissionSmokeSubsystem::Submit(AMarburgumInteractable* At, const TCHAR* Label)
{
    const FGuid Id = FGuid::NewGuid();
    if (AMarburgumPlayerController* Local = Controller.Get(); Local && At)
    {
        Local->SubmitInteraction(Id, At);
    }
    MB2_MISSION_LOG("event=submit kind=%s target=%s op=%s distance_m=%.1f", Label, At ? *At->InteractableId.ToString() : TEXT("-"),
        *Id.ToString(EGuidFormats::Digits),
        At && Controller.IsValid() && Controller->GetPawn() ? FVector::Dist(Controller->GetPawn()->GetActorLocation(), At->GetActorLocation()) / 100.f : -1.f);
    return Id;
}

const AMarburgumPlayerState* UMarburgumMissionSmokeSubsystem::LocalState() const
{
    const AMarburgumPlayerController* Local = Controller.Get();
    return Local ? Local->GetPlayerState<AMarburgumPlayerState>() : nullptr;
}

int32 UMarburgumMissionSmokeSubsystem::Count(const TCHAR* Item) const
{
    const AMarburgumPlayerState* State = LocalState();
    return State ? State->CountItem(FName(Item)) : -1;
}

int32 UMarburgumMissionSmokeSubsystem::Fish() const
{
    return Count(TEXT("chub")) + Count(TEXT("roach")) + Count(TEXT("perch"));
}

const FMarburgumMissionState* UMarburgumMissionSmokeSubsystem::Progress(const TCHAR* Mission) const
{
    const AMarburgumPlayerState* State = LocalState();
    return State ? State->FindMission(FName(Mission)) : nullptr;
}

void UMarburgumMissionSmokeSubsystem::Expect(bool bCondition, const FString& What)
{
    if (!bCondition)
    {
        Problems.Add(What);
        MB2_MISSION_LOG("event=expectation_failed what=%s", *What);
    }
}

void UMarburgumMissionSmokeSubsystem::Shot(const TCHAR* Label)
{
    if (ShotPrefix.IsEmpty())
    {
        return;
    }
    PendingShot = FString::Printf(TEXT("%s-%s.png"), *ShotPrefix, Label);
    IFileManager::Get().Delete(*PendingShot, false, true, true);
    FScreenshotRequest::RequestScreenshot(PendingShot, true, false);
    ShotRequestedSeconds = FPlatformTime::Seconds();
}

void UMarburgumMissionSmokeSubsystem::Advance(int32 Next, double Now)
{
    Step = Next;
    StepSeconds = Now;
}

bool UMarburgumMissionSmokeSubsystem::Tick(float DeltaTime)
{
    if (bDone)
    {
        return true;
    }
    const double Now = FPlatformTime::Seconds();
    if (Now - StartSeconds > TimeoutSeconds)
    {
        Fail(FString::Printf(TEXT("timeout step=%d"), Step));
        return true;
    }
    if (!PendingShot.IsEmpty())
    {
        const bool bSaved = IFileManager::Get().FileSize(*PendingShot) > 0;
        if (!bSaved && Now - ShotRequestedSeconds < ShotTimeoutSeconds)
        {
            return true;
        }
        MB2_MISSION_LOG("event=shot file=%s saved=%d", *FPaths::GetCleanFilename(PendingShot), bSaved ? 1 : 0);
        Expect(bSaved, FString::Printf(TEXT("shot_not_saved=%s"), *FPaths::GetCleanFilename(PendingShot)));
        PendingShot.Reset();
    }
    if (!Bind())
    {
        return true;
    }
    if (Role == TEXT("FISH")) TickFish(Now);
    else if (Role == TEXT("STEP")) TickStep(Now);
    else if (Role == TEXT("CHOICE")) TickChoice(Now);
    else if (Role == TEXT("CHECK")) TickCheck(Now);
    else if (Role == TEXT("PHOTO")) TickPhoto(Now);
    else if (Role == TEXT("STORYCHECK")) TickStoryCheck(Now);
    else Fail(TEXT("unknown_role"));
    return true;
}

void UMarburgumMissionSmokeSubsystem::TickFish(double Now)
{
    const AMarburgumPlayerState* State = LocalState();
    const FMarburgumInteractionReceipt* R = ReceiptFor(Op);
    const FMarburgumFishingStatus& Line = State->GetFishing();
    switch (Step)
    {
    case 0:
        if (Now - StepSeconds < SettleSeconds) return;
        Expect(Find(Angler) && Find(Spot), TEXT("angler_or_spot_missing"));
        Expect(Count(Rod) == 0 && State->GetMissions().IsEmpty(), FString::Printf(TEXT("start rods=%d missions=%d"), Count(Rod), State->GetMissions().Num()));
        Op = Submit(Find(Angler), TEXT("accept"));
        Advance(1, Now);
        return;
    case 1:
        if (!R || Count(Rod) < 1) return;
        Expect(R->Outcome == EMarburgumRequestOutcome::Applied && R->Effect == EMarburgumInteractionEffect::MissionStarted,
            FString::Printf(TEXT("accept=%s/%s"), ToString(R->Outcome), ToString(R->Effect)));
        Op = Submit(Find(Angler), TEXT("no_fish_yet"));
        Advance(2, Now);
        return;
    case 2:
        if (!R) return;
        Expect(R->Reason == EMarburgumInteractionReason::RequirementNotMet, FString::Printf(TEXT("no_fish=%s"), ToString(R->Reason)));
        AttemptBefore = Line.Attempt;
        Op = Submit(Find(Spot), TEXT("cast_1"));
        Advance(3, Now);
        return;
    case 3:
        if (!R) return;
        if (!bCastReplayed)
        {
            Expect(R->Effect == EMarburgumInteractionEffect::FishingCast, FString::Printf(TEXT("cast_1=%s/%s"), ToString(R->Outcome), ToString(R->Reason)));
            // The same cast id again, as a retry would send it: Duplicate, and the line stays in the water (QA 2026-11-19).
            bCastReplayed = true;
            Receipts.Remove(Op);
            if (AMarburgumPlayerController* Local = Controller.Get())
            {
                Local->SubmitInteraction(Op, Find(Spot));
            }
            MB2_MISSION_LOG("event=submit kind=cast_1_replay op=%s", *Op.ToString(EGuidFormats::Digits));
            return;
        }
        Expect(R->Outcome == EMarburgumRequestOutcome::Duplicate && R->Effect == EMarburgumInteractionEffect::FishingCast && Line.IsActive(),
            FString::Printf(TEXT("cast_1_replay=%s/%s/%s active=%d"), ToString(R->Outcome), ToString(R->Effect), ToString(R->Reason), Line.IsActive() ? 1 : 0));
        // Strike at once: nothing can have bitten in under the original's 1.5 s minimum wait.
        Op = Submit(Find(Spot), TEXT("strike_too_early"));
        Advance(4, Now);
        return;
    case 4:
        if (!R) return;
        Expect(R->Reason == EMarburgumInteractionReason::TooEarly, FString::Printf(TEXT("too_early=%s"), ToString(R->Reason)));
        // Straight away again: inside the minimum cast interval.
        Op = Submit(Find(Spot), TEXT("cast_too_soon"));
        Advance(5, Now);
        return;
    case 5:
        if (!R) return;
        Expect(R->Reason == EMarburgumInteractionReason::TooSoon, FString::Printf(TEXT("too_soon=%s/%s"), ToString(R->Outcome), ToString(R->Reason)));
        Advance(6, Now);
        return;
    case 6:
        if (Now - StepSeconds < 1.2) return;
        Op = Submit(Find(Spot), TEXT("cast_2"));
        Advance(7, Now);
        return;
    case 7:
        // Let this one get away: wait for the bite, then do nothing until the window passes.
        if (Line.LastEnd == EMarburgumFishingEnd::Escaped && !Line.IsActive())
        {
            MB2_MISSION_LOG("event=escaped attempt=%d", Line.Attempt);
            Advance(8, Now);
            return;
        }
        if (Now - StepSeconds >= FishingPhaseTimeoutSeconds) Fail(TEXT("no_escape"));
        return;
    case 8:
        FishBefore = Fish();
        Op = Submit(Find(Spot), TEXT("cast_3"));
        Shot(TEXT("waiting"));
        Advance(9, Now);
        return;
    case 9:
        if (Line.Phase != EMarburgumFishingPhase::Bite)
        {
            if (Now - StepSeconds >= FishingPhaseTimeoutSeconds) Fail(TEXT("no_bite"));
            return;
        }
        MB2_MISSION_LOG("event=bite attempt=%d after_s=%.2f", Line.Attempt, Now - StepSeconds);
        // ADR-081: the body shows the line it has out: the rod in the right hand and a fishing clip.
        if (const AMarburgumCharacter* Body = Controller.IsValid() ? Cast<AMarburgumCharacter>(Controller->GetPawn()) : nullptr)
        {
            MB2_MISSION_LOG("event=fishing_presentation pose=%s rod=%d clip=%d", ToString(Body->GetFishingPose()), Body->IsRodInHand() ? 1 : 0,
                Body->IsFishingClipPlaying() ? 1 : 0);
            Expect(Body->IsRodInHand() && Body->IsFishingClipPlaying(), TEXT("no_rod_or_clip_while_fishing"));
        }
        Op = Submit(Find(Spot), TEXT("strike"));
        Advance(10, Now);
        return;
    case 10:
        if (!R || Fish() <= FishBefore || State->GetSkillXp(TEXT("fishing")) <= 0) return;
        Expect(R->Outcome == EMarburgumRequestOutcome::Applied && R->Effect == EMarburgumInteractionEffect::FishCaught && R->SkillXp.Num() == 1,
            FString::Printf(TEXT("strike=%s/%s/%s"), ToString(R->Outcome), ToString(R->Effect), ToString(R->Reason)));
        MB2_MISSION_LOG("event=caught item=%s fishing_xp=%lld", *R->ItemId.ToString(), State->GetSkillXp(TEXT("fishing")));
        Advance(11, Now);
        return;
    case 11:
        if (Now - StepSeconds < ToastSettleSeconds) return;
        Shot(TEXT("caught"));
        Op = Submit(Find(Angler), TEXT("show_catch"));
        Advance(12, Now);
        return;
    case 12:
    {
        const FMarburgumMissionState* First = Progress(FirstCatch);
        if (!R || !First || First->StageId != FName(TEXT("recover_cashbox"))) return;
        Expect(R->Effect == EMarburgumInteractionEffect::StageCompleted, FString::Printf(TEXT("show_catch=%s"), ToString(R->Effect)));
        Expect(Fish() == FishBefore, FString::Printf(TEXT("fish_consumed fish=%d before=%d"), Fish(), FishBefore));
        Pass();
        return;
    }
    default:
        return;
    }
}

void UMarburgumMissionSmokeSubsystem::TickStep(double Now)
{
    const FMarburgumInteractionReceipt* R = ReceiptFor(Op);
    switch (Step)
    {
    case 0:
        if (Now - StepSeconds < SettleSeconds) return;
        if (!Find(TargetId))
        {
            Fail(FString::Printf(TEXT("target_missing=%s"), *TargetId.ToString()));
            return;
        }
        Op = Submit(Find(TargetId), TEXT("step"));
        Advance(1, Now);
        return;
    case 1:
        if (!R) return;
        // A step can also be expected to be refused (ADR-083): refused:<reason>, with nothing changed.
        Expect(ExpectEffect.ToString().StartsWith(TEXT("refused:"))
                ? R->Outcome == EMarburgumRequestOutcome::Rejected && ExpectEffect == FName(*FString::Printf(TEXT("refused:%s"), ToString(R->Reason)))
                : R->Outcome == EMarburgumRequestOutcome::Applied && ExpectEffect == FName(ToString(R->Effect)),
            FString::Printf(TEXT("step=%s/%s/%s want=%s"), ToString(R->Outcome), ToString(R->Reason), ToString(R->Effect), *ExpectEffect.ToString()));
        MB2_MISSION_LOG("event=step_result target=%s outcome=%s reason=%s effect=%s mission=%s stage=%s granted=%d money=%lld", *TargetId.ToString(),
            ToString(R->Outcome), ToString(R->Reason), ToString(R->Effect), *R->MissionId.ToString(), *R->MissionStage.ToString(), R->ItemsGranted.Num(),
            R->MoneyDelta);
        Advance(2, Now);
        return;
    case 2:
        if (Now - StepSeconds < ToastSettleSeconds) return;
        // ADR-081: the server told everyone this person was spoken to; on this client they turn and talk (the angler
        // keeps fishing, rod in hand).
        if (const AMarburgumNpc* Person = Cast<AMarburgumNpc>(Find(TargetId)))
        {
            MB2_MISSION_LOG("event=npc_presentation target=%s clip=%s rod=%d line=%d turn_deg=%.0f body=%s", *TargetId.ToString(),
                *Person->GetPlayingClip().ToString(), Person->IsRodInHand() ? 1 : 0, Person->IsLineOut() ? 1 : 0, Person->GetBodyTurnDegrees(),
                Person->GetBody() && Person->GetBody()->GetSkeletalMeshAsset() ? *Person->GetBody()->GetSkeletalMeshAsset()->GetName() : TEXT("-"));
            Expect(Person->GetBody() && Person->GetBody()->GetSkeletalMeshAsset() != nullptr, TEXT("npc_has_no_body"));
            Expect(Person->bHoldsRod ? Person->IsRodInHand() && Person->GetPlayingClip().ToString().StartsWith(TEXT("fish_"))
                                     : Person->GetPlayingClip() == FName(TEXT("talk")),
                FString::Printf(TEXT("npc_presentation clip=%s rod=%d"), *Person->GetPlayingClip().ToString(), Person->IsRodInHand() ? 1 : 0));
        }
        Shot(*TargetId.ToString());
        Advance(3, Now);
        return;
    default:
        Pass();
        return;
    }
}

void UMarburgumMissionSmokeSubsystem::TickPhoto(double Now)
{
    // ADR-081: aim the view at the subject's aim point the way a player would, then press the shutter (P).
    AMarburgumPlayerController* Local = Controller.Get();
    const UMarburgumPhotographySubsystem* Photography = Local ? Local->GetWorld()->GetSubsystem<UMarburgumPhotographySubsystem>() : nullptr;
    const FMarburgumInteractionReceipt* R = ReceiptFor(Op);
    switch (Step)
    {
    case 0:
    {
        if (Now - StepSeconds < SettleSeconds) return;
        FVector Aim;
        if (!Photography || !Photography->GetAimPoint(TargetId.ToString(), Aim) || !Local->GetPawn())
        {
            if (Now - StepSeconds > 10.0)
            {
                Fail(FString::Printf(TEXT("subject_unresolved=%s"), *TargetId.ToString()));
            }
            return;
        }
        const FVector Eye = Local->GetPawn()->GetPawnViewLocation();
        Local->SetControlRotation((Aim - Eye).Rotation());
        MB2_MISSION_LOG("event=aim subject=%s distance_m=%.1f", *TargetId.ToString(), FVector::Dist(Aim, Eye) / 100.0);
        Advance(1, Now);
        return;
    }
    case 1:
    {
        // Long enough for the view to reach the server with the next moves.
        if (Now - StepSeconds < 0.8) return;
        const UMarburgumPhotographySubsystem::FInFrame Frame = Photography->FindInFrame(Local->GetPawn()->GetPawnViewLocation(),
            Local->GetControlRotation(), Local->GetPawn());
        MB2_MISSION_LOG("event=frame subject=%s in_frame=%s off_axis=%.1f", *TargetId.ToString(), Frame.Subject ? Frame.Subject->Id : TEXT("-"),
            Frame.OffAxisDegrees);
        Local->RequestPhotograph();
        FGuid Pending;
        TWeakObjectPtr<AMarburgumInteractable> Ignored;
        Local->GetPendingInteraction(Pending, Ignored);
        Op = Pending;
        Advance(2, Now);
        return;
    }
    case 2:
        if (!R) return;
        Expect(ExpectEffect.ToString().StartsWith(TEXT("refused:"))
                ? R->Outcome == EMarburgumRequestOutcome::Rejected && ExpectEffect == FName(*FString::Printf(TEXT("refused:%s"), ToString(R->Reason)))
                : R->Outcome == EMarburgumRequestOutcome::Applied && ExpectEffect == FName(ToString(R->Effect)) && R->InteractableId == TargetId,
            FString::Printf(TEXT("photo=%s/%s/%s subject=%s want=%s"), ToString(R->Outcome), ToString(R->Reason), ToString(R->Effect),
                *R->InteractableId.ToString(), *ExpectEffect.ToString()));
        Advance(3, Now);
        return;
    case 3:
        if (Now - StepSeconds < ToastSettleSeconds) return;
        MB2_MISSION_LOG("event=photo_state photography_xp=%lld print=%d", LocalState()->GetSkillXp(TEXT("photography")),
            Count(*FString::Printf(TEXT("photo_%s"), *TargetId.ToString())));
        Shot(*FString::Printf(TEXT("photo-%s"), *TargetId.ToString()));
        Advance(4, Now);
        return;
    default:
        Pass();
        return;
    }
}

void UMarburgumMissionSmokeSubsystem::TickChoice(double Now)
{
    using namespace MarburgumII::Gameplay;
    UMarburgumUISubsystem* UI = GetGameInstance()->GetSubsystem<UMarburgumUISubsystem>();
    const AMarburgumPlayerState* State = LocalState();
    switch (Step)
    {
    case 0:
    {
        if (Now - StepSeconds < SettleSeconds) return;
        AMarburgumInteractable* Target = Find(TargetId);
        if (!Target)
        {
            Fail(FString::Printf(TEXT("target_missing=%s"), *TargetId.ToString()));
            return;
        }
        // A plain interaction at a decision is refused: the decision is made in the panel.
        Op = Submit(Target, TEXT("plain"));
        Advance(1, Now);
        return;
    }
    case 1:
    {
        const FMarburgumInteractionReceipt* R = ReceiptFor(Op);
        if (!R) return;
        Expect(R->Reason == EMarburgumInteractionReason::ChoiceRequired, FString::Printf(TEXT("plain=%s"), ToString(R->Reason)));
        if (UI)
        {
            UI->ToggleChoice(Find(TargetId), MissionId);
            Expect(UI->IsChoiceOpen(), TEXT("choice_panel_not_open"));
        }
        Advance(2, Now);
        return;
    }
    case 2:
        if (Now - StepSeconds < ToastSettleSeconds) return;
        // The capture is taken at the end of a frame: choose a moment later, or the panel has closed before it is drawn.
        Shot(TEXT("choice"));
        Advance(6, Now);
        return;
    case 6:
    {
        if (Now - StepSeconds < 1.0) return;
        const FMissionDefinition* Mission = FindMission(MissionId.ToString());
        const FMarburgumMissionState* Saved = State->FindMission(MissionId);
        const FMissionStageDefinition* Stage = Mission && Saved ? FindStage(*Mission, Saved->StageId.ToString()) : nullptr;
        int32 Number = 0;
        for (int32 Index = 0; Stage && Index < Stage->ChoiceCount; ++Index)
        {
            Number = ChoiceId == FName(Stage->Choices[Index].Id) ? Index + 1 : Number;
        }
        Expect(Number > 0, FString::Printf(TEXT("choice_not_offered=%s"), *ChoiceId.ToString()));
        if (UI)
        {
            Expect(UI->ChooseOption(Number), TEXT("choose_option_refused"));
        }
        else
        {
            Op = FGuid::NewGuid();
            Controller->SubmitMissionChoice(Op, Find(TargetId), MissionId, ChoiceId);
        }
        Advance(3, Now);
        return;
    }
    case 3:
    {
        // The panel sends with its own operation id; the receipt that carries this choice is the answer.
        const FMarburgumInteractionReceipt* R = nullptr;
        for (const TPair<FGuid, FMarburgumInteractionReceipt>& Pair : Receipts)
        {
            R = Pair.Value.Choice == ChoiceId ? &Pair.Value : R;
        }
        const FMarburgumMissionState* Saved = State->FindMission(MissionId);
        // A decision part-way through a mission (stage_completed) or a deduction's wrong answer (refused:wrong_conclusion,
        // ADR-083): the receipt says which, and nothing else has to have finished.
        if (!ExpectEffect.IsNone())
        {
            if (!R) return;
            const bool bRefusal = ExpectEffect.ToString().StartsWith(TEXT("refused:"));
            Expect(bRefusal
                    ? R->Outcome == EMarburgumRequestOutcome::Rejected && ExpectEffect == FName(*FString::Printf(TEXT("refused:%s"), ToString(R->Reason)))
                    : R->Outcome == EMarburgumRequestOutcome::Applied && ExpectEffect == FName(ToString(R->Effect)),
                FString::Printf(TEXT("choice=%s/%s/%s want=%s"), ToString(R->Outcome), ToString(R->Reason), ToString(R->Effect), *ExpectEffect.ToString()));
            MB2_MISSION_LOG("event=choice_result choice=%s outcome=%s reason=%s effect=%s stage=%s granted=%d money=%lld", *ChoiceId.ToString(),
                ToString(R->Outcome), ToString(R->Reason), ToString(R->Effect), *R->MissionStage.ToString(), R->ItemsGranted.Num(), R->MoneyDelta);
            Advance(4, Now);
            return;
        }
        if (!R || !Saved || !Saved->bCompleted) return;
        Expect(R->Outcome == EMarburgumRequestOutcome::Applied && R->Effect == EMarburgumInteractionEffect::MissionCompleted,
            FString::Printf(TEXT("choice=%s/%s/%s"), ToString(R->Outcome), ToString(R->Reason), ToString(R->Effect)));
        Expect(Saved->Choice == ChoiceId && !Saved->Outcome.IsNone(), TEXT("choice_not_recorded"));
        MB2_MISSION_LOG("event=decided choice=%s outcome=%s money=%lld reputation_xp=%lld", *Saved->Choice.ToString(), *Saved->Outcome.ToString(),
            State->GetMoney(), State->GetSkillXp(TEXT("reputation")));
        Advance(4, Now);
        return;
    }
    case 4:
        if (Now - StepSeconds < ToastSettleSeconds) return;
        Shot(TEXT("decided"));
        Advance(5, Now);
        return;
    default:
        Pass();
        return;
    }
}

void UMarburgumMissionSmokeSubsystem::TickCheck(double Now)
{
    const AMarburgumPlayerState* State = LocalState();
    UMarburgumUISubsystem* UI = GetGameInstance()->GetSubsystem<UMarburgumUISubsystem>();
    switch (Step)
    {
    case 0:
    {
        if (Now - StepSeconds < SettleSeconds) return;
        const FMarburgumMissionState* First = Progress(FirstCatch);
        const FMarburgumMissionState* Kiosk = Progress(TEXT("kiosk_67_customer_stopped_coming"));
        const FMarburgumMissionState* Names = Progress(TEXT("raeda_names_no_longer_match"));
        Expect(First && First->bCompleted, TEXT("first_catch_not_completed"));
        Expect(Names && Names->bCompleted && Names->Choice == FName(TEXT("tell_maja")) && Names->Outcome == FName(TEXT("maja_informed")),
            TEXT("names_decision_not_restored"));
        Expect(Kiosk && Kiosk->bCompleted && Kiosk->Choice == FName(TEXT("pay_tab")) && Kiosk->Outcome == FName(TEXT("tab_paid")),
            TEXT("kiosk_decision_not_restored"));
        const FMarburgumMissionState* Delivery = Progress(TEXT("lent_late_delivery"));
        const FMarburgumMissionState* Hall = Progress(TEXT("hall_three_last_shift"));
        const FMarburgumMissionState* Paint = Progress(TEXT("under_the_paint_four_names"));
        Expect(Delivery && Delivery->bCompleted && Delivery->Choice == FName(TEXT("leave_clerk_share")), TEXT("delivery_not_restored"));
        Expect(Hall && Hall->bCompleted && Hall->Choice == FName(TEXT("negotiate_settlement")), TEXT("hall_not_restored"));
        Expect(Paint && Paint->bCompleted && Paint->Choice == FName(TEXT("preserve_layers")), TEXT("paint_not_restored"));
        Expect(State->GetMoney() == 90, FString::Printf(TEXT("money=%lld want 90"), State->GetMoney()));
        Expect(State->GetSkillXp(TEXT("fishing")) > 0, TEXT("fishing_xp_lost"));
        const FMarburgumMissionState* Shift = Progress(TEXT("raeda_last_shift"));
        const FMarburgumMissionState* Sale = Progress(TEXT("raeda_city_for_sale"));
        Expect(Shift && Shift->bCompleted, TEXT("last_shift_not_restored"));
        Expect(Sale && Sale->bCompleted && Sale->Choice == FName(TEXT("trust_milan")), TEXT("city_sale_not_restored"));
        Expect(State->GetSkillXp(TEXT("reputation")) == 95, FString::Printf(TEXT("reputation=%lld want 95"), State->GetSkillXp(TEXT("reputation"))));
        Expect(Count(Rod) == 1 && Count(TEXT("brass_key")) == 1 && Count(TEXT("drava_cashbox")) == 1, TEXT("custody_not_restored"));
        MB2_MISSION_LOG("event=restored missions=%d money=%lld fishing_xp=%lld reputation_xp=%lld", State->GetMissions().Num(), State->GetMoney(),
            State->GetSkillXp(TEXT("fishing")), State->GetSkillXp(TEXT("reputation")));
        if (UI)
        {
            UI->ToggleSkills();
        }
        Advance(1, Now);
        return;
    }
    case 1:
        if (Now - StepSeconds < ToastSettleSeconds) return;
        Shot(TEXT("skills"));
        if (UI)
        {
            UI->ToggleJournal();
        }
        Advance(2, Now);
        return;
    case 2:
        if (Now - StepSeconds < ToastSettleSeconds) return;
        Shot(TEXT("journal"));
        Advance(3, Now);
        return;
    default:
        Pass();
        return;
    }
}

void UMarburgumMissionSmokeSubsystem::TickStoryCheck(double Now)
{
    // ADR-081, after a server restart: what scripts/story-smoke-linux.sh played is all still there.
    const AMarburgumPlayerState* State = LocalState();
    UMarburgumUISubsystem* UI = GetGameInstance()->GetSubsystem<UMarburgumUISubsystem>();
    switch (Step)
    {
    case 0:
    {
        if (Now - StepSeconds < SettleSeconds) return;
        const auto Done = [this](const TCHAR* Mission, const TCHAR* Choice, const TCHAR* Outcome)
        {
            const FMarburgumMissionState* P = Progress(Mission);
            Expect(P && P->bCompleted && (!Choice || P->Choice == FName(Choice)) && (!Outcome || P->Outcome == FName(Outcome)),
                FString::Printf(TEXT("%s_not_restored"), Mission));
        };
        Done(TEXT("raeda_voices_on_tape"), TEXT("ask_tone"), TEXT("tone_questioned"));
        Done(TEXT("raeda_mentor_folder"), nullptr, nullptr);
        Done(TEXT("nika_then_and_now"), TEXT("darkroom_lesson"), TEXT("darkroom_lesson"));
        Done(TEXT("brane_old_letters"), TEXT("give_copies"), TEXT("prints_given"));
        Expect(State->GetMoney() == 180, FString::Printf(TEXT("money=%lld want 180"), State->GetMoney()));
        Expect(State->GetSkillXp(TEXT("reputation")) == 70, FString::Printf(TEXT("reputation=%lld want 70"), State->GetSkillXp(TEXT("reputation"))));
        Expect(State->GetSkillXp(TEXT("photography")) == 215, FString::Printf(TEXT("photography=%lld want 215"), State->GetSkillXp(TEXT("photography"))));
        for (const TCHAR* Kept : { TEXT("camera"), TEXT("canteen_cassette_c60"), TEXT("canteen_tape_notes"), TEXT("mentor_folder_note"),
                 TEXT("photo_vodni_stolp"), TEXT("photo_prehod_sign_melje"), TEXT("photo_sinagoga") })
        {
            Expect(Count(Kept) == 1, FString::Printf(TEXT("custody_not_restored=%s"), Kept));
        }
        MB2_MISSION_LOG("event=restored missions=%d money=%lld reputation_xp=%lld photography_xp=%lld", State->GetMissions().Num(), State->GetMoney(),
            State->GetSkillXp(TEXT("reputation")), State->GetSkillXp(TEXT("photography")));
        if (UI)
        {
            UI->ToggleSkills();
        }
        Advance(1, Now);
        return;
    }
    case 1:
        if (Now - StepSeconds < ToastSettleSeconds) return;
        Shot(TEXT("story-skills"));
        if (UI)
        {
            UI->ToggleJournal();
        }
        Advance(2, Now);
        return;
    case 2:
        if (Now - StepSeconds < ToastSettleSeconds) return;
        Shot(TEXT("story-journal"));
        if (UI)
        {
            UI->ToggleInventory();
        }
        Advance(3, Now);
        return;
    case 3:
        if (Now - StepSeconds < ToastSettleSeconds) return;
        Shot(TEXT("story-inventory"));
        Advance(4, Now);
        return;
    default:
        Pass();
        return;
    }
}

void UMarburgumMissionSmokeSubsystem::Pass()
{
    if (Problems.IsEmpty())
    {
        MB2_MISSION_LOG("event=pass");
        bDone = true;
        FPlatformMisc::RequestExitWithStatus(false, 0);
    }
    else
    {
        Fail(TEXT("expectations"));
    }
}

void UMarburgumMissionSmokeSubsystem::Fail(const FString& Reason)
{
    if (bDone)
    {
        return;
    }
    Problems.Add(Reason);
    MB2_MISSION_LOG("event=fail reason=%s", *FString::Join(Problems, TEXT(",")));
    bDone = true;
    FPlatformMisc::RequestExitWithStatus(false, 1);
}

#undef MB2_MISSION_LOG
