#pragma once

#include "CoreMinimal.h"

/**
 * The street furniture (GFX-01, VQ-023): the original game's street kit, placed from this map's OpenStreetMap extract.
 *
 * tools/lent-ground (`--mode furniture`) furnishes every bus platform with a stop post, and with a shelter and a bench
 * where the map records them. It places every mapped bench and bollard, and lines fence, wall and bollard runs with
 * 2 m panels and 1.5 m posts. Where a paved promenade ends in a steep drop to the Drava, a railing runs along the bank's
 * crest, open where people meet the water. It writes Content/Marburgum/Furniture/lent_street_furniture.json. Nothing stands in the
 * carriageway, a building, the river, on a bridge or within reach of a tree's trunk, and no solid piece stands on the
 * street people's walk lines (a panel across one leaves a gap).
 *
 * The file is in plan metres (x east, y south of the middle of Stari most); everything here is in Unreal centimetres
 * (X north, Y east), converted once on load. Heights are the walkable top, which the game grounds by a trace.
 *
 * Each piece is the original's mesh (/Game/World/StreetFurniture/SM_Street_<Piece>_01). Its front, the open side of a
 * shelter, a bench's sitter or one face of a panel, is the mesh's +X, and the yaw turns it to face the way the file
 * says. A panel's width lies along its run.
 *
 * Pure and engine-neutral apart from the JSON reader; AMarburgumStreetFurniture draws them.
 */
namespace MarburgumII::StreetFurniture
{
    /** Relative to the project's Content directory. */
    inline constexpr const TCHAR* FurnitureFile = TEXT("Marburgum/Furniture/lent_street_furniture.json");

    /** The original kit's pieces, in its order. */
    enum class EPiece : uint8
    {
        /** The post and plate that mark a stop. */
        StopPost,
        /** Back, sides and roof; the roof does not block. */
        Shelter,
        Bench,
        /** A 2 m module of railing. */
        FencePanel,
        /** A 2 m module of masonry with a coping. */
        WallPanel,
        Bollard,
        /** A 2 m module of painted steel railing along a quay's edge over the river. */
        QuayRailing,
        Count,
    };

    enum class ESource : uint8
    {
        /** A stop's post, shelter or bench. */
        Stop,
        /** A mapped bench. */
        Bench,
        /** A mapped bollard. */
        Bollard,
        /** A fence, wall or bollard run. */
        Barrier,
        /** A quay's edge over the river. */
        Quay,
        Count,
    };

    struct FPiece
    {
        /** On the ground plane in Unreal centimetres; Z the walkable top, until grounded. */
        FVector Location = FVector::ZeroVector;
        /** Unreal yaw, degrees: the way the piece's front faces. */
        float Yaw = 0.0f;
        EPiece Piece = EPiece::StopPost;
        ESource Source = ESource::Stop;
        /** False where the world had no ground under it: such a piece is not put up. */
        bool bStanding = true;
    };

    /** Reads the furniture file. False with OutError when it is not one. */
    MARBURGUMIISHARED_API bool Parse(const FString& Json, TArray<FPiece>& OutPieces, FString& OutError);

    /** The piece's mesh, as an object path. */
    MARBURGUMIISHARED_API FString MeshPath(EPiece Piece);

    /** Small pieces are drawn this far, the shelter and the panels further; centimetres. */
    inline constexpr float SmallCullCm = 15000.0f;
    inline constexpr float LargeCullCm = 40000.0f;
    MARBURGUMIISHARED_API float CullDistanceCm(EPiece Piece);
}
