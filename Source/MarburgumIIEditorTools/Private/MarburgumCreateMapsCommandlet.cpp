#include "MarburgumCreateMapsCommandlet.h"

#include "AssetToolsModule.h"
#include "Components/BoxComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "FileHelpers.h"
#include "Components/SkyAtmosphereComponent.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "IAssetTools.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MarburgumArea.h"
#include "MarburgumFrontEndGameMode.h"
#include "MarburgumInteractable.h"
#include "MarburgumNpc.h"
#include "Misc/Parse.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumMaps, Log, All);

namespace MarburgumMaps
{
    // ---- shared helpers ---------------------------------------------------------------------------

    struct FMapBuildError
    {
        FString Message;
    };

    UWorld* NewMap()
    {
        UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
        if (!World)
        {
            throw FMapBuildError{ TEXT("Could not create a blank map") };
        }
        return World;
    }

    void Save(UWorld* World, const TCHAR* Path, int32 Pieces = -1)
    {
        if (!UEditorLoadingAndSavingUtils::SaveMap(World, Path))
        {
            throw FMapBuildError{ FString::Printf(TEXT("Could not save %s"), Path) };
        }
        if (Pieces >= 0)
        {
            UE_LOG(LogMarburgumMaps, Display, TEXT("MB2MAP saved %s blocks=%d"), Path, Pieces);
        }
        else
        {
            UE_LOG(LogMarburgumMaps, Display, TEXT("MB2MAP saved %s"), Path);
        }
    }

    template <typename T>
    T* Spawn(UWorld* World, const FVector& Location, const FRotator& Rotation = FRotator::ZeroRotator,
        const TCHAR* Label = nullptr, const TCHAR* Folder = nullptr, UClass* Class = T::StaticClass())
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        T* Actor = World->SpawnActor<T>(Class, Location, Rotation, Params);
        if (!Actor)
        {
            throw FMapBuildError{ FString::Printf(TEXT("Could not spawn %s"), Label ? Label : *Class->GetName()) };
        }
        if (Label)
        {
            Actor->SetActorLabel(Label);
        }
        if (Folder)
        {
            Actor->SetFolderPath(FName(Folder));
        }
        return Actor;
    }

    /** Movable lights need no baked lighting, so packaged builds show no "lighting needs to be rebuilt". */
    void Lighting(UWorld* World, const FVector& At, const FRotator& SunRotation, bool bFog)
    {
        ADirectionalLight* Sun = Spawn<ADirectionalLight>(World, At, SunRotation, TEXT("Sun"));
        Sun->GetRootComponent()->SetMobility(EComponentMobility::Movable);
        ASkyLight* Sky = Spawn<ASkyLight>(World, At, FRotator::ZeroRotator, TEXT("SkyLight"));
        Sky->GetRootComponent()->SetMobility(EComponentMobility::Movable);
        Sky->GetLightComponent()->bRealTimeCapture = true;
        Spawn<ASkyAtmosphere>(World, FVector::ZeroVector, FRotator::ZeroRotator, TEXT("SkyAtmosphere"));
        if (bFog)
        {
            AExponentialHeightFog* Fog = Spawn<AExponentialHeightFog>(World, FVector::ZeroVector, FRotator::ZeroRotator, TEXT("HeightFog"));
            Fog->GetComponent()->SetFogDensity(0.01f);
        }
    }

    UStaticMesh* Shape(const TCHAR* Name)
    {
        UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Name, Name));
        if (!Mesh)
        {
            throw FMapBuildError{ FString::Printf(TEXT("Missing engine shape %s"), Name) };
        }
        return Mesh;
    }

    // ---- /Game/Maps/FrontEnd ------------------------------------------------------------------------

    /**
     * Empty on purpose: the connection shell covers the whole frame, and its world-settings game mode
     * (AMarburgumFrontEndGameMode) owns no gameplay, so a client that is not connected to an
     * authoritative server never simulates game rules locally.
     */
    void FrontEnd()
    {
        UWorld* World = NewMap();
        World->GetWorldSettings()->DefaultGameMode = AMarburgumFrontEndGameMode::StaticClass();
        Save(World, TEXT("/Game/Maps/FrontEnd"));
    }

    // ---- /Game/Maps/NetSmoke ------------------------------------------------------------------------

    /**
     * Network and Gate 1 interaction test map. No Marburgum world content: a floor, player starts,
     * basic lighting and two labeled development fixtures granting the original catalogue's
     * `fishing_rod` from a limited server-owned stock (Data/Gameplay/gameplay.v0.1.json):
     *
     *   dev_supply_crate      3 units, within reach of every player start
     *   dev_supply_crate_far  1 unit, 25 m away, used to prove the server's range check
     */
    void Crate(UWorld* World, const TCHAR* Label, const TCHAR* InteractableId, const TCHAR* DisplayName, int32 Stock, const FVector& Location)
    {
        AMarburgumInteractable* Actor = Spawn<AMarburgumInteractable>(World, Location, FRotator::ZeroRotator, Label);
        Actor->InteractableId = FName(InteractableId);
        Actor->GrantedItemId = FName(TEXT("fishing_rod"));
        Actor->DisplayName = FText::FromString(DisplayName);
        Actor->Verb = FText::FromString(TEXT("Take"));
        Actor->InitialStock = Stock;
        Actor->InteractionRange = 300.f;
        Actor->bTestFixture = true;
        Actor->SetActorScale3D(FVector(0.8));
    }

    void NetSmoke()
    {
        const FVector CratePosition(220.0, 0.0, 40.0);
        constexpr double StartRadius = 220.0;
        constexpr double StartAngles[] = { 180.0, 150.0, 210.0, 120.0 }; // around the crate; every start faces it

        UWorld* World = NewMap();
        AStaticMeshActor* Floor = Spawn<AStaticMeshActor>(World, FVector::ZeroVector, FRotator::ZeroRotator, TEXT("Floor"));
        Floor->GetStaticMeshComponent()->SetStaticMesh(Shape(TEXT("Plane")));
        Floor->SetActorScale3D(FVector(100.0, 100.0, 1.0));

        for (int32 Index = 0; Index < UE_ARRAY_COUNT(StartAngles); ++Index)
        {
            const double Radians = FMath::DegreesToRadians(StartAngles[Index]);
            const double X = CratePosition.X + StartRadius * FMath::Cos(Radians);
            const double Y = CratePosition.Y + StartRadius * FMath::Sin(Radians);
            const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(CratePosition.Y - Y, CratePosition.X - X));
            Spawn<APlayerStart>(World, FVector(X, Y, 120.0), FRotator(0.0, Yaw, 0.0), *FString::Printf(TEXT("PlayerStart%d"), Index));
        }

        Crate(World, TEXT("SupplyCrate"), TEXT("dev_supply_crate"), TEXT("Test supply crate"), 3, CratePosition);
        Crate(World, TEXT("SupplyCrateFar"), TEXT("dev_supply_crate_far"), TEXT("Test supply crate (far)"), 1, FVector(2500.0, 0.0, 40.0));

        Lighting(World, FVector(0.0, 0.0, 1000.0), FRotator(-45.0, 30.0, 0.0), false);
        Save(World, TEXT("/Game/Maps/NetSmoke"));
    }

    // ---- /Game/Maps/Lent ----------------------------------------------------------------------------
    //
    // The deliberate fallback blockout of the Lent riverside (ADR-016): NOT the licensed Maribor model
    // and not the original game's generated geometry, but independently authored massing of engine
    // shapes so a public checkout has a playable district.
    //
    // Layout follows the original Marburgum coordinates (project/data, metres: x east and z south of the
    // middle of Stari most, y up), mapped to Unreal centimetres as X = x, Y = z, Z = y. Carried forward:
    //
    //   areas.json                `lent` box, centre (0, 5, -78), size (420, 12, 54); outside is "Maribor"
    //   interactions.json         `lent_angler` at (-127, -58) with its range (4 m) and dialogue line
    //   npc_visual_profiles.json  the angler's parka colours
    //
    // Everything else (quay line, railing, bridge deck, building row, lamps, trees) is placeholder
    // massing, not a survey of the real Lent. Player starts carry PlayerStartTag values the game mode
    // selects with ?MB2Start=: `arrival` (default, on the promenade 30 m east of the angler), `angler`
    // (within reach of him) and `bridge` (on Stari most, outside the Lent area).

    constexpr double QuayZ = -54.0;            // the river edge (negative = north bank)
    constexpr double StreetHalfWidth = 7.0;    // a street leads north from where Stari most lands
    constexpr const TCHAR* MaterialDir = TEXT("/Game/Blockout/Materials");

    struct FSwatch
    {
        const TCHAR* Name;
        const TCHAR* Hex; // sRGB
    };

    constexpr FSwatch Palette[] = {
        { TEXT("Paving"), TEXT("8a8178") }, { TEXT("Quay"), TEXT("6f675e") }, { TEXT("Water"), TEXT("2c4a55") },
        { TEXT("Riverbed"), TEXT("3a3a34") }, { TEXT("Bridge"), TEXT("9a9186") }, { TEXT("Railing"), TEXT("2f3133") },
        { TEXT("Lamp"), TEXT("26292b") }, { TEXT("LampGlass"), TEXT("e8dcb8") }, { TEXT("Trunk"), TEXT("4a3b2c") },
        { TEXT("Canopy"), TEXT("4f6b3a") }, { TEXT("Plinth"), TEXT("5d564e") }, { TEXT("Cornice"), TEXT("4a4440") },
        { TEXT("Ochre"), TEXT("c49a5a") }, { TEXT("Cream"), TEXT("d8cdb2") }, { TEXT("Grey"), TEXT("a39d94") },
        { TEXT("Rose"), TEXT("b98a7a") }, { TEXT("Sage"), TEXT("9fa58a") }, { TEXT("Umber"), TEXT("8f6f52") },
    };

    struct FBuilding
    {
        double Width;
        double Height;
        const TCHAR* Colour;
    };

    // Building row behind the promenade, west to east; a 6 m street after every third.
    constexpr FBuilding Buildings[] = {
        { 34, 14, TEXT("Ochre") }, { 26, 11, TEXT("Cream") }, { 30, 16, TEXT("Grey") },
        { 22, 12, TEXT("Rose") }, { 36, 15, TEXT("Cream") }, { 24, 10, TEXT("Umber") },
        { 28, 13, TEXT("Sage") }, { 32, 17, TEXT("Ochre") }, { 20, 11, TEXT("Grey") },
        { 30, 12, TEXT("Cream") }, { 26, 14, TEXT("Rose") }, { 34, 13, TEXT("Umber") },
        { 24, 15, TEXT("Grey") }, { 28, 11, TEXT("Ochre") },
    };

    FLinearColor Linear(const TCHAR* Hex)
    {
        return FLinearColor::FromSRGBColor(FColor::FromHex(Hex));
    }

    /** Original metres (x east, y up, z south) to Unreal centimetres. */
    FVector Ue(double X, double Y, double Z)
    {
        return FVector(X * 100.0, Z * 100.0, Y * 100.0);
    }

    TMap<FString, UMaterialInstanceConstant*> Materials()
    {
        UMaterial* Basic = LoadObject<UMaterial>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
        if (!Basic)
        {
            throw FMapBuildError{ TEXT("Missing /Engine/BasicShapes/BasicShapeMaterial") };
        }
        TArray<FMaterialParameterInfo> Parameters;
        TArray<FGuid> Ids;
        Basic->GetAllVectorParameterInfo(Parameters, Ids);
        if (!Parameters.ContainsByPredicate([](const FMaterialParameterInfo& Info) { return Info.Name == TEXT("Color"); }))
        {
            throw FMapBuildError{ TEXT("BasicShapeMaterial has no Color parameter") };
        }

        IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
        TMap<FString, UMaterialInstanceConstant*> Made;
        TArray<UPackage*> Packages;
        for (const FSwatch& Swatch : Palette)
        {
            const FString AssetName = FString::Printf(TEXT("MI_Blockout_%s"), Swatch.Name);
            const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), MaterialDir, *AssetName, *AssetName);
            UMaterialInstanceConstant* Instance = LoadObject<UMaterialInstanceConstant>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
            if (!Instance)
            {
                UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
                Factory->InitialParent = Basic;
                Instance = Cast<UMaterialInstanceConstant>(Tools.CreateAsset(AssetName, MaterialDir, UMaterialInstanceConstant::StaticClass(), Factory));
            }
            if (!Instance)
            {
                throw FMapBuildError{ FString::Printf(TEXT("Could not create %s"), *ObjectPath) };
            }
            Instance->SetParentEditorOnly(Basic);
            Instance->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Color")), Linear(Swatch.Hex));
            Instance->PostEditChange();
            Instance->MarkPackageDirty();
            Packages.Add(Instance->GetPackage());
            Made.Add(Swatch.Name, Instance);
        }
        if (!UEditorLoadingAndSavingUtils::SavePackages(Packages, false))
        {
            throw FMapBuildError{ TEXT("Could not save the blockout materials") };
        }
        return Made;
    }

    class FBlockout
    {
    public:
        FBlockout(UWorld* InWorld) : World(InWorld), Mats(Materials())
        {
            Meshes.Add(TEXT("cube"), Shape(TEXT("Cube")));
            Meshes.Add(TEXT("cylinder"), Shape(TEXT("Cylinder")));
            Meshes.Add(TEXT("sphere"), Shape(TEXT("Sphere")));
        }

        /** A shape centred at Centre (original metres) with full Size (x, y, z metres). */
        AStaticMeshActor* Block(const FString& Label, const FVector& Centre, const FVector& Size, const TCHAR* Colour,
            const TCHAR* Mesh = TEXT("cube"), bool bCollide = true, const TCHAR* Folder = TEXT("Blockout"))
        {
            AStaticMeshActor* Actor = Spawn<AStaticMeshActor>(World, Ue(Centre.X, Centre.Y, Centre.Z), FRotator::ZeroRotator, *Label, Folder);
            UStaticMeshComponent* Component = Actor->GetStaticMeshComponent();
            Component->SetStaticMesh(Meshes.FindChecked(Mesh));
            Component->SetMaterial(0, Mats.FindChecked(Colour));
            // Basic shapes are 1 m across; Unreal scale order is (X = x, Y = z, Z = y).
            Actor->SetActorScale3D(FVector(Size.X, Size.Z, Size.Y));
            if (!bCollide)
            {
                Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            }
            ++Count;
            return Actor;
        }

        void Start(const TCHAR* Tag, double X, double Z, double FacingX, double FacingZ, int32 Index)
        {
            const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(FacingZ - Z, FacingX - X));
            APlayerStart* Actor = Spawn<APlayerStart>(World, Ue(X, 1.1, Z), FRotator(0.0, Yaw, 0.0),
                *FString::Printf(TEXT("Start_%s_%d"), Tag, Index), TEXT("Starts"));
            Actor->PlayerStartTag = FName(Tag);
        }

        UWorld* World;
        TMap<FString, UMaterialInstanceConstant*> Mats;
        TMap<FString, UStaticMesh*> Meshes;
        int32 Count = 0;
    };

    void Lent()
    {
        UWorld* World = NewMap();
        FBlockout B(World);

        // Ground: north bank (Lent), river, south bank. Walking surfaces are at y = 0.
        B.Block(TEXT("LentPromenade"), FVector(0.0, -1.5, (QuayZ - 130.0) / 2.0), FVector(440.0, 3.0, 130.0 + QuayZ), TEXT("Paving"));
        B.Block(TEXT("SouthBank"), FVector(0.0, -1.5, 92.5), FVector(440.0, 3.0, 35.0), TEXT("Paving"));
        B.Block(TEXT("Riverbed"), FVector(0.0, -5.5, 10.5), FVector(520.0, 1.0, 129.0), TEXT("Riverbed"));
        B.Block(TEXT("Drava"), FVector(0.0, -1.6, 10.5), FVector(520.0, 0.1, 129.0), TEXT("Water"), TEXT("cube"), false);
        B.Block(TEXT("QuayFaceNorth"), FVector(0.0, -3.0, QuayZ + 0.25), FVector(440.0, 6.0, 0.5), TEXT("Quay"));
        B.Block(TEXT("QuayFaceSouth"), FVector(0.0, -3.0, 74.75), FVector(440.0, 6.0, 0.5), TEXT("Quay"));

        // Stari most: a flush deck with railings, so the bridge is outside the Lent area box.
        B.Block(TEXT("StariMostDeck"), FVector(0.0, -0.3, 10.5), FVector(14.0, 0.6, 129.0), TEXT("Bridge"));
        for (const int32 Side : { -1, 1 })
        {
            B.Block(FString::Printf(TEXT("StariMostRailing%+d"), Side), FVector(Side * 6.85, 0.55, 10.5), FVector(0.3, 1.1, 129.0), TEXT("Railing"));
        }
        for (const double PierZ : { -10.0, 30.0 })
        {
            B.Block(FString::Printf(TEXT("StariMostPier%d"), static_cast<int32>(PierZ)), FVector(0.0, -3.4, PierZ), FVector(12.0, 5.0, 4.0), TEXT("Bridge"));
        }

        // Quay railing along the promenade edge, open where the bridge lands; 1.1 m stops a jump over it.
        for (const int32 Side : { -1, 1 })
        {
            B.Block(FString::Printf(TEXT("QuayRailing%+d"), Side), FVector(Side * 111.0, 0.55, QuayZ - 0.15), FVector(208.0, 1.1, 0.3), TEXT("Railing"));
        }

        // Lamps along the quay and plane trees between them.
        for (int32 X = -195; X <= 195; X += 30)
        {
            if (FMath::Abs(X) < 12)
            {
                continue;
            }
            B.Block(FString::Printf(TEXT("LampPost%d"), X), FVector(X, 2.1, QuayZ - 1.5), FVector(0.16, 4.2, 0.16), TEXT("Lamp"), TEXT("cylinder"), true, TEXT("Blockout/Lamps"));
            B.Block(FString::Printf(TEXT("LampGlass%d"), X), FVector(X, 4.35, QuayZ - 1.5), FVector(0.45, 0.45, 0.45), TEXT("LampGlass"), TEXT("sphere"), false, TEXT("Blockout/Lamps"));
        }
        for (const int32 X : { -180, -150, -90, -60, 45, 75, 135, 165 })
        {
            B.Block(FString::Printf(TEXT("TreeTrunk%d"), X), FVector(X, 1.75, -71.0), FVector(0.45, 3.5, 0.45), TEXT("Trunk"), TEXT("cylinder"), true, TEXT("Blockout/Trees"));
            B.Block(FString::Printf(TEXT("TreeCanopy%d"), X), FVector(X, 5.5, -71.0), FVector(6.0, 4.5, 6.0), TEXT("Canopy"), TEXT("sphere"), false, TEXT("Blockout/Trees"));
        }

        // Building row: facades on the line z = -78, 22 m deep, with a darker plinth and a cornice.
        double X = -212.0;
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Buildings); ++Index)
        {
            const FBuilding& Building = Buildings[Index];
            if (X < StreetHalfWidth && X + Building.Width > -StreetHalfWidth)
            {
                X = StreetHalfWidth; // keep the street that leads off the bridge open
            }
            const double CentreX = X + Building.Width / 2.0;
            const TCHAR* Folder = TEXT("Blockout/Buildings");
            B.Block(FString::Printf(TEXT("Building%02d"), Index), FVector(CentreX, Building.Height / 2.0, -89.0), FVector(Building.Width, Building.Height, 22.0), Building.Colour, TEXT("cube"), true, Folder);
            B.Block(FString::Printf(TEXT("Plinth%02d"), Index), FVector(CentreX, 1.75, -78.1), FVector(Building.Width, 3.5, 0.4), TEXT("Plinth"), TEXT("cube"), true, Folder);
            B.Block(FString::Printf(TEXT("Cornice%02d"), Index), FVector(CentreX, Building.Height + 0.3, -88.8), FVector(Building.Width + 0.6, 0.6, 22.8), TEXT("Cornice"), TEXT("cube"), true, Folder);
            X += Building.Width + (Index % 3 == 2 ? 6.0 : 0.0);
        }

        // The named area from areas.json; the server derives each player's location from it.
        AMarburgumArea* Area = Spawn<AMarburgumArea>(World, Ue(0.0, 5.0, -78.0), FRotator::ZeroRotator, TEXT("Area_lent"), TEXT("Areas"));
        Area->AreaId = FName(TEXT("lent"));
        Area->Note = FText::FromString(TEXT("Maribor  ·  1990s  ·  fallback blockout, not the licensed city model"));
        Area->FindComponentByClass<UBoxComponent>()->SetBoxExtent(FVector(420.0 * 50.0, 54.0 * 50.0, 12.0 * 50.0));

        // The riverside angler (interactions.json `lent_angler`), facing the Drava.
        AMarburgumNpc* Angler = Spawn<AMarburgumNpc>(World, Ue(-127.0, 0.0, -58.0), FRotator(0.0, 90.0, 0.0), TEXT("Npc_lent_angler"), TEXT("Npcs"));
        Angler->InteractableId = FName(TEXT("lent_angler"));
        Angler->DisplayName = FText::FromString(TEXT("Riverside angler"));
        Angler->Verb = FText::FromString(TEXT("Talk"));
        Angler->InteractionRange = 400.0;
        Angler->bTestFixture = false;
        Angler->Dialogue = FText::FromString(TEXT("The Drava looks calm from here, but the current does the real work. Keep your first casts close to the bank."));
        Angler->PrimaryColor = Linear(TEXT("596455"));
        Angler->SecondaryColor = Linear(TEXT("353a34"));
        Angler->bHoldsRod = true;
        Angler->RerunConstructionScripts();

        const double Arrival[][2] = { { -96.0, -62.0 }, { -96.0, -65.0 }, { -93.0, -62.0 }, { -93.0, -65.0 } };
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Arrival); ++Index)
        {
            B.Start(TEXT("arrival"), Arrival[Index][0], Arrival[Index][1], Arrival[Index][0] - 10.0, Arrival[Index][1], Index);
        }
        const double AtAngler[][2] = { { -124.0, -60.0 }, { -123.5, -57.5 } };
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(AtAngler); ++Index)
        {
            B.Start(TEXT("angler"), AtAngler[Index][0], AtAngler[Index][1], -127.0, -58.0, Index);
        }
        // On the bridge, looking up the promenade towards the angler.
        const double OnBridge[] = { -2.0, 2.0 };
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(OnBridge); ++Index)
        {
            B.Start(TEXT("bridge"), OnBridge[Index], -40.0, -60.0, -66.0, Index);
        }

        // Late-afternoon sun from the south-west, so the riverside facades are lit.
        Lighting(World, Ue(0.0, 30.0, 0.0), FRotator(-32.0, -60.0, 0.0), true);
        Save(World, TEXT("/Game/Maps/Lent"), B.Count);
    }
}

UMarburgumCreateMapsCommandlet::UMarburgumCreateMapsCommandlet()
{
    IsClient = false;
    IsServer = false;
    IsEditor = true;
    LogToConsole = true;
}

int32 UMarburgumCreateMapsCommandlet::Main(const FString& Params)
{
    FString Selection = TEXT("frontend,netsmoke,lent");
    FParse::Value(*Params, TEXT("Maps="), Selection, /*bShouldStopOnSeparator=*/false);
    TArray<FString> Requested;
    Selection.ToLower().ParseIntoArray(Requested, TEXT(","));

    const TMap<FString, void (*)()> Builders = {
        { TEXT("frontend"), &MarburgumMaps::FrontEnd },
        { TEXT("netsmoke"), &MarburgumMaps::NetSmoke },
        { TEXT("lent"), &MarburgumMaps::Lent },
    };
    int32 Failures = 0;
    for (const FString& Name : Requested)
    {
        void (*const* Builder)() = Builders.Find(Name);
        if (!Builder)
        {
            UE_LOG(LogMarburgumMaps, Error, TEXT("MB2MAP unknown map '%s' (use frontend, netsmoke, lent)"), *Name);
            ++Failures;
            continue;
        }
        try
        {
            (*Builder)();
        }
        catch (const MarburgumMaps::FMapBuildError& Error)
        {
            UE_LOG(LogMarburgumMaps, Error, TEXT("MB2MAP failed %s: %s"), *Name, *Error.Message);
            ++Failures;
        }
    }
    return Failures == 0 ? 0 : 1;
}
