#pragma once

#include "CoreMinimal.h"
#include "MarburgumGameState.h"
#include "MarburgumInteractionTypes.generated.h"

/** Why the server refused an interaction. `None` accompanies Applied and Duplicate. */
UENUM(BlueprintType)
enum class EMarburgumInteractionReason : uint8
{
    None,
    InvalidOperation,     // Missing operation id.
    AuthorityUnavailable, // No authoritative interaction service in this world.
    StoreUnavailable,     // The durable world store could not be loaded; nothing can be committed.
    StoreWriteFailed,     // The commit could not be made durable, so it was not applied.
    NoCharacter,          // The connection has no development character identity.
    OperationConflict,    // The operation id already belongs to another character's receipt.
    UnknownTarget,        // Not an interactable in this world.
    OutOfRange,           // The player's pawn is too far from the target.
    OutOfStock,           // The limited resource is exhausted.
    RequirementNotMet,    // The current mission stage needs something the character does not have.
    NothingToDo,          // The target offers this character nothing (e.g. its mission is already under way elsewhere).
    ChoiceRequired,       // The current stage here is a decision: send a choice, not a plain interaction (ADR-049).
    UnknownChoice,        // No such choice at the current stage, or no decision is due here.
    InsufficientMoney,    // The choice costs more money than the character has.
    PrerequisiteIncomplete, // The mission offered here needs another mission completed first.
    NoRod,                // Fishing needs a fishing rod in the character's custody (ADR-048).
    TooSoon,              // Casts closer together than the minimum interval.
    CannotFishNow,        // Seated, swimming or down: fishing needs both feet on the bank.
    TooEarly              // Struck before anything bit: the attempt is over.
};

/** What a committed interaction did. Replays (Duplicate) report the original effect. */
UENUM(BlueprintType)
enum class EMarburgumInteractionEffect : uint8
{
    None,
    ItemGranted,    // Took one unit from a limited stock.
    MissionStarted, // Accepted a mission offer; its start items were granted in the same commit.
    StageCompleted, // Completed the current stage of a mission; MissionStage is the new current stage.
    MissionCompleted, // Completed the final stage (or decision) of a mission; reward and journal entry recorded.
    FishingCast,    // Cast at a fishing spot. Nothing durable changes: the wait is the server's, not the store's.
    FishCaught      // Struck while a fish was on: the fish and its Fishing XP were committed together.
};

/** Where a character's cast has got to (ADR-048); replicated to its owner only. */
UENUM(BlueprintType)
enum class EMarburgumFishingPhase : uint8
{
    None,
    Waiting, // Line in the water; nothing has taken an interest yet.
    Bite     // Something is on: strike now.
};

/** How the last cast ended. */
UENUM(BlueprintType)
enum class EMarburgumFishingEnd : uint8
{
    None,
    Caught,
    Escaped,   // The bite window passed without a strike.
    TooEarly,  // Struck before the bite.
    Cancelled  // Walked off, sat down in a car, fell in or went down.
};

/** One character's progress in one mission (ontology `MissionState`); replicated to its owner only. */
USTRUCT(BlueprintType)
struct FMarburgumMissionState
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Mission")
    FName MissionId;

    /** Current stage id. */
    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Mission")
    FName StageId;

    /** World-store revision at which the mission started. */
    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Mission")
    int64 StartedRevision = 0;

    /** The final stage is done; StageId is the last stage (ADR-049). */
    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Mission")
    bool bCompleted = false;

    /** The choice taken at a decision stage and the outcome it recorded; None when there was no decision. */
    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Mission")
    FName Choice;

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Mission")
    FName Outcome;
};

/** XP a character has in one skill (ADR-047); level and progress come from MarburgumSkillCurve. Owner only. */
USTRUCT(BlueprintType)
struct FMarburgumSkillProgress
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Skills")
    FName SkillId;

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Skills")
    int64 Xp = 0;
};

/** A character's line in the water (ADR-048); owner only. */
USTRUCT(BlueprintType)
struct FMarburgumFishingStatus
{
    GENERATED_BODY()

    /** Fishing spot the line is in; None when not fishing. */
    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Fishing")
    FName SpotId;

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Fishing")
    EMarburgumFishingPhase Phase = EMarburgumFishingPhase::None;

    /** Counts this character's casts in this session; a new number means a new cast. */
    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Fishing")
    int32 Attempt = 0;

    /** How the last finished cast ended, and what it landed. */
    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Fishing")
    EMarburgumFishingEnd LastEnd = EMarburgumFishingEnd::None;

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Fishing")
    FName LastCatch;

    bool IsActive() const { return Phase != EMarburgumFishingPhase::None; }

    bool operator==(const FMarburgumFishingStatus& Other) const
    {
        return SpotId == Other.SpotId && Phase == Other.Phase && Attempt == Other.Attempt && LastEnd == Other.LastEnd
            && LastCatch == Other.LastCatch;
    }
};

/**
 * What a character's body has left (ontology signal `player_condition`); replicated to its owner only.
 * Whole points: the movement component keeps the exact running value, this is what the HUD, the
 * Character panel and the world store see. Drowning is the only thing that changes vitality yet
 * (ADR-032): it takes all of it, and the body recovers in full after RecoverySeconds.
 */
USTRUCT(BlueprintType)
struct FMarburgumCondition
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Condition")
    uint8 Vitality = 100;

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Condition")
    uint8 MaxVitality = 100;

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Condition")
    uint8 Stamina = 100;

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Condition")
    uint8 MaxStamina = 100;

    /** Emptied by a run; cannot run again until stamina is back to RunResumeStamina. */
    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Condition")
    bool bWinded = false;

    /** Running right now, as the server simulated the last move. */
    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Condition")
    bool bRunning = false;

    /** In the water, as the server simulated the last move (ADR-032). */
    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Condition")
    bool bSwimming = false;

    /** The mouth is under: stamina is the breath being held. */
    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Condition")
    bool bSubmerged = false;

    /** Whole seconds until a body with no vitality is alive again; 0 when it is alive. */
    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Condition")
    uint8 RecoverySeconds = 0;

    bool IsDown() const { return Vitality == 0; }

    bool operator==(const FMarburgumCondition& Other) const
    {
        return Vitality == Other.Vitality && MaxVitality == Other.MaxVitality && Stamina == Other.Stamina
            && MaxStamina == Other.MaxStamina && bWinded == Other.bWinded && bRunning == Other.bRunning
            && bSwimming == Other.bSwimming && bSubmerged == Other.bSubmerged && RecoverySeconds == Other.RecoverySeconds;
    }
};

/** A validated stack of one item definition in a character's custody (ontology `Inventory`). */
USTRUCT(BlueprintType)
struct FMarburgumItemStack
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Inventory")
    FName ItemId;

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Inventory")
    int32 Quantity = 0;
};

/**
 * Authoritative answer to one interaction request (ontology `authoritative_receipt`). Applied means the
 * change was durably stored before it was replicated; Duplicate repeats the original committed grant of
 * the same operation id and changes nothing; Rejected changes nothing.
 */
USTRUCT(BlueprintType)
struct FMarburgumInteractionReceipt
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Interaction")
    FGuid OperationId;

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Interaction")
    EMarburgumRequestOutcome Outcome = EMarburgumRequestOutcome::Rejected;

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Interaction")
    EMarburgumInteractionReason Reason = EMarburgumInteractionReason::None;

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Interaction")
    FName InteractableId;

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Interaction")
    EMarburgumInteractionEffect Effect = EMarburgumInteractionEffect::None;

    /** Mission started or advanced by the committed operation, and its stage afterwards. */
    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Interaction")
    FName MissionId;

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Interaction")
    FName MissionStage;

    /** Item granted (ItemGranted, MissionStarted, FishCaught) or consumed (StageCompleted) by the operation. */
    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Interaction")
    FName ItemId;

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Interaction")
    int32 Quantity = 0;

    /** Everything else the operation handed over (a stage's grant items, a mission's further start items). */
    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Interaction")
    TArray<FMarburgumItemStack> ItemsGranted;

    /** Choice taken and outcome recorded (a decision stage). */
    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Interaction")
    FName Choice;

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Interaction")
    FName ChoiceOutcome;

    /** Money paid (positive) or spent (negative): a mission reward or a choice. */
    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Interaction")
    int64 MoneyDelta = 0;

    /** Skill XP the operation awarded. */
    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Interaction")
    TArray<FMarburgumSkillProgress> SkillXp;

    /** Stock of the target after this decision; -1 when unknown. */
    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Interaction")
    int32 StockRemaining = -1;

    /** World-store revision that recorded the grant; 0 for refusals. */
    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Interaction")
    int64 WorldRevision = 0;
};

namespace MarburgumII::Gameplay
{
    MARBURGUMII_API const TCHAR* ToString(EMarburgumInteractionReason Reason);
    MARBURGUMII_API const TCHAR* ToString(EMarburgumInteractionEffect Effect);
    MARBURGUMII_API const TCHAR* ToString(EMarburgumRequestOutcome Outcome);
    MARBURGUMII_API const TCHAR* ToString(EMarburgumFishingPhase Phase);
    MARBURGUMII_API const TCHAR* ToString(EMarburgumFishingEnd End);
}
