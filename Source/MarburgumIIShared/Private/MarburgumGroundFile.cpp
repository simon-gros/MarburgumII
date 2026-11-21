#include "MarburgumGroundFile.h"

#include "Dom/JsonObject.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace MarburgumII::GroundFile
{
    namespace
    {
        constexpr const TCHAR* Format = TEXT("marburgumii.ground");
    }

    FString PathFor(const FString& PlacementFile)
    {
        return FPaths::Combine(FPaths::GetPath(PlacementFile), FPaths::GetBaseFilename(PlacementFile) + TEXT("_ground.json"));
    }

    FString Fingerprint(const FString& PlacementJson)
    {
        const FTCHARToUTF8 Utf8(*PlacementJson);
        FSHAHash Hash;
        FSHA1::HashBuffer(Utf8.Get(), Utf8.Length(), Hash.Hash);
        return Hash.ToString().ToLower();
    }

    FString Write(const FString& PlacementFingerprint, const TArray<TOptional<double>>& GroundCm)
    {
        TArray<TSharedPtr<FJsonValue>> Rows;
        Rows.Reserve(GroundCm.Num());
        for (const TOptional<double>& Z : GroundCm)
        {
            // A tenth of a millimetre is finer than any surface the trace stands a thing on.
            Rows.Add(Z.IsSet() ? TSharedPtr<FJsonValue>(MakeShared<FJsonValueNumber>(FMath::RoundToDouble(*Z * 10.0) / 10.0))
                               : TSharedPtr<FJsonValue>(MakeShared<FJsonValueNull>()));
        }
        const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetStringField(TEXT("format"), Format);
        Root->SetNumberField(TEXT("version"), 1);
        Root->SetStringField(TEXT("placement_sha1"), PlacementFingerprint);
        Root->SetArrayField(TEXT("ground_cm"), Rows);
        FString Out;
        FJsonSerializer::Serialize(Root, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out));
        return Out + TEXT("\n");
    }

    bool Parse(const FString& Json, const FString& PlacementFingerprint, int32 Count, TArray<TOptional<double>>& OutGroundCm, FString& OutError)
    {
        OutGroundCm.Reset();
        TSharedPtr<FJsonObject> Root;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
        {
            OutError = TEXT("not JSON");
            return false;
        }
        FString FileFormat;
        int32 Version = 0;
        if (!Root->TryGetStringField(TEXT("format"), FileFormat) || FileFormat != Format || !Root->TryGetNumberField(TEXT("version"), Version) || Version != 1)
        {
            OutError = FString::Printf(TEXT("not a ground file (format %s, version 1)"), Format);
            return false;
        }
        FString For;
        if (!Root->TryGetStringField(TEXT("placement_sha1"), For) || For != PlacementFingerprint)
        {
            OutError = FString::Printf(TEXT("made for placement file %s, not %s"), *For, *PlacementFingerprint);
            return false;
        }
        const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
        if (!Root->TryGetArrayField(TEXT("ground_cm"), Rows) || Rows == nullptr || Rows->Num() != Count)
        {
            OutError = FString::Printf(TEXT("%d heights for %d items"), Rows ? Rows->Num() : 0, Count);
            return false;
        }
        OutGroundCm.Reserve(Count);
        for (int32 Index = 0; Index < Rows->Num(); ++Index)
        {
            const TSharedPtr<FJsonValue>& Row = (*Rows)[Index];
            if (Row.IsValid() && Row->Type == EJson::Number && FMath::IsFinite(Row->AsNumber()))
            {
                OutGroundCm.Add(Row->AsNumber());
            }
            else if (Row.IsValid() && Row->Type == EJson::Null)
            {
                OutGroundCm.Add(TOptional<double>());
            }
            else
            {
                OutGroundCm.Reset();
                OutError = FString::Printf(TEXT("item %d's height is neither a number nor null"), Index);
                return false;
            }
        }
        return true;
    }
}
