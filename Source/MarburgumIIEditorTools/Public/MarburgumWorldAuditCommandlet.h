#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MarburgumWorldAuditCommandlet.generated.h"

/**
 * Read-only visual and technical audit of a level and the project's mesh, texture and material assets
 * (Docs/VISUAL_QUALITY_BACKLOG.md):
 *
 *   UnrealEditor-Cmd MarburgumII.uproject -run=MarburgumWorldAudit [-Map=/Game/World/lent] -Out=<dir>
 *
 * Level checks, per placed primitive: missing mesh, empty or engine-default material slots, props (each actor's
 * visible meshes together; parked cars excepted) floating above or sunk into the ground under them (one ray at the
 * centre of their footprint), exact duplicates
 * (same mesh, transform within 1 cm and 0.1 degrees: z-fighting), actors outside the playable square,
 * and per-folder counts of actors, triangles, Nanite, LODs, collision and draw distance.
 *
 * Asset checks under /Game: static meshes (triangles, LODs, Nanite, collision, UV channels, sections,
 * bounds), textures (size, mips, compression, sRGB on normal maps, streaming) and material instances
 * without a parent.
 *
 * Writes findings.tsv, meshes.tsv, textures.tsv, folders.tsv and summary.txt to -Out and logs
 * "MB2AUDIT ..." lines. Never saves a package. Returns 0 when the audit ran, whatever it found.
 */
UCLASS()
class UMarburgumWorldAuditCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UMarburgumWorldAuditCommandlet();
    virtual int32 Main(const FString& Params) override;
};
