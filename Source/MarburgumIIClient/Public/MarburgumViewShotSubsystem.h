#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MarburgumViewShotSubsystem.generated.h"

class ACameraActor;

/**
 * Fixed-camera screenshots of the loaded level, active only with -MB2ViewShots. Used as before/after
 * evidence for content changes such as rebuilt buildings.
 *
 *   -MB2ViewShots="name@eyeX,eyeY,eyeZ@targetX,targetY,targetZ@fov;..."  -MB2ViewShotDir=<abs dir>
 *
 * Coordinates are Unreal centimetres, fov is horizontal degrees. After the map has settled, the player
 * camera is switched to each view in turn, the HUD is removed, and one screenshot per view is written
 * as <dir>/<name>.png. The process exits 0 when every view was written, 1 otherwise.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumViewShotSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:
    struct FView
    {
        FString Name;
        FVector Eye;
        FVector Target;
        float Fov = 60.f;
    };

    bool Tick(float DeltaTime);
    void Finish(int32 Code, const TCHAR* Reason);

    TArray<FView> Views;
    FString Directory;
    FTSTicker::FDelegateHandle TickerHandle;
    TWeakObjectPtr<ACameraActor> Camera;
    int32 Current = -1;
    double StartSeconds = 0.0;
    double ViewSeconds = 0.0;
    FString PendingFile;
    bool bRequested = false;
    bool bFinished = false;
};
