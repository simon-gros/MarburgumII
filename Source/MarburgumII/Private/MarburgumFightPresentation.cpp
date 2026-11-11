#include "MarburgumFightPresentation.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "MarburgumCombatContract.h"
#include "MarburgumNetworkContract.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace Combat = MarburgumII::Combat;

namespace
{
    const FName FightSlot(TEXT("DefaultSlot"));
    // A swing snaps in: the original blends a one-shot in 0.08 s.
    constexpr float SwingBlendIn = 0.08f;
    constexpr float SwingBlendOut = 0.15f;
    constexpr float GuardBlend = 0.2f;
    constexpr float DownBlendIn = 0.12f;
    constexpr float DownBlendOut = 0.2f;
    constexpr float GetUpBlendOut = 0.45f;
    // Standing still enough to show the guard rather than walking over it.
    constexpr float GuardStillCmPerSecond = 20.f;
    constexpr int32 GuardLoops = 100000;

    template <typename T>
    T* Find(const TCHAR* Path)
    {
        ConstructorHelpers::FObjectFinderOptional<T> Finder(Path);
        return Finder.Get();
    }

    void FindSounds(TArray<TObjectPtr<USoundBase>>& Out, const TCHAR* Stem)
    {
        for (int32 Variant = 0; Variant < Combat::SoundVariants; ++Variant)
        {
            const FString Path = Combat::SoundPath(Stem, Variant);
            if (USoundBase* Sound = Find<USoundBase>(*Path))
            {
                Out.Add(Sound);
            }
        }
    }

    const UMarburgumFightAssets& Assets()
    {
        return *GetDefault<UMarburgumFightAssets>();
    }
}

UMarburgumFightAssets::UMarburgumFightAssets()
{
    if (!HasAnyFlags(RF_ClassDefaultObject))
    {
        return;
    }
    Guard = Find<UAnimSequenceBase>(Combat::GuardClipPath);
    Jab = Find<UAnimSequenceBase>(Combat::JabClipPath);
    Heavy = Find<UAnimSequenceBase>(Combat::HeavyClipPath);
    Down = Find<UAnimSequenceBase>(Combat::DownClipPath);
    FindSounds(JabHits, Combat::JabHitSound);
    FindSounds(HeavyHits, Combat::HeavyHitSound);
    FindSounds(Blocked, Combat::BlockedSound);
    FindSounds(Floored, Combat::FloorSound);
}

void FMarburgumFightAnimator::Update(UAnimInstance* Anim, const FMarburgumFightPose& Pose, float Speed, bool bMayGuard)
{
    if (!Anim)
    {
        return;
    }
    const UMarburgumFightAssets& Clips = Assets();

    // On the floor: the fall, held on its last frame; nothing else shows.
    if (Pose.bDown)
    {
        bSeenFirst = true;
        SeenSwingSeq = Pose.SwingSeq;
        UAnimMontage* Down = DownMontage.Get();
        if (!Down || !Anim->Montage_IsActive(Down))
        {
            if (Clips.Down)
            {
                DownMontage = Anim->PlaySlotAnimationAsDynamicMontage(Clips.Down, FightSlot, DownBlendIn, DownBlendOut);
            }
            SwingMontage.Reset();
            GuardMontage.Reset();
        }
        else if (Anim->Montage_IsPlaying(Down))
        {
            // Pause before the automatic blend-out at the end would stand the body back up.
            if (Anim->Montage_GetPosition(Down) >= Down->GetPlayLength() - DownBlendOut - 0.05f)
            {
                Anim->Montage_Pause(Down);
            }
        }
        return;
    }
    if (UAnimMontage* Down = DownMontage.Get())
    {
        Anim->Montage_Stop(GetUpBlendOut, Down);
        DownMontage.Reset();
    }

    // A new sequence is a new swing; the first sequence seen is history, not a swing to draw.
    if (!bSeenFirst)
    {
        bSeenFirst = true;
        SeenSwingSeq = Pose.SwingSeq;
    }
    if (Pose.SwingSeq != SeenSwingSeq)
    {
        SeenSwingSeq = Pose.SwingSeq;
        UAnimSequenceBase* Clip = Pose.bHeavy ? Clips.Heavy.Get() : Clips.Jab.Get();
        if (Clip)
        {
            SwingMontage = Anim->PlaySlotAnimationAsDynamicMontage(Clip, FightSlot, SwingBlendIn, SwingBlendOut);
            bSwingHeavy = Pose.bHeavy;
            GuardMontage.Reset();
        }
    }
    const bool bSwinging = SwingMontage.IsValid() && Anim->Montage_IsPlaying(SwingMontage.Get());

    const bool bShowGuard = bMayGuard && Pose.bGuarding && !bSwinging && Speed < GuardStillCmPerSecond && Clips.Guard;
    UAnimMontage* Guard = GuardMontage.Get();
    const bool bGuardPlaying = Guard && Anim->Montage_IsPlaying(Guard);
    if (bShowGuard && !bGuardPlaying)
    {
        GuardMontage = Anim->PlaySlotAnimationAsDynamicMontage(Clips.Guard, FightSlot, GuardBlend, GuardBlend, 1.f, GuardLoops);
    }
    else if (!bShowGuard && bGuardPlaying)
    {
        Anim->Montage_Stop(GuardBlend, Guard);
        GuardMontage.Reset();
    }
}

FName FMarburgumFightAnimator::GetPlaying(const UAnimInstance* Anim) const
{
    if (!Anim)
    {
        return NAME_None;
    }
    if (DownMontage.IsValid() && Anim->Montage_IsActive(DownMontage.Get()))
    {
        return TEXT("down");
    }
    if (SwingMontage.IsValid() && Anim->Montage_IsPlaying(SwingMontage.Get()))
    {
        return bSwingHeavy ? TEXT("heavy") : TEXT("jab");
    }
    if (GuardMontage.IsValid() && Anim->Montage_IsPlaying(GuardMontage.Get()))
    {
        return TEXT("guard");
    }
    return NAME_None;
}

void FMarburgumFightAnimator::Stop(UAnimInstance* Anim)
{
    for (TWeakObjectPtr<UAnimMontage>* Montage : { &SwingMontage, &GuardMontage, &DownMontage })
    {
        if (Anim && Montage->IsValid())
        {
            Anim->Montage_Stop(SwingBlendOut, Montage->Get());
        }
        Montage->Reset();
    }
}

namespace MarburgumFightSound
{
    void PlayImpact(UWorld* World, const FVector& At, FName Outcome, bool bHeavy, bool bFloored)
    {
        if (!World || World->GetNetMode() == NM_DedicatedServer)
        {
            return;
        }
        const UMarburgumFightAssets& Sounds = Assets();
        const TArray<TObjectPtr<USoundBase>>* Set = nullptr;
        if (Outcome == Combat::OutcomeBlocked)
        {
            Set = &Sounds.Blocked;
        }
        else if (Outcome == Combat::OutcomeHit)
        {
            Set = bHeavy ? &Sounds.HeavyHits : &Sounds.JabHits;
        }
        USoundBase* Blow = Set && Set->Num() > 0 ? (*Set)[FMath::RandRange(0, Set->Num() - 1)].Get() : nullptr;
        USoundBase* Fall = bFloored && Sounds.Floored.Num() > 0 ? Sounds.Floored[FMath::RandRange(0, Sounds.Floored.Num() - 1)].Get() : nullptr;
        for (USoundBase* Sound : { Blow, Fall })
        {
            if (Sound)
            {
                UGameplayStatics::PlaySoundAtLocation(World, Sound, At);
            }
        }
        // Evidence for the combat smoke, which runs without an audio device: which sounds this blow chose.
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=client event=impact outcome=%s heavy=%d floored=%d sound=%s fall=%s at=%.0f,%.0f,%.0f"),
            Combat::CombatLogPrefix, *Outcome.ToString(), bHeavy ? 1 : 0, bFloored ? 1 : 0, Blow ? *Blow->GetName() : TEXT("-"),
            Fall ? *Fall->GetName() : TEXT("-"), At.X, At.Y, At.Z);
    }
}
