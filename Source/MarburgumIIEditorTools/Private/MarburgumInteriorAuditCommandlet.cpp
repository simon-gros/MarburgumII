#include "MarburgumInteriorAuditCommandlet.h"

#include "AssetCompilingManager.h"
#include "CollisionQueryParams.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "MarburgumInteriorCore.h"
#include "MarburgumInteriorSubsystem.h"
#include "MarburgumInteriorVolume.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include <cmath>
#include <limits>

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumInteriorAudit, Log, All);

namespace MarburgumInteriorAudit
{
    namespace MI = MarburgumInteriors;

    FVector ToUe(const MI::FVec2& P, double Z)
    {
        return FVector(-P.Y * 100.0, P.X * 100.0, Z * 100.0);
    }

    FString Clean(const FString& Text)
    {
        return Text.Replace(TEXT("\t"), TEXT(" ")).Replace(TEXT("\n"), TEXT(" "));
    }

    // What a foreign actor in a room is: the ground, another building, or a placed thing.
    FString KindOf(const AActor* Actor, const TMap<FString, int32>& BuildingIndex)
    {
        if (!Actor)
        {
            return TEXT("unknown");
        }
        const FString Label = Actor->GetActorLabel();
        if (BuildingIndex.Contains(Label))
        {
            return TEXT("building");
        }
        if (Label.StartsWith(TEXT("MB2Ground")) || Actor->GetClass()->GetName().Contains(TEXT("Landscape")))
        {
            return TEXT("ground");
        }
        return TEXT("prop");
    }

    // The top of the ground (the ground meshes or a landscape, nothing else) at a plan point, or NaN.
    double GroundAt(UWorld& World, const MI::FVec2& P, double Around, const TMap<FString, int32>& BuildingIndex)
    {
        FCollisionObjectQueryParams Statics;
        Statics.AddObjectTypesToQuery(ECC_WorldStatic);
        TArray<FHitResult> Hits;
        const FVector Top = ToUe(P, Around + 40.0);
        const FVector Bottom = ToUe(P, Around - 40.0);
        World.LineTraceMultiByObjectType(Hits, Top, Bottom, Statics, FCollisionQueryParams(SCENE_QUERY_STAT(MarburgumInteriorGround), true));
        double Best = std::numeric_limits<double>::quiet_NaN();
        for (const FHitResult& Hit : Hits)
        {
            if (KindOf(Hit.GetActor(), BuildingIndex) == TEXT("ground"))
            {
                const double Z = Hit.ImpactPoint.Z / 100.0;
                Best = std::isfinite(Best) ? std::max(Best, Z) : Z;
            }
        }
        return Best;
    }

    struct FCounts
    {
        int32 Total = 0;
        int32 WithShellActor = 0;
        int32 Eligible = 0;
        int32 OpenAsBuilt = 0;
        int32 WalkAsBuilt = 0;
        int32 FunctionalDoors = 0;
        int32 OpenIfCut = 0;
        int32 WalkIfCut = 0;
        int32 RoomsInvaded = 0;
        int32 InvadedByGround = 0;
        int32 InvadedByBuilding = 0;
        int32 InvadedByProp = 0;
        int32 LeafBlocked = 0;
        int32 NoShellActor = 0;
        TMap<FString, int32> Exceptions;
        TMap<FString, int32> SealedIfCut;
        TMap<FString, int32> SealedAsBuilt;
        double BuildMs = 0.0;
        int32 Built = 0;
        int32 Instances = 0;
        int32 Rooms = 0;
        int32 Leaves = 0;
    };
}

UMarburgumInteriorAuditCommandlet::UMarburgumInteriorAuditCommandlet()
{
    IsClient = false;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
}

int32 UMarburgumInteriorAuditCommandlet::Main(const FString& Params)
{
    using namespace MarburgumInteriorAudit;
    FString Map = TEXT("/Game/World/lent");
    FParse::Value(*Params, TEXT("Map="), Map);
    FString Out;
    if (!FParse::Value(*Params, TEXT("Out="), Out))
    {
        Out = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Interiors"), TEXT("audit"), FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ")));
    }
    FString OnlyText;
    FParse::Value(*Params, TEXT("Only="), OnlyText);
    TArray<FString> Only;
    OnlyText.ParseIntoArray(Only, TEXT(","));

    const bool bWriteFloors = FParse::Param(*Params, TEXT("WriteFloors"));
    std::vector<MI::FPlan> Plans;
    TArray<FString> Names;
    const double ClassifyStart = FPlatformTime::Seconds();
    if (!UMarburgumInteriorSubsystem::ReadPlans(Plans, &Names, !bWriteFloors))
    {
        UE_LOG(LogMarburgumInteriorAudit, Error, TEXT("MB2INTERIOR_AUDIT cannot read buildings.json"));
        return 1;
    }
    std::vector<MI::FLayout> Layouts;
    Layouts.reserve(Plans.size());
    for (const MI::FPlan& Plan : Plans)
    {
        Layouts.push_back(MI::Generate(Plan));
    }
    const double ClassifySeconds = FPlatformTime::Seconds() - ClassifyStart;

    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Map);
    if (!World)
    {
        UE_LOG(LogMarburgumInteriorAudit, Error, TEXT("MB2INTERIOR_AUDIT cannot load %s"), *Map);
        return 1;
    }
    World->UpdateWorldComponents(true, false);
    FAssetCompilingManager::Get().FinishAllCompilation();

    TMap<FString, int32> BuildingIndex;
    for (int32 Index = 0; Index < static_cast<int32>(Plans.size()); ++Index)
    {
        BuildingIndex.Add(UTF8_TO_TCHAR(Plans[static_cast<size_t>(Index)].Id.c_str()), Index);
    }
    TMap<FString, AActor*> Shells;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (BuildingIndex.Contains(It->GetActorLabel()))
        {
            Shells.Add(It->GetActorLabel(), *It);
        }
    }

    FCollisionObjectQueryParams Statics;
    Statics.AddObjectTypesToQuery(ECC_WorldStatic);

    if (bWriteFloors)
    {
        // Measure every eligible building's floor against the level's own ground: high enough to clear the ground
        // under every room, and the street at the door. Refuse a building whose rooms another building or a prop
        // stands in, or whose doorway something else blocks once its own wall is cut.
        TSharedRef<FJsonObject> Floors = MakeShared<FJsonObject>();
        int32 Measured = 0;
        int32 Refused = 0;
        int32 Stepped = 0;
        int32 Confined = 0;
        for (int32 Index = 0; Index < static_cast<int32>(Plans.size()); ++Index)
        {
            MI::FPlan Plan = Plans[static_cast<size_t>(Index)];
            const MI::FLayout First = MI::Generate(Plan);
            if (First.Status != MI::EStatus::Eligible)
            {
                continue;
            }
            const FString Id = UTF8_TO_TCHAR(Plan.Id.c_str());
            AActor* const* ShellFound = Shells.Find(Id);
            const MI::FOpening& Door = First.Openings[0];
            const MI::FVec2 Dir = MI::Normalize(Door.B - Door.A);
            MI::FVec2 Into{ -Dir.Y, Dir.X };
            if (MI::Dot(MI::RoomMiddle(First.Rooms[0]) - (Door.A + Door.B) * 0.5, Into) < 0.0)
            {
                Into = Into * -1.0;
            }
            const MI::FVec2 Mid = (Door.A + Door.B) * 0.5;
            double Street = std::numeric_limits<double>::quiet_NaN();
            for (const double Side : { -0.45, 0.0, 0.45 })
            {
                const double Z = GroundAt(*World, Mid - Into * 0.60 + Dir * Side, Plan.Ground, BuildingIndex);
                if (std::isfinite(Z))
                {
                    Street = std::isfinite(Street) ? std::max(Street, Z) : Z;
                }
            }
            auto MaxGroundUnder = [&](const MI::FLayout& Layout)
            {
                double Highest = std::numeric_limits<double>::quiet_NaN();
                for (const MI::FRoom& Room : Layout.Rooms)
                {
                    const MI::FVec2 Along = Room.Corner[1] - Room.Corner[0];
                    const MI::FVec2 Across = Room.Corner[3] - Room.Corner[0];
                    const int32 NU = FMath::Max(1, FMath::CeilToInt32(MI::Length(Along) / 0.25));
                    const int32 NV = FMath::Max(1, FMath::CeilToInt32(MI::Length(Across) / 0.25));
                    for (int32 U = 0; U <= NU; ++U)
                    {
                        for (int32 V = 0; V <= NV; ++V)
                        {
                            const MI::FVec2 P = Room.Corner[0] + Along * (static_cast<double>(U) / NU) + Across * (static_cast<double>(V) / NV);
                            const double Z = GroundAt(*World, P, Plan.Ground, BuildingIndex);
                            if (std::isfinite(Z))
                            {
                                Highest = std::isfinite(Highest) ? std::max(Highest, Z) : Z;
                            }
                        }
                    }
                }
                return Highest;
            };
            double Inside = MaxGroundUnder(First);
            // On a slope the ground under the back of the plan rises more than a step above the street. Keep the
            // rooms to the largest rectangle, from the door wall in, whose ground stays within a step of the street.
            bool bRestricted = false;
            if (std::isfinite(Street) && std::isfinite(Inside) && Inside + MI::FloorClearance - Street > MI::MaxDoorStep)
            {
                const MI::FVec2 A = Plan.Outline[static_cast<size_t>(First.EntranceEdge)];
                const MI::FVec2 B = Plan.Outline[(static_cast<size_t>(First.EntranceEdge) + 1) % Plan.Outline.size()];
                const MI::FVec2 Along = MI::Normalize(B - A);
                const MI::FVec2 Inward{ -Along.Y, Along.X };
                const double DoorU = MI::Length(B - A) * 0.5;
                double U0 = 1.0e9, U1 = -1.0e9, V1 = 0.0;
                for (const MI::FRoom& Room : First.Rooms)
                {
                    for (const MI::FVec2& C : Room.Corner)
                    {
                        U0 = std::min(U0, MI::Dot(C - A, Along));
                        U1 = std::max(U1, MI::Dot(C - A, Along));
                        V1 = std::max(V1, MI::Dot(C - A, Inward));
                    }
                }
                constexpr double Step = 0.25;
                const int32 Cols = FMath::Max(1, FMath::FloorToInt32((U1 - U0) / Step) + 1);
                const int32 Rows = FMath::Max(1, FMath::FloorToInt32(V1 / Step) + 1);
                const double Limit = Street + MI::MaxDoorStep - MI::FloorClearance - 0.02;
                // Run of low samples from the door wall inward, per column.
                TArray<int32> Run;
                Run.Init(0, Cols);
                for (int32 C = 0; C < Cols; ++C)
                {
                    for (int32 R = 0; R < Rows; ++R)
                    {
                        const double Z = GroundAt(*World, A + Along * (U0 + C * Step) + Inward * (R * Step), Plan.Ground, BuildingIndex);
                        if (std::isfinite(Z) && Z > Limit)
                        {
                            break;
                        }
                        ++Run[C];
                    }
                }
                const int32 DoorCol = FMath::Clamp(FMath::RoundToInt32((DoorU - U0) / Step), 0, Cols - 1);
                double BestArea = 0.0;
                int32 BestL = DoorCol, BestR = DoorCol, BestH = 0;
                for (int32 L = DoorCol; L >= 0; --L)
                {
                    int32 H = TNumericLimits<int32>::Max();
                    for (int32 C = L; C <= DoorCol; ++C)
                    {
                        H = FMath::Min(H, Run[C]);
                    }
                    for (int32 R = DoorCol; R < Cols; ++R)
                    {
                        H = FMath::Min(H, Run[R]);
                        const double Area = (R - L) * Step * (H - 1) * Step;
                        if (H > 1 && Area > BestArea)
                        {
                            BestArea = Area;
                            BestL = L;
                            BestR = R;
                            BestH = H;
                        }
                    }
                }
                if (BestArea > 0.0)
                {
                    Plan.WithinU0 = FMath::RoundToDouble((U0 + BestL * Step) * 100.0) / 100.0;
                    Plan.WithinU1 = FMath::RoundToDouble((U0 + BestR * Step) * 100.0) / 100.0;
                    Plan.WithinV0 = 0.0;
                    Plan.WithinV1 = FMath::RoundToDouble(((BestH - 1) * Step) * 100.0) / 100.0;
                    const MI::FLayout Low = MI::Generate(Plan);
                    if (Low.Status == MI::EStatus::Eligible)
                    {
                        bRestricted = true;
                        Inside = MaxGroundUnder(Low);
                    }
                }
                if (!bRestricted)
                {
                    Plan.WithinU0 = std::numeric_limits<double>::quiet_NaN();
                }
            }
            TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
            FString Refusal;
            if (!std::isfinite(Street))
            {
                Refusal = TEXT("no street in front of the door");
            }
            else
            {
                const double Highest = std::isfinite(Inside) ? std::max(Inside, Street) : Street;
                Plan.Floor = FMath::CeilToDouble((Highest + MI::FloorClearance) * 100.0) / 100.0;
                Plan.Street = FMath::RoundToDouble(Street * 100.0) / 100.0;
                Entry->SetNumberField(TEXT("floor"), Plan.Floor);
                Entry->SetNumberField(TEXT("street"), Plan.Street);
                if (bRestricted)
                {
                    TArray<TSharedPtr<FJsonValue>> Within;
                    for (const double Value : { Plan.WithinU0, Plan.WithinU1, Plan.WithinV0, Plan.WithinV1 })
                    {
                        Within.Add(MakeShared<FJsonValueNumber>(Value));
                    }
                    Entry->SetArrayField(TEXT("rooms_within"), Within);
                    ++Confined;
                }
                if (std::isfinite(Inside))
                {
                    Entry->SetNumberField(TEXT("ground_inside"), FMath::RoundToDouble(Inside * 100.0) / 100.0);
                }
                const MI::FLayout Raised = MI::Generate(Plan);
                if (Raised.Status != MI::EStatus::Eligible)
                {
                    ++Stepped;
                }
                else
                {
                    FCollisionQueryParams OwnShellAside(SCENE_QUERY_STAT(MarburgumInteriorFloors), false);
                    if (ShellFound)
                    {
                        OwnShellAside.AddIgnoredActor(*ShellFound);
                    }
                    for (const MI::FRoom& Room : Raised.Rooms)
                    {
                        const FVector C0 = ToUe(Room.Corner[0], Room.FloorZ);
                        const FVector C1 = ToUe(Room.Corner[1], Room.FloorZ);
                        const FVector C3 = ToUe(Room.Corner[3], Room.FloorZ);
                        const FVector Centre = (C0 + ToUe(Room.Corner[2], Room.FloorZ)) * 0.5;
                        const float Height = static_cast<float>((Room.CeilingZ - Room.FloorZ) * 100.0);
                        const FVector Half(FMath::Max(1.f, static_cast<float>((C1 - C0).Size()) * 0.5f - 5.f),
                            FMath::Max(1.f, static_cast<float>((C3 - C0).Size()) * 0.5f - 5.f), Height * 0.5f - 5.f);
                        TArray<FOverlapResult> Overlaps;
                        World->OverlapMultiByObjectType(Overlaps, Centre + FVector(0, 0, Height * 0.5f), FRotator(0.f, (C1 - C0).Rotation().Yaw, 0.f).Quaternion(),
                            Statics, FCollisionShape::MakeBox(Half), OwnShellAside);
                        for (const FOverlapResult& Overlap : Overlaps)
                        {
                            const FString Kind = KindOf(Overlap.GetActor(), BuildingIndex);
                            if (Kind == TEXT("building") && Refusal.IsEmpty())
                            {
                                Refusal = FString::Printf(TEXT("another building stands in the rooms (%s)"), *Overlap.GetActor()->GetActorLabel());
                            }
                            else if (Kind == TEXT("prop") && Refusal.IsEmpty())
                            {
                                Refusal = FString::Printf(TEXT("something stands in the rooms (%s)"), Overlap.GetActor() ? *Overlap.GetActor()->GetActorLabel() : TEXT("?"));
                            }
                        }
                    }
                    if (Refusal.IsEmpty())
                    {
                        const UMarburgumInteriorSubsystem::FEntry IfCut = UMarburgumInteriorSubsystem::ProbeEntrance(*World, Raised, ShellFound ? *ShellFound : nullptr);
                        if (!IfCut.bOpen)
                        {
                            Refusal = FString::Printf(TEXT("doorway blocked: %s%s%s"), *IfCut.Reason, IfCut.Blocker.IsEmpty() ? TEXT("") : TEXT(" by "), *IfCut.Blocker);
                        }
                    }
                }
            }
            if (!Refusal.IsEmpty())
            {
                Entry->SetStringField(TEXT("refused"), Refusal);
                ++Refused;
            }
            Floors->SetObjectField(Id, Entry);
            ++Measured;
        }
        TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetStringField(TEXT("about"), TEXT("Ground-floor levels measured in the level by MarburgumInteriorAudit -WriteFloors (ADR-076): floor clears the highest ground under the rooms and the street at the door by 3 cm; street is the highest ground 0.6 m in front of the door; refused names what blocks a building. Regenerate after a ground, facade or level change."));
        Root->SetStringField(TEXT("map"), Map);
        Root->SetObjectField(TEXT("floors"), Floors);
        FString Json;
        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
        FJsonSerializer::Serialize(Root, Writer);
        const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/World/lent_ground/interior_floors.json"));
        const bool bSaved = FFileHelper::SaveStringToFile(Json + TEXT("\n"), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        UE_LOG(LogMarburgumInteriorAudit, Display, TEXT("MB2INTERIOR_AUDIT floors measured=%d refused=%d too_steep=%d confined=%d written=%d path=%s"), Measured, Refused, Stepped, Confined,
            bSaved ? 1 : 0, *Path);
        return bSaved ? 0 : 1;
    }

    const float Radius = UMarburgumInteriorSubsystem::PlayerRadiusCm;
    const float HalfHeight = UMarburgumInteriorSubsystem::PlayerHalfHeightCm;
    const FCollisionShape Capsule = FCollisionShape::MakeCapsule(Radius, HalfHeight);
    const FVector Lift(0, 0, HalfHeight + 6.f);

    FString Buildings = TEXT("id\tname\tkind\tuse\tdoor_x\tdoor_y\tdoor_z\tstatus\treason\trooms\tfloors_noted\tshell_actor\tentrance\tentrance_reason\tstep_cm\tif_cut\tif_cut_reason\tinvaded\tinvaders\twalk\twalk_detail\tleaves\tleaf_detail\tbuild_ms\tinstances\n");
    FString RoomsTsv = TEXT("id\troom\twidth_m\tdepth_m\tfloor_z\tceiling_z\tinvaders\n");
    FCounts Counts;
    for (int32 Index = 0; Index < static_cast<int32>(Plans.size()); ++Index)
    {
        const MI::FPlan& Plan = Plans[static_cast<size_t>(Index)];
        const MI::FLayout& Layout = Layouts[static_cast<size_t>(Index)];
        const FString Id = UTF8_TO_TCHAR(Plan.Id.c_str());
        if (!Only.IsEmpty() && !Only.Contains(Id))
        {
            continue;
        }
        ++Counts.Total;
        AActor* const* ShellFound = Shells.Find(Id);
        AActor* Shell = ShellFound ? *ShellFound : nullptr;
        Counts.WithShellActor += Shell ? 1 : 0;
        const bool bEligible = Layout.Status == MI::EStatus::Eligible;
        FString Row = FString::Printf(TEXT("%s\t%s\t%hs\t%hs\t"), *Id, *Clean(Names.IsValidIndex(Index) ? Names[Index] : FString()), Plan.Kind.c_str(), MI::UseName(Layout.Use));
        if (!bEligible)
        {
            Counts.Exceptions.FindOrAdd(UTF8_TO_TCHAR(Layout.Reason.c_str()))++;
            Row += FString::Printf(TEXT("\t\t\texception\t%hs\t0\t%d\t%s\t\t\t\t\t\t\t\t\t\t\t\t\t0\n"), Layout.Reason.c_str(), Layout.FloorsNoted, Shell ? TEXT("yes") : TEXT("no"));
            Buildings += Row;
            continue;
        }
        ++Counts.Eligible;
        if (!Shell)
        {
            ++Counts.NoShellActor;
        }
        const MI::FOpening& Street = Layout.Openings[0];
        const FVector DoorAt = ToUe((Street.A + Street.B) * 0.5, Street.Z0);
        Row += FString::Printf(TEXT("%.0f\t%.0f\t%.0f\teligible\t\t%d\t%d\t%s\t"), DoorAt.X, DoorAt.Y, DoorAt.Z, static_cast<int32>(Layout.Rooms.size()), Layout.FloorsNoted,
            Shell ? TEXT("yes") : TEXT("no"));

        // The door as the level stands, and with this building's own shell set aside (as if its door were cut).
        const UMarburgumInteriorSubsystem::FEntry AsBuilt = UMarburgumInteriorSubsystem::ProbeEntrance(*World, Layout);
        const UMarburgumInteriorSubsystem::FEntry IfCut = UMarburgumInteriorSubsystem::ProbeEntrance(*World, Layout, Shell);
        Counts.OpenAsBuilt += AsBuilt.bOpen ? 1 : 0;
        Counts.OpenIfCut += IfCut.bOpen ? 1 : 0;
        if (!AsBuilt.bOpen)
        {
            Counts.SealedAsBuilt.FindOrAdd(AsBuilt.Reason)++;
        }
        if (!IfCut.bOpen)
        {
            Counts.SealedIfCut.FindOrAdd(IfCut.Reason)++;
        }
        Row += FString::Printf(TEXT("%s\t%s\t%.0f\t%s\t%s%s%s\t"), AsBuilt.bOpen ? TEXT("open") : TEXT("sealed"), *AsBuilt.Reason, AsBuilt.StepCm,
            IfCut.bOpen ? TEXT("open") : TEXT("sealed"), *IfCut.Reason, IfCut.Blocker.IsEmpty() ? TEXT("") : TEXT(" by "), *IfCut.Blocker);

        // Anything but this building's own shell between each room's floor and ceiling.
        FCollisionQueryParams OwnShellAside(SCENE_QUERY_STAT(MarburgumInteriorAudit), false);
        if (Shell)
        {
            OwnShellAside.AddIgnoredActor(Shell);
        }
        TSet<FString> Invaders;
        bool bGround = false;
        bool bBuilding = false;
        bool bProp = false;
        for (int32 RoomIndex = 0; RoomIndex < static_cast<int32>(Layout.Rooms.size()); ++RoomIndex)
        {
            const MI::FRoom& Room = Layout.Rooms[static_cast<size_t>(RoomIndex)];
            const FVector C0 = ToUe(Room.Corner[0], Room.FloorZ);
            const FVector C1 = ToUe(Room.Corner[1], Room.FloorZ);
            const FVector C3 = ToUe(Room.Corner[3], Room.FloorZ);
            const FVector Centre = (C0 + ToUe(Room.Corner[2], Room.FloorZ)) * 0.5;
            const float Height = static_cast<float>((Room.CeilingZ - Room.FloorZ) * 100.0);
            const FVector Half(FMath::Max(1.f, static_cast<float>((C1 - C0).Size()) * 0.5f - 5.f), FMath::Max(1.f, static_cast<float>((C3 - C0).Size()) * 0.5f - 5.f),
                Height * 0.5f - 5.f);
            const FQuat Turn = FRotator(0.f, (C1 - C0).Rotation().Yaw, 0.f).Quaternion();
            TArray<FOverlapResult> Overlaps;
            World->OverlapMultiByObjectType(Overlaps, Centre + FVector(0, 0, Height * 0.5f), Turn, Statics, FCollisionShape::MakeBox(Half), OwnShellAside);
            TSet<FString> Here;
            for (const FOverlapResult& Overlap : Overlaps)
            {
                const AActor* Other = Overlap.GetActor();
                const FString Kind = KindOf(Other, BuildingIndex);
                bGround |= Kind == TEXT("ground");
                bBuilding |= Kind == TEXT("building");
                bProp |= Kind == TEXT("prop");
                const FString Name = FString::Printf(TEXT("%s:%s"), *Kind, Other ? *Other->GetActorLabel() : TEXT("?"));
                Here.Add(Name);
                Invaders.Add(Name);
            }
            RoomsTsv += FString::Printf(TEXT("%s\t%d\t%.2f\t%.2f\t%.2f\t%.2f\t%s\n"), *Id, RoomIndex, (C1 - C0).Size() / 100.0, (C3 - C0).Size() / 100.0, Room.FloorZ,
                Room.CeilingZ, *FString::Join(Here.Array(), TEXT(",")));
        }
        Counts.RoomsInvaded += Invaders.IsEmpty() ? 0 : 1;
        Counts.InvadedByGround += bGround ? 1 : 0;
        Counts.InvadedByBuilding += bBuilding ? 1 : 0;
        Counts.InvadedByProp += bProp ? 1 : 0;
        Row += FString::Printf(TEXT("%d\t%s\t"), Invaders.Num(), *FString::Join(Invaders.Array(), TEXT(",")));

        // Build the rooms here, then walk them: every leg a player's capsule, with the leaves open (no doors spawned).
        FActorSpawnParameters Spawn;
        Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Spawn.ObjectFlags |= RF_Transient;
        const double BuildStart = FPlatformTime::Seconds();
        AMarburgumInteriorVolume* Volume = World->SpawnActor<AMarburgumInteriorVolume>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
        if (Volume)
        {
            Volume->Build(Layout);
        }
        const double BuildMs = (FPlatformTime::Seconds() - BuildStart) * 1000.0;
        int32 Instances = 0;
        if (Volume)
        {
            TArray<UInstancedStaticMeshComponent*> Kinds;
            Volume->GetComponents(Kinds);
            for (const UInstancedStaticMeshComponent* Kind : Kinds)
            {
                Instances += Kind->GetInstanceCount();
            }
        }
        Counts.BuildMs += BuildMs;
        Counts.Instances += Instances;
        Counts.Rooms += static_cast<int32>(Layout.Rooms.size());
        ++Counts.Built;

        const bool bCutMode = !AsBuilt.bOpen;
        FCollisionQueryParams Walk(SCENE_QUERY_STAT(MarburgumInteriorWalk), false);
        if (bCutMode && Shell)
        {
            Walk.AddIgnoredActor(Shell);
        }
        const MI::FVec2 Dir = MI::Normalize(Street.B - Street.A);
        MI::FVec2 Into{ -Dir.Y, Dir.X };
        if (MI::Dot(MI::RoomMiddle(Layout.Rooms[0]) - (Street.A + Street.B) * 0.5, Into) < 0.0)
        {
            Into = Into * -1.0;
        }
        const double Floor = Street.Z0;
        auto At = [&](const MI::FVec2& P) { return ToUe(P, Floor) + Lift; };
        FString WalkDetail;
        auto Leg = [&](const FVector& From, const FVector& To, const TCHAR* Name) -> bool
        {
            FHitResult Hit;
            if (World->SweepSingleByObjectType(Hit, From, To, FQuat::Identity, Statics, Capsule, Walk))
            {
                WalkDetail = FString::Printf(TEXT("%s blocked by %s%s"), Name, Hit.GetActor() ? *Hit.GetActor()->GetActorLabel() : TEXT("?"),
                    Hit.bStartPenetrating ? TEXT(" (start)") : TEXT(""));
                return false;
            }
            return true;
        };
        const MI::FVec2 DoorMid = (Street.A + Street.B) * 0.5;
        // Straight in from the street, through the door and the vestibule, to a point 0.6 m into the front room.
        const double Vestibule = Layout.Openings.size() > 1 && !Layout.Openings[1].bLeaf
            ? MI::Dot((Layout.Openings[1].A + Layout.Openings[1].B) * 0.5 - DoorMid, Into) : 0.0;
        const MI::FVec2 Inside = DoorMid + Into * (Vestibule + 0.6);
        bool bWalk = Leg(At(DoorMid - Into * 0.8), At(Inside), TEXT("street->front room")) && Leg(At(Inside), At(DoorMid - Into * 0.8), TEXT("front room->street"));
        // From there to each room door, squarely through it into the room's middle, and back.
        for (size_t O = 1; bWalk && O < Layout.Openings.size(); ++O)
        {
            const MI::FOpening& Opening = Layout.Openings[O];
            if (!Opening.bLeaf)
            {
                continue;
            }
            const MI::FVec2 Mid = (Opening.A + Opening.B) * 0.5;
            const MI::FVec2 Across = MI::Normalize(Opening.B - Opening.A);
            MI::FVec2 Normal{ -Across.Y, Across.X };
            const MI::FVec2 Far = MI::RoomMiddle(Layout.Rooms[static_cast<size_t>(Opening.RoomB)]);
            if (MI::Dot(Far - Mid, Normal) < 0.0)
            {
                Normal = Normal * -1.0;
            }
            const MI::FVec2 Before = Mid - Normal * 0.6;
            const MI::FVec2 After = Mid + Normal * 0.6;
            const FString Name = FString::Printf(TEXT("door %d"), static_cast<int32>(O));
            bWalk = Leg(At(Inside), At(Before), *Name) && Leg(At(Before), At(After), *Name) && Leg(At(After), At(Far), *Name)
                && Leg(At(Far), At(After), *Name) && Leg(At(After), At(Before), *Name) && Leg(At(Before), At(Inside), *Name);
        }
        // Somewhere to stand and turn in every room.
        for (size_t R = Vestibule > 0.0 ? 1 : 0; bWalk && R < Layout.Rooms.size(); ++R)
        {
            TArray<FOverlapResult> Overlaps;
            const FVector Stand = At(MI::RoomMiddle(Layout.Rooms[R]));
            if (World->OverlapMultiByObjectType(Overlaps, Stand, FQuat::Identity, Statics, Capsule, Walk))
            {
                WalkDetail = FString::Printf(TEXT("cannot stand in room %d (%s)"), static_cast<int32>(R), Overlaps[0].GetActor() ? *Overlaps[0].GetActor()->GetActorLabel() : TEXT("?"));
                bWalk = false;
            }
        }
        if (AsBuilt.bOpen)
        {
            Counts.WalkAsBuilt += bWalk ? 1 : 0;
        }
        else
        {
            Counts.WalkIfCut += bWalk && IfCut.bOpen ? 1 : 0;
        }
        Row += FString::Printf(TEXT("%s%s\t%s\t"), bWalk ? TEXT("ok") : TEXT("fail"), bCutMode ? TEXT("_if_cut") : TEXT(""), *WalkDetail);

        // Each leaf shut and fully open: clear of everything except its own shell's doorway.
        int32 LeavesClear = 0;
        int32 LeavesTotal = 0;
        FString LeafDetail;
        for (const MI::FOpening& Opening : Layout.Openings)
        {
            const MI::FLeaf Leaf = MI::LeafFor(Layout, Opening);
            if (!Leaf.bValid)
            {
                continue;
            }
            ++LeavesTotal;
            bool bClear = true;
            for (int Pose = 0; Pose < 2 && bClear; ++Pose)
            {
                const MI::FVec2 Tip = Pose == 0 ? Leaf.Hinge + Leaf.ClosedDir * Leaf.Width : MI::OpenLeafTip(Leaf);
                const FVector H = ToUe(Leaf.Hinge, Leaf.Z);
                const FVector T = ToUe(Tip, Leaf.Z);
                const FVector Centre = (H + T) * 0.5 + FVector(0, 0, Leaf.Height * 50.0);
                const FVector Half(static_cast<float>(Leaf.Width * 50.0) - 2.f, MI::LeafThickness * 50.0 - 0.5, Leaf.Height * 50.0 - 2.0);
                const FQuat Turn = FRotator(0.f, (T - H).Rotation().Yaw, 0.f).Quaternion();
                TArray<FOverlapResult> Overlaps;
                if (World->OverlapMultiByObjectType(Overlaps, Centre, Turn, Statics, FCollisionShape::MakeBox(Half), Walk))
                {
                    bClear = false;
                    LeafDetail += FString::Printf(TEXT("%s %s hits %s; "), Opening.bEntrance ? TEXT("street") : TEXT("inside"), Pose == 0 ? TEXT("shut") : TEXT("open"),
                        Overlaps[0].GetActor() ? *Overlaps[0].GetActor()->GetActorLabel() : TEXT("?"));
                }
            }
            LeavesClear += bClear ? 1 : 0;
        }
        Counts.Leaves += LeavesTotal;
        Counts.LeafBlocked += LeavesClear < LeavesTotal ? 1 : 0;
        if (AsBuilt.bOpen && bWalk && LeavesClear == LeavesTotal)
        {
            ++Counts.FunctionalDoors;
        }
        Row += FString::Printf(TEXT("%d/%d\t%s\t%.2f\t%d\n"), LeavesClear, LeavesTotal, *LeafDetail, BuildMs, Instances);
        Buildings += Row;
        UE_LOG(LogMarburgumInteriorAudit, Display, TEXT("MB2INTERIOR_AUDIT id=%s entrance=%s if_cut=%s invaded=%d walk=%s leaves=%d/%d"), *Id,
            AsBuilt.bOpen ? TEXT("open") : TEXT("sealed"), IfCut.bOpen ? TEXT("open") : TEXT("sealed"), Invaders.Num(), bWalk ? TEXT("ok") : TEXT("fail"), LeavesClear, LeavesTotal);
        if (Volume)
        {
            World->DestroyActor(Volume);
        }
    }

    FString Summary;
    Summary += FString::Printf(TEXT("map\t%s\nfootprints\t%d\nwith_shell_actor\t%d\neligible\t%d\neligible_without_shell_actor\t%d\n"), *Map, Counts.Total, Counts.WithShellActor,
        Counts.Eligible, Counts.NoShellActor);
    Summary += FString::Printf(TEXT("entrance_open_as_built\t%d\nwalk_ok_as_built\t%d\nfunctional_doors_as_built\t%d\n"), Counts.OpenAsBuilt, Counts.WalkAsBuilt, Counts.FunctionalDoors);
    Summary += FString::Printf(TEXT("entrance_open_if_cut\t%d\nwalk_ok_if_cut\t%d\n"), Counts.OpenIfCut, Counts.WalkIfCut);
    Summary += FString::Printf(TEXT("rooms_invaded\t%d\ninvaded_by_ground\t%d\ninvaded_by_building\t%d\ninvaded_by_prop\t%d\nleaf_blocked\t%d\n"), Counts.RoomsInvaded,
        Counts.InvadedByGround, Counts.InvadedByBuilding, Counts.InvadedByProp, Counts.LeafBlocked);
    Summary += FString::Printf(TEXT("classify_seconds_all\t%.3f\nbuild_ms_mean\t%.3f\nrooms_mean\t%.2f\ninstances_mean\t%.1f\nleaves_mean\t%.2f\n"), ClassifySeconds,
        Counts.Built ? Counts.BuildMs / Counts.Built : 0.0, Counts.Built ? static_cast<double>(Counts.Rooms) / Counts.Built : 0.0,
        Counts.Built ? static_cast<double>(Counts.Instances) / Counts.Built : 0.0, Counts.Built ? static_cast<double>(Counts.Leaves) / Counts.Built : 0.0);
    for (const TPair<FString, int32>& Pair : Counts.Exceptions)
    {
        Summary += FString::Printf(TEXT("exception\t%s\t%d\n"), *Pair.Key, Pair.Value);
    }
    for (const TPair<FString, int32>& Pair : Counts.SealedAsBuilt)
    {
        Summary += FString::Printf(TEXT("sealed_as_built\t%s\t%d\n"), *Pair.Key, Pair.Value);
    }
    for (const TPair<FString, int32>& Pair : Counts.SealedIfCut)
    {
        Summary += FString::Printf(TEXT("sealed_if_cut\t%s\t%d\n"), *Pair.Key, Pair.Value);
    }
    IFileManager::Get().MakeDirectory(*Out, true);
    const bool bWritten = FFileHelper::SaveStringToFile(Buildings, *FPaths::Combine(Out, TEXT("buildings.tsv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
        && FFileHelper::SaveStringToFile(RoomsTsv, *FPaths::Combine(Out, TEXT("rooms.tsv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
        && FFileHelper::SaveStringToFile(Summary, *FPaths::Combine(Out, TEXT("summary.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    TArray<FString> Lines;
    Summary.ParseIntoArrayLines(Lines);
    for (const FString& Line : Lines)
    {
        UE_LOG(LogMarburgumInteriorAudit, Display, TEXT("MB2INTERIOR_AUDIT summary %s"), *Line.Replace(TEXT("\t"), TEXT(" ")));
    }
    UE_LOG(LogMarburgumInteriorAudit, Display, TEXT("MB2INTERIOR_AUDIT out=%s written=%d"), *Out, bWritten ? 1 : 0);
    return bWritten ? 0 : 1;
}
