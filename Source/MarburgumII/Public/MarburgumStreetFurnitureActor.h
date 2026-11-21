#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MarburgumStreetFurniture.h"
#include "Subsystems/WorldSubsystem.h"

#include "MarburgumStreetFurnitureActor.generated.h"

class UHierarchicalInstancedStaticMeshComponent;

/**
 * The street furniture standing in the world (GFX-01): every piece of Content/Marburgum/Furniture/
 * lent_street_furniture.json, grounded where it is loaded, as instances of the original game's kit, one component per
 * piece. The server and every client put up the same pieces from the same file, so they block alike everywhere without
 * being replicated, as the street lamps do. Each piece blocks with its mesh's own simple collision (the shelter's walls,
 * not its roof).
 */
UCLASS(NotPlaceable, Transient)
class MARBURGUMII_API AMarburgumStreetFurniture : public AActor
{
    GENERATED_BODY()

public:
    AMarburgumStreetFurniture();

    void Build(const TArray<MarburgumII::StreetFurniture::FPiece>& Pieces);

    int32 NumKinds() const { return Kinds.Num(); }
    int32 NumMissingMeshes() const { return MissingMeshes; }

private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USceneComponent> Root;

    UPROPERTY(VisibleAnywhere)
    TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> Kinds;

    int32 MissingMeshes = 0;
};

/**
 * Puts the street furniture up on the Lent map (/Game/World/lent), on the server and on every client alike.
 * -MB2NoStreetFurniture leaves it out.
 */
UCLASS()
class MARBURGUMII_API UMarburgumStreetFurnitureSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Deinitialize() override;

    AMarburgumStreetFurniture* GetFurniture() const { return Furniture; }
    int32 NumRead() const { return Read; }
    int32 NumGrounded() const { return Grounded; }

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    UPROPERTY(Transient)
    TObjectPtr<AMarburgumStreetFurniture> Furniture;

    int32 Read = 0;
    int32 Grounded = 0;
};
