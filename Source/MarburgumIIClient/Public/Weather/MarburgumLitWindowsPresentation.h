#pragma once

#include "CoreMinimal.h"
#include "MarburgumLitWindows.h"
#include "Subsystems/WorldSubsystem.h"

#include "MarburgumLitWindowsPresentation.generated.h"

class UMaterialParameterCollection;

/**
 * The lit windows (ADR-066), in a client world: from the world clock (its 1990s weekday) and the server's weather, the
 * share of households up with a light on and how dark it is outside (MarburgumLitWindows.h), written into
 * MPC_LitWindows. The façades' glass reads its pane's household from the mesh and lights itself; nothing else is drawn,
 * no light is cast, and nothing is sent: every client works the same windows out from the same clock.
 *
 * Nothing runs on a dedicated server. -MB2NoLitWindows keeps every window dark, for comparison captures.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumLitWindowsPresentation : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;

    /** For smokes and evidence: whether the clock has been read, what was written, and the collection found. */
    bool HasState() const { return bHasState; }
    bool HasCollection() const { return Collection != nullptr; }
    const MarburgumII::LitWindows::FState& GetState() const { return State; }

    /** The windows of the façades in the world, read once from their meshes, and how many of them are lit now. */
    struct FCount
    {
        int32 Buildings = 0;
        int32 Panes = 0;
        int32 Households = 0;
        int32 LitPanes = 0;
        int32 LitHouseholds = 0;
        int32 GroundPanes = 0;
        int32 LitGroundPanes = 0;
    };
    FCount CountWindows();

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    void ReadPanes();

    UPROPERTY(Transient)
    TObjectPtr<UMaterialParameterCollection> Collection;

    MarburgumII::LitWindows::FState State;
    bool bHasState = false;
    bool bDark = false;
    bool bTried = false;
    double SinceUpdate = 1e9;

    /** Every pane's household, and its building's index, read from the façade meshes' texture coordinates. */
    struct FPane
    {
        MarburgumII::LitWindows::FTag Tag;
        int32 Building = 0;
        /** Its panes: a flat spans two or three. */
        int32 Panes = 1;
    };
    TArray<FPane> Panes;
    int32 Buildings = 0;
    bool bPanesRead = false;
};
