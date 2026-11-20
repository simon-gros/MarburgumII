#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MarburgumDoor.generated.h"

class UStaticMeshComponent;

/** How far a street or room door has got. The server owns this. Clients only play it. */
UENUM()
enum class EMarburgumDoorMotion : uint8
{
    Closed,
    Opening,
    Open,
    Closing
};

/**
 * One hinged door (ADR-076). The shell and the room walls are built locally from the footprint. This actor is the
 * only replicated part: open or closed, and the swing between them. Nothing is written to the world store. A door
 * that is streamed out, or a server that restarts, comes back shut.
 *
 * Only a shut leaf stops people. A leaf that is moving or open lets them through, so a swing never shoves anybody
 * into a wall or traps them; and the server will not close a door, or keeps a closing door open, while somebody
 * stands in its swing.
 */
UCLASS()
class MARBURGUMII_API AMarburgumDoor : public AActor
{
    GENERATED_BODY()

public:
    AMarburgumDoor();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    /**
     * Server, after spawn. Hinge is the bottom of the leaf's hinge edge, in centimetres, on the leaf's centre line.
     * ClosedDir is the shut leaf, unit length. SwingDegrees is the yaw added when the door is open (positive or
     * negative). Width and height are the leaf, already narrower than the opening.
     */
    void InitAuthority(const FVector& InHinge, const FVector& ClosedDir, float InSwingDegrees, float InWidthCm, float InHeightCm,
        FName InBuildingId, const FString& InPlaceName, bool bInStreetDoor);

    /** Server. Reverse a swing that is already going, otherwise start the other way. False when somebody is in the way. */
    bool ToggleAuthority();

    EMarburgumDoorMotion GetMotion() const { return Motion; }
    FName GetBuildingId() const { return BuildingId; }
    const FString& GetPlaceName() const { return PlaceName; }
    bool IsStreetDoor() const { return bStreetDoor; }
    float GetInteractionRange() const { return InteractionRange; }
    bool IsOpen() const { return Motion == EMarburgumDoorMotion::Open || Motion == EMarburgumDoorMotion::Opening; }

    /** The middle of the shut leaf, in the doorway: where a person reaches for it from either side. */
    FVector GetDoorwayCentre() const;

    /** Whether a body at this location can reach the door: within range of the doorway, not a storey away. */
    bool IsWithinReach(const FVector& BodyLocation, float Slack = 0.f) const;

    /** Server. True when no pawn stands where the leaf would sweep closing from its current angle. */
    bool IsSwingClear() const;

    /** Horizontal reach from the middle of the doorway. */
    UPROPERTY(EditAnywhere, Category = "Marburgum|Door")
    float InteractionRange = 160.f;

protected:
    UFUNCTION()
    void OnRep_Motion();

    UFUNCTION()
    void OnRep_Leaf();

private:
    void EnsureLeaf();
    void ApplySwing();
    void ApplyCollision();
    float TargetAlpha() const;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USceneComponent> HingeRoot;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> Leaf;

    UPROPERTY(ReplicatedUsing = OnRep_Motion)
    EMarburgumDoorMotion Motion = EMarburgumDoorMotion::Closed;

    UPROPERTY(ReplicatedUsing = OnRep_Leaf)
    FVector Hinge = FVector::ZeroVector;

    UPROPERTY(ReplicatedUsing = OnRep_Leaf)
    float ClosedYaw = 0.f;

    UPROPERTY(ReplicatedUsing = OnRep_Leaf)
    float SwingDegrees = 88.f;

    UPROPERTY(ReplicatedUsing = OnRep_Leaf)
    float WidthCm = 0.f;

    UPROPERTY(ReplicatedUsing = OnRep_Leaf)
    float HeightCm = 0.f;

    UPROPERTY(Replicated)
    FName BuildingId;

    UPROPERTY(Replicated)
    FString PlaceName;

    UPROPERTY(Replicated)
    bool bStreetDoor = false;

    float OpenAlpha = 0.f;
    bool bLeafBuilt = false;
};
