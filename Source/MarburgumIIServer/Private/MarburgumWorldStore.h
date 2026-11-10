#pragma once

#include "CoreMinimal.h"

/**
 * One committed operation, kept so a replayed operation id is answered without applying it again.
 * Kind is the effect's wire name (`item_granted`, `mission_started`, `stage_completed`); schema 1
 * receipts, which predate missions, load as `item_granted`.
 */
struct FMarburgumStoredReceipt
{
    FString Character;
    FString Kind;
    FString Interactable;
    FString Item;
    int32 Quantity = 0;
    FString Mission;
    FString Stage;
    int64 Revision = 0;
    FString CommittedUtc;
};

/** One character's progress in one mission. */
struct FMarburgumStoredMission
{
    FString Stage;
    int64 StartedRevision = 0;
};

/** A character's saved condition (ADR-020). Absent from the record means a full, rested body. */
struct FMarburgumStoredCondition
{
    int32 Vitality = 100;
    int32 Stamina = 100;
    bool bWinded = false;
    /** Seconds a body with no vitality still had to wait when it was saved (ADR-032); 0 when alive. */
    int32 RecoverySeconds = 0;

    bool operator==(const FMarburgumStoredCondition& Other) const
    {
        return Vitality == Other.Vitality && Stamina == Other.Stamina && bWinded == Other.bWinded
            && RecoverySeconds == Other.RecoverySeconds;
    }
};

/** Durable authoritative world state for one map (schema `marburgumii.world_store` v3). */
struct FMarburgumWorldRecord
{
    int64 Revision = 0;
    /** Interactable id -> remaining stock. Absent ids still have their authored initial stock. */
    TMap<FString, int32> Stocks;
    /** Character key -> item id -> quantity. */
    TMap<FString, TMap<FString, int32>> Inventories;
    /** Character key -> mission id -> progress. */
    TMap<FString, TMap<FString, FMarburgumStoredMission>> Missions;
    /** Character key -> condition, for characters whose body is not full and rested. */
    TMap<FString, FMarburgumStoredCondition> Conditions;
    /** Operation id (32 hex digits) -> committed operation. */
    TMap<FString, FMarburgumStoredReceipt> Receipts;
};

/**
 * Development persistence adapter (ADR-014): one JSON document per map, replaced atomically on every
 * commit (write a temporary file, flush it to disk, rename it over the old one). A commit is applied
 * in memory only after the new document is durable, so a crash or a failed write never exposes state
 * that a restart would lose. Single-process and synchronous by design; not the production database.
 */
class FMarburgumWorldStore
{
public:
    explicit FMarburgumWorldStore(const FString& InPath) : Path(InPath) {}

    /**
     * A missing file is an empty world. A present but unreadable file is an error, never overwritten.
     * Older documents load as-is (schema 1 has no missions, schema 2 no conditions) and the next commit
     * rewrites them at the current schema.
     */
    bool Load(FString& OutError);

    /** Schema version of the loaded document; 0 for a new world. */
    int32 GetLoadedSchema() const { return LoadedSchema; }

    /** Make Next durable, then adopt it. On failure the previous record stays current. */
    bool Commit(const FMarburgumWorldRecord& Next, FString& OutError);

    const FMarburgumWorldRecord& Get() const { return Record; }
    const FString& GetPath() const { return Path; }

    /**
     * Test-only (-MB2StoreFaultInjection): while <store>.fail holds a positive count, that many
     * commits fail as if the disk write had failed. Used to prove a failed write changes nothing.
     */
    void EnableFaultInjection() { bFaultInjection = true; }

private:
    bool ConsumeInjectedFault() const;

    static FString Serialize(const FMarburgumWorldRecord& Source);
    static bool Deserialize(const FString& Text, FMarburgumWorldRecord& Out, int32& OutSchema, FString& OutError);

    FString Path;
    FMarburgumWorldRecord Record;
    int32 LoadedSchema = 0;
    bool bFaultInjection = false;
};
