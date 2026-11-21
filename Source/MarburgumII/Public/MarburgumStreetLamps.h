#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MarburgumStreetLights.h"
#include "Subsystems/WorldSubsystem.h"

#include "MarburgumStreetLamps.generated.h"

class UHierarchicalInstancedStaticMeshComponent;

/**
 * The street lamps standing in the world (ADR-065): every lamp of Content/Marburgum/Lights/lent_street_lights.json,
 * grounded where it is loaded, as instances of the two lamp meshes. The server and every client put up the same lamps
 * from the same file, so the posts block alike everywhere without being replicated: people walk round them and a car
 * stops at one.
 *
 * What a lamp is doing is the client's business (UMarburgumStreetLightPresentation): it writes each lamp's output and
 * colour into the instance's custom data, which the glass glows by, and lights the nearest ones.
 */
UCLASS(NotPlaceable, Transient)
class MARBURGUMII_API AMarburgumStreetLamps : public AActor
{
    GENERATED_BODY()

public:
    AMarburgumStreetLamps();

    /** Puts the lamps up; their order is the file's, and an index here is the lamp's index there. */
    void Build(const TArray<MarburgumII::StreetLights::FLamp>& InLamps);

    const TArray<MarburgumII::StreetLights::FLamp>& GetLamps() const { return Lamps; }
    /** Whether both meshes were found (scripts/create-weather-content.sh lamps); a kind without its mesh is not put up. */
    bool HasMeshes() const { return bHasMeshes; }

    /**
     * Writes a lamp's state into its instance. The instance-data manager sends only the changed instances to the GPU
     * scene; marking the meshes' render state dirty would rebuild both whole scene proxies (QA 2026-11-19).
     */
    void SetState(int32 Lamp, float Output, const FLinearColor& Colour);

private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USceneComponent> Root;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Masts;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Lanterns;

    TArray<MarburgumII::StreetLights::FLamp> Lamps;
    /** Each lamp's instance in its kind's component. */
    TArray<int32> Instance;
    bool bHasMeshes = false;
};

/**
 * Puts the street lamps up on the Lent map (/Game/World/lent), on the server and on every client alike. -MB2NoStreetLamps
 * leaves them out, for captures of the level as it was.
 */
UCLASS()
class MARBURGUMII_API UMarburgumStreetLampsSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Deinitialize() override;

    AMarburgumStreetLamps* GetLamps() const { return Lamps; }
    /** Lamps in the file, and how many found ground where they stand. */
    int32 NumRead() const { return Read; }
    int32 NumGrounded() const { return Grounded; }

    /**
     * What a lamp surveyed at SurveyedTop stands on: the walkable surface nearest that height a little above or below it,
     * so an eave or a canopy over the pavement is not stood on. Unset where the world has none there. The game and the
     * editor's ground file (MarburgumPartitionLevel -Ground) use this same rule.
     */
    static TOptional<double> FindGround(const UWorld& World, const FVector& SurveyedTop);

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    UPROPERTY(Transient)
    TObjectPtr<AMarburgumStreetLamps> Lamps;

    int32 Read = 0;
    int32 Grounded = 0;
};
