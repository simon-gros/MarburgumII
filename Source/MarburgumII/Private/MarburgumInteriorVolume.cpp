#include "MarburgumInteriorVolume.h"

#include "MarburgumInteriorCore.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"

namespace MarburgumInteriorVolumePrivate
{
    namespace MI = MarburgumInteriors;

    constexpr float WallThicknessCm = 12.f;

    FVector ToUe(double X, double Y, double Z)
    {
        return FVector(-Y * 100.0, X * 100.0, Z * 100.0);
    }

    const TCHAR* WallMaterial(MI::EUse Use)
    {
        switch (Use)
        {
        case MI::EUse::Commercial:
            return TEXT("/Game/Marburgum/Materials/Instances/building_envelope/MI_plaster_pale_yellow.MI_plaster_pale_yellow");
        case MI::EUse::Industrial:
            return TEXT("/Game/Marburgum/Materials/Instances/building_envelope/MI_plaster_render_grey.MI_plaster_render_grey");
        case MI::EUse::Public:
            return TEXT("/Game/Marburgum/Materials/Instances/building_envelope/MI_plaster_cream.MI_plaster_cream");
        default:
            return TEXT("/Game/Marburgum/Materials/Instances/building_envelope/MI_plaster_beige.MI_plaster_beige");
        }
    }

    // The stretch of edge E0-E1 (metres from E0) that segment A-B lies along, when A-B is on that edge's line.
    bool GapOnEdge(const MI::FVec2& E0, const MI::FVec2& E1, const MI::FVec2& A, const MI::FVec2& B, double& Out0, double& Out1)
    {
        const MI::FVec2 D = E1 - E0;
        const double Len = MI::Length(D);
        if (Len < 0.2)
        {
            return false;
        }
        const MI::FVec2 N = MI::Normalize(MI::FVec2{ -D.Y, D.X });
        if (std::fabs(MI::Dot(A - E0, N)) > 0.05 || std::fabs(MI::Dot(B - E0, N)) > 0.05)
        {
            return false;
        }
        double Ua = MI::Dot(A - E0, D) / (Len * Len);
        double Ub = MI::Dot(B - E0, D) / (Len * Len);
        if (Ua > Ub)
        {
            std::swap(Ua, Ub);
        }
        Out0 = std::clamp(Ua, 0.0, 1.0) * Len;
        Out1 = std::clamp(Ub, 0.0, 1.0) * Len;
        return Out1 - Out0 > 0.05;
    }

    // Sorted, merged intervals.
    TArray<TPair<double, double>> Merge(TArray<TPair<double, double>> In)
    {
        In.Sort([](const TPair<double, double>& A, const TPair<double, double>& B) { return A.Key < B.Key; });
        TArray<TPair<double, double>> Out;
        for (const TPair<double, double>& Item : In)
        {
            if (!Out.IsEmpty() && Item.Key <= Out.Last().Value + 1.0e-4)
            {
                Out.Last().Value = FMath::Max(Out.Last().Value, Item.Value);
            }
            else
            {
                Out.Add(Item);
            }
        }
        return Out;
    }
}

AMarburgumInteriorVolume::AMarburgumInteriorVolume()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = false;
    SetCanBeDamaged(false);
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Root->SetMobility(EComponentMobility::Static);
    SetRootComponent(Root);
}

void AMarburgumInteriorVolume::Build(const MarburgumInteriors::FLayout& Layout)
{
    using namespace MarburgumInteriorVolumePrivate;
    namespace MI = MarburgumInteriors;
    BuildingId = UTF8_TO_TCHAR(Layout.Id.c_str());

    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!Cube)
    {
        return;
    }
    const bool bLights = GetWorld() && GetWorld()->GetNetMode() != NM_DedicatedServer;

    // One instanced component per finish: the walls, the floors and the ceilings of the whole floor plan are
    // three draws, not one component per piece.
    auto MakeKind = [&](const TCHAR* Name, const TCHAR* MaterialPath) -> UInstancedStaticMeshComponent*
    {
        UInstancedStaticMeshComponent* Kind = NewObject<UInstancedStaticMeshComponent>(this, Name);
        Kind->SetStaticMesh(Cube);
        if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, MaterialPath))
        {
            Kind->SetMaterial(0, Material);
        }
        Kind->SetupAttachment(Root);
        Kind->SetMobility(EComponentMobility::Static);
        Kind->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
        Kind->SetCanEverAffectNavigation(false);
        Kind->SetCastShadow(false);
        // The room lamps are on channel 1 so they light these and the people in them, not the façades.
        Kind->SetLightingChannels(true, true, false);
        return Kind;
    };
    Walls = MakeKind(TEXT("Walls"), WallMaterial(Layout.Use));
    Floors = MakeKind(TEXT("Floors"), TEXT("/Game/Marburgum/Materials/Instances/building_envelope/MI_facade_socle.MI_facade_socle"));
    Ceilings = MakeKind(TEXT("Ceilings"), TEXT("/Game/Marburgum/Materials/Instances/building_envelope/MI_plaster_off_white.MI_plaster_off_white"));

    auto AddBox = [](UInstancedStaticMeshComponent* Kind, const FVector& Center, const FVector& AxisX, const FVector& SizeCm)
    {
        const FVector Flat = FVector(AxisX.X, AxisX.Y, 0.f).GetSafeNormal();
        const FRotator Yaw = Flat.IsNearlyZero() ? FRotator::ZeroRotator : Flat.Rotation();
        Kind->AddInstance(FTransform(Yaw, Center, SizeCm / 100.f), false);
    };

    // The street door's wall lies on the shell. Its partition is moved wholly inside, so none of it stands
    // proud of the façade outside.
    const MI::FOpening* Street = !Layout.Openings.empty() && Layout.Openings[0].bEntrance ? &Layout.Openings[0] : nullptr;

    for (int32 RoomIndex = 0; RoomIndex < static_cast<int32>(Layout.Rooms.size()); ++RoomIndex)
    {
        const MI::FRoom& Room = Layout.Rooms[static_cast<size_t>(RoomIndex)];
        const FVector C0 = ToUe(Room.Corner[0].X, Room.Corner[0].Y, 0.0);
        const FVector C1 = ToUe(Room.Corner[1].X, Room.Corner[1].Y, 0.0);
        const FVector C3 = ToUe(Room.Corner[3].X, Room.Corner[3].Y, 0.0);
        const FVector Along = C1 - C0;
        const FVector Across = C3 - C0;
        const FVector Center = (C0 + ToUe(Room.Corner[2].X, Room.Corner[2].Y, 0.0)) * 0.5;
        const float FloorTop = static_cast<float>(Room.FloorZ * 100.0);
        const float CeilingBottom = static_cast<float>(Room.CeilingZ * 100.0);
        AddBox(Floors, FVector(Center.X, Center.Y, FloorTop - 10.f), Along, FVector(Along.Size(), Across.Size(), 20.f));
        AddBox(Ceilings, FVector(Center.X, Center.Y, CeilingBottom + 8.f), Along, FVector(Along.Size(), Across.Size(), 16.f));

        if (bLights)
        {
            UPointLightComponent* Lamp = NewObject<UPointLightComponent>(this, *FString::Printf(TEXT("Lamp_%d"), RoomIndex));
            Lamp->SetupAttachment(Root);
            Lamp->SetRelativeLocation(FVector(Center.X, Center.Y, CeilingBottom - 25.f));
            Lamp->SetIntensity(1800.f);
            Lamp->SetAttenuationRadius(FMath::Max(Along.Size(), Across.Size()) * 0.75f + 100.f);
            Lamp->SetLightColor(FLinearColor(1.f, 0.85f, 0.65f));
            Lamp->SetCastShadows(false);
            Lamp->SetLightingChannels(false, true, false);
            Lamp->SetMobility(EComponentMobility::Movable);
            Lamp->RegisterComponent();
        }

        const MI::FVec2 RoomMid = MI::RoomMiddle(Room);
        for (int Edge = 0; Edge < 4; ++Edge)
        {
            const MI::FVec2& E0 = Room.Corner[Edge];
            const MI::FVec2& E1 = Room.Corner[(Edge + 1) % 4];
            const double EdgeLen = MI::Length(E1 - E0);
            if (EdgeLen < 0.05)
            {
                continue;
            }

            // A wall shared with an earlier room is that room's, but only along the stretch they share: a
            // narrow vestibule takes only its own width of the front room's street-side wall.
            TArray<TPair<double, double>> Taken;
            for (int32 Other = 0; Other < RoomIndex; ++Other)
            {
                const MI::FRoom& Neighbour = Layout.Rooms[static_cast<size_t>(Other)];
                for (int NeighbourEdge = 0; NeighbourEdge < 4; ++NeighbourEdge)
                {
                    double S0 = 0.0;
                    double S1 = 0.0;
                    if (GapOnEdge(E0, E1, Neighbour.Corner[NeighbourEdge], Neighbour.Corner[(NeighbourEdge + 1) % 4], S0, S1))
                    {
                        Taken.Emplace(S0, S1);
                    }
                }
            }
            Taken = Merge(Taken);

            struct FGap
            {
                double U0, U1, Top;
            };
            TArray<FGap> Gaps;
            bool bOnStreet = false;
            for (const MI::FOpening& Opening : Layout.Openings)
            {
                double U0 = 0.0;
                double U1 = 0.0;
                if (GapOnEdge(E0, E1, Opening.A, Opening.B, U0, U1))
                {
                    Gaps.Add({ U0, U1, Opening.Z1 * 100.0 });
                    bOnStreet |= &Opening == Street;
                }
            }
            Gaps.Sort([](const FGap& A, const FGap& B) { return A.U0 < B.U0; });

            const FVector Start = ToUe(E0.X, E0.Y, 0.0);
            const FVector EdgeDir = ToUe(E1.X, E1.Y, 0.0) - Start;
            // Into the room, horizontal, in centimetres per centimetre.
            const MI::FVec2 Mid2 = (E0 + E1) * 0.5;
            const FVector InRoom = (ToUe(RoomMid.X, RoomMid.Y, 0.0) - ToUe(Mid2.X, Mid2.Y, 0.0)).GetSafeNormal2D();
            const float Shift = bOnStreet ? WallThicknessCm * 0.5f + 0.5f : 0.f;
            const float WallHeight = CeilingBottom - FloorTop;
            auto Piece = [&](double From, double To, float Bottom, float Top)
            {
                if (To - From < 0.04 || Top - Bottom < 1.f)
                {
                    return;
                }
                // Skip what an earlier room already built on this line.
                TArray<TPair<double, double>> Pieces{ TPair<double, double>(From, To) };
                for (const TPair<double, double>& Owned : Taken)
                {
                    TArray<TPair<double, double>> Next;
                    for (const TPair<double, double>& P : Pieces)
                    {
                        if (Owned.Value <= P.Key || Owned.Key >= P.Value)
                        {
                            Next.Add(P);
                            continue;
                        }
                        if (Owned.Key > P.Key)
                        {
                            Next.Emplace(P.Key, Owned.Key);
                        }
                        if (Owned.Value < P.Value)
                        {
                            Next.Emplace(Owned.Value, P.Value);
                        }
                    }
                    Pieces = MoveTemp(Next);
                }
                for (const TPair<double, double>& P : Pieces)
                {
                    if (P.Value - P.Key < 0.04)
                    {
                        continue;
                    }
                    const double MidM = (P.Key + P.Value) * 0.5;
                    const FVector At = Start + EdgeDir * static_cast<float>(MidM / EdgeLen) + InRoom * Shift;
                    AddBox(Walls, FVector(At.X, At.Y, (Bottom + Top) * 0.5f), EdgeDir,
                        FVector(static_cast<float>((P.Value - P.Key) * 100.0), WallThicknessCm, Top - Bottom));
                }
            };
            double Cursor = 0.0;
            for (const FGap& Gap : Gaps)
            {
                Piece(Cursor, Gap.U0, FloorTop, FloorTop + WallHeight);
                // A lintel over every opening, up to the ceiling.
                Piece(Gap.U0, Gap.U1, static_cast<float>(Gap.Top), CeilingBottom);
                Cursor = FMath::Max(Cursor, Gap.U1);
            }
            Piece(Cursor, EdgeLen, FloorTop, FloorTop + WallHeight);
        }
    }

    for (UInstancedStaticMeshComponent* Kind : { Walls.Get(), Floors.Get(), Ceilings.Get() })
    {
        Kind->RegisterComponent();
    }
}
