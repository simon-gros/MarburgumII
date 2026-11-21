#include "MarburgumVegetation.h"

#include "Dom/JsonObject.h"
#include "MarburgumStreetLights.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace MarburgumII::Vegetation
{
    namespace
    {
        constexpr double ViewPerMetre = 30.0;
        constexpr double MinViewMetres = 70.0;
        constexpr double MaxViewMetres = 700.0;
        /** The engine cylinder (/Engine/BasicShapes/Cylinder) is 100 cm across and tall, about its middle. */
        constexpr double CylinderCm = 100.0;
    }

    bool Parse(const FString& Json, TArray<FSpecies>& OutSpecies, TArray<FPlant>& OutPlants, FString& OutError)
    {
        OutSpecies.Reset();
        OutPlants.Reset();
        TSharedPtr<FJsonObject> Root;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
        {
            OutError = TEXT("not JSON");
            return false;
        }
        FString Format;
        if (!Root->TryGetStringField(TEXT("format"), Format) || Format != TEXT("marburgumii.vegetation"))
        {
            OutError = TEXT("not a vegetation file (format marburgumii.vegetation)");
            return false;
        }
        int32 Version = 0;
        if (!Root->TryGetNumberField(TEXT("version"), Version) || Version != 1)
        {
            OutError = FString::Printf(TEXT("vegetation version %d, this build reads 1"), Version);
            return false;
        }
        const TArray<TSharedPtr<FJsonValue>>* Species = nullptr;
        if (!Root->TryGetArrayField(TEXT("species"), Species) || Species == nullptr || Species->IsEmpty())
        {
            OutError = TEXT("no species");
            return false;
        }
        for (int32 Index = 0; Index < Species->Num(); ++Index)
        {
            const TSharedPtr<FJsonObject>* Entry = nullptr;
            FSpecies Read;
            double Height = 0.0, Radius = 0.0, TrunkRadius = 0.0, TrunkHeight = 0.0;
            if (!(*Species)[Index].IsValid() || !(*Species)[Index]->TryGetObject(Entry) || Entry == nullptr
                || !(*Entry)->TryGetStringField(TEXT("id"), Read.Id) || Read.Id.IsEmpty()
                || !(*Entry)->TryGetBoolField(TEXT("tree"), Read.bTree)
                || !(*Entry)->TryGetNumberField(TEXT("natural_height_m"), Height) || !(*Entry)->TryGetNumberField(TEXT("natural_radius_m"), Radius)
                || !(*Entry)->TryGetNumberField(TEXT("trunk_radius_m"), TrunkRadius) || !(*Entry)->TryGetNumberField(TEXT("trunk_height_m"), TrunkHeight))
            {
                OutError = FString::Printf(TEXT("species %d is not {id, tree, natural_height_m, natural_radius_m, trunk_radius_m, trunk_height_m}"), Index);
                return false;
            }
            if (!(Height > 0.0) || Radius < 0.0 || TrunkRadius < 0.0 || TrunkHeight < 0.0)
            {
                OutError = FString::Printf(TEXT("species %s has invalid dimensions"), *Read.Id);
                return false;
            }
            Read.NaturalHeightCm = static_cast<float>(Height * 100.0);
            Read.NaturalRadiusCm = static_cast<float>(Radius * 100.0);
            Read.TrunkRadiusCm = static_cast<float>(TrunkRadius * 100.0);
            Read.TrunkHeightCm = static_cast<float>(TrunkHeight * 100.0);
            OutSpecies.Add(MoveTemp(Read));
        }
        const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
        if (!Root->TryGetArrayField(TEXT("plants"), Rows) || Rows == nullptr)
        {
            OutError = TEXT("no plants");
            return false;
        }
        OutPlants.Reserve(Rows->Num());
        for (int32 Index = 0; Index < Rows->Num(); ++Index)
        {
            const TArray<TSharedPtr<FJsonValue>>* Row = nullptr;
            if (!(*Rows)[Index].IsValid() || !(*Rows)[Index]->TryGetArray(Row) || Row == nullptr || Row->Num() != 7)
            {
                OutError = FString::Printf(TEXT("plant %d is not [east, south, ground, species, yaw, spread, grow]"), Index);
                return false;
            }
            double Values[7];
            for (int32 Field = 0; Field < 7; ++Field)
            {
                if ((*Row)[Field]->Type != EJson::Number || !FMath::IsFinite((*Row)[Field]->AsNumber()))
                {
                    OutError = FString::Printf(TEXT("plant %d field %d is not a number"), Index, Field);
                    return false;
                }
                Values[Field] = (*Row)[Field]->AsNumber();
            }
            const int32 Kind = static_cast<int32>(Values[3]);
            if (Values[3] != Kind || !OutSpecies.IsValidIndex(Kind) || !(Values[5] > 0.0) || !(Values[6] > 0.0))
            {
                OutError = FString::Printf(TEXT("plant %d has species %g, spread %g, grow %g"), Index, Values[3], Values[5], Values[6]);
                return false;
            }
            FPlant& Plant = OutPlants.AddDefaulted_GetRef();
            Plant.Location = StreetLights::FromPlan(Values[0], Values[1], Values[2]);
            Plant.Species = Kind;
            Plant.Yaw = static_cast<float>(FRotator::NormalizeAxis(Values[4]));
            Plant.Spread = static_cast<float>(Values[5]);
            Plant.Grow = static_cast<float>(Values[6]);
        }
        return true;
    }

    FString MeshPath(const FString& SpeciesId)
    {
        return FString::Printf(TEXT("/Game/Vegetation/Species/%s/SM_%s.SM_%s"), *SpeciesId, *SpeciesId, *SpeciesId);
    }

    float CullDistanceCm(const FSpecies& Species)
    {
        const double Metres = FMath::Clamp(ViewPerMetre * Species.NaturalHeightCm / 100.0, MinViewMetres, MaxViewMetres);
        return static_cast<float>(Metres * 100.0);
    }

    FTransform TrunkTransform(const FSpecies& Species, const FPlant& Plant)
    {
        const double Radius = FMath::Max<double>(MinTrunkRadiusCm, Species.TrunkRadiusCm * Plant.Spread);
        const double Height = FMath::Max<double>(MinTrunkHeightCm, Species.TrunkHeightCm * Plant.Grow);
        return FTransform(FQuat::Identity, Plant.Location + FVector(0.0, 0.0, Height * 0.5),
            FVector(2.0 * Radius / CylinderCm, 2.0 * Radius / CylinderCm, Height / CylinderCm));
    }
}
