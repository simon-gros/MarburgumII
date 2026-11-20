#pragma once

#include "CoreMinimal.h"
#include "MarburgumInteractable.h"
#include "MarburgumNpc.generated.h"

class APawn;
class UAnimSequenceBase;
class USkeletalMeshComponent;
class UStaticMeshComponent;

/**
 * A walk between two of a person's posts (ADR-081): where from, where to and when it began on the server's world
 * clock. Every machine works out the same position from it, so it travels once per walk, not every frame.
 */
USTRUCT()
struct FMarburgumNpcWalk
{
    GENERATED_BODY()

    UPROPERTY()
    FVector_NetQuantize From = FVector::ZeroVector;

    UPROPERTY()
    FVector_NetQuantize To = FVector::ZeroVector;

    UPROPERTY()
    double StartSeconds = -1.0;

    /** Where the body is at ServerSeconds, and whether it is still on its way. */
    FVector At(double ServerSeconds, bool& bOutWalking) const;
};

/**
 * An authored non-player character the player can talk to (ontology `npc`). Talking is an ordinary
 * interaction request: the server decides what it means for the requesting character, e.g. accepting
 * the mission this NPC offers (MarburgumII::Gameplay::FindMissionOfferedBy). An NPC holds no stock.
 *
 * The body is the template mannequin (ADR-081), tinted with the person's colours from the original
 * npc_visual_profiles.json, breathing in the template idle. It turns toward the nearest player who comes close
 * and, when the server says someone spoke to it, turns to them and talks with its hands for a few seconds; every
 * client sees the same. The riverside angler holds a period spinning rod on fishing_rod_r and runs a fishing cycle
 * (cast, wait with a float out on the Drava, reel in, stand) read from the server's clock, so every client draws
 * the same cast at the same moment (MarburgumII::Npc::AnglerMomentAt). Nothing about the body is gameplay: the
 * collision cylinder that stands in the way and the interaction range are what the server uses.
 *
 * A person with day posts (ADR-081) walks between them as the Marburgum clock turns: the server decides the post and
 * the walk, the walk replicates, and everyone sees the same stroll. Speaking to someone on the way stops them for the
 * conversation; the next post check sets them off again.
 */
UCLASS()
class MARBURGUMII_API AMarburgumNpc : public AMarburgumInteractable
{
    GENERATED_BODY()

public:
    AMarburgumNpc();

    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual bool IsItemSource() const override { return false; }

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marburgum|Npc")
    FLinearColor PrimaryColor = FLinearColor(0.1f, 0.13f, 0.09f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marburgum|Npc")
    FLinearColor SecondaryColor = FLinearColor(0.035f, 0.04f, 0.035f);

    /** Hold the fishing rod and fish (the riverside angler). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marburgum|Npc")
    bool bHoldsRod = false;

    /** Server: someone was answered here; everybody sees this person turn to them and talk. */
    void ConverseAuthority(APawn* With);

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /** Server, after spawning: the authored spot first, then one resolved point per day post (ADR-081). */
    void SetPostsAuthority(const TArray<FVector>& Points);
    /** Server: be at post Index (0 the authored spot); starts a walk when the person is elsewhere. */
    void GoToPostAuthority(int32 Index);
    int32 GetPostCount() const { return Posts.Num(); }
    /** The post the person is at or walking to; INDEX_NONE before any. */
    int32 GetCurrentPost() const { return CurrentPost; }
    /** Presentation and evidence: on the way between posts right now. */
    bool IsWalking() const;

    /** Presentation evidence: the clip the body is playing (idle, talk, fish_cast, ...), None on a server. */
    FName GetPlayingClip() const { return PlayingClip; }
    /** Presentation evidence: the rod is on the right hand's fishing_rod_r socket. */
    bool IsRodInHand() const;
    /** Presentation evidence: the float is out on the water. */
    bool IsLineOut() const;
    /** Presentation evidence: degrees the body is turned from where the person was placed to face. */
    double GetBodyTurnDegrees() const { return BodyTurn; }
    USkeletalMeshComponent* GetBody() const { return Body; }

protected:
    virtual void ConfigureFromCatalogue() override;

private:
    UFUNCTION(NetMulticast, Unreliable)
    void MulticastConverse(APawn* With);

    void ApplyLook();
    void Play(UAnimSequenceBase* Clip, FName Name, bool bLoop, float Position);
    /** The pawn this person is paying attention to this frame, if any. */
    APawn* AttentionTarget() const;
    void TurnBody(float DeltaSeconds, const APawn* Toward, bool bTalking);
    void UpdateAngler(double ServerSeconds);
    void ShowLine(bool bOut, double Reeled);

    UPROPERTY(VisibleAnywhere, Category = "Marburgum|Npc")
    TObjectPtr<USceneComponent> Root;

    UPROPERTY(VisibleAnywhere, Category = "Marburgum|Npc")
    TObjectPtr<USkeletalMeshComponent> Body;

    UPROPERTY(VisibleAnywhere, Category = "Marburgum|Npc")
    TObjectPtr<UStaticMeshComponent> Rod;

    /** One segment from the rod tip to the float; drawn only while the line is out. */
    UPROPERTY(VisibleAnywhere, Category = "Marburgum|Npc")
    TObjectPtr<UStaticMeshComponent> Line;

    UPROPERTY(VisibleAnywhere, Category = "Marburgum|Npc")
    TObjectPtr<UStaticMeshComponent> Float;

    UPROPERTY(Transient)
    TObjectPtr<UAnimSequenceBase> IdleClip;

    UPROPERTY(Transient)
    TObjectPtr<UAnimSequenceBase> TalkClip;

    UPROPERTY(Transient)
    TObjectPtr<UAnimSequenceBase> WalkClip;

    UFUNCTION()
    void OnRep_Walk();
    /** The server world time every machine agrees on. */
    double ServerSeconds() const;

    UPROPERTY(ReplicatedUsing = OnRep_Walk)
    FMarburgumNpcWalk Walk;

    /** Server: the resolved posts, 0 the authored spot. */
    TArray<FVector> Posts;
    int32 CurrentPost = INDEX_NONE;
    /** Server: a conversation stopped a walk until then. */
    double PausedUntilSeconds = 0.0;

    UPROPERTY(Transient)
    TObjectPtr<UAnimSequenceBase> FishClips[4];

    TWeakObjectPtr<APawn> TalkingTo;
    double TalkUntilSeconds = 0.0;
    FName PlayingClip;
    int64 PlayingCycle = -1;
    double BodyTurn = 0.0;
    uint32 Seed = 0;
    /** Where the float lands for this cast, found once per cast. */
    FVector FloatAt = FVector::ZeroVector;
    bool bFloatKnown = false;
    bool bFloatOnWater = false;
};
