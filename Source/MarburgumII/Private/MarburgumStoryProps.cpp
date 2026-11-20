#include "MarburgumStoryProps.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MarburgumVehicleContract.h"

namespace MarburgumStoryProps
{
    namespace
    {
        // Flat colours, linear: period furniture rather than anything glossy.
        const FLinearColor Wood(0.11f, 0.065f, 0.035f);
        const FLinearColor DarkWood(0.06f, 0.035f, 0.02f);
        const FLinearColor Paper(0.78f, 0.72f, 0.58f);
        const FLinearColor LedgerGreen(0.04f, 0.11f, 0.07f);
        const FLinearColor LedgerBlue(0.03f, 0.05f, 0.13f);
        const FLinearColor Steel(0.16f, 0.17f, 0.18f);
        const FLinearColor DarkSteel(0.07f, 0.075f, 0.08f);
        const FLinearColor Opal(0.95f, 0.95f, 0.91f);
        const FLinearColor Film(0.05f, 0.045f, 0.04f);
        const FLinearColor Cardboard(0.4f, 0.28f, 0.15f);
        const FLinearColor Rubber(0.02f, 0.02f, 0.02f);
        const FLinearColor Concrete(0.33f, 0.32f, 0.3f);
        const FLinearColor BoardGreen(0.035f, 0.1f, 0.065f);
        const FLinearColor Stencil(0.85f, 0.85f, 0.8f);
        const FLinearColor DeskGreen(0.16f, 0.19f, 0.16f);
        const FLinearColor Binding(0.18f, 0.05f, 0.04f);
        const FLinearColor KainachPaint(0.32f, 0.075f, 0.03f);

        /** Parts under Root in its frame; Z is height above the ground. */
        class FKit
        {
        public:
            FKit(AActor& InOwner, USceneComponent& InRoot, const FPrimitives& InPrimitives)
                : Owner(InOwner), Root(InRoot), Primitives(InPrimitives), bDraws(InOwner.GetNetMode() != NM_DedicatedServer)
            {
            }

            UStaticMeshComponent* Mesh(UStaticMesh* Asset, USceneComponent& Parent, const FVector& At, const FRotator& Turn = FRotator::ZeroRotator,
                const FVector& Scale = FVector::OneVector)
            {
                UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(&Owner);
                Part->SetMobility(EComponentMobility::Movable);
                Part->SetStaticMesh(Asset);
                Part->SetupAttachment(&Parent);
                Part->SetRelativeLocationAndRotation(At, Turn);
                Part->SetRelativeScale3D(Scale);
                // Furniture stops the camera and the body; nothing about it simulates.
                Part->SetCollisionProfileName(TEXT("BlockAll"));
                Part->SetCanEverAffectNavigation(false);
                Part->RegisterComponent();
                Owner.AddInstanceComponent(Part);
                return Part;
            }

            void Paint(UStaticMeshComponent* Part, const FLinearColor& Colour) const
            {
                if (!bDraws || !Part || !Primitives.Plain)
                {
                    return;
                }
                UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Primitives.Plain, Part);
                Material->SetVectorParameterValue(TEXT("Color"), Colour);
                Part->SetMaterial(0, Material);
            }

            /** A box Size centimetres, centred at At (Z above the ground). */
            UStaticMeshComponent* Box(const FVector& At, const FVector& Size, const FLinearColor& Colour, const FRotator& Turn = FRotator::ZeroRotator)
            {
                UStaticMeshComponent* Part = Mesh(Primitives.Cube, Root, Ground(At), Turn, Size / 100.0);
                Paint(Part, Colour);
                return Part;
            }

            /** An upright cylinder of Diameter and Height centred at At, or turned by Turn. */
            UStaticMeshComponent* Cylinder(const FVector& At, double Diameter, double Height, const FLinearColor& Colour,
                const FRotator& Turn = FRotator::ZeroRotator)
            {
                UStaticMeshComponent* Part = Mesh(Primitives.Cylinder, Root, Ground(At), Turn, FVector(Diameter, Diameter, Height) / 100.0);
                Paint(Part, Colour);
                return Part;
            }

            /** Lettering facing the -X side (where a visitor stands) unless Turn says otherwise. */
            void Text(const FVector& At, const FString& Words, float Height, const FLinearColor& Colour, const FRotator& Turn = FRotator(0.0, 180.0, 0.0),
                USceneComponent* Parent = nullptr)
            {
                if (!bDraws)
                {
                    return;
                }
                UTextRenderComponent* Letters = NewObject<UTextRenderComponent>(&Owner);
                Letters->SetupAttachment(Parent ? Parent : &Root);
                Letters->SetRelativeLocationAndRotation(Parent ? At : Ground(At), Turn);
                Letters->SetText(FText::FromString(Words));
                Letters->SetWorldSize(Height);
                Letters->SetTextRenderColor(Colour.ToFColor(true));
                Letters->SetHorizontalAlignment(EHTA_Center);
                Letters->SetVerticalAlignment(EVRTA_TextCenter);
                Letters->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                Letters->RegisterComponent();
                Owner.AddInstanceComponent(Letters);
            }

            void Lamp(const FVector& At, float Intensity, float Radius, const FLinearColor& Colour)
            {
                if (!bDraws)
                {
                    return;
                }
                UPointLightComponent* Light = NewObject<UPointLightComponent>(&Owner);
                Light->SetupAttachment(&Root);
                Light->SetRelativeLocation(Ground(At));
                Light->SetMobility(EComponentMobility::Movable);
                Light->SetIntensityUnits(ELightUnits::Candelas);
                Light->SetIntensity(Intensity);
                Light->SetAttenuationRadius(Radius);
                Light->SetLightColor(Colour);
                Light->SetCastShadows(false);
                Light->RegisterComponent();
                Owner.AddInstanceComponent(Light);
            }

            USceneComponent* Frame(const FVector& At, const FRotator& Turn)
            {
                USceneComponent* Node = NewObject<USceneComponent>(&Owner);
                Node->SetMobility(EComponentMobility::Movable);
                Node->SetupAttachment(&Root);
                Node->SetRelativeLocationAndRotation(Ground(At), Turn);
                Node->RegisterComponent();
                Owner.AddInstanceComponent(Node);
                return Node;
            }

            static FVector Ground(const FVector& At) { return At - FVector(0.0, 0.0, GroundBelowRootCm); }

            AActor& Owner;
            USceneComponent& Root;
            const FPrimitives& Primitives;
            const bool bDraws;
        };

        /** Foto Vidmar's counter with the order book open on it and the earlier years stacked beside it. */
        void OrderBook(FKit& Kit)
        {
            Kit.Box(FVector(0.0, 0.0, 47.0), FVector(55.0, 110.0, 94.0), Wood);
            Kit.Box(FVector(0.0, 0.0, 95.5), FVector(62.0, 118.0, 3.0), DarkWood);
            // The open book, angled a little towards whoever reads it.
            Kit.Box(FVector(-4.0, -6.0, 97.6), FVector(31.0, 46.0, 1.2), LedgerGreen);
            Kit.Box(FVector(-4.0, -17.0, 98.6), FVector(28.0, 21.0, 0.9), Paper, FRotator(0.0, 0.0, 5.0));
            Kit.Box(FVector(-4.0, 5.0, 98.6), FVector(28.0, 21.0, 0.9), Paper, FRotator(0.0, 0.0, -5.0));
            Kit.Cylinder(FVector(-12.0, 16.0, 99.6), 0.8, 15.0, DarkWood, FRotator(90.0, 20.0, 0.0));
            // The earlier years, spines out.
            for (int32 Year = 0; Year < 4; ++Year)
            {
                Kit.Box(FVector(4.0, 40.0, 100.0 + Year * 5.2), FVector(30.0, 22.0, 5.0), Year % 2 ? LedgerBlue : LedgerGreen);
            }
            Kit.Text(FVector(-28.2, 0.0, 74.0), TEXT("FOTO VIDMAR"), 9.f, Paper);
            Kit.Text(FVector(-28.2, 0.0, 62.0), TEXT("NAROČILA 1990"), 6.f, Paper);
        }

        /** An opal-glass light table with a loupe, a contact sheet's worth of film on it and a lamp over it. */
        void LightTable(FKit& Kit)
        {
            for (const double X : { -32.0, 32.0 })
            {
                for (const double Y : { -55.0, 55.0 })
                {
                    Kit.Box(FVector(X, Y, 40.0), FVector(5.0, 5.0, 80.0), DarkSteel);
                }
            }
            Kit.Box(FVector(0.0, 0.0, 82.0), FVector(72.0, 122.0, 8.0), Steel);
            Kit.Box(FVector(0.0, 0.0, 86.4), FVector(60.0, 108.0, 1.0), Opal);
            Kit.Box(FVector(-4.0, -18.0, 87.1), FVector(25.0, 20.0, 0.4), Film);
            Kit.Cylinder(FVector(8.0, 12.0, 90.0), 5.0, 6.0, DarkSteel);
            Kit.Cylinder(FVector(26.0, 54.0, 112.0), 3.0, 50.0, DarkSteel);
            Kit.Cylinder(FVector(14.0, 40.0, 136.0), 16.0, 9.0, DarkSteel, FRotator(-30.0, 0.0, 0.0));
            // The glass glows: a small warm light just above it.
            Kit.Lamp(FVector(0.0, 0.0, 96.0), 60.f, 160.f, FLinearColor(1.0f, 0.96f, 0.88f));
            Kit.Text(FVector(-36.2, 0.0, 76.0), TEXT("FOTO VIDMAR"), 6.f, Paper);
        }

        /** A wire-mesh cellar storage cage with Stefan Zupan's office boxes, a pair of winter tyres and jars on a shelf. */
        void StorageCage(FKit& Kit)
        {
            constexpr double HalfDepth = 80.0;
            constexpr double HalfWidth = 110.0;
            constexpr double Height = 220.0;
            Kit.Box(FVector(0.0, 0.0, 1.0), FVector(HalfDepth * 2.0 + 20.0, HalfWidth * 2.0 + 20.0, 2.0), Concrete);
            for (const double X : { -HalfDepth, HalfDepth })
            {
                for (const double Y : { -HalfWidth, HalfWidth })
                {
                    Kit.Box(FVector(X, Y, Height * 0.5), FVector(4.0, 4.0, Height), Steel);
                }
                Kit.Box(FVector(X, 0.0, Height - 2.0), FVector(4.0, HalfWidth * 2.0, 4.0), Steel);
                Kit.Box(FVector(X, 0.0, Height * 0.5), FVector(3.0, HalfWidth * 2.0, 3.0), Steel);
            }
            for (const double Y : { -HalfWidth, HalfWidth })
            {
                Kit.Box(FVector(0.0, Y, Height - 2.0), FVector(HalfDepth * 2.0, 4.0, 4.0), Steel);
                Kit.Box(FVector(0.0, Y, Height * 0.5), FVector(HalfDepth * 2.0, 3.0, 3.0), Steel);
            }
            // The mesh: bars every 12 cm on the back and the sides, and on the front either side of the open door.
            for (double Y = -HalfWidth + 12.0; Y < HalfWidth; Y += 12.0)
            {
                Kit.Box(FVector(HalfDepth, Y, Height * 0.5), FVector(1.2, 1.2, Height - 4.0), DarkSteel);
                if (FMath::Abs(Y) > 45.0)
                {
                    Kit.Box(FVector(-HalfDepth, Y, Height * 0.5), FVector(1.2, 1.2, Height - 4.0), DarkSteel);
                }
            }
            for (double X = -HalfDepth + 12.0; X < HalfDepth; X += 12.0)
            {
                for (const double Y : { -HalfWidth, HalfWidth })
                {
                    Kit.Box(FVector(X, Y, Height * 0.5), FVector(1.2, 1.2, Height - 4.0), DarkSteel);
                }
            }
            // Stefan's boxes, the tyres and the jars.
            Kit.Box(FVector(42.0, -62.0, 26.0), FVector(46.0, 40.0, 50.0), Cardboard);
            Kit.Box(FVector(42.0, -60.0, 73.0), FVector(42.0, 38.0, 44.0), Cardboard, FRotator(0.0, 4.0, 0.0));
            Kit.Box(FVector(42.0, -12.0, 26.0), FVector(46.0, 40.0, 50.0), Cardboard, FRotator(0.0, -3.0, 0.0));
            Kit.Text(FVector(18.6, -62.0, 30.0), TEXT("STEFAN - PISARNA"), 4.5f, DarkWood);
            Kit.Text(FVector(18.6, -12.0, 30.0), TEXT("STEFAN - PISARNA"), 4.5f, DarkWood);
            Kit.Cylinder(FVector(40.0, 62.0, 11.0), 62.0, 20.0, Rubber);
            Kit.Cylinder(FVector(40.0, 62.0, 32.0), 62.0, 20.0, Rubber);
            Kit.Box(FVector(62.0, 30.0, 130.0), FVector(28.0, 70.0, 2.5), Wood);
            for (int32 Jar = 0; Jar < 4; ++Jar)
            {
                Kit.Cylinder(FVector(62.0, 6.0 + Jar * 15.0, 139.0), 9.0, 15.0, FLinearColor(0.35f, 0.18f, 0.08f));
            }
            Kit.Text(FVector(-HalfDepth - 3.0, 0.0, Height - 14.0), TEXT("13 · ZUPAN"), 12.f, Stencil);
        }

        /** The Raeda yard's dispatch date board on two steel posts, its slot for the day's card empty. */
        void DateBoard(FKit& Kit)
        {
            for (const double Y : { -85.0, 85.0 })
            {
                Kit.Box(FVector(4.0, Y, 110.0), FVector(8.0, 8.0, 220.0), DarkSteel);
            }
            Kit.Box(FVector(0.0, 0.0, 165.0), FVector(4.0, 180.0, 90.0), BoardGreen);
            Kit.Box(FVector(-2.4, 0.0, 140.0), FVector(1.0, 72.0, 20.0), Film);
            Kit.Text(FVector(-2.6, 0.0, 192.0), TEXT("RAEDA · ODPREMA"), 15.f, Stencil);
            Kit.Text(FVector(-2.6, -64.0, 140.0), TEXT("DATUM"), 11.f, Stencil);
        }

        /** Two of Kainach's cab-over lorries, weathered, from the project's lorry mesh; K 07 nearest the visitor. */
        void Trucks(FKit& Kit)
        {
            using namespace MarburgumII::Vehicles;
            const FVehicleDefinition* Lorry = FindDefinition(TEXT("city_truck_a"));
            if (!Lorry)
            {
                return;
            }
            const FString Id = Lorry->Id;
            UStaticMesh* BodyMesh = LoadObject<UStaticMesh>(nullptr, *BodyMeshPath(Id), nullptr, LOAD_NoWarn | LOAD_Quiet);
            if (!BodyMesh)
            {
                return;
            }
            // The bodywork's origin is the ground under the middle of the axles (MarburgumVehicle.cpp).
            const FVector ShellAt = LocalPointCm(0.0, WheelAttachY - Lorry->SuspensionRestLength - Lorry->WheelRadius, 0.0);
            int32 Number = 7;
            for (const double X : { 260.0, 580.0 })
            {
                // Long side to the visitor: the lorry's +Y (its right) faces the place's -X.
                USceneComponent* Truck = Kit.Frame(FVector(X, 0.0, -ShellAt.Z), FRotator(0.0, 90.0, 0.0));
                UStaticMeshComponent* Shell = Kit.Mesh(BodyMesh, *Truck, ShellAt);
                if (Kit.bDraws)
                {
                    for (int32 Slot = 0; Slot < Shell->GetNumMaterials(); ++Slot)
                    {
                        if (UMaterialInstanceDynamic* Material = Shell->CreateDynamicMaterialInstance(Slot))
                        {
                            Material->SetVectorParameterValue(TEXT("Paint"), KainachPaint);
                        }
                    }
                }
                for (int32 Wheel = 0; Wheel < WheelCount; ++Wheel)
                {
                    if (UStaticMesh* WheelMesh = LoadObject<UStaticMesh>(nullptr, *WheelMeshPath(Id, Wheel), nullptr, LOAD_NoWarn | LOAD_Quiet))
                    {
                        Kit.Mesh(WheelMesh, *Truck, WheelAttachCm(*Lorry, Wheel) - FVector(0.0, 0.0, Lorry->SuspensionRestLength * CentimetresPerMetre));
                    }
                    const FString Socket = FString::Printf(TEXT("Door%d"), Wheel);
                    const UStaticMeshSocket* Hinge = BodyMesh->FindSocket(FName(*Socket));
                    UStaticMesh* DoorMesh = Hinge ? LoadObject<UStaticMesh>(nullptr, *DoorMeshPath(Id, Wheel), nullptr, LOAD_NoWarn | LOAD_Quiet) : nullptr;
                    if (DoorMesh)
                    {
                        Kit.Mesh(DoorMesh, *Shell, Hinge->RelativeLocation);
                    }
                }
                // The fleet number on the cab door and the Austrian oval at the back.
                const double Side = Lorry->BodyWidth * 50.0 + 2.0;
                const double Front = Lorry->BodyLength * 50.0;
                Kit.Text(FVector(Front - 75.0, Side, 175.0 + ShellAt.Z), FString::Printf(TEXT("K %02d"), Number), 26.f, Stencil, FRotator(0.0, 90.0, 0.0), Truck);
                Kit.Text(FVector(Front - 75.0, Side, 140.0 + ShellAt.Z), TEXT("KAINACH"), 14.f, Stencil, FRotator(0.0, 90.0, 0.0), Truck);
                Kit.Text(FVector(-Front - 2.0, 0.0, 70.0 + ShellAt.Z), TEXT("(A)"), 16.f, Stencil, FRotator(0.0, 180.0, 0.0), Truck);
                Number += 4;
            }
        }

        /** The Mariborski Glas archive's steel desk with bound volumes of the paper and a green-shaded lamp. */
        void ArchiveDesk(FKit& Kit)
        {
            Kit.Box(FVector(0.0, 0.0, 75.0), FVector(75.0, 150.0, 3.0), DeskGreen);
            Kit.Box(FVector(0.0, -52.0, 37.0), FVector(70.0, 42.0, 74.0), DeskGreen);
            for (const double Y : { 30.0, 70.0 })
            {
                Kit.Box(FVector(0.0, Y, 37.0), FVector(70.0, 3.0, 74.0), DeskGreen);
            }
            for (int32 Volume = 0; Volume < 5; ++Volume)
            {
                Kit.Box(FVector(8.0, 38.0, 80.0 + Volume * 6.2), FVector(34.0, 26.0, 6.0), Volume % 2 ? Binding : DarkWood,
                    FRotator(0.0, Volume * 3.0 - 6.0, 0.0));
            }
            Kit.Box(FVector(-6.0, -20.0, 77.2), FVector(30.0, 42.0, 1.0), Paper);
            Kit.Cylinder(FVector(16.0, -58.0, 80.0), 12.0, 4.0, DarkSteel);
            Kit.Cylinder(FVector(16.0, -58.0, 98.0), 2.0, 34.0, DarkSteel);
            Kit.Cylinder(FVector(8.0, -58.0, 116.0), 26.0, 10.0, FLinearColor(0.02f, 0.18f, 0.06f), FRotator(-20.0, 0.0, 0.0));
            Kit.Lamp(FVector(0.0, -40.0, 100.0), 30.f, 140.f, FLinearColor(1.0f, 0.9f, 0.75f));
            Kit.Text(FVector(-38.0, 0.0, 60.0), TEXT("MARIBORSKI GLAS · ARHIV"), 6.f, Paper);
        }
    }

    void Build(AActor& Owner, USceneComponent& Root, MarburgumII::Gameplay::EPropKind Kind, const FPrimitives& Primitives)
    {
        using MarburgumII::Gameplay::EPropKind;
        FKit Kit(Owner, Root, Primitives);
        switch (Kind)
        {
        case EPropKind::OrderBook: OrderBook(Kit); break;
        case EPropKind::LightTable: LightTable(Kit); break;
        case EPropKind::StorageCage: StorageCage(Kit); break;
        case EPropKind::DateBoard: DateBoard(Kit); break;
        case EPropKind::Truck: Trucks(Kit); break;
        case EPropKind::ArchiveDesk: ArchiveDesk(Kit); break;
        case EPropKind::None: break;
        }
    }
}
