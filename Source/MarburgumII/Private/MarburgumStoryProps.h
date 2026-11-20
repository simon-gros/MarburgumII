#pragma once

#include "CoreMinimal.h"
#include "MarburgumGameplayContract.h"

class AActor;
class USceneComponent;
class UStaticMesh;
class UMaterialInterface;

/**
 * Story furniture (ADR-083): what an authored place looks like when the catalogue gives it a prop, built at runtime on
 * every machine from engine primitives (and the Kainach trucks from the project's lorry mesh), so the server traces the
 * same shapes the clients draw. No imported or generated art: dimensions are period furniture's, colours are flat.
 *
 * The parts hang from Root in the actor's frame with the ground at Z = -GroundBelowRootCm; +X is the way the place
 * faces, so the side a visitor walks up from (-X) is the readable one. Every part blocks the camera and the body.
 */
namespace MarburgumStoryProps
{
    /** A place marker stands this far above the ground it was put on (MarburgumWorldAuthority). */
    inline constexpr double GroundBelowRootCm = 25.0;

    struct FPrimitives
    {
        UStaticMesh* Cube = nullptr;
        UStaticMesh* Cylinder = nullptr;
        UMaterialInterface* Plain = nullptr;
    };

    /** Builds Kind's parts under Root. Text and lights are left out on a dedicated server, which draws nothing. */
    void Build(AActor& Owner, USceneComponent& Root, MarburgumII::Gameplay::EPropKind Kind, const FPrimitives& Primitives);
}
