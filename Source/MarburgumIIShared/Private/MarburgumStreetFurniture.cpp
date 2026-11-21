#include "MarburgumStreetFurniture.h"

#include "Dom/JsonObject.h"
#include "MarburgumStreetLights.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace MarburgumII::StreetFurniture
{
    bool Parse(const FString& Json, TArray<FPiece>& OutPieces, FString& OutError)
    {
        OutPieces.Reset();
        TSharedPtr<FJsonObject> Root;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
        {
            OutError = TEXT("not JSON");
            return false;
        }
        FString Format;
        if (!Root->TryGetStringField(TEXT("format"), Format) || Format != TEXT("marburgumii.street_furniture"))
        {
            OutError = TEXT("not a street furniture file (format marburgumii.street_furniture)");
            return false;
        }
        int32 Version = 0;
        if (!Root->TryGetNumberField(TEXT("version"), Version) || Version != 1)
        {
            OutError = FString::Printf(TEXT("street furniture version %d, this build reads 1"), Version);
            return false;
        }
        const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
        if (!Root->TryGetArrayField(TEXT("furniture"), Rows) || Rows == nullptr)
        {
            OutError = TEXT("no furniture");
            return false;
        }
        OutPieces.Reserve(Rows->Num());
        for (int32 Index = 0; Index < Rows->Num(); ++Index)
        {
            const TArray<TSharedPtr<FJsonValue>>* Row = nullptr;
            if (!(*Rows)[Index].IsValid() || !(*Rows)[Index]->TryGetArray(Row) || Row == nullptr || Row->Num() != 6)
            {
                OutError = FString::Printf(TEXT("piece %d is not [east, south, top, piece, yaw, source]"), Index);
                return false;
            }
            double Values[6];
            for (int32 Field = 0; Field < 6; ++Field)
            {
                if ((*Row)[Field]->Type != EJson::Number || !FMath::IsFinite((*Row)[Field]->AsNumber()))
                {
                    OutError = FString::Printf(TEXT("piece %d field %d is not a number"), Index, Field);
                    return false;
                }
                Values[Field] = (*Row)[Field]->AsNumber();
            }
            const int32 Piece = static_cast<int32>(Values[3]);
            const int32 Source = static_cast<int32>(Values[5]);
            if (Piece < 0 || Piece >= static_cast<int32>(EPiece::Count) || Source < 0 || Source >= static_cast<int32>(ESource::Count))
            {
                OutError = FString::Printf(TEXT("piece %d has kind %d, source %d"), Index, Piece, Source);
                return false;
            }
            FPiece& Out = OutPieces.AddDefaulted_GetRef();
            Out.Location = StreetLights::FromPlan(Values[0], Values[1], Values[2]);
            Out.Piece = static_cast<EPiece>(Piece);
            // The front is the mesh's +X, which an Unreal yaw turns as a plan yaw turns east: the same conversion as the
            // lamps' heads.
            Out.Yaw = StreetLights::YawFromPlan(Values[4]);
            Out.Source = static_cast<ESource>(Source);
        }
        return true;
    }

    FString MeshPath(EPiece Piece)
    {
        static const TCHAR* Names[] = { TEXT("StopPost"), TEXT("Shelter"), TEXT("Bench"), TEXT("FencePanel"), TEXT("WallPanel"), TEXT("Bollard") };
        static_assert(UE_ARRAY_COUNT(Names) == static_cast<int32>(EPiece::Count));
        const TCHAR* Name = Names[static_cast<int32>(Piece)];
        return FString::Printf(TEXT("/Game/World/StreetFurniture/SM_Street_%s_01.SM_Street_%s_01"), Name, Name);
    }

    float CullDistanceCm(EPiece Piece)
    {
        switch (Piece)
        {
        case EPiece::Shelter:
        case EPiece::FencePanel:
        case EPiece::WallPanel:
            return LargeCullCm;
        default:
            return SmallCullCm;
        }
    }
}
