#include "MarburgumCreateMapsCommandlet.h"

#include "AssetToolsModule.h"
#include "Components/BoxComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PointLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextRenderActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/LightComponent.h"
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
#include "MarburgumGameMode.h"
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
    void Lighting(UWorld* World, const FVector& At, const FRotator& SunRotation, bool bFog, float FogDensity = 0.01f)
    {
        ADirectionalLight* Sun = Spawn<ADirectionalLight>(World, At, SunRotation, TEXT("Sun"));
        Sun->GetRootComponent()->SetMobility(EComponentMobility::Movable);
        // A directional light spawned through SpawnActor kept the yaw but not the pitch (-78 instead of
        // -32 on the Lent map); set the rotation again once the actor exists.
        Sun->SetActorRotation(SunRotation);
        ASkyLight* Sky = Spawn<ASkyLight>(World, At, FRotator::ZeroRotator, TEXT("SkyLight"));
        Sky->GetRootComponent()->SetMobility(EComponentMobility::Movable);
        Sky->GetLightComponent()->bRealTimeCapture = true;
        Spawn<ASkyAtmosphere>(World, FVector::ZeroVector, FRotator::ZeroRotator, TEXT("SkyAtmosphere"));
        if (bFog)
        {
            AExponentialHeightFog* Fog = Spawn<AExponentialHeightFog>(World, FVector::ZeroVector, FRotator::ZeroRotator, TEXT("HeightFog"));
            Fog->GetComponent()->SetFogDensity(FogDensity);
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
    // The authored Lent district (ADR-016, detailed by ADR-024): NOT the licensed Maribor model,
    // not a survey of the real Lent, and not TRELLIS-generated city geometry. The TRELLIS map
    // roadmap keeps image-to-3D off the terrain, the river and ordinary building shells. Those
    // shells are modular engine shapes with invented shop names. Gameplay anchors are unchanged.
    //
    // Layout follows the original Marburgum coordinates (project/data, metres: x east and z south of the
    // middle of Stari most, y up), mapped to Unreal centimetres as X = x, Y = z, Z = y. Carried forward:
    //
    //   areas.json                `lent` box, centre (0, 5, -78), size (420, 12, 54); outside is "Maribor"
    //   interactions.json         `lent_angler` at (-127, -58) with its range (4 m) and dialogue line
    //   npc_visual_profiles.json  the angler's parka colours
    //
    // The promenade, quay, bridge, jetty, building shells, lamps and trees are authored massing,
    // not a survey of the real Lent. Player starts carry PlayerStartTag values the game mode
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
        { TEXT("Glass"), TEXT("1a242c") }, { TEXT("Door"), TEXT("3a2a22") }, { TEXT("Roof"), TEXT("6e3b34") },
        { TEXT("Ridge"), TEXT("4a302c") }, { TEXT("Shop"), TEXT("6d5344") }, { TEXT("Awning"), TEXT("8c3a3a") },
        { TEXT("Sign"), TEXT("e6d7b0") }, { TEXT("Cobble"), TEXT("6e6860") }, { TEXT("Bench"), TEXT("5c4632") },
        { TEXT("Metal"), TEXT("3c4044") }, { TEXT("Shrub"), TEXT("3d5a34") }, { TEXT("Far"), TEXT("8a847c") },
        { TEXT("Spire"), TEXT("8d8f86") }, { TEXT("Foam"), TEXT("8e9ea4") },
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

    // Invented 1990s names. nullptr is a house with no shop. ASCII only: the engine debug font
    // is not guaranteed to draw Slovenian diacritics. These are not real Lent businesses.
    constexpr const TCHAR* Shops[] = {
        TEXT("Kavarna Ob Toku"), TEXT("Foto 1994"), nullptr,
        TEXT("Trgovina Pri Mostu"), TEXT("Ribarnica"), nullptr,
        TEXT("Gostilna Pri Vodi"), TEXT("Krojac Lent"), nullptr,
        TEXT("Pekarna Na Bregu"), TEXT("Cvetlicarna"), nullptr,
        TEXT("Knjigarna Obrezje"), TEXT("Delavska menza"),
    };
    static_assert(UE_ARRAY_COUNT(Shops) == UE_ARRAY_COUNT(Buildings), "one shop slot per promenade building");

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
            Meshes.Add(TEXT("cone"), Shape(TEXT("Cone")));
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

        /** Shop lettering faces the river. FacingZ > 0 looks south (+Y in Unreal). */
        void Sign(const FString& Label, const FString& Text, double X, double Y, double Z, double FacingZ)
        {
            const FRotator Rotation(0.0, FacingZ > 0.0 ? 90.0 : -90.0, 0.0);
            ATextRenderActor* Actor = Spawn<ATextRenderActor>(World, Ue(X, Y, Z), Rotation, *Label, TEXT("Blockout/Signs"));
            UTextRenderComponent* Render = Actor->GetTextRender();
            Render->SetText(FText::FromString(Text));
            Render->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
            Render->SetVerticalAlignment(EVRTA_TextCenter);
            Render->SetWorldSize(36.f);
            Render->SetTextRenderColor(FColor(42, 28, 18));
            ++Count;
        }

        void LampGlow(const FString& Label, double X, double Y, double Z)
        {
            APointLight* Lamp = Spawn<APointLight>(World, Ue(X, Y, Z), FRotator::ZeroRotator, *Label, TEXT("Blockout/Lamps"));
            Lamp->GetRootComponent()->SetMobility(EComponentMobility::Movable);
            UPointLightComponent* Light = CastChecked<UPointLightComponent>(Lamp->GetLightComponent());
            Light->SetMobility(EComponentMobility::Movable);
            Light->SetIntensity(2200.f);
            Light->SetAttenuationRadius(650.f);
            Light->SetLightColor(Linear(TEXT("e8dcb8")));
            Light->SetCastShadows(false);
            ++Count;
        }

        /**
         * One riverside shell. The mass is the collision. Windows, doors and signs are proud of the
         * river face and do not collide, so a player walking the promenade cannot snag on them.
         * ShopName nullptr is a plain house. Depth runs along z; FacingZ +1 dresses the south face.
         */
        void House(const TCHAR* Prefix, int32 Index, double CentreX, double Width, double Height, const TCHAR* Colour, const TCHAR* ShopName,
            double CentreZ, double Depth, double FacingZ, const TCHAR* Folder)
        {
            const double FrontZ = CentreZ + FacingZ * Depth * 0.5;
            const auto Out = [FacingZ](double Metres) { return FacingZ * Metres; };
            const FString Id = FString::Printf(TEXT("%s%02d"), Prefix, Index);
            Block(FString::Printf(TEXT("Building%s"), *Id), FVector(CentreX, Height / 2.0, CentreZ), FVector(Width, Height, Depth), Colour, TEXT("cube"), true, Folder);
            Block(FString::Printf(TEXT("Plinth%s"), *Id), FVector(CentreX, 0.55, FrontZ + Out(0.12)), FVector(Width + 0.15, 1.1, 0.28), TEXT("Plinth"), TEXT("cube"), true, Folder);
            Block(FString::Printf(TEXT("Cornice%s"), *Id), FVector(CentreX, Height + 0.15, CentreZ), FVector(Width + 0.7, 0.4, Depth + 0.7), TEXT("Cornice"), TEXT("cube"), true, Folder);
            Block(FString::Printf(TEXT("Roof%s"), *Id), FVector(CentreX, Height + 0.55, CentreZ), FVector(Width + 0.5, 0.7, Depth + 0.5), TEXT("Roof"), TEXT("cube"), true, Folder);
            Block(FString::Printf(TEXT("Ridge%s"), *Id), FVector(CentreX, Height + 1.15, CentreZ), FVector(Width * 0.92, 0.55, 1.05), TEXT("Ridge"), TEXT("cube"), true, Folder);
            for (const int32 Chimney : { 0, 1 })
            {
                const double ChimneyX = CentreX + (Chimney == 0 ? -0.28 : 0.28) * Width;
                Block(FString::Printf(TEXT("Chimney%s_%d"), *Id, Chimney), FVector(ChimneyX, Height + 1.7, CentreZ), FVector(0.7, 1.5, 0.7), TEXT("Plinth"), TEXT("cube"), true, Folder);
            }

            const double FloorStart = ShopName ? 5.0 : 3.4;
            for (double FloorY = FloorStart; FloorY + 1.4 < Height - 0.5; FloorY += 3.0)
            {
                Block(FString::Printf(TEXT("Windows%s_%.0f"), *Id, FloorY), FVector(CentreX, FloorY, FrontZ + Out(0.16)),
                    FVector(Width * 0.78, 1.35, 0.1), TEXT("Glass"), TEXT("cube"), false, Folder);
            }
            const int32 Bays = FMath::Clamp(static_cast<int32>(Width / 4.2), 2, 7);
            for (int32 Bay = 0; Bay < Bays; ++Bay)
            {
                const double MullionX = CentreX - Width * 0.36 + Bay * (Width * 0.72 / static_cast<double>(Bays - 1));
                Block(FString::Printf(TEXT("Mullion%s_%d"), *Id, Bay), FVector(MullionX, (FloorStart + Height) / 2.0, FrontZ + Out(0.2)),
                    FVector(0.16, FMath::Max(1.0, Height - FloorStart - 0.4), 0.08), Colour, TEXT("cube"), false, Folder);
            }

            const double DoorX = CentreX + (Index % 2 == 0 ? -0.18 : 0.18) * Width;
            Block(FString::Printf(TEXT("Door%s"), *Id), FVector(DoorX, 1.25, FrontZ + Out(0.18)), FVector(1.25, 2.5, 0.1), TEXT("Door"), TEXT("cube"), false, Folder);
            if (ShopName)
            {
                Block(FString::Printf(TEXT("Shop%s"), *Id), FVector(CentreX, 1.7, FrontZ + Out(0.14)), FVector(Width * 0.9, 3.0, 0.12), TEXT("Shop"), TEXT("cube"), false, Folder);
                Block(FString::Printf(TEXT("Awning%s"), *Id), FVector(CentreX, 3.25, FrontZ + Out(0.7)), FVector(FMath::Min(Width * 0.62, 9.0), 0.12, 1.15), TEXT("Awning"), TEXT("cube"), false, Folder);
                const double SignWidth = FMath::Clamp(static_cast<double>(FCString::Strlen(ShopName)) * 0.42, 3.2, Width * 0.8);
                Block(FString::Printf(TEXT("SignBoard%s"), *Id), FVector(CentreX, 3.85, FrontZ + Out(0.22)), FVector(SignWidth, 0.7, 0.08), TEXT("Sign"), TEXT("cube"), false, Folder);
                Sign(FString::Printf(TEXT("Sign%s"), *Id), ShopName, CentreX, 3.85, FrontZ + Out(0.32), FacingZ);
            }
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
        // The north quay wall has a gap at the fishing jetty (x -164 to -146); the jetty covers it.
        B.Block(TEXT("LentPromenade"), FVector(0.0, -1.5, (QuayZ - 130.0) / 2.0), FVector(540.0, 3.0, 130.0 + QuayZ), TEXT("Paving"));
        B.Block(TEXT("SouthBank"), FVector(0.0, -1.5, 92.5), FVector(540.0, 3.0, 35.0), TEXT("Paving"));
        B.Block(TEXT("Riverbed"), FVector(0.0, -5.5, 10.5), FVector(520.0, 1.0, 129.0), TEXT("Riverbed"));
        B.Block(TEXT("Drava"), FVector(0.0, -1.55, 10.5), FVector(520.0, 0.15, 129.0), TEXT("Water"), TEXT("cube"), false);
        B.Block(TEXT("DravaEdge"), FVector(0.0, -1.35, -46.0), FVector(520.0, 0.06, 8.0), TEXT("Foam"), TEXT("cube"), false);
        B.Block(TEXT("QuayFaceWest"), FVector(-196.0, -3.0, QuayZ + 0.25), FVector(48.0, 6.0, 0.5), TEXT("Quay"));
        B.Block(TEXT("QuayFaceEast"), FVector(66.0, -3.0, QuayZ + 0.25), FVector(408.0, 6.0, 0.5), TEXT("Quay"));
        B.Block(TEXT("QuayFaceSouth"), FVector(0.0, -3.0, 74.75), FVector(440.0, 6.0, 0.5), TEXT("Quay"));
        // Cobble bands sit a centimetre above the promenade and do not collide, so they cannot bump the capsule.
        for (const int32 Band : { 0, 1, 2 })
        {
            B.Block(FString::Printf(TEXT("Cobble%d"), Band), FVector(0.0, 0.02, -60.0 - Band * 6.0), FVector(520.0, 0.02, 1.4), Band % 2 == 0 ? TEXT("Cobble") : TEXT("Paving"), TEXT("cube"), false, TEXT("Blockout/Ground"));
        }

        // Stari most: a flush deck with railings. The deck stays outside the Lent area box.
        B.Block(TEXT("StariMostDeck"), FVector(0.0, -0.3, 10.5), FVector(14.0, 0.6, 129.0), TEXT("Bridge"));
        for (const int32 Side : { -1, 1 })
        {
            B.Block(FString::Printf(TEXT("StariMostRailing%+d"), Side), FVector(Side * 6.85, 0.55, 10.5), FVector(0.3, 1.1, 129.0), TEXT("Railing"));
        }
        for (const double PierZ : { -30.0, -10.0, 30.0, 55.0 })
        {
            B.Block(FString::Printf(TEXT("StariMostPier%d"), static_cast<int32>(PierZ)), FVector(0.0, -3.2, PierZ), FVector(12.0, 4.6, 3.2), TEXT("Bridge"));
            B.Block(FString::Printf(TEXT("StariMostArch%d"), static_cast<int32>(PierZ)), FVector(0.0, -2.2, PierZ), FVector(10.0, 1.2, 8.0), TEXT("Quay"), TEXT("cube"), false);
        }
        for (const double LampZ : { -40.0, 20.0, 60.0 })
        {
            for (const int32 Side : { -1, 1 })
            {
                const double LampX = Side * 5.2;
                const FString LampId = FString::Printf(TEXT("%d_%d"), static_cast<int32>(LampZ), Side);
                B.Block(FString::Printf(TEXT("BridgeLamp%s"), *LampId), FVector(LampX, 1.8, LampZ), FVector(0.16, 3.6, 0.16), TEXT("Lamp"), TEXT("cylinder"), true, TEXT("Blockout/Lamps"));
                B.Block(FString::Printf(TEXT("BridgeLampGlass%s"), *LampId), FVector(LampX, 3.75, LampZ), FVector(0.4, 0.4, 0.4), TEXT("LampGlass"), TEXT("sphere"), false, TEXT("Blockout/Lamps"));
                B.LampGlow(FString::Printf(TEXT("BridgeLampGlow%s"), *LampId), LampX, 3.75, LampZ);
            }
        }

        // Quay railing, open at the bridge mouth (x -7 to 7) and at the jetty (x -164 to -146).
        B.Block(TEXT("QuayRailingFarWest"), FVector(-193.5, 0.55, QuayZ - 0.15), FVector(43.0, 1.1, 0.3), TEXT("Railing"));
        B.Block(TEXT("QuayRailingWest"), FVector(-72.5, 0.55, QuayZ - 0.15), FVector(131.0, 1.1, 0.3), TEXT("Railing"));
        B.Block(TEXT("QuayRailingEast"), FVector(133.5, 0.55, QuayZ - 0.15), FVector(253.0, 1.1, 0.3), TEXT("Railing"));

        // Fishing jetty west of the angler. Rails on three sides; the promenade side is open.
        B.Block(TEXT("JettyDeck"), FVector(-155.0, -0.1, -48.0), FVector(38.0, 0.2, 16.0), TEXT("Bridge"), TEXT("cube"), true, TEXT("Blockout/Jetty"));
        B.Block(TEXT("JettyRailWest"), FVector(-173.2, 0.5, -46.0), FVector(0.25, 1.0, 12.0), TEXT("Railing"), TEXT("cube"), true, TEXT("Blockout/Jetty"));
        B.Block(TEXT("JettyRailEast"), FVector(-136.8, 0.5, -46.0), FVector(0.25, 1.0, 12.0), TEXT("Railing"), TEXT("cube"), true, TEXT("Blockout/Jetty"));
        B.Block(TEXT("JettyRailEnd"), FVector(-155.0, 0.45, -41.0), FVector(36.0, 0.9, 0.25), TEXT("Railing"), TEXT("cube"), true, TEXT("Blockout/Jetty"));

        // Lamps along the quay and plane trees between them. Skip the bridge mouth and the jetty.
        for (int32 LampX = -195; LampX <= 195; LampX += 30)
        {
            if (FMath::Abs(LampX) < 12 || (LampX > -176 && LampX < -134))
            {
                continue;
            }
            B.Block(FString::Printf(TEXT("LampPost%d"), LampX), FVector(LampX, 2.1, QuayZ - 1.5), FVector(0.16, 4.2, 0.16), TEXT("Lamp"), TEXT("cylinder"), true, TEXT("Blockout/Lamps"));
            B.Block(FString::Printf(TEXT("LampGlass%d"), LampX), FVector(LampX, 4.35, QuayZ - 1.5), FVector(0.45, 0.45, 0.45), TEXT("LampGlass"), TEXT("sphere"), false, TEXT("Blockout/Lamps"));
            B.LampGlow(FString::Printf(TEXT("LampGlow%d"), LampX), LampX, 4.35, QuayZ - 1.5);
        }
        for (const int32 TreeX : { -180, -90, -60, 45, 75, 135, 165 })
        {
            B.Block(FString::Printf(TEXT("TreeTrunk%d"), TreeX), FVector(TreeX, 1.75, -71.0), FVector(0.45, 3.5, 0.45), TEXT("Trunk"), TEXT("cylinder"), true, TEXT("Blockout/Trees"));
            B.Block(FString::Printf(TEXT("TreeCanopy%d"), TreeX), FVector(TreeX, 5.5, -71.0), FVector(6.0, 4.5, 6.0), TEXT("Canopy"), TEXT("sphere"), false, TEXT("Blockout/Trees"));
            B.Block(FString::Printf(TEXT("Shrub%d"), TreeX), FVector(TreeX + 4.0, 0.45, -63.0), FVector(1.6, 0.9, 1.6), TEXT("Shrub"), TEXT("sphere"), false, TEXT("Blockout/Trees"));
        }
        for (const int32 BenchX : { -180, -60, -30, 40, 80, 120, 170 })
        {
            const FString BenchId = FString::Printf(TEXT("%d"), BenchX);
            B.Block(FString::Printf(TEXT("BenchSeat%s"), *BenchId), FVector(BenchX, 0.48, -66.5), FVector(1.8, 0.1, 0.45), TEXT("Bench"), TEXT("cube"), true, TEXT("Blockout/Street"));
            B.Block(FString::Printf(TEXT("BenchBack%s"), *BenchId), FVector(BenchX, 0.85, -66.75), FVector(1.8, 0.55, 0.08), TEXT("Bench"), TEXT("cube"), true, TEXT("Blockout/Street"));
            for (const int32 Leg : { -1, 1 })
            {
                B.Block(FString::Printf(TEXT("BenchLeg%s_%d"), *BenchId, Leg), FVector(BenchX + Leg * 0.7, 0.22, -66.5), FVector(0.08, 0.44, 0.08), TEXT("Metal"), TEXT("cylinder"), true, TEXT("Blockout/Street"));
            }
        }
        B.Block(TEXT("StreetSignPole"), FVector(9.2, 1.3, -61.0), FVector(0.1, 2.6, 0.1), TEXT("Metal"), TEXT("cylinder"), true, TEXT("Blockout/Street"));
        B.Block(TEXT("StreetSignBoard"), FVector(9.2, 2.45, -61.0), FVector(0.08, 0.4, 2.4), TEXT("Sign"), TEXT("cube"), false, TEXT("Blockout/Street"));
        B.Sign(TEXT("StreetSign"), TEXT("Obrezna pot"), 9.35, 2.45, -61.0, 1.0);

        // Promenade row: facades toward the river (south). A 6 m street after every third house,
        // and the bridge street at x = 0 stays open. Depth 16 m, front face near z = -79.
        double RowX = -212.0;
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Buildings); ++Index)
        {
            const FBuilding& Building = Buildings[Index];
            if (RowX < StreetHalfWidth && RowX + Building.Width > -StreetHalfWidth)
            {
                RowX = StreetHalfWidth;
            }
            const double CentreX = RowX + Building.Width / 2.0;
            B.House(TEXT("F"), Index, CentreX, Building.Width, Building.Height, Building.Colour, Shops[Index], -87.0, 16.0, 1.0, TEXT("Blockout/Buildings"));
            RowX += Building.Width + (Index % 3 == 2 ? 6.0 : 0.0);
        }

        // Lane and a plainer back row, plus a water tower for the west silhouette.
        // The tower sits on the promenade slab, clear of the bridge street and of the front row.
        B.Block(TEXT("WaterTower"), FVector(-208.0, 12.0, -114.0), FVector(8.0, 24.0, 8.0), TEXT("Grey"), TEXT("cube"), true, TEXT("Blockout/Buildings"));
        B.Block(TEXT("WaterTowerBand"), FVector(-208.0, 20.5, -114.0), FVector(8.6, 1.2, 8.6), TEXT("Cornice"), TEXT("cube"), true, TEXT("Blockout/Buildings"));
        B.Block(TEXT("WaterTowerSpire"), FVector(-208.0, 27.0, -114.0), FVector(6.0, 6.0, 6.0), TEXT("Spire"), TEXT("cone"), false, TEXT("Blockout/Buildings"));
        constexpr FBuilding BackRow[] = {
            { 22, 9, TEXT("Cream") }, { 18, 11, TEXT("Grey") }, { 26, 8, TEXT("Umber") },
            { 20, 10, TEXT("Sage") }, { 24, 9, TEXT("Rose") }, { 18, 12, TEXT("Ochre") },
            { 22, 8, TEXT("Grey") }, { 16, 10, TEXT("Cream") },
        };
        double BackX = -190.0;
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(BackRow); ++Index)
        {
            const FBuilding& Building = BackRow[Index];
            if (BackX < StreetHalfWidth && BackX + Building.Width > -StreetHalfWidth)
            {
                BackX = StreetHalfWidth;
            }
            B.House(TEXT("B"), Index, BackX + Building.Width / 2.0, Building.Width, Building.Height, Building.Colour, nullptr, -114.0, 12.0, 1.0, TEXT("Blockout/BackRow"));
            BackX += Building.Width + 3.0;
        }

        // Far bank: enough mass that the view across the Drava is not an empty slab. Faces the river.
        constexpr FBuilding FarBank[] = {
            { 28, 12, TEXT("Far") }, { 22, 16, TEXT("Grey") }, { 34, 11, TEXT("Cream") }, { 18, 20, TEXT("Umber") },
            { 30, 13, TEXT("Far") }, { 24, 10, TEXT("Rose") }, { 26, 15, TEXT("Grey") }, { 20, 9, TEXT("Cream") },
        };
        double FarX = -200.0;
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(FarBank); ++Index)
        {
            const FBuilding& Building = FarBank[Index];
            if (FarX < 12.0 && FarX + Building.Width > -12.0)
            {
                FarX = 12.0; // keep the south landing of the bridge clear
            }
            B.House(TEXT("S"), Index, FarX + Building.Width / 2.0, Building.Width, Building.Height, Building.Colour, nullptr, 102.0, 14.0, -1.0, TEXT("Blockout/FarBank"));
            FarX += Building.Width + 4.0;
        }
        B.Block(TEXT("SouthTower"), FVector(130.0, 11.0, 98.0), FVector(9.0, 22.0, 9.0), TEXT("Grey"), TEXT("cube"), true, TEXT("Blockout/FarBank"));
        B.Block(TEXT("SouthTowerSpire"), FVector(130.0, 25.0, 98.0), FVector(7.0, 6.0, 7.0), TEXT("Spire"), TEXT("cone"), false, TEXT("Blockout/FarBank"));

        // The named area from areas.json; the server derives each player's location from it.
        AMarburgumArea* Area = Spawn<AMarburgumArea>(World, Ue(0.0, 5.0, -78.0), FRotator::ZeroRotator, TEXT("Area_lent"), TEXT("Areas"));
        Area->AreaId = FName(TEXT("lent"));
        Area->Note = FText::FromString(TEXT("Marburgum  ·  1990s  ·  authored Lent, not the licensed city model"));
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
        Lighting(World, Ue(0.0, 30.0, 0.0), FRotator(-32.0, -60.0, 0.0), true, 0.006f);
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

namespace MarburgumMaps
{
    int32 FictionLent()
    {
        UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(TEXT("/Game/World/lent"));
        if (!World)
        {
            UE_LOG(LogMarburgumMaps, Error, TEXT("MB2FICTION cannot load /Game/World/lent"));
            return 1;
        }

        TArray<AActor*> Previous;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            if (It->GetFolderPath() == FName(TEXT("Marburgum/Opening")))
            {
                Previous.Add(*It);
            }
        }
        for (AActor* Actor : Previous)
        {
            Actor->Destroy();
        }

        auto BoundsOf = [World](const TCHAR* Label, FVector& Origin, FVector& Extent) -> bool
        {
            for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
            {
                if (It->GetActorLabel() == Label)
                {
                    It->GetActorBounds(false, Origin, Extent);
                    return true;
                }
            }
            return false;
        };

        FVector RiverOrigin = FVector::ZeroVector;
        FVector RiverExtent = FVector::ZeroVector;
        FVector BridgeOrigin = FVector::ZeroVector;
        FVector BridgeExtent = FVector::ZeroVector;
        if (!BoundsOf(TEXT("Drava"), RiverOrigin, RiverExtent) || !BoundsOf(TEXT("Bridge_stari"), BridgeOrigin, BridgeExtent))
        {
            UE_LOG(LogMarburgumMaps, Error, TEXT("MB2FICTION missing Drava or Bridge_stari bounds"));
            return 1;
        }
        const bool bAlongX = RiverExtent.X >= RiverExtent.Y;
        const FVector Along = bAlongX ? FVector(1.0, 0.0, 0.0) : FVector(0.0, 1.0, 0.0);
        const FVector Across = bAlongX ? FVector(0.0, 1.0, 0.0) : FVector(1.0, 0.0, 0.0);

        struct FBuilding
        {
            FVector Origin = FVector::ZeroVector;
            FVector Extent = FVector::ZeroVector;
            double Side = 0.0;
            double AlongPos = 0.0;
        };
        TArray<FBuilding> LentBuildings;
        double Positive = 0.0;
        double Negative = 0.0;
        for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
        {
            const FString Label = It->GetActorLabel();
            if (Label.Len() < 2 || (Label[0] != TCHAR('w') && Label[0] != TCHAR('r')))
            {
                continue;
            }
            bool bDigits = true;
            for (int32 Index = 1; Index < Label.Len(); ++Index)
            {
                if (!FChar::IsDigit(Label[Index]))
                {
                    bDigits = false;
                    break;
                }
            }
            if (!bDigits)
            {
                continue;
            }
            FBuilding Building;
            It->GetActorBounds(false, Building.Origin, Building.Extent);
            const FVector Delta = Building.Origin - RiverOrigin;
            Building.Side = FVector::DotProduct(Delta, Across);
            Building.AlongPos = FVector::DotProduct(Delta, Along);
            if (Building.Side >= 0.0)
            {
                Positive += 1.0;
            }
            else
            {
                Negative += 1.0;
            }
            LentBuildings.Add(Building);
        }
        if (LentBuildings.Num() == 0)
        {
            UE_LOG(LogMarburgumMaps, Error, TEXT("MB2FICTION found no Lent building meshes"));
            return 1;
        }
        const double LentSign = Positive >= Negative ? 1.0 : -1.0;
        const FVector Inland = Across * LentSign;
        const FVector Riverward = Across * -LentSign;
        const FBuilding* Landing = nullptr;
        double Nearest = TNumericLimits<double>::Max();
        for (const FBuilding& Building : LentBuildings)
        {
            if (Building.Side * LentSign <= 0.0)
            {
                continue;
            }
            const double Dist = FVector::Dist2D(Building.Origin, BridgeOrigin);
            if (Dist < Nearest)
            {
                Nearest = Dist;
                Landing = &Building;
            }
        }
        if (!Landing)
        {
            UE_LOG(LogMarburgumMaps, Error, TEXT("MB2FICTION found no buildings on the Lent bank"));
            return 1;
        }
        const float Ground = static_cast<float>(Landing->Origin.Z - Landing->Extent.Z);
        const float WalkZ = Ground + 96.0f;
        const double FaceAcross = FMath::Abs(FVector::DotProduct(Landing->Extent, Across));
        const FVector Arrival = Landing->Origin + Riverward * (FaceAcross + 250.0) + FVector(0.0, 0.0, -Landing->Extent.Z + 96.0);
        const double West = (FMath::Abs(Along.X) >= FMath::Abs(Along.Y)) ? -FMath::Sign(Along.X) : -1.0;
        const FVector TowardWest = Along * (West == 0.0 ? -1.0 : West);
        const FVector AnglerAt = Arrival + TowardWest * 2200.0;
        const double BridgeHalf = FMath::Abs(FVector::DotProduct(BridgeExtent, Across));
        const FVector BridgeAt = FVector(BridgeOrigin.X, BridgeOrigin.Y, 0.0) + Inland * BridgeHalf * 0.72 + FVector(0.0, 0.0, WalkZ);
        FVector BuildingMin = LentBuildings[0].Origin;
        FVector BuildingMax = LentBuildings[0].Origin;
        for (const FBuilding& Building : LentBuildings)
        {
            BuildingMin = BuildingMin.ComponentMin(Building.Origin);
            BuildingMax = BuildingMax.ComponentMax(Building.Origin);
        }
        UE_LOG(LogMarburgumMaps, Display, TEXT("MB2FICTION river=%s extent=%s bridge=%s bextent=%s along=%s lent_sign=%.0f buildings=%d ground=%.0f..%.0f buildings_xy=%s..%s"),
            *RiverOrigin.ToString(), *RiverExtent.ToString(), *BridgeOrigin.ToString(), *BridgeExtent.ToString(),
            bAlongX ? TEXT("X") : TEXT("Y"), LentSign, LentBuildings.Num(), Ground, Ground,
            *BuildingMin.ToString(), *BuildingMax.ToString());

        World->GetWorldSettings()->DefaultGameMode = AMarburgumGameMode::StaticClass();

        const FRotator SunRotation(-28.0, FMath::RadiansToDegrees(FMath::Atan2(Inland.Y, Inland.X)), 0.0);
        ADirectionalLight* Sun = Spawn<ADirectionalLight>(World, Arrival + FVector(0.0, 0.0, 2000.0), SunRotation, TEXT("MB2Sun"), TEXT("Marburgum/Opening"));
        Sun->SetActorRotation(SunRotation);
        Sun->GetRootComponent()->SetMobility(EComponentMobility::Movable);
        UDirectionalLightComponent* SunLight = CastChecked<UDirectionalLightComponent>(Sun->GetLightComponent());
        SunLight->SetMobility(EComponentMobility::Movable);
        SunLight->SetIntensity(8.0f);
        SunLight->SetUseTemperature(true);
        SunLight->SetTemperature(4800.0f);

        ASkyLight* Sky = Spawn<ASkyLight>(World, Arrival, FRotator::ZeroRotator, TEXT("MB2SkyLight"), TEXT("Marburgum/Opening"));
        Sky->GetRootComponent()->SetMobility(EComponentMobility::Movable);
        Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
        Sky->GetLightComponent()->bRealTimeCapture = true;
        Spawn<ASkyAtmosphere>(World, FVector::ZeroVector, FRotator::ZeroRotator, TEXT("MB2Sky"), TEXT("Marburgum/Opening"));

        AExponentialHeightFog* Fog = Spawn<AExponentialHeightFog>(World, FVector::ZeroVector, FRotator::ZeroRotator, TEXT("MB2Fog"), TEXT("Marburgum/Opening"));
        Fog->GetComponent()->SetFogDensity(0.004f);
        Fog->GetComponent()->SetFogHeightFalloff(0.15f);
        Fog->GetComponent()->SetFogInscatteringColor(FLinearColor(0.72f, 0.68f, 0.58f));

        APostProcessVolume* Grade = Spawn<APostProcessVolume>(World, FVector::ZeroVector, FRotator::ZeroRotator, TEXT("MB2Grade"), TEXT("Marburgum/Opening"));
        Grade->bUnbound = true;
        Grade->Settings.bOverride_AutoExposureBias = true;
        Grade->Settings.AutoExposureBias = 0.4f;
        Grade->Settings.bOverride_WhiteTemp = true;
        Grade->Settings.WhiteTemp = 5800.0f;
        Grade->Settings.bOverride_BloomIntensity = true;
        Grade->Settings.BloomIntensity = 0.15f;

        auto PlaceStart = [&](const TCHAR* Tag, const FVector& At, const FVector& LookAt, int32 Index)
        {
            const FVector Flat = LookAt - At;
            const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Flat.Y, Flat.X));
            APlayerStart* Start = Spawn<APlayerStart>(World, At, FRotator(0.0, Yaw, 0.0),
                *FString::Printf(TEXT("MB2Start_%s_%d"), Tag, Index), TEXT("Marburgum/Opening"));
            Start->PlayerStartTag = FName(Tag);
        };
        PlaceStart(TEXT("arrival"), Arrival, BridgeAt, 0);
        PlaceStart(TEXT("arrival"), Arrival + TowardWest * 180.0, BridgeAt, 1);
        PlaceStart(TEXT("bridge"), BridgeAt, Arrival, 0);
        PlaceStart(TEXT("angler"), AnglerAt + Inland * 200.0, AnglerAt, 0);

        FVector AreaMin = Arrival;
        FVector AreaMax = Arrival;
        for (const FBuilding& Building : LentBuildings)
        {
            AreaMin = AreaMin.ComponentMin(Building.Origin - Building.Extent);
            AreaMax = AreaMax.ComponentMax(Building.Origin + Building.Extent);
        }
        const FVector AreaCenter = (AreaMin + AreaMax) * 0.5;
        const FVector AreaExtent = (AreaMax - AreaMin) * 0.5 + FVector(400.0, 400.0, 1500.0);
        AMarburgumArea* Area = Spawn<AMarburgumArea>(World, FVector(AreaCenter.X, AreaCenter.Y, WalkZ), FRotator::ZeroRotator, TEXT("MB2Area_lent"), TEXT("Marburgum/Opening"));
        Area->AreaId = FName(TEXT("lent"));
        Area->Note = FText::FromString(TEXT("Marburgum  \u00b7  1990s  \u00b7  original Lent geometry, fictional opening-route names"));
        if (UBoxComponent* Box = Area->FindComponentByClass<UBoxComponent>())
        {
            Box->SetBoxExtent(AreaExtent);
        }

        const float SignYaw = FMath::RadiansToDegrees(FMath::Atan2(Riverward.Y, Riverward.X));
        auto PlaceSign = [&](const TCHAR* Label, const TCHAR* Text, const FVector& At)
        {
            ATextRenderActor* Sign = Spawn<ATextRenderActor>(World, At, FRotator(0.0, SignYaw, 0.0), Label, TEXT("Marburgum/Opening"));
            UTextRenderComponent* Render = Sign->GetTextRender();
            Render->SetText(FText::FromString(Text));
            Render->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
            Render->SetVerticalAlignment(EVRTA_TextCenter);
            Render->SetWorldSize(42.0f);
            Render->SetTextRenderColor(FColor(48, 32, 20));
        };
        PlaceSign(TEXT("MB2Sign_street"), TEXT("Obre\u0161ka pot"), Arrival + FVector(0.0, 0.0, 140.0));

        TArray<FBuilding> Frontage;
        for (const FBuilding& Building : LentBuildings)
        {
            if (Building.Side * LentSign > 0.0 && FMath::Abs(Building.Side - Landing->Side) < 8000.0)
            {
                Frontage.Add(Building);
            }
        }
        Frontage.Sort([](const FBuilding& A, const FBuilding& B) { return A.AlongPos < B.AlongPos; });
        const TCHAR* ShopNames[] = { TEXT("Kavarna Pri Toku"), TEXT("Ribarnica Na Bregu"), TEXT("Pekarna Pri Mostu") };
        const int32 ShopCount = FMath::Min(3, Frontage.Num());
        for (int32 Shop = 0; Shop < ShopCount; ++Shop)
        {
            const int32 Index = FMath::Clamp(Frontage.Num() * (Shop + 1) / (ShopCount + 1), 0, Frontage.Num() - 1);
            const FBuilding& Building = Frontage[Index];
            const double Face = bAlongX ? Building.Extent.Y : Building.Extent.X;
            const FVector At = Building.Origin + Riverward * Face + FVector(0.0, 0.0, -Building.Extent.Z + 340.0);
            PlaceSign(*FString::Printf(TEXT("MB2Sign_shop_%d"), Shop), ShopNames[Shop], At);
        }

        AMarburgumNpc* Angler = Spawn<AMarburgumNpc>(World, AnglerAt, FRotator(0.0, SignYaw, 0.0), TEXT("MB2Npc_lent_angler"), TEXT("Marburgum/Opening"));
        Angler->InteractableId = FName(TEXT("lent_angler"));
        Angler->DisplayName = FText::FromString(TEXT("Riverside angler"));
        Angler->Verb = FText::FromString(TEXT("Talk"));
        Angler->InteractionRange = 400.0;
        Angler->bTestFixture = false;
        Angler->Dialogue = FText::FromString(TEXT("The Drava looks calm from here, but the current does the real work. Keep your first casts close to the bank."));
        Angler->PrimaryColor = FLinearColor(0.349f, 0.392f, 0.333f);
        Angler->SecondaryColor = FLinearColor(0.208f, 0.227f, 0.204f);
        Angler->bHoldsRod = true;
        Angler->RerunConstructionScripts();

        if (UMaterialInstanceConstant* Plaster = LoadObject<UMaterialInstanceConstant>(nullptr,
            TEXT("/Game/Marburgum/Materials/Instances/building_envelope/MI_plaster_mineral.MI_plaster_mineral")))
        {
            float Roughness = 1.0f;
            Plaster->GetScalarParameterValue(FMaterialParameterInfo(TEXT("RoughnessDefault")), Roughness);
            UE_LOG(LogMarburgumMaps, Display, TEXT("MB2FICTION plaster roughness %.2f"), Roughness);
            if (Roughness < 0.6f)
            {
                Plaster->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("RoughnessDefault")), 0.72f);
                Plaster->PostEditChange();
                Plaster->MarkPackageDirty();
                UEditorLoadingAndSavingUtils::SavePackages({ Plaster->GetOutermost() }, false);
            }
        }

        if (!UEditorLoadingAndSavingUtils::SaveMap(World, TEXT("/Game/World/lent")))
        {
            UE_LOG(LogMarburgumMaps, Error, TEXT("MB2FICTION could not save /Game/World/lent"));
            return 1;
        }
        UE_LOG(LogMarburgumMaps, Display, TEXT("MB2FICTION saved /Game/World/lent arrival=%s angler=%s"), *Arrival.ToString(), *AnglerAt.ToString());
        return 0;
    }

    /** Read-only: log every actor of a saved map with its class, transform and light settings. */
    int32 Describe(const FString& MapPath)
    {
        UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(MapPath);
        if (!World)
        {
            UE_LOG(LogMarburgumMaps, Error, TEXT("MB2MAP cannot load %s"), *MapPath);
            return 1;
        }
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            const FTransform Transform = It->GetActorTransform();
            FString Extra;
            if (const ULightComponent* Light = It->FindComponentByClass<ULightComponent>())
            {
                Extra = FString::Printf(TEXT(" light_dir=%s intensity=%.2f mobility=%d"),
                    *Light->GetDirection().ToString(), Light->Intensity, static_cast<int32>(Light->Mobility.GetValue()));
            }
            UE_LOG(LogMarburgumMaps, Display, TEXT("MB2DESCRIBE %s class=%s loc=%s rot=%s scale=%s%s"), *It->GetActorLabel(),
                *It->GetClass()->GetName(), *Transform.GetLocation().ToString(), *Transform.Rotator().ToString(),
                *Transform.GetScale3D().ToString(), *Extra);
        }
        return 0;
    }
}

int32 UMarburgumCreateMapsCommandlet::Main(const FString& Params)
{
    FString DescribePath;
    if (FParse::Value(*Params, TEXT("Describe="), DescribePath))
    {
        return MarburgumMaps::Describe(DescribePath);
    }
    if (FParse::Param(*Params, TEXT("FictionLent")))
    {
        return MarburgumMaps::FictionLent();
    }
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
