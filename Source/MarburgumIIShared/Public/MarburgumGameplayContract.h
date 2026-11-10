#pragma once

#include "CoreMinimal.h"

// Gameplay vocabulary shared by the core, client and server modules (Gate 1 and the Lent slice).
// Engine-neutral: ids are the stable snake_case content ids of Data/Gameplay/gameplay.v0.1.json, which
// scripts/validate-gameplay-contract.py keeps in step with this header.
namespace MarburgumII::Gameplay
{
    /** One registered item definition (ontology `ItemDefinition`). Display text is presentation only. */
    struct FItemDefinition
    {
        const TCHAR* Id;
        const TCHAR* DisplayName;
    };

    // Runtime subset of the item catalogue; ids and English names from the original Marburgum
    // project/data/items.json (version 3). The three fish are registered because the first mission's
    // requirement names them; nothing in this build can grant one yet (fishing is Stage 2).
    inline constexpr FItemDefinition Items[] = {
        { TEXT("fishing_rod"), TEXT("Fishing rod") },
        { TEXT("chub"), TEXT("European chub") },
        { TEXT("roach"), TEXT("Common roach") },
        { TEXT("perch"), TEXT("European perch") },
    };

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

    // Development test fixtures placed on /Game/Maps/NetSmoke by scripts/editor/create_net_smoke_map.py.
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

    /** One mission stage (original project/data/missions.json). Text is presentation only. */
    struct FMissionStageDefinition
    {
        const TCHAR* Id;
        const TCHAR* ObjectiveText;
        /** Interactable that completes the stage. */
        const TCHAR* CompleteTarget;
        /** Verb shown at CompleteTarget while this stage is current. */
        const TCHAR* Action;
        /** `inventory_any`: one of these items, consumed on completion. Empty for interaction stages. */
        const TCHAR* const* RequiredAnyItems;
        int32 RequiredAnyItemCount;
    };

    /** A registered mission. Only the stages this build can reach are registered. */
    struct FMissionDefinition
    {
        const TCHAR* Id;
        const TCHAR* Name;
        const TCHAR* Category;
        const TCHAR* OfferTarget;
        const TCHAR* Summary;
        /** Granted, all or nothing, when the mission starts (`start_items`). */
        const TCHAR* StartItem;
        int32 StartItemQuantity;
        const FMissionStageDefinition* Stages;
        int32 StageCount;
    };

    inline constexpr const TCHAR* OrdinaryFish[] = { TEXT("chub"), TEXT("roach"), TEXT("perch") };

    // `lent_first_catch` stages 1-2 of 5. Stage 1 cannot complete until fishing exists (Stage 2), and the
    // stage 2 debris is not placed in any map yet, so a mission never gets past stage 2 in this build.
    // Stages 3-5 (Petra Kralj, the service room, Vera Horvat) are not registered. Completing a stage
    // moves to the next registered one; there is no "mission complete" state until the last stage exists.
    inline constexpr FMissionStageDefinition LentFirstCatchStages[] = {
        { TEXT("catch_first_fish"),
          TEXT("Catch one ordinary fish from the nearby Drava spot, then show it to the riverside angler."),
          TEXT("lent_angler"), TEXT("Show the catch"), OrdinaryFish, UE_ARRAY_COUNT(OrdinaryFish) },
        { TEXT("recover_cashbox"),
          TEXT("Inspect the debris beside the Drava and pull out the battered metal cashbox."),
          TEXT("drava_cashbox_debris"), TEXT("Pull out the cashbox"), nullptr, 0 },
    };

    inline constexpr FMissionDefinition Missions[] = {
        { TEXT("lent_first_catch"), TEXT("Something in the Water"), TEXT("story"), TEXT("lent_angler"),
          TEXT("An ordinary Fishing assignment on the Drava has turned into a trail of old records, addresses and people."),
          TEXT("fishing_rod"), 1, LentFirstCatchStages, UE_ARRAY_COUNT(LentFirstCatchStages) },
    };

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

    /** The mission an interactable offers, if any. */
    inline const FMissionDefinition* FindMissionOfferedBy(const FString& InteractableId)
    {
        for (const FMissionDefinition& Mission : Missions)
        {
            if (InteractableId == Mission.OfferTarget)
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
    // Schema 2 adds per-character missions and typed receipts; schema 3 adds per-character condition.
    // Older documents load and are rewritten at the current schema by the next commit.
    inline constexpr int32 WorldStoreSchemaVersion = 3;
    inline constexpr int32 WorldStoreOldestReadableSchema = 1;

    // Prefix for machine-parsed interaction evidence lines (scripts/interaction-smoke-linux.sh).
    inline constexpr const TCHAR* InteractLogPrefix = TEXT("MB2INTERACT");
}
