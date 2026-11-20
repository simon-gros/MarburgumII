#include "MarburgumInteriorSubsystem.h"

#include "MarburgumCharacter.h"
#include "MarburgumDoor.h"
#include "MarburgumInteriorCore.h"
#include "MarburgumInteriorVolume.h"
#include "CollisionQueryParams.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumInteriors, Log, All);

namespace MarburgumInteriorRuntime
{
    namespace MI = MarburgumInteriors;

    FVector ToUe(const MI::FVec2& P, double Z)
    {
        return FVector(-P.Y * 100.0, P.X * 100.0, Z * 100.0);
    }

    MI::FVec2 PlanOf(const FVector& Ue)
    {
        return { Ue.Y / 100.0, -Ue.X / 100.0 };
    }

    // Across the street door into the building, unit length in plan.
    MI::FVec2 IntoBuilding(const MI::FLayout& Layout)
    {
        const MI::FOpening& Door = Layout.Openings[0];
        const MI::FVec2 Dir = MI::Normalize(Door.B - Door.A);
        MI::FVec2 Into{ -Dir.Y, Dir.X };
        if (MI::Dot(MI::RoomMiddle(Layout.Rooms[0]) - (Door.A + Door.B) * 0.5, Into) < 0.0)
        {
            Into = Into * -1.0;
        }
        return Into;
    }

    double DistanceToPlan(const MI::FPlan& Plan, const MI::FVec2& Point)
    {
        if (MI::PointInside(Plan.Outline, Point))
        {
            return 0.0;
        }
        double Best = 1.0e9;
        const size_t N = Plan.Outline.size();
        for (size_t I = 0; I < N; ++I)
        {
            const MI::FVec2 A = Plan.Outline[I];
            const MI::FVec2 B = Plan.Outline[(I + 1) % N];
            const MI::FVec2 AB = B - A;
            const double L2 = MI::Dot(AB, AB);
            const double T = L2 > 1.0e-12 ? std::clamp(MI::Dot(Point - A, AB) / L2, 0.0, 1.0) : 0.0;
            Best = std::min(Best, MI::Length(Point - (A + AB * T)));
        }
        return Best;
    }
}

FString UMarburgumInteriorSubsystem::RuntimeFile(const TCHAR* Name)
{
    return FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Marburgum/Interiors"), Name);
}

FString UMarburgumInteriorSubsystem::SourceFile(const TCHAR* Name)
{
    return FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/World/lent_ground"), Name);
}

bool UMarburgumInteriorSubsystem::ReadPlans(std::vector<MarburgumInteriors::FPlan>& OutPlans, TArray<FString>* OutNames, bool bMeasured)
{
    TSharedPtr<FJsonObject> Floors;
    if (bMeasured)
    {
        FString FloorsText;
        TSharedPtr<FJsonObject> FloorsRoot;
        const FString FloorsPath = RuntimeFile(TEXT("interior_floors.json"));
        if (FFileHelper::LoadFileToString(FloorsText, *FloorsPath) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FloorsText), FloorsRoot)
            && FloorsRoot.IsValid())
        {
            const TSharedPtr<FJsonObject>* Found = nullptr;
            if (FloorsRoot->TryGetObjectField(TEXT("floors"), Found) && Found)
            {
                Floors = *Found;
            }
        }
    }
    const FString Path = RuntimeFile(TEXT("buildings.json"));
    FString Text;
    TSharedPtr<FJsonObject> Root;
    if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("buildings")))
    {
        const TSharedPtr<FJsonObject> Object = Value->AsObject();
        if (!Object.IsValid())
        {
            continue;
        }
        MarburgumInteriors::FPlan Plan;
        Plan.Id = TCHAR_TO_UTF8(*Object->GetStringField(TEXT("id")));
        FString Kind;
        Object->TryGetStringField(TEXT("kind"), Kind);
        Plan.Kind = TCHAR_TO_UTF8(*Kind);
        double Number = 0.0;
        Plan.Area = Object->TryGetNumberField(TEXT("area"), Number) ? Number : 0.0;
        Plan.Ground = Object->TryGetNumberField(TEXT("ground"), Number) ? Number : 0.0;
        Plan.Eaves = Object->TryGetNumberField(TEXT("eaves"), Number) ? Number : Plan.Ground + 3.0;
        if (Object->TryGetNumberField(TEXT("levels"), Number))
        {
            Plan.Levels = static_cast<int>(Number);
        }
        for (const TSharedPtr<FJsonValue>& Point : Object->GetArrayField(TEXT("outline")))
        {
            const TArray<TSharedPtr<FJsonValue>>& Pair = Point->AsArray();
            if (Pair.Num() >= 2)
            {
                Plan.Outline.push_back({ Pair[0]->AsNumber(), Pair[1]->AsNumber() });
            }
        }
        Plan.RawPoints = static_cast<int>(Plan.Outline.size());
        const TSharedPtr<FJsonObject>* Measured = nullptr;
        if (Floors.IsValid() && Floors->TryGetObjectField(UTF8_TO_TCHAR(Plan.Id.c_str()), Measured) && Measured)
        {
            if ((*Measured)->TryGetNumberField(TEXT("floor"), Number))
            {
                Plan.Floor = Number;
            }
            if ((*Measured)->TryGetNumberField(TEXT("street"), Number))
            {
                Plan.Street = Number;
            }
            FString Refused;
            if ((*Measured)->TryGetStringField(TEXT("refused"), Refused))
            {
                Plan.Refused = TCHAR_TO_UTF8(*Refused);
            }
            const TArray<TSharedPtr<FJsonValue>>* Within = nullptr;
            if ((*Measured)->TryGetArrayField(TEXT("rooms_within"), Within) && Within && Within->Num() == 4)
            {
                Plan.WithinU0 = (*Within)[0]->AsNumber();
                Plan.WithinU1 = (*Within)[1]->AsNumber();
                Plan.WithinV0 = (*Within)[2]->AsNumber();
                Plan.WithinV1 = (*Within)[3]->AsNumber();
            }
        }
        OutPlans.push_back(std::move(Plan));
        if (OutNames)
        {
            FString Name;
            Object->TryGetStringField(TEXT("name"), Name);
            OutNames->Add(Name);
        }
    }
    MarburgumInteriors::PrepareCity(OutPlans);
    return true;
}

UMarburgumInteriorSubsystem::FEntry UMarburgumInteriorSubsystem::ProbeEntrance(UWorld& World, const MarburgumInteriors::FLayout& Layout, const AActor* Ignore)
{
    namespace MI = MarburgumInteriors;
    using MarburgumInteriorRuntime::ToUe;
    FEntry Entry;
    if (Layout.Openings.empty() || !Layout.Openings[0].bEntrance || Layout.Rooms.empty())
    {
        Entry.Reason = TEXT("no street door");
        return Entry;
    }
    const MI::FOpening& Door = Layout.Openings[0];
    const MI::FVec2 Dir = MI::Normalize(Door.B - Door.A);
    MI::FVec2 Into{ -Dir.Y, Dir.X };
    if (MI::Dot(MI::RoomMiddle(Layout.Rooms[0]) - (Door.A + Door.B) * 0.5, Into) < 0.0)
    {
        Into = Into * -1.0;
    }
    const MI::FVec2 Mid = (Door.A + Door.B) * 0.5;
    const double Floor = Door.Z0;

    // Only what is built into the world: buildings, ground, placed props. People, cars and doors come and go.
    FCollisionObjectQueryParams Statics;
    Statics.AddObjectTypesToQuery(ECC_WorldStatic);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MarburgumInteriorProbe), false);
    if (Ignore)
    {
        Params.AddIgnoredActor(Ignore);
    }

    // The street outside the door: the step from it to the threshold must be one a person takes.
    FHitResult Ground;
    const FVector Outside = ToUe(Mid - Into * 0.60, Floor);
    if (World.LineTraceSingleByObjectType(Ground, Outside + FVector(0, 0, 150), Outside - FVector(0, 0, 150), Statics, Params))
    {
        Entry.StepCm = static_cast<float>(Ground.ImpactPoint.Z - Floor * 100.0);
    }
    else
    {
        Entry.Reason = TEXT("no ground in front of the door");
        return Entry;
    }
    if (Entry.StepCm > 4.f)
    {
        Entry.Reason = TEXT("street is above the threshold");
        return Entry;
    }
    if (Entry.StepCm < -MaxStepCm)
    {
        Entry.Reason = TEXT("street is a step too far below the threshold");
        return Entry;
    }

    // A player's capsule, from in front of the door, through the hole, to the vestibule: the doorway really is
    // open in the shell, at the height the rooms are built, and nothing stands in it.
    const float HalfHeight = PlayerHalfHeightCm;
    const FCollisionShape Capsule = FCollisionShape::MakeCapsule(PlayerRadiusCm, HalfHeight);
    const FVector Lift(0, 0, HalfHeight + 6.f);
    const FVector From = ToUe(Mid - Into * 0.80, Floor) + Lift;
    const FVector To = ToUe(Mid + Into * 0.60, Floor) + Lift;
    FHitResult Hit;
    if (World.SweepSingleByObjectType(Hit, From, To, FQuat::Identity, Statics, Capsule, Params))
    {
        Entry.Reason = Hit.bStartPenetrating ? TEXT("something stands in front of the door") : TEXT("shell has no doorway");
        Entry.Blocker = Hit.GetActor() ? Hit.GetActor()->GetName() : FString();
        return Entry;
    }
    Entry.bOpen = true;
    return Entry;
}

struct UMarburgumInteriorSubsystem::FData
{
    std::vector<MarburgumInteriors::FPlan> Plans;
    TArray<FString> Names;
    std::vector<MarburgumInteriors::FLayout> Layouts;
    std::vector<char> Ready;
    // 0 not probed, 1 open, 2 sealed.
    std::vector<char> Entry;

    struct FBuilt
    {
        AMarburgumInteriorVolume* Volume = nullptr;
        TArray<AMarburgumDoor*> Doors;
    };

    TMap<int32, FBuilt> Streamed;
};

bool UMarburgumInteriorSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    return Super::ShouldCreateSubsystem(Outer) && !FParse::Param(FCommandLine::Get(), TEXT("MB2NoInteriors"));
}

bool UMarburgumInteriorSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UMarburgumInteriorSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    if (!InWorld.GetMapName().EndsWith(TEXT("lent")))
    {
        return;
    }
    Data = new FData();
    const double Started = FPlatformTime::Seconds();
    if (!ReadPlans(Data->Plans, &Data->Names))
    {
        UE_LOG(LogMarburgumInteriors, Warning, TEXT("MB2INTERIOR event=none reason=\"cannot read buildings.json\""));
        delete Data;
        Data = nullptr;
        return;
    }
    Data->Layouts.resize(Data->Plans.size());
    Data->Ready.assign(Data->Plans.size(), 0);
    Data->Entry.assign(Data->Plans.size(), 0);
    for (size_t Index = 0; Index < Data->Plans.size(); ++Index)
    {
        Data->Layouts[Index] = MarburgumInteriors::Generate(Data->Plans[Index]);
        Data->Ready[Index] = 1;
        if (Data->Layouts[Index].Status == MarburgumInteriors::EStatus::Eligible)
        {
            ++Eligible;
        }
        else
        {
            ++Exceptions;
        }
    }
    UE_LOG(LogMarburgumInteriors, Log, TEXT("MB2INTERIOR event=classified buildings=%d eligible=%d exceptions=%d seconds=%.2f"),
        static_cast<int32>(Data->Plans.size()), Eligible, Exceptions, FPlatformTime::Seconds() - Started);

    // Development starts in front of each street door, two abreast so two players can arrive together.
    if (InWorld.GetNetMode() != NM_Client && FParse::Param(FCommandLine::Get(), TEXT("MB2DoorStarts")))
    {
        namespace MI = MarburgumInteriors;
        int32 Starts = 0;
        for (const MI::FLayout& Layout : Data->Layouts)
        {
            if (Layout.Status != MI::EStatus::Eligible || Layout.Openings.empty() || !Layout.Openings[0].bEntrance)
            {
                continue;
            }
            const MI::FOpening& Door = Layout.Openings[0];
            const MI::FVec2 Into = MarburgumInteriorRuntime::IntoBuilding(Layout);
            const MI::FVec2 Along = MI::Normalize(Door.B - Door.A);
            const MI::FVec2 Mid = (Door.A + Door.B) * 0.5;
            const FName Tag(*FString::Printf(TEXT("door_%hs"), Layout.Id.c_str()));
            for (const double Side : { -0.6, 0.6 })
            {
                const FVector At = MarburgumInteriorRuntime::ToUe(Mid - Into * 2.2 + Along * Side, Door.Z0) + FVector(0, 0, 100.0);
                const FVector Facing = MarburgumInteriorRuntime::ToUe(Mid, Door.Z0) + FVector(0, 0, 100.0) - At;
                FActorSpawnParameters StartParams;
                StartParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                StartParams.ObjectFlags |= RF_Transient;
                if (APlayerStart* Start = InWorld.SpawnActor<APlayerStart>(At, Facing.GetSafeNormal2D().Rotation(), StartParams))
                {
                    Start->PlayerStartTag = Tag;
                    ++Starts;
                }
            }
        }
        UE_LOG(LogMarburgumInteriors, Log, TEXT("MB2INTERIOR event=door_starts starts=%d"), Starts);
    }

    InWorld.GetTimerManager().SetTimer(Timer, this, &UMarburgumInteriorSubsystem::UpdateNear, 0.5f, true);
}

void UMarburgumInteriorSubsystem::Deinitialize()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(Timer);
        if (Data)
        {
            for (TPair<int32, FData::FBuilt>& Pair : Data->Streamed)
            {
                if (Pair.Value.Volume)
                {
                    Pair.Value.Volume->Destroy();
                }
                if (World->GetNetMode() != NM_Client)
                {
                    for (AMarburgumDoor* Door : Pair.Value.Doors)
                    {
                        if (Door)
                        {
                            Door->Destroy();
                        }
                    }
                }
            }
        }
    }
    delete Data;
    Data = nullptr;
    Super::Deinitialize();
}

int32 UMarburgumInteriorSubsystem::NumStreamed() const
{
    return Data ? Data->Streamed.Num() : 0;
}

bool UMarburgumInteriorSubsystem::IsStreamed(const FString& BuildingId) const
{
    if (!Data)
    {
        return false;
    }
    for (const TPair<int32, FData::FBuilt>& Pair : Data->Streamed)
    {
        if (Pair.Value.Volume && UTF8_TO_TCHAR(Data->Plans[static_cast<size_t>(Pair.Key)].Id.c_str()) == BuildingId)
        {
            return true;
        }
    }
    return false;
}

bool UMarburgumInteriorSubsystem::GetWalkRoute(const FString& BuildingId, TArray<FVector>& OutPoints) const
{
    namespace MI = MarburgumInteriors;
    using MarburgumInteriorRuntime::ToUe;
    OutPoints.Reset();
    if (!Data)
    {
        return false;
    }
    for (const MI::FLayout& Layout : Data->Layouts)
    {
        if (UTF8_TO_TCHAR(Layout.Id.c_str()) != BuildingId)
        {
            continue;
        }
        if (Layout.Status != MI::EStatus::Eligible || Layout.Openings.empty() || Layout.Rooms.empty())
        {
            return false;
        }
        const MI::FOpening& Street = Layout.Openings[0];
        const MI::FVec2 Into = MarburgumInteriorRuntime::IntoBuilding(Layout);
        const MI::FVec2 DoorMid = (Street.A + Street.B) * 0.5;
        const double Floor = Street.Z0;
        const double Vestibule = Layout.Openings.size() > 1 && !Layout.Openings[1].bLeaf
            ? MI::Dot((Layout.Openings[1].A + Layout.Openings[1].B) * 0.5 - DoorMid, Into) : 0.0;
        const FVector Outside = ToUe(DoorMid - Into * 1.2, Floor);
        const FVector Inside = ToUe(DoorMid + Into * (Vestibule + 0.6), Floor);
        OutPoints.Add(Outside);
        OutPoints.Add(Inside);
        for (size_t O = 1; O < Layout.Openings.size(); ++O)
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
            const FVector Before = ToUe(Mid - Normal * 0.6, Floor);
            const FVector After = ToUe(Mid + Normal * 0.6, Floor);
            OutPoints.Append({ Before, After, ToUe(Far, Floor), After, Before, Inside });
        }
        OutPoints.Add(Outside);
        return true;
    }
    return false;
}

void UMarburgumInteriorSubsystem::UpdateNear()
{
    namespace MI = MarburgumInteriors;
    UWorld* World = GetWorld();
    if (!World || !Data || Data->Plans.empty())
    {
        return;
    }
    TArray<FVector> People;
    for (TActorIterator<AMarburgumCharacter> It(World); It; ++It)
    {
        People.Add(It->GetActorLocation());
    }
    // A client also builds what its own camera looks at, so a spectator or a view shot sees the rooms too.
    if (World->GetNetMode() != NM_DedicatedServer)
    {
        for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
        {
            if (const APlayerController* Controller = It->Get(); Controller && Controller->IsLocalController())
            {
                FVector Eye;
                FRotator Unused;
                Controller->GetPlayerViewPoint(Eye, Unused);
                People.Add(Eye);
            }
        }
    }
    if (People.IsEmpty())
    {
        return;
    }
    for (int32 Index = 0; Index < static_cast<int32>(Data->Plans.size()); ++Index)
    {
        const MI::FLayout& Layout = Data->Layouts[static_cast<size_t>(Index)];
        if (Layout.Status != MI::EStatus::Eligible || Data->Entry[static_cast<size_t>(Index)] == 2)
        {
            continue;
        }
        const bool bActive = Data->Streamed.Contains(Index);
        const double Limit = bActive ? 40.0 : 22.0;
        bool bKeep = false;
        for (const FVector& Person : People)
        {
            if (MarburgumInteriorRuntime::DistanceToPlan(Data->Plans[static_cast<size_t>(Index)], MarburgumInteriorRuntime::PlanOf(Person)) <= Limit)
            {
                bKeep = true;
                break;
            }
        }
        if (bKeep && !bActive)
        {
            // Rooms go only behind a doorway a person can actually walk through. A shell that has not been
            // rebuilt with its door cut stays a closed building: no hidden rooms, and no door on a solid wall.
            if (Data->Entry[static_cast<size_t>(Index)] == 0)
            {
                const FEntry Entry = ProbeEntrance(*World, Layout);
                Data->Entry[static_cast<size_t>(Index)] = Entry.bOpen ? 1 : 2;
                UE_LOG(LogMarburgumInteriors, Log, TEXT("MB2INTERIOR event=%s id=%s step_cm=%.0f reason=\"%s\" blocker=%s"),
                    Entry.bOpen ? TEXT("entrance_open") : TEXT("entrance_sealed"), UTF8_TO_TCHAR(Layout.Id.c_str()), Entry.StepCm, *Entry.Reason,
                    Entry.Blocker.IsEmpty() ? TEXT("-") : *Entry.Blocker);
                if (!Entry.bOpen)
                {
                    continue;
                }
            }
            FData::FBuilt Built;
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Params.ObjectFlags |= RF_Transient;
            Built.Volume = World->SpawnActor<AMarburgumInteriorVolume>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
            if (Built.Volume)
            {
                Built.Volume->Build(Layout);
            }
            if (World->GetNetMode() != NM_Client)
            {
                const FString& Name = Data->Names.IsValidIndex(Index) ? Data->Names[Index] : FString();
                for (const MI::FOpening& Opening : Layout.Openings)
                {
                    const MI::FLeaf Leaf = MI::LeafFor(Layout, Opening);
                    if (!Leaf.bValid)
                    {
                        continue;
                    }
                    const FVector Hinge = MarburgumInteriorRuntime::ToUe(Leaf.Hinge, Leaf.Z);
                    const FVector ClosedDir = (MarburgumInteriorRuntime::ToUe(Leaf.Hinge + Leaf.ClosedDir, Leaf.Z) - Hinge).GetSafeNormal();
                    const FVector Inward = (MarburgumInteriorRuntime::ToUe(Leaf.Hinge + Leaf.Into, Leaf.Z) - Hinge).GetSafeNormal();
                    const FVector Spun = ClosedDir.RotateAngleAxis(10.f, FVector::UpVector);
                    const float Sign = FVector::DotProduct(Spun - ClosedDir, Inward) >= 0.f ? 1.f : -1.f;
                    FActorSpawnParameters DoorParams;
                    DoorParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                    DoorParams.ObjectFlags |= RF_Transient;
                    AMarburgumDoor* Door = World->SpawnActor<AMarburgumDoor>(Hinge, FRotator::ZeroRotator, DoorParams);
                    if (!Door)
                    {
                        continue;
                    }
                    Door->InitAuthority(Hinge, ClosedDir, Sign * static_cast<float>(MI::SwingDegrees), static_cast<float>(Leaf.Width * 100.0),
                        static_cast<float>(Leaf.Height * 100.0), FName(UTF8_TO_TCHAR(Layout.Id.c_str())), Opening.bEntrance ? Name : FString(),
                        Opening.bEntrance);
                    Built.Doors.Add(Door);
                }
            }
            UE_LOG(LogMarburgumInteriors, Log, TEXT("MB2INTERIOR event=stream id=%s rooms=%d doors=%d"), UTF8_TO_TCHAR(Layout.Id.c_str()),
                static_cast<int32>(Layout.Rooms.size()), Built.Doors.Num());
            Data->Streamed.Add(Index, Built);
        }
        else if (!bKeep && bActive)
        {
            FData::FBuilt Built = Data->Streamed.FindAndRemoveChecked(Index);
            if (Built.Volume)
            {
                Built.Volume->Destroy();
            }
            if (World->GetNetMode() != NM_Client)
            {
                for (AMarburgumDoor* Door : Built.Doors)
                {
                    if (Door)
                    {
                        Door->Destroy();
                    }
                }
            }
            UE_LOG(LogMarburgumInteriors, Log, TEXT("MB2INTERIOR event=unstream id=%s"), UTF8_TO_TCHAR(Layout.Id.c_str()));
        }
    }
}
