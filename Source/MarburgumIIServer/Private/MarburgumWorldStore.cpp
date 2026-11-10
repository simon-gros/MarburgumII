#include "MarburgumWorldStore.h"

#include "Dom/JsonObject.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "MarburgumGameplayContract.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
    template <typename ValueType>
    TArray<FString> SortedKeys(const TMap<FString, ValueType>& Map)
    {
        TArray<FString> Keys;
        Map.GetKeys(Keys);
        Keys.Sort();
        return Keys;
    }
}

FString FMarburgumWorldStore::Serialize(const FMarburgumWorldRecord& Source)
{
    // Keys are inserted in sorted order so successive documents diff cleanly.
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("format"), MarburgumII::Gameplay::WorldStoreFormat);
    Root->SetNumberField(TEXT("schema_version"), MarburgumII::Gameplay::WorldStoreSchemaVersion);
    Root->SetNumberField(TEXT("revision"), static_cast<double>(Source.Revision));

    TSharedRef<FJsonObject> Stocks = MakeShared<FJsonObject>();
    for (const FString& Key : SortedKeys(Source.Stocks))
    {
        Stocks->SetNumberField(Key, Source.Stocks[Key]);
    }
    Root->SetObjectField(TEXT("stocks"), Stocks);

    TSharedRef<FJsonObject> Inventories = MakeShared<FJsonObject>();
    for (const FString& Character : SortedKeys(Source.Inventories))
    {
        TSharedRef<FJsonObject> Items = MakeShared<FJsonObject>();
        const TMap<FString, int32>& Owned = Source.Inventories[Character];
        for (const FString& Item : SortedKeys(Owned))
        {
            Items->SetNumberField(Item, Owned[Item]);
        }
        Inventories->SetObjectField(Character, Items);
    }
    Root->SetObjectField(TEXT("inventories"), Inventories);

    TSharedRef<FJsonObject> Receipts = MakeShared<FJsonObject>();
    for (const FString& Operation : SortedKeys(Source.Receipts))
    {
        const FMarburgumStoredReceipt& Receipt = Source.Receipts[Operation];
        TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("character"), Receipt.Character);
        Entry->SetStringField(TEXT("interactable"), Receipt.Interactable);
        Entry->SetStringField(TEXT("item"), Receipt.Item);
        Entry->SetNumberField(TEXT("quantity"), Receipt.Quantity);
        Entry->SetNumberField(TEXT("revision"), static_cast<double>(Receipt.Revision));
        Entry->SetStringField(TEXT("committed_utc"), Receipt.CommittedUtc);
        Receipts->SetObjectField(Operation, Entry);
    }
    Root->SetObjectField(TEXT("receipts"), Receipts);

    FString Text;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
    FJsonSerializer::Serialize(Root, Writer);
    Text += TEXT("\n");
    return Text;
}

bool FMarburgumWorldStore::Deserialize(const FString& Text, FMarburgumWorldRecord& Out, FString& OutError)
{
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
    {
        OutError = TEXT("not valid JSON");
        return false;
    }
    FString Format;
    int32 Schema = 0;
    if (!Root->TryGetStringField(TEXT("format"), Format) || Format != MarburgumII::Gameplay::WorldStoreFormat
        || !Root->TryGetNumberField(TEXT("schema_version"), Schema))
    {
        OutError = TEXT("not a marburgumii.world_store document");
        return false;
    }
    if (Schema != MarburgumII::Gameplay::WorldStoreSchemaVersion)
    {
        // No migrations exist yet; refusing is safer than reinterpreting an unknown schema.
        OutError = FString::Printf(TEXT("unsupported schema_version %d"), Schema);
        return false;
    }

    FMarburgumWorldRecord Parsed;
    double Revision = 0.0;
    Root->TryGetNumberField(TEXT("revision"), Revision);
    Parsed.Revision = static_cast<int64>(Revision);

    const TSharedPtr<FJsonObject>* Stocks = nullptr;
    if (Root->TryGetObjectField(TEXT("stocks"), Stocks))
    {
        for (const auto& Pair : (*Stocks)->Values)
        {
            Parsed.Stocks.Add(FString(*Pair.Key), static_cast<int32>(Pair.Value->AsNumber()));
        }
    }
    const TSharedPtr<FJsonObject>* Inventories = nullptr;
    if (Root->TryGetObjectField(TEXT("inventories"), Inventories))
    {
        for (const auto& Character : (*Inventories)->Values)
        {
            TMap<FString, int32>& Items = Parsed.Inventories.Add(FString(*Character.Key));
            for (const auto& Item : Character.Value->AsObject()->Values)
            {
                Items.Add(FString(*Item.Key), static_cast<int32>(Item.Value->AsNumber()));
            }
        }
    }
    const TSharedPtr<FJsonObject>* Receipts = nullptr;
    if (Root->TryGetObjectField(TEXT("receipts"), Receipts))
    {
        for (const auto& Pair : (*Receipts)->Values)
        {
            const TSharedPtr<FJsonObject> Entry = Pair.Value->AsObject();
            if (!Entry.IsValid())
            {
                continue;
            }
            FMarburgumStoredReceipt Receipt;
            Receipt.Character = Entry->GetStringField(TEXT("character"));
            Receipt.Interactable = Entry->GetStringField(TEXT("interactable"));
            Receipt.Item = Entry->GetStringField(TEXT("item"));
            Receipt.Quantity = static_cast<int32>(Entry->GetNumberField(TEXT("quantity")));
            Receipt.Revision = static_cast<int64>(Entry->GetNumberField(TEXT("revision")));
            Receipt.CommittedUtc = Entry->GetStringField(TEXT("committed_utc"));
            Parsed.Receipts.Add(FString(*Pair.Key), MoveTemp(Receipt));
        }
    }
    Out = MoveTemp(Parsed);
    return true;
}

bool FMarburgumWorldStore::ConsumeInjectedFault() const
{
    // <store>.fail holds how many upcoming commits must fail; each failure decrements it.
    const FString FaultPath = Path + TEXT(".fail");
    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *FaultPath))
    {
        return false;
    }
    const int32 Remaining = FCString::Atoi(*Text.TrimStartAndEnd());
    if (Remaining <= 0)
    {
        return false;
    }
    FFileHelper::SaveStringToFile(FString::FromInt(Remaining - 1), *FaultPath);
    return true;
}

bool FMarburgumWorldStore::Load(FString& OutError)
{
    IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
    if (!Files.FileExists(*Path))
    {
        Record = FMarburgumWorldRecord();
        return true;
    }
    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *Path))
    {
        OutError = TEXT("could not read the store file");
        return false;
    }
    return Deserialize(Text, Record, OutError);
}

bool FMarburgumWorldStore::Commit(const FMarburgumWorldRecord& Next, FString& OutError)
{
    IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
    if (bFaultInjection && ConsumeInjectedFault())
    {
        OutError = TEXT("injected write fault");
        return false;
    }
    const FString Directory = FPaths::GetPath(Path);
    if (!Directory.IsEmpty() && !Files.CreateDirectoryTree(*Directory))
    {
        OutError = TEXT("could not create the store directory");
        return false;
    }

    const FString TempPath = Path + TEXT(".tmp");
    const FTCHARToUTF8 Utf8(*Serialize(Next));
    {
        TUniquePtr<IFileHandle> Handle(Files.OpenWrite(*TempPath, false, false));
        if (!Handle)
        {
            OutError = TEXT("could not open the temporary store file");
            return false;
        }
        // Flush(true) is fsync on Linux: the new document is on disk before it replaces the old one.
        if (!Handle->Write(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length()) || !Handle->Flush(true))
        {
            Handle.Reset();
            Files.DeleteFile(*TempPath);
            OutError = TEXT("could not write the temporary store file");
            return false;
        }
    }
    // rename(2) replaces the previous document atomically on the same filesystem.
    if (!Files.MoveFile(*Path, *TempPath))
    {
        Files.DeleteFile(*TempPath);
        OutError = TEXT("could not replace the store file");
        return false;
    }
    Record = Next;
    return true;
}
