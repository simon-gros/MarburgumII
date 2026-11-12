#include "MarburgumGround.h"

#include "Engine/HitResult.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "MarburgumSurface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumGround, Log, All);

namespace MarburgumII::Ground
{
    namespace
    {
        const Surface::FLandscapeMap& LandscapeMap()
        {
            static const Surface::FLandscapeMap Map = []
            {
                Surface::FLandscapeMap Loaded;
                const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), Surface::LandscapeMapFile);
                TArray<uint8> Bytes;
                if (!FFileHelper::LoadFileToArray(Bytes, *Path, FILEREAD_Silent) || !Loaded.Parse(Bytes))
                {
                    UE_LOG(LogMarburgumGround, Warning, TEXT("MB2SURFACE no landscape surface map at %s: the landscape reads as unknown ground"), *Path);
                }
                else
                {
                    UE_LOG(LogMarburgumGround, Log, TEXT("MB2SURFACE landscape surface map %dx%d"), Loaded.GetCells(), Loaded.GetCells());
                }
                return Loaded;
            }();
            return Map;
        }
    }

    FString SurfaceOf(const FHitResult& Hit)
    {
        if (Cast<ULandscapeHeightfieldCollisionComponent>(Hit.GetComponent()))
        {
            return LandscapeMap().SurfaceAt(Hit.ImpactPoint.X, Hit.ImpactPoint.Y);
        }
        const UPhysicalMaterial* Material = Hit.PhysMaterial.Get();
        return Material ? Surface::FromPhysicalMaterialName(Material->GetName()) : FString(Surface::Unknown);
    }

    bool HasLandscapeMap()
    {
        return LandscapeMap().IsLoaded();
    }
}
