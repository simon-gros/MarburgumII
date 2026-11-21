#include "Weather/MarburgumLitWindowsPresentation.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "MarburgumGameState.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "StaticMeshResources.h"
#include "WorldPartition/HLOD/HLODActor.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumLitWindows, Log, All);

namespace MarburgumLitWindowsPresentation
{
    namespace Windows = MarburgumII::LitWindows;

    /** The shares move by the minute; this is often enough. */
    constexpr double UpdateSeconds = 0.5;
    /** A pane's glass is a closed box: six faces of four corners. */
    constexpr int32 VerticesPerPane = 24;
}

bool UMarburgumLitWindowsPresentation::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* World = Cast<UWorld>(Outer);
    return World && World->IsGameWorld() && !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

bool UMarburgumLitWindowsPresentation::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UMarburgumLitWindowsPresentation::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UMarburgumLitWindowsPresentation, STATGROUP_Tickables);
}

void UMarburgumLitWindowsPresentation::Tick(float DeltaTime)
{
    using namespace MarburgumLitWindowsPresentation;
    namespace Lights = MarburgumII::StreetLights;
    namespace Clock = MarburgumII::Clock;
    UWorld* World = GetWorld();
    if (!World || World->GetNetMode() == NM_DedicatedServer)
    {
        return;
    }
    SinceUpdate += DeltaTime;
    if (SinceUpdate < UpdateSeconds)
    {
        return;
    }
    SinceUpdate = 0.0;
    if (!bTried)
    {
        bTried = true;
        bDark = FParse::Param(FCommandLine::Get(), TEXT("MB2NoLitWindows"));
        Collection = LoadObject<UMaterialParameterCollection>(nullptr, Windows::Collection, nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (!Collection)
        {
            UE_LOG(LogMarburgumLitWindows, Warning, TEXT("lit windows: no %s (scripts/create-weather-content.sh windows); the windows stay dark"), Windows::Collection);
        }
    }
    FDateTime Utc;
    if (!AMarburgumGameState::WorldUtc(World, Utc))
    {
        return;
    }
    const FMarburgumWeather* Weather = AMarburgumGameState::WeatherOf(World);
    const bool bWeather = Weather && Weather->bKnown;
    const Lights::FSky Sky = Lights::SkyAt(Utc, bWeather ? Weather->CloudCover : 0.0, bWeather ? Weather->Fog : 0.0, bWeather ? Weather->Thunder : 0.0);
    const Clock::FCivil Local = Clock::MarburgumAt(Utc);
    State = Windows::StateAt(Local, Sky);
    if (bDark)
    {
        State.Dusk = 0.0f;
    }
    if (!bHasState)
    {
        bHasState = true;
        UE_LOG(LogMarburgumLitWindows, Log, TEXT("lit windows from the world clock marburgum=\"%s\" sun_elevation=%.2f evening=%.3f morning=%.3f ground=%.3f dusk=%.3f dark=%d"),
            *Clock::ShortLabel(Local), Sky.SunElevationDeg, State.Evening, State.Morning, State.Ground, State.Dusk, bDark ? 1 : 0);
    }
    if (UMaterialParameterCollectionInstance* Instance = Collection ? World->GetParameterCollectionInstance(Collection) : nullptr)
    {
        Instance->SetScalarParameterValue(FName(Windows::EveningParameter), State.Evening);
        Instance->SetScalarParameterValue(FName(Windows::MorningParameter), State.Morning);
        Instance->SetScalarParameterValue(FName(Windows::GroundParameter), State.Ground);
        Instance->SetScalarParameterValue(FName(Windows::DuskParameter), State.Dusk);
    }
}

void UMarburgumLitWindowsPresentation::ReadPanes()
{
    using namespace MarburgumLitWindowsPresentation;
    bPanesRead = true;
    Panes.Reset();
    Buildings = 0;
    UWorld* World = GetWorld();
    const UMaterialInterface* FacadeGlass = LoadObject<UMaterialInterface>(nullptr, Windows::FacadeGlassInstance, nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (!World || !FacadeGlass)
    {
        return;
    }
    TSet<const UStaticMesh*> Seen;
    for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
    {
        const UStaticMeshComponent* Component = *It;
        const UStaticMesh* Mesh = Component && Component->GetWorld() == World ? Component->GetStaticMesh() : nullptr;
        // A World Partition HLOD (ADR-091) draws far buildings again, merged; its mesh keeps no vertices on the CPU.
        if (!Mesh || Seen.Contains(Mesh) || !Mesh->GetRenderData() || Mesh->GetRenderData()->LODResources.IsEmpty()
            || Component->GetOwner<AWorldPartitionHLOD>())
        {
            continue;
        }
        const TArray<FStaticMaterial>& Materials = Mesh->GetStaticMaterials();
        const int32 GlassSlot = Materials.IndexOfByPredicate([FacadeGlass](const FStaticMaterial& M) { return M.MaterialInterface == FacadeGlass; });
        if (GlassSlot == INDEX_NONE)
        {
            continue;
        }
        Seen.Add(Mesh);
        const FStaticMeshLODResources& Lod = Mesh->GetRenderData()->LODResources[0];
        const FStaticMeshVertexBuffer& Vertices = Lod.VertexBuffers.StaticMeshVertexBuffer;
        if (Vertices.GetNumTexCoords() < static_cast<uint32>(Windows::UVChannels) || Vertices.GetNumVertices() == 0 || !Vertices.GetTexCoordData())
        {
            continue;
        }
        // Count each household's vertices; a pane is a box of VerticesPerPane.
        // Keyed by the household's three rank bytes and its floor (ranks are (2k + 1) / 512).
        const auto Byte = [](float Rank) { return static_cast<uint32>(FMath::Clamp(FMath::RoundToInt((Rank * 512.0f - 1.0f) * 0.5f), 0, 255)); };
        TMap<uint32, int32> Households;
        for (const FStaticMeshSection& Section : Lod.Sections)
        {
            if (Section.MaterialIndex != GlassSlot || Section.NumTriangles == 0)
            {
                continue;
            }
            for (uint32 Vertex = Section.MinVertexIndex; Vertex <= Section.MaxVertexIndex; ++Vertex)
            {
                const FVector2f Tag = Vertices.GetVertexUV(Vertex, Windows::TagChannel);
                const FVector2f Kind = Vertices.GetVertexUV(Vertex, Windows::KindChannel);
                Households.FindOrAdd(Byte(Tag.X) | Byte(Tag.Y) << 8 | Byte(Kind.X) << 16 | (Kind.Y > 0.5f ? 1u << 24 : 0u))++;
            }
        }
        if (Households.IsEmpty())
        {
            continue;
        }
        for (const TPair<uint32, int32>& Household : Households)
        {
            FPane Pane;
            Pane.Tag.Evening = Windows::Rank(Household.Key);
            Pane.Tag.Morning = Windows::Rank(Household.Key >> 8);
            Pane.Tag.Kind = Windows::Rank(Household.Key >> 16);
            Pane.Tag.bGround = (Household.Key >> 24) != 0;
            Pane.Building = Buildings;
            Pane.Panes = FMath::Max(1, Household.Value / VerticesPerPane);
            Panes.Add(Pane);
        }
        ++Buildings;
    }
    int32 Total = 0;
    for (const FPane& Pane : Panes)
    {
        Total += Pane.Panes;
    }
    UE_LOG(LogMarburgumLitWindows, Log, TEXT("lit windows: %d buildings, %d households, %d panes read from the facades"), Buildings, Panes.Num(), Total);
}

UMarburgumLitWindowsPresentation::FCount UMarburgumLitWindowsPresentation::CountWindows()
{
    if (!bPanesRead)
    {
        ReadPanes();
    }
    FCount Count;
    Count.Buildings = Buildings;
    Count.Households = Panes.Num();
    for (const FPane& Pane : Panes)
    {
        const bool bLit = bHasState && MarburgumII::LitWindows::IsLit(Pane.Tag, State);
        Count.Panes += Pane.Panes;
        Count.LitPanes += bLit ? Pane.Panes : 0;
        Count.LitHouseholds += bLit ? 1 : 0;
        Count.GroundPanes += Pane.Tag.bGround ? Pane.Panes : 0;
        Count.LitGroundPanes += Pane.Tag.bGround && bLit ? Pane.Panes : 0;
    }
    return Count;
}
