#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MarburgumPlaceSmokeSubsystem.generated.h"

class AMarburgumPlayerController;
class UMarburgumLocationBanner;

/**
 * Place-name driver (ADR-063), active only with -MB2PlaceSmoke. One client against a real dedicated server on
 * /Game/World/lent started with -MB2PlaceRoute, which walks the player along the route in MarburgumPlaceRoute.h. At
 * each stop the client checks what the location banner shows (place, street or square, landmark) against the route,
 * that the banner came up for the stop, and takes a capture; it exits 0 when every stop held, else 1. See
 * scripts/place-smoke-linux.sh.
 *
 * Options: -MB2PlaceShot=<path prefix> -MB2PlaceTimeout=<s>
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumPlaceSmokeSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:
    bool Tick(float DeltaTime);
    bool Bind();
    const UMarburgumLocationBanner* Banner() const;
    void Shot(const TCHAR* Label);
    void Expect(bool bCondition, const FString& What);
    void Pass();
    void Fail(const FString& Reason);

    FTSTicker::FDelegateHandle TickerHandle;
    TWeakObjectPtr<AMarburgumPlayerController> Controller;

    FString ShotPrefix;
    double TimeoutSeconds = 180.0;
    double StartSeconds = 0.0;

    int32 Stop = 0;
    /** When the body reached the current stop; negative before it has. */
    double ArrivedAt = -1.0;
    /** When the banner first matched the stop; negative before it has. */
    double MatchedAt = -1.0;
    int32 AnnouncementsBefore = 0;
    bool bShotTaken = false;

    FString PendingShot;
    double ShotRequestedSeconds = 0.0;
    TArray<FString> Problems;
    bool bDone = false;
};
