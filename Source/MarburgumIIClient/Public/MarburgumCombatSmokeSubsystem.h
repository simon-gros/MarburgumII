#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "MarburgumCombatTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MarburgumCombatSmokeSubsystem.generated.h"

class AMarburgumCharacter;
class AMarburgumFighter;
class AMarburgumPlayerController;
class UMarburgumCharacterMovement;

/**
 * Fighting driver (ADR-041), active only with -MB2CombatSmoke. Each client process plays one role against a real
 * dedicated server on /Game/World/lent, starting in front of the Lent drinker (?MB2Start=fight_lent_drunk), and
 * exits 0 (its expectations held) or 1. See scripts/combat-smoke-linux.sh, which runs B, then A.
 *
 *   B  beaten. Jabs the drinker once (he turns hostile), then stands with its hands down until his blows put it
 *      on the floor: it plays the fall, cannot swing from the ground (incapacitated), and the drinker gives up.
 *   A  the fight. A jab from out of reach misses and leaves the drinker alone. Closer, a jab lands for 7; a second
 *      at once is too soon. The drinker turns on A, and the passer-by steps away. A raises its guard and stands:
 *      the guard clip shows, the drinker's jab is blocked (a quarter of the damage, the guard's stamina paid) and
 *      sounds as blocked. Hands down, his next jab lands in full. A heavy swing lands for 14.7, then A keeps
 *      swinging until he is down: he plays the fall and the floor sound, and a jab at him on the floor misses.
 *
 * Options: -MB2CombatRole= -MB2CombatShot=<path prefix> -MB2CombatTimeout=<s>
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumCombatSmokeSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:
    bool Tick(float DeltaTime);
    bool Bind();
    void TickA(double Now);
    void TickB(double Now);
    /** Walk toward the drinker until within Distance of him; true once there. */
    bool Approach(double DistanceCm);
    void Observe();

    AMarburgumCharacter* Body() const;
    UMarburgumCharacterMovement* Movement() const;
    int32 Vitality() const;
    int32 Stamina() const;
    void OnCombatResult(const FMarburgumCombatResult& Result);
    /** The next answer after Since, or nullptr while waiting. */
    const FMarburgumCombatResult* AnswerSince(int32 Since) const;
    void Expect(bool bCondition, const FString& What);
    void Shot(const TCHAR* Label);
    void Advance(int32 Next, double Now);
    void Report(const TCHAR* Event, double Now);
    /** The drawn right foot ahead of the left along the body's forward, after the whole animation graph (cm). */
    double StrideCm() const;
    void Pass();
    void Fail(const FString& Reason);

    FTSTicker::FDelegateHandle TickerHandle;
    TWeakObjectPtr<AMarburgumPlayerController> Controller;
    TWeakObjectPtr<AMarburgumFighter> Drinker;
    TWeakObjectPtr<AMarburgumFighter> Walker;

    FString Role = TEXT("A");
    FString ShotPrefix;
    double TimeoutSeconds = 240.0;
    double StartSeconds = 0.0;

    int32 Step = 0;
    double StepSeconds = 0.0;
    TArray<FMarburgumCombatResult> Answers;
    int32 AnswerMark = 0;
    int32 PrevVitality = 0;
    int32 PrevStamina = 0;
    int32 StaminaMark = 0;
    double LastSwingSeconds = 0.0;

    // Seen at any time since the fight started.
    bool bSawWalkerAvoid = false;
    bool bSawDrinkerJab = false;
    bool bSawOwnJab = false;
    bool bSawOwnGuard = false;
    // The longest drawn stride while the own heavy swing played.
    double LongestHeavyStrideCm = -1000.0;

    bool bGuardShot = false;
    FString PendingShot;
    bool bDone = false;
    TArray<FString> Problems;
};
