#include "MarburgumFishingRules.h"

#include "MarburgumMissionRules.h"

using namespace MarburgumII::Gameplay;

namespace MarburgumII::Fishing
{
    double BiteDelaySeconds(uint32 Roll)
    {
        const uint32 Span = static_cast<uint32>(FishingMaxWaitMs - FishingMinWaitMs);
        return (FishingMinWaitMs + static_cast<int32>(Span == 0 ? 0 : Roll % (Span + 1))) / 1000.0;
    }

    double BiteWindowSeconds()
    {
        return FishingBiteWindowMs / 1000.0;
    }

    const FFishSpecies* DrawSpecies(const FFishingSpotDefinition& Spot, int64 Roll)
    {
        int64 Total = 0;
        for (int32 Index = 0; Index < Spot.SpeciesCount; ++Index)
        {
            Total += FMath::Max(Spot.Species[Index].Weight, 0);
        }
        if (Total <= 0)
        {
            return nullptr;
        }
        int64 Bucket = Roll % Total;
        if (Bucket < 0)
        {
            Bucket += Total;
        }
        for (int32 Index = 0; Index < Spot.SpeciesCount; ++Index)
        {
            const int64 Weight = FMath::Max(Spot.Species[Index].Weight, 0);
            if (Bucket < Weight)
            {
                return &Spot.Species[Index];
            }
            Bucket -= Weight;
        }
        return nullptr;
    }

    FMarburgumCatchDecision DecideCatch(const FMarburgumWorldRecord& Current, const FString& Character, const FString& SpotId,
        int64 Roll, FMarburgumWorldRecord& OutNext)
    {
        FMarburgumCatchDecision Decision;
        const FFishingSpotDefinition* Spot = FindFishingSpot(SpotId);
        const FFishSpecies* Species = Spot ? DrawSpecies(*Spot, Roll) : nullptr;
        if (!Species || !FindItem(Species->Item) || Species->Xp <= 0)
        {
            return Decision;
        }
        FMarburgumWorldRecord Next = Current;
        ++Next.Revision;
        Next.Inventories.FindOrAdd(Character).FindOrAdd(Species->Item) += 1;
        if (!Missions::AddSkillXp(Next, Character, FishingSkill, Species->Xp))
        {
            return Decision;
        }
        OutNext = MoveTemp(Next);
        Decision.bApplied = true;
        Decision.Item = Species->Item;
        Decision.Xp = Species->Xp;
        return Decision;
    }
}
