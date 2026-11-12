#pragma once

#include "CoreMinimal.h"

// Gameplay vocabulary shared by the core, client and server modules (Gate 1 and the Lent slice).
// Engine-neutral: ids are the stable snake_case content ids of Data/Gameplay/gameplay.v0.1.json, which
// `mb2 validate gameplay` keeps in step with this header and the generated MarburgumGameplayCatalogue.h.
namespace MarburgumII::Gameplay
{
    // ---- Catalogue definition types. The tables themselves are generated from Data/Gameplay/gameplay.v0.1.json
    // into MarburgumGameplayCatalogue.h (`mb2 generate gameplay`, ADR-049). Display text is presentation only. ----

    enum class EItemKind : uint8 { Tool, Fish, Clue };

    /** One registered item definition (ontology `ItemDefinition`), ids and English names from the original items.json. */
    struct FItemDefinition
    {
        const TCHAR* Id;
        const TCHAR* DisplayName;
        EItemKind Kind;
    };

    /** A skill (original skills.json). Progress is XP on the 0-1990 curve (MarburgumSkillCurve.h, ADR-047). */
    struct FSkillDefinition
    {
        const TCHAR* Id;
        const TCHAR* DisplayName;
        /** Reputation can be lost as well as earned. */
        bool bReversible;
    };

    /** One species a fishing spot can yield: weighted draw, XP to Fishing on landing it (original fishing.json). */
    struct FFishSpecies
    {
        const TCHAR* Item;
        int32 Weight;
        int32 Xp;
    };

    struct FFishingSpotDefinition
    {
        const TCHAR* Id;
        const TCHAR* DisplayName;
        const FFishSpecies* Species;
        int32 SpeciesCount;
    };

    enum class EInteractableKind : uint8 { Npc, FishingSpot, Clue, Location };

    /**
     * Where an authored interactable stands. Map: the map commandlet places it (the angler). Original: the server
     * spawns it at the original game's position on the Lent level. AnglerRelative: at the original offset from
     * wherever the map put the angler. Pad: outside the built windows, stood on a temporary floor until that
     * window is imported. Unplaced: still has no floor.
     */
    enum class EPlacement : uint8 { Map, Original, AnglerRelative, Unplaced, Pad };

    /** An authored interactable from the original interactions.json. */
    struct FInteractableDefinition
    {
        const TCHAR* Id;
        EInteractableKind Kind;
        const TCHAR* DisplayName;
        const TCHAR* Verb;
        const TCHAR* Dialogue;
        /** Original Godot metres: x east, y up, z south of the middle of Stari most. */
        double GodotPosition[3];
        float RangeCm;
        EPlacement Placement;
        float PrimaryColor[3];
        float SecondaryColor[3];
    };

    struct FItemQuantity
    {
        const TCHAR* Id;
        int32 Quantity;
    };

    struct FMissionFlag
    {
        const TCHAR* Id;
        bool bValue;
    };

    struct FSkillXp
    {
        const TCHAR* Skill;
        int32 Xp;
    };

    /** One way to resolve a choice stage: money, skill XP, a recorded outcome and flags (original missions.json). */
    struct FMissionChoiceDefinition
    {
        const TCHAR* Id;
        const TCHAR* Label;
        int32 MoneyDelta;
        const FSkillXp* SkillXp;
        int32 SkillXpCount;
        const TCHAR* Outcome;
        const TCHAR* CompletionText;
        const FMissionFlag* SetFlags;
        int32 SetFlagCount;
    };

    /** Interaction: talk to or inspect CompleteTarget. InventoryAny: hold one of ObjectiveItems. Choice: pick a choice there. */
    enum class EObjectiveType : uint8 { Interaction, InventoryAny, Choice };

    /** One mission stage (original project/data/missions.json). Text is presentation only. */
    struct FMissionStageDefinition
    {
        const TCHAR* Id;
        const TCHAR* ObjectiveText;
        /** Interactable that completes the stage. */
        const TCHAR* CompleteTarget;
        /** Verb shown at CompleteTarget while this stage is current. */
        const TCHAR* Action;
        EObjectiveType Objective;
        /** InventoryAny: one of these, ObjectiveQuantity of it, consumed when bConsume. */
        const TCHAR* const* ObjectiveItems;
        int32 ObjectiveItemCount;
        int32 ObjectiveQuantity;
        bool bConsume;
        /** Held, not consumed (a key that opens a door stays in the pocket). */
        const FItemQuantity* RequiredItems;
        int32 RequiredItemCount;
        /** Handed over when the stage completes. */
        const FItemQuantity* GrantItems;
        int32 GrantItemCount;
        const FMissionFlag* SetFlags;
        int32 SetFlagCount;
        const FMissionChoiceDefinition* Choices;
        int32 ChoiceCount;
        bool bFinal;
        const TCHAR* CompletionText;
    };

    /** A registered mission with every stage of the original. */
    struct FMissionDefinition
    {
        const TCHAR* Id;
        const TCHAR* Name;
        const TCHAR* Category;
        const TCHAR* OfferTarget;
        const TCHAR* Summary;
        const TCHAR* StoryFamily;
        /** Missions that must be completed before this one is offered. */
        const TCHAR* const* RequiresCompleted;
        int32 RequiresCompletedCount;
        /** Granted, all or nothing, when the mission starts (`start_items`). */
        const FItemQuantity* StartItems;
        int32 StartItemCount;
        const FMissionStageDefinition* Stages;
        int32 StageCount;
        /** Paid once, when the final stage completes. */
        int32 RewardMoney;
        /** Journal entry unlocked on completion; empty id for none. */
        const TCHAR* JournalEntryId;
        const TCHAR* JournalEntryTitle;
        const TCHAR* JournalEntryText;
    };
}

#include "MarburgumGameplayCatalogue.h"

namespace MarburgumII::Gameplay
{
    inline const FItemDefinition* FindItem(const FString& Id)
    {
        for (const FItemDefinition& Item : Items)
        {
            if (Id == Item.Id)
            {
                return &Item;
            }
        }
        return nullptr;
    }

    inline const FSkillDefinition* FindSkill(const FString& Id)
    {
        for (const FSkillDefinition& Skill : Skills)
        {
            if (Id == Skill.Id)
            {
                return &Skill;
            }
        }
        return nullptr;
    }

    inline const FFishingSpotDefinition* FindFishingSpot(const FString& Id)
    {
        for (const FFishingSpotDefinition& Spot : FishingSpots)
        {
            if (Id == Spot.Id)
            {
                return &Spot;
            }
        }
        return nullptr;
    }

    inline const FInteractableDefinition* FindInteractable(const FString& Id)
    {
        for (const FInteractableDefinition& Entry : Interactables)
        {
            if (Id == Entry.Id)
            {
                return &Entry;
            }
        }
        return nullptr;
    }

    // Development test fixtures placed on /Game/Maps/NetSmoke by the map commandlet.
    // They are labeled as fixtures in the world and the HUD; they are not authored Marburgum content.
    inline constexpr const TCHAR* SupplyCrateId = TEXT("dev_supply_crate");
    inline constexpr const TCHAR* FarSupplyCrateId = TEXT("dev_supply_crate_far");

    // ---- Lent slice: authored content carried forward from the original Marburgum data ----

    /** A named map area (original project/data/areas.json). The first area containing a point wins. */
    struct FAreaDefinition
    {
        const TCHAR* Id;
        const TCHAR* DisplayName;
    };

    inline constexpr FAreaDefinition Areas[] = {
        { TEXT("lent"), TEXT("Lent") },
    };

    /** What the original game calls a place outside every named area (areas.json `outside.name`). */
    inline constexpr const TCHAR* OutsideAreaName = TEXT("Maribor");

    inline const FAreaDefinition* FindArea(const FString& Id)
    {
        for (const FAreaDefinition& Area : Areas)
        {
            if (Id == Area.Id)
            {
                return &Area;
            }
        }
        return nullptr;
    }

    // The riverside angler from the original interactions.json; offers the first story mission.
    inline constexpr const TCHAR* LentAnglerId = TEXT("lent_angler");

    inline const FMissionDefinition* FindMission(const FString& Id)
    {
        for (const FMissionDefinition& Mission : Missions)
        {
            if (Id == Mission.Id)
            {
                return &Mission;
            }
        }
        return nullptr;
    }

    inline const FMissionStageDefinition* FindStage(const FMissionDefinition& Mission, const FString& StageId)
    {
        for (int32 Index = 0; Index < Mission.StageCount; ++Index)
        {
            if (StageId == Mission.Stages[Index].Id)
            {
                return &Mission.Stages[Index];
            }
        }
        return nullptr;
    }

    inline const FMissionChoiceDefinition* FindChoice(const FMissionStageDefinition& Stage, const FString& ChoiceId)
    {
        for (int32 Index = 0; Index < Stage.ChoiceCount; ++Index)
        {
            if (ChoiceId == Stage.Choices[Index].Id)
            {
                return &Stage.Choices[Index];
            }
        }
        return nullptr;
    }

    // ---- Fishing (original src/shared/protocol.h and MarburgumFishingRules.cpp, ADR-048) ----

    // After a cast something takes an interest between FishingMinWaitMs and FishingMaxWaitMs later; then the
    // fish is on for FishingBiteWindowMs, and a strike in that window lands it. A strike before the bite, or
    // none in time, ends the attempt. The original's purchasable ranks that shortened the wait or widened the
    // window are excluded with that system (ADR-004): this is the original's base rule.
    inline constexpr int32 FishingMinWaitMs = 1500;
    inline constexpr int32 FishingMaxWaitMs = 4000;
    inline constexpr int32 FishingBiteWindowMs = 1800;
    // Casts closer together than this are refused (MIN_FISHING_INTERVAL_MSEC).
    inline constexpr int32 FishingMinCastIntervalMs = 1000;
    // Fishing needs both feet on the bank: a body more than this far from where it cast ends the cast.
    inline constexpr float FishingMoveCancelCm = 75.f;
    // The rod every cast needs.
    inline constexpr const TCHAR* FishingRodItem = TEXT("fishing_rod");
    // The skill a landed fish trains.
    inline constexpr const TCHAR* FishingSkill = TEXT("fishing");

    // ---- Player condition (original src/shared/combat.h and protocol.h) ----

    // Walking is a brisk walk; running is exactly twice that, a run rather than a sprint.
    inline constexpr float WalkSpeedCm = 220.f;
    inline constexpr float RunSpeedCm = 2.f * WalkSpeedCm;

    // Vitality and stamina of an ordinary person.
    inline constexpr float DefaultVitality = 100.f;
    inline constexpr float DefaultStamina = 100.f;

    // Running spends breath: a full bar is ten seconds of running. Breath comes back at a steady rate
    // whenever the body is not running. A run that empties the bar leaves the body winded, and a winded
    // body walks until a little breath has come back, so it does not flicker between walking and running.
    inline constexpr float RunStaminaDrainPerSecond = 10.f;
    inline constexpr float StaminaRecoveryPerSecond = 11.f;
    inline constexpr float RunResumeStamina = 15.f;

    // The server corrects a client whose predicted stamina differs from its own by more than this.
    inline constexpr float StaminaCorrectionTolerance = 1.f;

    // ---- Swimming, breath and drowning (original protocol.h, combat.h, world_simulation.cpp tick_water) ----

    // Swimming is slower than walking: you are pushing water, not ground.
    inline constexpr float SwimSpeedCm = 110.f;
    // Mouth of a standing adult above the feet. Under that line there is no air.
    inline constexpr float MouthHeightCm = 155.f;
    // Holding a direction keeps the mouth this far above the surface, rising at SwimRiseCmPerSecond.
    inline constexpr float SwimMouthAboveSurfaceCm = 15.f;
    inline constexpr float SwimRiseCmPerSecond = 120.f;
    // Letting go, the body slips under until the mouth is this far below, sinking at SinkCmPerSecond,
    // and never further than MaxSinkBelowSurfaceCm of feet under the surface.
    inline constexpr float IdleMouthBelowSurfaceCm = 35.f;
    inline constexpr float SinkCmPerSecond = 45.f;
    inline constexpr float MaxSinkBelowSurfaceCm = 260.f;
    // A fall into the river puts the mouth this far under. Swimming is what brings it back up.
    inline constexpr float FallInMouthBelowSurfaceCm = 40.f;
    // The mouth counts as under once it is this far below the surface.
    inline constexpr float SubmergedMarginCm = 2.f;

    // The bar is also the breath you can hold: an ordinary untrained person lasts about a minute under.
    // Still under with none left, the body breathes water.
    inline constexpr float BreathHoldSeconds = 60.f;
    inline constexpr float BreathDrainPerSecond = DefaultStamina / BreathHoldSeconds;

    // Water gives, until it doesn't: an untrained impact from about forty-five metres meets the river as
    // a solid. Below that a fall into water does no harm.
    inline constexpr float LethalWaterImpactCm = 4500.f;

    // Climbing out: steering, with the mouth no deeper than BankReachBelowSurfaceCm, a swimmer pulls up
    // onto standable ground within BankSearchCm ahead whose top is no lower than ShoreMinBelowSurfaceCm
    // under the surface and no higher than BankMaxRiseCm above it.
    inline constexpr float BankReachBelowSurfaceCm = 80.f;
    inline constexpr float BankSearchCm = 160.f;
    inline constexpr int32 BankAheadSteps = 4;
    inline constexpr float ShoreMinBelowSurfaceCm = 25.f;
    inline constexpr float BankMaxRiseCm = 100.f;

    // A body whose vitality reaches zero is taken to the recovery point and stands there this long
    // before it is alive again, with full vitality and breath.
    inline constexpr float RecoverySeconds = 60.f;

    // The server accepts an interaction when the pawn is within the target's range plus this
    // allowance for movement that replicated after the client chose the target.
    inline constexpr float RangeToleranceCm = 50.f;

    // Requests beyond this rate are dropped without a reply (no reliable-RPC amplification).
    inline constexpr int32 MaxRequestsPerSecond = 8;

    // Clients that hear nothing back stop showing "waiting" after this long.
    inline constexpr double ReceiptTimeoutSeconds = 6.0;

    // How often the server re-derives each player's named area from the pawn position.
    inline constexpr float LocationUpdateSeconds = 0.25f;

    // Development spawn selection: a join-URL option (?MB2Start=<PlayerStart tag>). Without it, or when
    // no start has the tag, the server uses starts tagged DefaultStartTag, then any start.
    inline constexpr const TCHAR* StartOption = TEXT("MB2Start");
    inline constexpr const TCHAR* DefaultStartTag = TEXT("arrival");
    // A body with no vitality is taken to the start with this tag, else to the arrival start (ADR-032).
    inline constexpr const TCHAR* RecoveryStartTag = TEXT("recovery");

    // Development character identity: a join-URL option (?MB2Character=<key>). It is NOT
    // authentication; see ADR-014. Keys are 3-40 characters of [a-z0-9_-].
    inline constexpr const TCHAR* CharacterOption = TEXT("MB2Character");

    inline bool IsValidCharacterKey(const FString& Key)
    {
        if (Key.Len() < 3 || Key.Len() > 40)
        {
            return false;
        }
        for (const TCHAR Char : Key)
        {
            const bool bAllowed = (Char >= TEXT('a') && Char <= TEXT('z')) || (Char >= TEXT('0') && Char <= TEXT('9'))
                || Char == TEXT('_') || Char == TEXT('-');
            if (!bAllowed)
            {
                return false;
            }
        }
        return true;
    }

    // Durable world-store record format written by the server (MarburgumIIServer).
    inline constexpr const TCHAR* WorldStoreFormat = TEXT("marburgumii.world_store");
    // Schema 2 adds per-character missions and typed receipts; schema 3 adds per-character condition; schema 4
    // adds skill XP, money and mission outcomes (ADR-047, ADR-049). Older documents load and are rewritten at the
    // current schema by the next commit.
    inline constexpr int32 WorldStoreSchemaVersion = 4;
    inline constexpr int32 WorldStoreOldestReadableSchema = 1;

    // Prefix for machine-parsed interaction evidence lines (scripts/interaction-smoke-linux.sh).
    inline constexpr const TCHAR* InteractLogPrefix = TEXT("MB2INTERACT");
}
