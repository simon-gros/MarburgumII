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
