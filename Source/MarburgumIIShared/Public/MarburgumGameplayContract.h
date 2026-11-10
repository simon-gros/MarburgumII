#pragma once

#include "CoreMinimal.h"

// Gate 1 gameplay vocabulary shared by the core, client and server modules. Engine-neutral: ids are
// the stable snake_case content ids of Data/Gameplay/gameplay.v0.1.json, which
// scripts/validate-gameplay-contract.py keeps in step with this header.
namespace MarburgumII::Gameplay
{
    /** One registered item definition (ontology `ItemDefinition`). Display text is presentation only. */
    struct FItemDefinition
    {
        const TCHAR* Id;
        const TCHAR* DisplayName;
    };

    // Runtime subset of the item catalogue. `fishing_rod` keeps its id from the original Marburgum
    // project/data/items.json (version 3); no other original item is registered yet.
    inline constexpr FItemDefinition Items[] = {
        { TEXT("fishing_rod"), TEXT("Fishing rod") },
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

    // The server accepts an interaction when the pawn is within the target's range plus this
    // allowance for movement that replicated after the client chose the target.
    inline constexpr float RangeToleranceCm = 50.f;

    // Requests beyond this rate are dropped without a reply (no reliable-RPC amplification).
    inline constexpr int32 MaxRequestsPerSecond = 8;

    // Clients that hear nothing back stop showing "waiting" after this long.
    inline constexpr double ReceiptTimeoutSeconds = 6.0;

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
    inline constexpr int32 WorldStoreSchemaVersion = 1;

    // Prefix for machine-parsed interaction evidence lines (scripts/interaction-smoke-linux.sh).
    inline constexpr const TCHAR* InteractLogPrefix = TEXT("MB2INTERACT");
}
