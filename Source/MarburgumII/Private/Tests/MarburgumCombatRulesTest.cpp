#include "MarburgumCombatContract.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumCombatRulesTest,
    "Marburgum.Gameplay.CombatRules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarburgumCombatRulesTest::RunTest(const FString& Parameters)
{
    using namespace MarburgumII::Combat;

    // attack(): refusals in the original's order, and a swing that goes ahead.
    FSwingAsk Ask;
    Ask.Stamina = 100.f;
    TestNull(TEXT("rested body swings"), SwingRefusal(Ask));
    {
        FSwingAsk Seated = Ask;
        Seated.bSeated = true;
        Seated.bDown = true;
        TestEqual(TEXT("seated before down"), FString(SwingRefusal(Seated)), FString(ErrorSeated));
    }
    {
        FSwingAsk Down = Ask;
        Down.bDown = true;
        Down.Stamina = 0.f;
        TestEqual(TEXT("down before exhausted"), FString(SwingRefusal(Down)), FString(ErrorIncapacitated));
    }
    {
        FSwingAsk Soon = Ask;
        Soon.NextSwingAt = 0.1;
        TestEqual(TEXT("own cooldown"), FString(SwingRefusal(Soon)), FString(ErrorTooSoon));
        Soon.NextSwingAt = 0.0;
        Soon.StaggeredUntil = 0.1;
        TestEqual(TEXT("somebody else's blow"), FString(SwingRefusal(Soon)), FString(ErrorTooSoon));
    }
    {
        FSwingAsk Tired = Ask;
        Tired.Stamina = FistStaminaCost - 0.5f;
        TestEqual(TEXT("no breath for a jab"), FString(SwingRefusal(Tired)), FString(ErrorExhausted));
        Tired.Stamina = FistStaminaCost;
        TestNull(TEXT("exactly enough for a jab"), SwingRefusal(Tired));
        Tired.bHeavy = true;
        TestEqual(TEXT("not enough for a committed swing"), FString(SwingRefusal(Tired)), FString(ErrorExhausted));
    }
    {
        FSwingAsk Nobody = Ask;
        Nobody.bHasCharacter = false;
        TestEqual(TEXT("no character first"), FString(SwingRefusal(Nobody)), FString(ErrorNoCharacter));
    }

    // Numbers: a heavy blow is worth 2.1 jabs, and about eight jabs put the drinker down.
    TestEqual(TEXT("heavy damage"), SwingDamage(true), FistDamage * HeavyMultiplier);
    int32 Jabs = 0;
    float Vitality = FindNpc(TEXT("lent_drunk"))->Vitality;
    while (Vitality > 0.f && Jabs < 100)
    {
        Vitality = ResolveBlow(Vitality, SwingDamage(false), false, StaggerSeconds).VitalityAfter;
        ++Jabs;
    }
    TestEqual(TEXT("jabs to floor the drinker"), Jabs, 13);
    TestTrue(TEXT("no stun-lock"), StaggerSeconds < FistCooldownSeconds && HeavyStaggerSeconds < HeavyCooldownSeconds);

    // melee_target(): reach and arc off the nose; standing on someone is always in reach.
    double Distance = 0.0;
    const FVector From(0.0, 0.0, 0.0);
    TestTrue(TEXT("straight ahead in reach"), InSwingReach(From, 0.0, FVector(150.0, 0.0, 0.0), Distance));
    TestEqual(TEXT("distance on the ground plane"), Distance, 150.0);
    TestFalse(TEXT("too far"), InSwingReach(From, 0.0, FVector(FistReachCm + 1.0, 0.0, 0.0), Distance));
    TestTrue(TEXT("inside the arc"), InSwingReach(From, 0.0, FRotator(0.0, 70.0, 0.0).Vector() * 100.0, Distance));
    TestFalse(TEXT("outside the arc"), InSwingReach(From, 0.0, FRotator(0.0, 80.0, 0.0).Vector() * 100.0, Distance));
    TestFalse(TEXT("behind"), InSwingReach(From, 0.0, FVector(-100.0, 0.0, 0.0), Distance));
    TestTrue(TEXT("on top"), InSwingReach(From, 90.0, From, Distance));
    TestTrue(TEXT("height does not count"), InSwingReach(From, 0.0, FVector(100.0, 0.0, 400.0), Distance));

    // A guard: paid for if it can be, otherwise it breaks and the blow arrives in full.
    FGuardPayment Paid = PayForGuard(true, 50.f);
    TestTrue(TEXT("guard holds"), Paid.bGuarded);
    TestEqual(TEXT("guard costs"), Paid.StaminaAfter, 50.f - GuardStaminaCost);
    FGuardPayment Broke = PayForGuard(true, GuardStaminaCost - 1.f);
    TestFalse(TEXT("guard breaks"), Broke.bGuarded);
    TestEqual(TEXT("broken guard costs nothing"), Broke.StaminaAfter, GuardStaminaCost - 1.f);
    TestFalse(TEXT("no guard, no payment"), PayForGuard(false, 50.f).bGuarded);

    // apply_damage(): a guarded blow is a quarter and half the stagger; the floor ends the stagger.
    FBlowResult Guarded = ResolveBlow(100.f, 8.f, true, 0.2f);
    TestEqual(TEXT("guarded share"), Guarded.Applied, 2.f);
    TestEqual(TEXT("guarded stagger"), Guarded.StaggerSeconds, 0.1f);
    TestEqual(TEXT("blocked"), FString(Guarded.Outcome), FString(OutcomeBlocked));
    FBlowResult Last = ResolveBlow(5.f, 7.f, false, 0.2f);
    TestTrue(TEXT("down"), Last.bIncapacitated);
    TestEqual(TEXT("never below zero"), Last.VitalityAfter, 0.f);
    TestEqual(TEXT("no stagger on the floor"), Last.StaggerSeconds, 0.f);
    TestEqual(TEXT("hit"), FString(Last.Outcome), FString(OutcomeHit));

    // tick_combat(): a hostile's step, in the original's order.
    FHostileView View;
    View.bTargetPresent = true;
    View.DistanceCm = 150.0;
    View.Now = 10.0;
    TestTrue(TEXT("strike"), DecideHostile(View) == EHostileStep::Strike);
    {
        FHostileView Gone = View;
        Gone.TargetFromHomeCm = Gone.PursuitRangeCm + 1.0;
        Gone.bStaggered = true;
        TestTrue(TEXT("pursuit range before stagger"), DecideHostile(Gone) == EHostileStep::GiveUp);
        Gone = View;
        Gone.bTargetDown = true;
        TestTrue(TEXT("nobody hits a body on the floor"), DecideHostile(Gone) == EHostileStep::GiveUp);
    }
    {
        FHostileView Staggered = View;
        Staggered.bStaggered = true;
        Staggered.DistanceCm = 5000.0;
        TestTrue(TEXT("stagger before closing"), DecideHostile(Staggered) == EHostileStep::Staggered);
    }
    {
        FHostileView Far = View;
        Far.DistanceCm = Far.ReachCm + 1.0;
        TestTrue(TEXT("close in"), DecideHostile(Far) == EHostileStep::Close);
        Far = View;
        Far.bWalled = true;
        TestTrue(TEXT("a wall between is not reach"), DecideHostile(Far) == EHostileStep::Close);
        Far = View;
        Far.DistanceCm = Far.ReachCm * BackOffReachShare - 1.0;
        TestTrue(TEXT("back off"), DecideHostile(Far) == EHostileStep::BackOff);
    }
    {
        FHostileView Busy = View;
        Busy.SwingUntil = 11.0;
        Busy.NextAttackAt = 20.0;
        TestTrue(TEXT("still swinging"), DecideHostile(Busy) == EHostileStep::Swinging);
        Busy.SwingUntil = 0.0;
        TestTrue(TEXT("hands up between blows"), DecideHostile(Busy) == EHostileStep::Recover);
    }

    // The original's tick arithmetic and Godot's String::hash().
    TestEqual(TEXT("attack interval ticks"), TicksForSeconds(1.1), int64(11));
    TestEqual(TEXT("swing ticks"), TicksForSeconds(SwingSeconds), int64(4));
    TestEqual(TEXT("heavy stagger ticks"), TicksForSeconds(HeavyStaggerSeconds), int64(6));
    TestEqual(TEXT("at least one tick"), TicksForSeconds(0.01), int64(1));
    TestEqual(TEXT("djb2 of empty"), GodotIdHash(TEXT("")), uint32(5381));
    TestEqual(TEXT("djb2 of a"), GodotIdHash(TEXT("a")), uint32(5381 * 33 + 'a'));
    TestTrue(TEXT("first pause"), FirstPauseTicks(GodotIdHash(TEXT("lent_drunk"))) >= PauseTicks
        && FirstPauseTicks(GodotIdHash(TEXT("lent_drunk"))) < PauseTicks + 25);
    for (int64 Tick = 0; Tick < 200; ++Tick)
    {
        const FVector Offset = WanderOffsetCm(GodotIdHash(TEXT("lent_passerby")), Tick);
        if (Offset.Size2D() > WanderRadiusCm + 0.5 || Offset.Z != 0.0)
        {
            AddError(FString::Printf(TEXT("wander offset %s at tick %lld leaves the stroll"), *Offset.ToString(), Tick));
            break;
        }
    }

    // The original's frame onto Unreal's: x east, z south, y up, metres; facing 0 looks along -z (north).
    const double Godot[3] = { -118.0, 1.8, -62.0 };
    TestEqual(TEXT("drinker in Unreal"), GodotPositionToUnreal(Godot), FVector(6200.0, -11800.0, 180.0));
    TestEqual(TEXT("facing 0 is yaw 0"), GodotFacingToUnrealYaw(0.0), 0.0);
    TestTrue(TEXT("facing +pi/2 looks along -x, yaw -90"), FMath::IsNearlyEqual(GodotFacingToUnrealYaw(UE_DOUBLE_HALF_PI), -90.0, 1e-4));

    // The cast: every person resolves, one fights back.
    int32 Hostiles = 0;
    for (const FNpcDefinition& Npc : Npcs)
    {
        TestNotNull(TEXT("found by id"), FindNpc(Npc.Id));
        Hostiles += FCString::Strcmp(Npc.Role, RoleHostile) == 0 ? 1 : 0;
    }
    TestEqual(TEXT("one hostile"), Hostiles, 1);
    TestEqual(TEXT("sound path"), SoundPath(JabHitSound, 3), FString(TEXT("/Game/Audio/Combat/impactPunch_medium_003.impactPunch_medium_003")));
    return true;
}

#endif
