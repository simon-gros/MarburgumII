#include "MarburgumCombatContentCommandlet.h"

#include "MarburgumGodotClip.h"

#include "Animation/AnimSequence.h"
#include "AssetToolsModule.h"
#include "AutomatedAssetImportData.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "IAssetTools.h"
#include "MarburgumCombatContract.h"
#include "Misc/Paths.h"
#include "Sound/SoundWave.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumCombatContent, Log, All);

namespace MarburgumCombatContent
{
    namespace Combat = MarburgumII::Combat;

    constexpr const TCHAR* SoundSource = TEXT("SourceArt/Audio/Combat/KenneyImpactSounds");

    // ======================================================================================================
    // The fight clips (tools/build_character.gd: _guard_pose, _guard_animation, _fight_animation).
    // ======================================================================================================

    /** One _guard_pose dictionary: model-space Euler keys per bone. */
    struct FPose
    {
        FVector Hips;
        FVector Spine;
        FVector Head;
        FVector LeftUpper;
        FVector LeftLower;
        FVector RightUpper;
        FVector RightLower;
        FVector LeftThigh;
        FVector RightThigh;
        FVector LeftKnee;
        FVector RightKnee;
    };

    /** _guard_pose(open): 0 is the tight guard, 1 a slightly dropped breath between beats. */
    FPose GuardPose(double Open)
    {
        FPose Pose;
        Pose.Hips = FVector(0.04, 0, 0);
        Pose.Spine = FVector(0.08, -0.12, 0);
        Pose.Head = FVector(0.06, -0.1, 0);
        Pose.LeftUpper = FMath::Lerp(FVector(0.85, 0.15, 0.95), FVector(0.7, 0.1, 0.8), Open);
        Pose.LeftLower = FVector(1.35, 0, 0);
        Pose.RightUpper = FMath::Lerp(FVector(0.95, -0.1, -0.85), FVector(0.75, -0.05, -0.7), Open);
        Pose.RightLower = FVector(1.45, 0, 0);
        Pose.LeftThigh = FVector(0.12, 0.08, 0);
        Pose.RightThigh = FVector(0.18, -0.1, 0);
        Pose.LeftKnee = FVector(-0.2, 0, 0);
        Pose.RightKnee = FVector(-0.28, 0, 0);
        return Pose;
    }

    /** _apply_pose's bones, each a track over the given poses. */
    TArray<MarburgumGodotClip::FTrack> Tracks(const TArray<FPose>& Keys)
    {
        const auto Track = [&Keys](const TCHAR* Bone, FVector FPose::*Field)
        {
            MarburgumGodotClip::FTrack Out{ Bone, {} };
            for (const FPose& Pose : Keys)
            {
                Out.Keys.Add(Pose.*Field);
            }
            return Out;
        };
        return {
            Track(TEXT("Hips"), &FPose::Hips), Track(TEXT("Spine"), &FPose::Spine), Track(TEXT("Head"), &FPose::Head),
            Track(TEXT("LeftUpperArm"), &FPose::LeftUpper), Track(TEXT("LeftLowerArm"), &FPose::LeftLower),
            Track(TEXT("RightUpperArm"), &FPose::RightUpper), Track(TEXT("RightLowerArm"), &FPose::RightLower),
            Track(TEXT("LeftUpperLeg"), &FPose::LeftThigh), Track(TEXT("RightUpperLeg"), &FPose::RightThigh),
            Track(TEXT("LeftLowerLeg"), &FPose::LeftKnee), Track(TEXT("RightLowerLeg"), &FPose::RightKnee),
        };
    }

    /** _guard_animation: the tight guard, the dropped breath, the tight guard; the hips sink a little between. */
    MarburgumGodotClip::FClip GuardClip()
    {
        MarburgumGodotClip::FClip Clip;
        Clip.Package = TEXT("/Game/Characters/Animations/A_FightGuard");
        Clip.Title = TEXT("Marburgum fight guard");
        Clip.LengthSeconds = Combat::GuardClipSeconds;
        Clip.bLoop = true;
        Clip.Tracks = Tracks({ GuardPose(0.0), GuardPose(1.0), GuardPose(0.0) });
        Clip.HipsOffsets = { FVector::ZeroVector, FVector(0, -0.012, 0), FVector::ZeroVector };
        return Clip;
    }

    /** _fight_animation(heavy): guard, wind-up, strike, guard, at the original's key times. */
    MarburgumGodotClip::FClip FightClip(bool bHeavy)
    {
        const FPose Guard = GuardPose(0.0);
        FPose Wind = Guard;
        FPose Strike = Guard;
        if (bHeavy)
        {
            // A heavy swing loads the right hip and comes across.
            Wind.Spine = FVector(0.12, 0.55, 0);
            Wind.Hips = FVector(-0.05, 0.35, 0);
            Wind.RightUpper = FVector(-0.35, -0.2, -0.9);
            Wind.RightLower = FVector(1.5, 0, 0);
            Wind.RightThigh = FVector(-0.15, 0, 0);
            Wind.Head = FVector(0.05, 0.25, 0);
            Strike.Spine = FVector(0.2, -0.7, 0);
            Strike.Hips = FVector(0.18, -0.45, 0);
            Strike.RightUpper = FVector(1.25, 0.35, -0.25);
            Strike.RightLower = FVector(0.35, 0, 0);
            Strike.RightThigh = FVector(0.55, 0, 0);
            Strike.LeftThigh = FVector(-0.1, 0, 0);
            Strike.Head = FVector(0.08, -0.3, 0);
        }
        else
        {
            // A jab is a short straight right.
            Wind.RightUpper = FVector(0.45, -0.25, -0.55);
            Wind.RightLower = FVector(1.55, 0, 0);
            Wind.Spine = FVector(0.06, 0.18, 0);
            Strike.RightUpper = FVector(1.35, 0.05, -0.12);
            Strike.RightLower = FVector(0.2, 0, 0);
            Strike.Spine = FVector(0.1, -0.28, 0);
            Strike.Hips = FVector(0.08, -0.16, 0);
            Strike.RightThigh = FVector(0.28, 0, 0);
        }
        MarburgumGodotClip::FClip Clip;
        Clip.Package = bHeavy ? TEXT("/Game/Characters/Animations/A_FightHeavy") : TEXT("/Game/Characters/Animations/A_FightJab");
        Clip.Title = bHeavy ? TEXT("Marburgum fight heavy") : TEXT("Marburgum fight jab");
        Clip.LengthSeconds = bHeavy ? Combat::HeavyClipSeconds : Combat::JabClipSeconds;
        Clip.bLoop = false;
        Clip.Tracks = Tracks({ Guard, Wind, Strike, Guard });
        const double L = Clip.LengthSeconds;
        Clip.KeyTimes = bHeavy ? TArray<double>{ 0.0, L * 0.32, L * 0.55, L } : TArray<double>{ 0.0, L * 0.28, L * 0.5, L };
        return Clip;
    }

    bool BuildAnimations()
    {
        bool bOk = true;
        for (const MarburgumGodotClip::FClip& Clip : { GuardClip(), FightClip(false), FightClip(true) })
        {
            const UAnimSequence* Sequence = MarburgumGodotClip::Build(Clip, TEXT("MB2COMBAT"));
            bOk &= Sequence != nullptr;
            if (Sequence)
            {
                // The strike key carries the reach; check it and the ends against the original's own directions.
                TArray<double> Times = { 0.0, Clip.LengthSeconds * 0.5, Clip.LengthSeconds };
                if (Clip.KeyTimes.Num() == 4)
                {
                    Times = { 0.0, Clip.KeyTimes[1], Clip.KeyTimes[2], Clip.LengthSeconds };
                }
                MarburgumGodotClip::LogLimbCheck(*Sequence, Times, TEXT("MB2COMBAT"));
            }
        }
        return bOk;
    }

    // ======================================================================================================
    // The impact sounds (Kenney Impact Sounds 1.0, CC0).
    // ======================================================================================================

    bool ImportSounds()
    {
        const FString Directory = FPaths::Combine(FPaths::ProjectDir(), SoundSource);
        TArray<FString> Files;
        for (const TCHAR* Stem : { Combat::JabHitSound, Combat::HeavyHitSound, Combat::BlockedSound, Combat::FloorSound })
        {
            for (int32 Variant = 0; Variant < Combat::SoundVariants; ++Variant)
            {
                const FString File = FPaths::Combine(Directory, FString::Printf(TEXT("%s_%03d.wav"), Stem, Variant));
                if (!IFileManager::Get().FileExists(*File))
                {
                    UE_LOG(LogMarburgumCombatContent, Error, TEXT("MB2COMBAT missing sound %s"), *File);
                    return false;
                }
                Files.Add(File);
            }
        }
        UAutomatedAssetImportData* Import = NewObject<UAutomatedAssetImportData>();
        Import->DestinationPath = Combat::SoundFolder;
        Import->Filenames = Files;
        Import->bReplaceExisting = true;
        IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
        const TArray<UObject*> Imported = AssetTools.ImportAssetsAutomated(Import);
        TArray<UPackage*> Packages;
        for (UObject* Asset : Imported)
        {
            if (const USoundWave* Wave = Cast<USoundWave>(Asset))
            {
                Packages.AddUnique(Wave->GetOutermost());
                UE_LOG(LogMarburgumCombatContent, Display, TEXT("MB2COMBAT sound %s duration=%.3f channels=%d rate=%d"), *Wave->GetPathName(),
                    Wave->Duration, Wave->NumChannels, static_cast<int32>(Wave->GetSampleRateForCurrentPlatform()));
            }
        }
        if (Packages.Num() != Files.Num())
        {
            UE_LOG(LogMarburgumCombatContent, Error, TEXT("MB2COMBAT imported %d of %d sounds"), Packages.Num(), Files.Num());
            return false;
        }
        if (!UEditorLoadingAndSavingUtils::SavePackages(Packages, false))
        {
            UE_LOG(LogMarburgumCombatContent, Error, TEXT("MB2COMBAT could not save the sounds"));
            return false;
        }
        UE_LOG(LogMarburgumCombatContent, Display, TEXT("MB2COMBAT saved sounds=%d under %s"), Packages.Num(), Combat::SoundFolder);
        return true;
    }
}

UMarburgumCombatContentCommandlet::UMarburgumCombatContentCommandlet()
{
    IsClient = false;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
}

int32 UMarburgumCombatContentCommandlet::Main(const FString& Params)
{
    FString Steps = TEXT("animation,audio");
    FParse::Value(*Params, TEXT("Steps="), Steps, false);
    TArray<FString> List;
    Steps.ParseIntoArray(List, TEXT(","));
    bool bOk = true;
    for (const FString& Step : List)
    {
        if (Step == TEXT("animation"))
        {
            bOk &= MarburgumCombatContent::BuildAnimations();
        }
        else if (Step == TEXT("audio"))
        {
            bOk &= MarburgumCombatContent::ImportSounds();
        }
        else
        {
            UE_LOG(LogMarburgumCombatContent, Error, TEXT("MB2COMBAT unknown step %s"), *Step);
            bOk = false;
        }
    }
    UE_LOG(LogMarburgumCombatContent, Display, TEXT("MB2COMBAT result=%d"), bOk ? 0 : 1);
    return bOk ? 0 : 1;
}
