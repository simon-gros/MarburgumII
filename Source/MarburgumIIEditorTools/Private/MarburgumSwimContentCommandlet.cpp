#include "MarburgumSwimContentCommandlet.h"

#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "BSPOps.h"
#include "Builders/CubeBuilder.h"
#include "Engine/Polys.h"
#include "LandscapeProxy.h"
#include "MeshDescription.h"
#include "Model.h"
#include "Components/BrushComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/PhysicsVolume.h"
#include "Landscape.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumSwim, Log, All);

namespace
{
    constexpr const TCHAR* SkeletonPath = TEXT("/Game/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin");
    constexpr const TCHAR* PreviewMeshPath = TEXT("/Game/Characters/Protagonist/SKM_Protagonist.SKM_Protagonist");
    constexpr const TCHAR* SwimPackage = TEXT("/Game/Characters/Animations/A_Swim");
    constexpr const TCHAR* TreadPackage = TEXT("/Game/Characters/Animations/A_SwimTread");
    constexpr const TCHAR* LentMap = TEXT("/Game/World/lent");

    // ---- The original swim cycle (magna-raedarum-marburgum tools/build_character.gd, _swim_animation) ----
    //
    // Values are the original's model-space Euler angles in radians on the Godot humanoid: Y up, the body
    // faces -Z, a positive X rotation swings a limb forward and a positive Z rotation lifts the left arm
    // out to the side. Godot composes them Y, then X, then Z (Basis.from_euler, EULER_ORDER_YXZ).
    constexpr double SwimCycleSeconds = 1.2;
    constexpr double TreadCycleSeconds = 1.6;
    constexpr int32 FramesPerSecond = 30;

    struct FGodotTrack
    {
        const TCHAR* GodotBone;
        TArray<FVector> Keys; // evenly spaced over the cycle; the last key equals the first
    };

    TArray<FVector> Shift(const TArray<FVector>& Values, int32 Steps)
    {
        // _shift(): the same loop started Steps keys later (the last key repeats the first).
        TArray<FVector> Out;
        const int32 Period = Values.Num() - 1;
        for (int32 Index = 0; Index < Values.Num(); ++Index)
        {
            Out.Add(Values[(Index + Steps) % Period]);
        }
        return Out;
    }

    TArray<FVector> Mirror(TArray<FVector> Values)
    {
        // _mirror(): the same motion on the other side of the body.
        for (FVector& Value : Values)
        {
            Value = FVector(Value.X, -Value.Y, -Value.Z);
        }
        return Values;
    }

    TArray<FVector> Cycle(std::initializer_list<double> Values)
    {
        // _cycle(values, Vector3.RIGHT): an X rotation per key, looping back to the first.
        TArray<FVector> Out;
        const TArray<double> List(Values);
        for (int32 Index = 0; Index <= List.Num(); ++Index)
        {
            Out.Add(FVector(List[Index % List.Num()], 0.0, 0.0));
        }
        return Out;
    }

    TArray<FGodotTrack> SwimTracks()
    {
        TArray<FGodotTrack> Tracks;
        // ADR-033: the original pitched the hips 49 degrees and swung both thighs forward together, so the
        // legs hung under the body and the swimmer looked as if it were drowning. The body now lies along
        // the water: the hips pitch 77 degrees and the chest arches 3 degrees back up, so the chest sits
        // where the original's did (74 degrees against 70) and the original arm stroke below still fits it.
        Tracks.Add({ TEXT("Hips"), { FVector(-1.35, 0.06, 0), FVector(-1.35, -0.06, 0), FVector(-1.35, 0.06, 0) } });
        Tracks.Add({ TEXT("Spine"), { FVector(0.05, -0.05, 0), FVector(0.05, 0.05, 0), FVector(0.05, -0.05, 0) } });
        Tracks.Add({ TEXT("UpperChest"), { FVector(0, 0, 0), FVector(0, 0, 0), FVector(0, 0, 0) } });
        // The head comes back up so the face looks ahead, not into the water.
        Tracks.Add({ TEXT("Head"), { FVector(0.8, 0.08, 0), FVector(0.8, -0.08, 0), FVector(0.8, 0.08, 0) } });

        // Reach, catch, pull, recover. The other arm is half a cycle behind.
        const TArray<FVector> UpperArm = {
            FVector(1.45, 0, 0.35), FVector(0.55, 0, 0.15), FVector(-0.35, 0, 0.55), FVector(0.45, 0, 0.7), FVector(1.45, 0, 0.35),
        };
        const TArray<FVector> LowerArm = {
            FVector(0.15, 0, 0), FVector(0.9, 0, 0), FVector(0.45, 0, 0), FVector(0.2, 0, 0), FVector(0.15, 0, 0),
        };
        Tracks.Add({ TEXT("LeftUpperArm"), UpperArm });
        Tracks.Add({ TEXT("LeftLowerArm"), LowerArm });
        Tracks.Add({ TEXT("RightUpperArm"), Mirror(Shift(UpperArm, 2)) });
        Tracks.Add({ TEXT("RightLowerArm"), Mirror(Shift(LowerArm, 2)) });

        // Flutter kick (ADR-033): the legs stay in line with the body, a little past straight, and kick
        // twice a cycle, the other leg half a kick behind. The knee bends on the up beat and straightens
        // on the down beat.
        const TArray<FVector> Thigh = Cycle({ 0.05, -0.25, 0.05, -0.25 });
        const TArray<FVector> Knee = Cycle({ -0.1, -0.4, -0.1, -0.4 });
        Tracks.Add({ TEXT("LeftUpperLeg"), Thigh });
        Tracks.Add({ TEXT("LeftLowerLeg"), Knee });
        Tracks.Add({ TEXT("RightUpperLeg"), Shift(Thigh, 1) });
        Tracks.Add({ TEXT("RightLowerLeg"), Shift(Knee, 1) });
        return Tracks;
    }

    // The hips bob two centimetres up and down, once per cycle (the original's position track).
    const TArray<double> HipsBobMetres = { 0.02, -0.02, 0.02 };

    /**
     * Treading water (ADR-036), Marburgum II's own; the original had no idle swim. Upright, the chest a
     * little forward and the face level. The legs pedal, each thigh rising and the knee bending in turn,
     * the other leg half a cycle behind. The forearms scull out and back in front of the chest, both
     * together. The body rises a little on each leg's push.
     */
    TArray<FGodotTrack> TreadTracks()
    {
        TArray<FGodotTrack> Tracks;
        Tracks.Add({ TEXT("Hips"), { FVector(-0.12, 0.04, 0), FVector(-0.12, -0.04, 0), FVector(-0.12, 0.04, 0) } });
        Tracks.Add({ TEXT("Spine"), { FVector(-0.05, 0, 0), FVector(-0.05, 0, 0), FVector(-0.05, 0, 0) } });
        Tracks.Add({ TEXT("Head"), { FVector(0.17, 0, 0), FVector(0.17, 0, 0), FVector(0.17, 0, 0) } });

        // The upper arm is out to the side and forward, under the surface; the forearm sweeps out and in.
        const TArray<FVector> UpperArm = {
            FVector(0.55, 0, 0.75), FVector(0.4, 0, 0.95), FVector(0.55, 0, 0.75), FVector(0.4, 0, 0.95), FVector(0.55, 0, 0.75),
        };
        const TArray<FVector> LowerArm = {
            FVector(0.7, 0, 0), FVector(0.45, 0, 0), FVector(0.7, 0, 0), FVector(0.45, 0, 0), FVector(0.7, 0, 0),
        };
        Tracks.Add({ TEXT("LeftUpperArm"), UpperArm });
        Tracks.Add({ TEXT("LeftLowerArm"), LowerArm });
        Tracks.Add({ TEXT("RightUpperArm"), Mirror(UpperArm) });
        Tracks.Add({ TEXT("RightLowerArm"), Mirror(LowerArm) });

        // Up, push down: the thigh rises with the knee bent and presses down as the knee straightens.
        const TArray<FVector> Thigh = { FVector(0.75, 0, 0), FVector(0.45, 0, 0), FVector(0.2, 0, 0), FVector(0.45, 0, 0), FVector(0.75, 0, 0) };
        const TArray<FVector> Knee = { FVector(-1.1, 0, 0), FVector(-0.55, 0, 0), FVector(-0.3, 0, 0), FVector(-0.85, 0, 0), FVector(-1.1, 0, 0) };
        Tracks.Add({ TEXT("LeftUpperLeg"), Thigh });
        Tracks.Add({ TEXT("LeftLowerLeg"), Knee });
        Tracks.Add({ TEXT("RightUpperLeg"), Shift(Thigh, 2) });
        Tracks.Add({ TEXT("RightLowerLeg"), Shift(Knee, 2) });
        return Tracks;
    }

    // A push from each leg lifts the body two centimetres.
    const TArray<double> TreadBobMetres = { -0.01, 0.02, -0.01, 0.02, -0.01 };

    struct FSwimClip
    {
        const TCHAR* Package;
        double CycleSeconds;
        TArray<FGodotTrack> Tracks;
        TArray<double> BobMetres;
    };

    // The Godot humanoid's chain for the bones the cycle moves: a bone not listed follows its parent.
    const TMap<FString, FString> GodotParents = {
        { TEXT("Spine"), TEXT("Hips") },
        { TEXT("UpperChest"), TEXT("Spine") },
        { TEXT("Head"), TEXT("UpperChest") },
        { TEXT("LeftUpperArm"), TEXT("UpperChest") },
        { TEXT("LeftLowerArm"), TEXT("LeftUpperArm") },
        { TEXT("RightUpperArm"), TEXT("UpperChest") },
        { TEXT("RightLowerArm"), TEXT("RightUpperArm") },
        { TEXT("LeftUpperLeg"), TEXT("Hips") },
        { TEXT("LeftLowerLeg"), TEXT("LeftUpperLeg") },
        { TEXT("RightUpperLeg"), TEXT("Hips") },
        { TEXT("RightLowerLeg"), TEXT("RightUpperLeg") },
    };

    // Mannequin bone -> the Godot bone whose motion it carries.
    const TMap<FName, FString> MannequinToGodot = {
        { TEXT("pelvis"), TEXT("Hips") },
        { TEXT("spine_01"), TEXT("Spine") },
        { TEXT("spine_04"), TEXT("UpperChest") },
        { TEXT("head"), TEXT("Head") },
        { TEXT("upperarm_l"), TEXT("LeftUpperArm") },
        { TEXT("lowerarm_l"), TEXT("LeftLowerArm") },
        { TEXT("upperarm_r"), TEXT("RightUpperArm") },
        { TEXT("lowerarm_r"), TEXT("RightLowerArm") },
        { TEXT("thigh_l"), TEXT("LeftUpperLeg") },
        { TEXT("calf_l"), TEXT("LeftLowerLeg") },
        { TEXT("thigh_r"), TEXT("RightUpperLeg") },
        { TEXT("calf_r"), TEXT("RightLowerLeg") },
    };

    /**
     * The limbs' rest directions on the original humanoid (citizen_male_a.glb and player_male_a.glb, both
     * the same skeleton), Godot model space, from each joint to the next. The original's arms hang
     * nearly straight; the mannequin's reference pose is an A-pose with bent elbows, so each limb is
     * first turned onto the original's rest direction and the cycle is applied from there.
     */
    struct FRestAim
    {
        FName Bone;
        FName Child;
        FVector GodotDirection;
    };
    const FRestAim RestAims[] = {
        { TEXT("upperarm_l"), TEXT("lowerarm_l"), FVector(0.035, -0.285, 0.0) },
        { TEXT("lowerarm_l"), TEXT("hand_l"), FVector(0.032, -0.260, 0.0) },
        { TEXT("upperarm_r"), TEXT("lowerarm_r"), FVector(-0.035, -0.285, 0.0) },
        { TEXT("lowerarm_r"), TEXT("hand_r"), FVector(-0.032, -0.260, 0.0) },
        { TEXT("thigh_l"), TEXT("calf_l"), FVector(-0.016, -0.450, -0.005) },
        { TEXT("calf_l"), TEXT("foot_l"), FVector(-0.010, -0.385, 0.005) },
        { TEXT("thigh_r"), TEXT("calf_r"), FVector(0.016, -0.450, -0.005) },
        { TEXT("calf_r"), TEXT("foot_r"), FVector(0.010, -0.385, 0.005) },
    };

    /**
     * Godot model space -> mannequin component space. Up is +Y there and +Z here; the original faces -Z
     * and the mannequin +Y; the original's left bones sit at +X, as the mannequin's do. The map is a
     * proper rotation, so a rotation keeps its angle and only its axis moves.
     */
    FVector GodotToMannequin(const FVector& V)
    {
        return FVector(V.X, -V.Z, V.Y);
    }

    FQuat GodotEulerToMannequin(const FVector& Euler)
    {
        const FQuat Y(GodotToMannequin(FVector(0, 1, 0)), Euler.Y);
        const FQuat X(GodotToMannequin(FVector(1, 0, 0)), Euler.X);
        const FQuat Z(GodotToMannequin(FVector(0, 0, 1)), Euler.Z);
        return Y * X * Z;
    }

    /** Godot's looping cubic track, sampled at T seconds: Catmull-Rom through evenly spaced keys. */
    FVector SampleLoop(const TArray<FVector>& Keys, double T, double CycleSeconds)
    {
        const int32 Segments = Keys.Num() - 1;
        const double Position = FMath::Fmod(T / CycleSeconds, 1.0) * Segments;
        const int32 Segment = FMath::Clamp(FMath::FloorToInt32(Position), 0, Segments - 1);
        const double U = Position - Segment;
        const auto At = [&Keys, Segments](int32 Index) { return Keys[((Index % Segments) + Segments) % Segments]; };
        const FVector P0 = At(Segment - 1);
        const FVector P1 = At(Segment);
        const FVector P2 = At(Segment + 1);
        const FVector P3 = At(Segment + 2);
        return 0.5 * ((2.0 * P1) + (-P0 + P2) * U + (2.0 * P0 - 5.0 * P1 + 4.0 * P2 - P3) * U * U
            + (-P0 + 3.0 * P1 - 3.0 * P2 + P3) * U * U * U);
    }

    double SampleLoop(const TArray<double>& Keys, double T, double CycleSeconds)
    {
        TArray<FVector> Vectors;
        for (double Key : Keys)
        {
            Vectors.Add(FVector(Key, 0, 0));
        }
        return SampleLoop(Vectors, T, CycleSeconds).X;
    }

    UPackage* PreparePackage(const TCHAR* PackageName, UClass* Class, UObject*& OutExisting)
    {
        UPackage* Package = CreatePackage(PackageName);
        Package->FullyLoad();
        OutExisting = StaticFindObject(Class, Package, *FPackageName::GetShortName(PackageName));
        return Package;
    }

    bool BuildSwimAnimation(const FSwimClip& Clip)
    {
        USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, SkeletonPath);
        USkeletalMesh* Preview = LoadObject<USkeletalMesh>(nullptr, PreviewMeshPath);
        if (!Skeleton)
        {
            UE_LOG(LogMarburgumSwim, Error, TEXT("MB2SWIM missing skeleton %s"), SkeletonPath);
            return false;
        }
        const FReferenceSkeleton& Reference = Skeleton->GetReferenceSkeleton();
        const int32 BoneCount = Reference.GetRawBoneNum();
        const TArray<FTransform>& LocalRest = Reference.GetRawRefBonePose();

        // Component-space reference pose.
        TArray<FTransform> GlobalRest;
        GlobalRest.SetNum(BoneCount);
        for (int32 Bone = 0; Bone < BoneCount; ++Bone)
        {
            const int32 Parent = Reference.GetRawParentIndex(Bone);
            GlobalRest[Bone] = Parent == INDEX_NONE ? LocalRest[Bone] : LocalRest[Bone] * GlobalRest[Parent];
        }
        for (const TPair<FName, FString>& Pair : MannequinToGodot)
        {
            const int32 Bone = Reference.FindRawBoneIndex(Pair.Key);
            if (Bone == INDEX_NONE)
            {
                UE_LOG(LogMarburgumSwim, Error, TEXT("MB2SWIM mannequin has no bone %s"), *Pair.Key.ToString());
                return false;
            }
            const FVector At = GlobalRest[Bone].GetLocation();
            UE_LOG(LogMarburgumSwim, Display, TEXT("MB2SWIM bone %s <- %s rest=(%.1f, %.1f, %.1f)"), *Pair.Key.ToString(), *Pair.Value,
                At.X, At.Y, At.Z);
        }

        // Rest correction: turn each listed limb onto the original's rest direction (its parent's turn
        // carried down first), so the cycle starts from the pose it was keyed on.
        TArray<FQuat> Correction;
        Correction.Init(FQuat::Identity, BoneCount);
        for (int32 Bone = 0; Bone < BoneCount; ++Bone)
        {
            const int32 Parent = Reference.GetRawParentIndex(Bone);
            Correction[Bone] = Parent == INDEX_NONE ? FQuat::Identity : Correction[Parent];
            for (const FRestAim& Aim : RestAims)
            {
                if (Reference.GetBoneName(Bone) != Aim.Bone)
                {
                    continue;
                }
                const int32 Child = Reference.FindRawBoneIndex(Aim.Child);
                if (Child == INDEX_NONE)
                {
                    UE_LOG(LogMarburgumSwim, Error, TEXT("MB2SWIM mannequin has no bone %s"), *Aim.Child.ToString());
                    return false;
                }
                const FVector Current = Correction[Bone].RotateVector(GlobalRest[Child].GetLocation() - GlobalRest[Bone].GetLocation()).GetSafeNormal();
                const FVector Target = GodotToMannequin(Aim.GodotDirection).GetSafeNormal();
                Correction[Bone] = FQuat::FindBetweenNormals(Current, Target) * Correction[Bone];
                UE_LOG(LogMarburgumSwim, Display, TEXT("MB2SWIM rest aim %s turned %.1f deg"), *Aim.Bone.ToString(),
                    FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Current, Target), -1.0, 1.0))));
            }
        }

        TMap<FString, const FGodotTrack*> TrackByBone;
        const TArray<FGodotTrack>& Tracks = Clip.Tracks;
        for (const FGodotTrack& Track : Tracks)
        {
            TrackByBone.Add(Track.GodotBone, &Track);
        }

        const int32 Frames = FMath::RoundToInt32(Clip.CycleSeconds * FramesPerSecond);
        TMap<FName, TArray<FVector>> Positions;
        TMap<FName, TArray<FQuat>> Rotations;
        TMap<FName, TArray<FVector>> Scales;
        for (int32 Frame = 0; Frame <= Frames; ++Frame)
        {
            const double T = static_cast<double>(Frame) / FramesPerSecond;

            // Model-space turn of each Godot bone: its parent's turn, then its own key (_bone_pose).
            TMap<FString, FQuat> GodotTurn;
            const TArray<FString> Order = {
                TEXT("Hips"), TEXT("Spine"), TEXT("UpperChest"), TEXT("Head"), TEXT("LeftUpperArm"), TEXT("LeftLowerArm"),
                TEXT("RightUpperArm"), TEXT("RightLowerArm"), TEXT("LeftUpperLeg"), TEXT("LeftLowerLeg"), TEXT("RightUpperLeg"),
                TEXT("RightLowerLeg"),
            };
            for (const FString& GodotBone : Order)
            {
                const FString* Parent = GodotParents.Find(GodotBone);
                const FQuat ParentTurn = Parent ? GodotTurn.FindChecked(*Parent) : FQuat::Identity;
                const FGodotTrack* const* Track = TrackByBone.Find(GodotBone);
                const FQuat Own = Track ? GodotEulerToMannequin(SampleLoop((*Track)->Keys, T, Clip.CycleSeconds)) : FQuat::Identity;
                GodotTurn.Add(GodotBone, ParentTurn * Own);
            }

            // Component-space pose: a mapped bone takes its Godot bone's turn, any other bone keeps its
            // parent's; each is applied to the corrected reference pose.
            TArray<FQuat> Turn;
            Turn.SetNum(BoneCount);
            TArray<FQuat> Global;
            Global.SetNum(BoneCount);
            for (int32 Bone = 0; Bone < BoneCount; ++Bone)
            {
                const int32 Parent = Reference.GetRawParentIndex(Bone);
                const FString* GodotBone = MannequinToGodot.Find(Reference.GetBoneName(Bone));
                Turn[Bone] = GodotBone ? GodotTurn.FindChecked(*GodotBone) : (Parent == INDEX_NONE ? FQuat::Identity : Turn[Parent]);
                Global[Bone] = Turn[Bone] * Correction[Bone] * GlobalRest[Bone].GetRotation();
                Global[Bone].Normalize();
            }

            for (const TPair<FName, FString>& Pair : MannequinToGodot)
            {
                const int32 Bone = Reference.FindRawBoneIndex(Pair.Key);
                const int32 Parent = Reference.GetRawParentIndex(Bone);
                FQuat Local = Parent == INDEX_NONE ? Global[Bone] : Global[Parent].Inverse() * Global[Bone];
                Local.Normalize();
                FVector Position = LocalRest[Bone].GetLocation();
                if (Pair.Value == TEXT("Hips"))
                {
                    const FVector Bob(0.0, 0.0, SampleLoop(Clip.BobMetres, T, Clip.CycleSeconds) * 100.0);
                    Position += Parent == INDEX_NONE ? Bob : GlobalRest[Parent].GetRotation().UnrotateVector(Bob);
                }
                Positions.FindOrAdd(Pair.Key).Add(Position);
                Rotations.FindOrAdd(Pair.Key).Add(Local);
                Scales.FindOrAdd(Pair.Key).Add(LocalRest[Bone].GetScale3D());
            }
        }

        UObject* Existing = nullptr;
        UPackage* Package = PreparePackage(Clip.Package, UAnimSequence::StaticClass(), Existing);
        UAnimSequence* Sequence = Cast<UAnimSequence>(Existing);
        if (!Sequence)
        {
            Sequence = NewObject<UAnimSequence>(Package, *FPackageName::GetShortName(Clip.Package), RF_Public | RF_Standalone);
            FAssetRegistryModule::AssetCreated(Sequence);
        }
        Sequence->SetSkeleton(Skeleton);
        if (Preview)
        {
            Sequence->SetPreviewMesh(Preview);
        }
        IAnimationDataController& Controller = Sequence->GetController();
        Controller.InitializeModel();
        Controller.OpenBracket(FText::FromString(FString::Printf(TEXT("Marburgum %s"), *FPackageName::GetShortName(Clip.Package))), false);
        Controller.RemoveAllBoneTracks(false);
        Controller.SetFrameRate(FFrameRate(FramesPerSecond, 1), false);
        Controller.SetNumberOfFrames(FFrameNumber(Frames), false);
        for (const TPair<FName, FString>& Pair : MannequinToGodot)
        {
            Controller.AddBoneCurve(Pair.Key, false);
            if (!Controller.SetBoneTrackKeys(Pair.Key, Positions[Pair.Key], Rotations[Pair.Key], Scales[Pair.Key], false))
            {
                UE_LOG(LogMarburgumSwim, Error, TEXT("MB2SWIM could not key %s"), *Pair.Key.ToString());
                Controller.CloseBracket(false);
                return false;
            }
        }
        Controller.NotifyPopulated();
        Controller.CloseBracket(false);
        // In place, like every clip the original had: the movement component owns where the body goes.
        Sequence->bEnableRootMotion = false;
        Sequence->bLoop = true;
        Package->MarkPackageDirty();
        if (!UEditorLoadingAndSavingUtils::SavePackages({ Package }, false))
        {
            UE_LOG(LogMarburgumSwim, Error, TEXT("MB2SWIM could not save %s"), Clip.Package);
            return false;
        }
        UE_LOG(LogMarburgumSwim, Display, TEXT("MB2SWIM saved %s frames=%d length=%.2fs tracks=%d"), Clip.Package, Frames,
            Sequence->GetPlayLength(), MannequinToGodot.Num());

        // Evidence: limb directions read back from the saved clip, in the original's model axes (x right,
        // y up, z back), to compare with the same directions computed from the original's own clip.
        const auto Direction = [](const TArray<FTransform>& Pose, int32 From, int32 To)
        {
            const FVector D = (Pose[To].GetLocation() - Pose[From].GetLocation()).GetSafeNormal();
            return FVector(D.X, D.Z, -D.Y);
        };
        const FString Name = FPackageName::GetShortName(Clip.Package);
        for (const double Quarter : { 0.0, 0.25, 0.5, 0.75 })
        {
            const double T = Quarter * Clip.CycleSeconds;
            TArray<FTransform> Pose;
            Pose.SetNum(BoneCount);
            for (int32 Bone = 0; Bone < BoneCount; ++Bone)
            {
                FTransform Local = LocalRest[Bone];
                if (MannequinToGodot.Contains(Reference.GetBoneName(Bone)))
                {
                    Sequence->GetBoneTransform(Local, FSkeletonPoseBoneIndex(Bone), FAnimExtractContext(T), true);
                }
                const int32 Parent = Reference.GetRawParentIndex(Bone);
                Pose[Bone] = Parent == INDEX_NONE ? Local : Local * Pose[Parent];
            }
            const auto Index = [&Reference](const TCHAR* Name) { return Reference.FindRawBoneIndex(Name); };
            const FVector Torso = Direction(Pose, Index(TEXT("pelvis")), Index(TEXT("head")));
            const FVector ArmL = Direction(Pose, Index(TEXT("upperarm_l")), Index(TEXT("hand_l")));
            const FVector ArmR = Direction(Pose, Index(TEXT("upperarm_r")), Index(TEXT("hand_r")));
            const FVector LegL = Direction(Pose, Index(TEXT("thigh_l")), Index(TEXT("foot_l")));
            const FVector LegR = Direction(Pose, Index(TEXT("thigh_r")), Index(TEXT("foot_r")));
            UE_LOG(LogMarburgumSwim, Display,
                TEXT("MB2SWIM check %s t=%.2f torso=(%.2f,%.2f,%.2f) arm_l=(%.2f,%.2f,%.2f) arm_r=(%.2f,%.2f,%.2f) leg_l=(%.2f,%.2f,%.2f) leg_r=(%.2f,%.2f,%.2f)"), *Name, T,
                Torso.X, Torso.Y, Torso.Z, ArmL.X, ArmL.Y, ArmL.Z, ArmR.X, ArmR.Y, ArmR.Z, LegL.X, LegL.Y, LegL.Z, LegR.X, LegR.Y, LegR.Z);
            // Heights above the feet, for the swim lift (AMarburgumCharacter::SwimLiftCm) and the mouth rule.
            const auto Height = [&Pose, &Index](const TCHAR* Name) { return Pose[Index(Name)].GetLocation().Z; };
            UE_LOG(LogMarburgumSwim, Display,
                TEXT("MB2SWIM height %s t=%.2f pelvis=%.1f spine_04=%.1f head=%.1f thigh_l=%.1f foot_l=%.1f foot_r=%.1f hand_l=%.1f"), *Name, T,
                Height(TEXT("pelvis")), Height(TEXT("spine_04")), Height(TEXT("head")), Height(TEXT("thigh_l")), Height(TEXT("foot_l")),
                Height(TEXT("foot_r")), Height(TEXT("hand_l")));
        }
        return true;
    }

    // ---- Water volumes over the Drava ----

    constexpr const TCHAR* WaterSheetLabel = TEXT("Drava");
    constexpr const TCHAR* WaterVolumePrefix = TEXT("MB2Water_");
    // Sample spacing. A metre follows the banks closely enough that the volume edge never decides whether
    // a body swims: under the sheet near a bank the bed is too shallow for the capsule centre to be in it.
    constexpr double CellCm = 100.0;
    // Ground at least this far under the sheet is water; shallower than that is a wet margin.
    constexpr double MinDepthCm = 5.0;
    // The volume reaches this far under the deepest bed it covers.
    constexpr double BelowBedCm = 100.0;

    struct FTriangle2D
    {
        FVector2D A;
        FVector2D B;
        FVector2D C;
        FBox2D Bounds;
    };

    bool InTriangle(const FTriangle2D& T, const FVector2D& P)
    {
        const auto Side = [](const FVector2D& U, const FVector2D& V, const FVector2D& W) { return FVector2D::CrossProduct(V - U, W - U); };
        const double D1 = Side(T.A, T.B, P);
        const double D2 = Side(T.B, T.C, P);
        const double D3 = Side(T.C, T.A, P);
        const bool bNegative = D1 < 0.0 || D2 < 0.0 || D3 < 0.0;
        const bool bPositive = D1 > 0.0 || D2 > 0.0 || D3 > 0.0;
        return !(bNegative && bPositive);
    }

    APhysicsVolume* SpawnWaterVolume(UWorld* World, const FBox& Box, int32 Index)
    {
        FActorSpawnParameters Spawn;
        Spawn.Name = MakeUniqueObjectName(World->PersistentLevel, APhysicsVolume::StaticClass(),
            *FString::Printf(TEXT("%s%03d"), WaterVolumePrefix, Index));
        APhysicsVolume* Volume = World->SpawnActor<APhysicsVolume>(Box.GetCenter(), FRotator::ZeroRotator, Spawn);
        if (!Volume)
        {
            return nullptr;
        }
        UCubeBuilder* Builder = NewObject<UCubeBuilder>(Volume, NAME_None, RF_Transactional);
        const FVector Size = Box.GetSize();
        Builder->X = Size.X;
        Builder->Y = Size.Y;
        Builder->Z = Size.Z;
        // As UActorFactoryBoxVolume builds a box volume placed in the editor.
        Volume->PreEditChange(nullptr);
        Volume->PolyFlags = 0;
        Volume->Brush = NewObject<UModel>(Volume, NAME_None, RF_Transactional);
        Volume->Brush->Initialize(nullptr, true);
        Volume->Brush->Polys = NewObject<UPolys>(Volume->Brush, NAME_None, RF_Transactional);
        Volume->GetBrushComponent()->Brush = Volume->Brush;
        Volume->BrushBuilder = Builder;
        Builder->Build(World, Volume);
        FBSPOps::csgPrepMovingBrush(Volume);
        for (FPoly& Poly : Volume->Brush->Polys->Element)
        {
            Poly.Material = nullptr;
        }
        Volume->bWaterVolume = true;
        Volume->SetActorLabel(FString::Printf(TEXT("%s%03d"), WaterVolumePrefix, Index));
        Volume->SetFolderPath(TEXT("Water"));
        Volume->PostEditChange();
        return Volume;
    }

    bool BuildWaterVolumes()
    {
        UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(LentMap);
        if (!World)
        {
            UE_LOG(LogMarburgumSwim, Error, TEXT("MB2SWIM cannot load %s"), LentMap);
            return false;
        }
        AStaticMeshActor* Sheet = nullptr;
        TArray<ALandscapeProxy*> Landscapes;
        TArray<AActor*> OldVolumes;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            if (It->GetActorLabel() == WaterSheetLabel && It->IsA<AStaticMeshActor>())
            {
                Sheet = Cast<AStaticMeshActor>(*It);
            }
            else if (ALandscapeProxy* Landscape = Cast<ALandscapeProxy>(*It))
            {
                Landscapes.Add(Landscape);
            }
            else if (It->IsA<APhysicsVolume>() && It->GetActorLabel().StartsWith(WaterVolumePrefix))
            {
                OldVolumes.Add(*It);
            }
        }
        UStaticMesh* SheetMesh = Sheet ? Sheet->GetStaticMeshComponent()->GetStaticMesh() : nullptr;
        const FMeshDescription* Description = SheetMesh ? SheetMesh->GetMeshDescription(0) : nullptr;
        if (!Description || Landscapes.IsEmpty())
        {
            UE_LOG(LogMarburgumSwim, Error, TEXT("MB2SWIM %s needs the %s water mesh and a landscape"), LentMap, WaterSheetLabel);
            return false;
        }
        for (AActor* Old : OldVolumes)
        {
            World->EditorDestroyActor(Old, true);
        }

        // The sheet's triangles in world XY, and its level.
        const FTransform SheetTransform = Sheet->GetActorTransform();
        const TVertexAttributesConstRef<FVector3f> Positions = Description->GetVertexPositions();
        TArray<FTriangle2D> Triangles;
        double SurfaceZ = -UE_BIG_NUMBER;
        for (const FTriangleID Triangle : Description->Triangles().GetElementIDs())
        {
            const TArrayView<const FVertexID> Vertices = Description->GetTriangleVertices(Triangle);
            FVector World3[3];
            for (int32 Corner = 0; Corner < 3; ++Corner)
            {
                World3[Corner] = SheetTransform.TransformPosition(FVector(Positions[Vertices[Corner]]));
                SurfaceZ = FMath::Max(SurfaceZ, World3[Corner].Z);
            }
            FTriangle2D Flat{ FVector2D(World3[0]), FVector2D(World3[1]), FVector2D(World3[2]) };
            Flat.Bounds = FBox2D(TArray<FVector2D>{ Flat.A, Flat.B, Flat.C });
            Triangles.Add(Flat);
        }
        const FBox SheetBounds = Sheet->GetComponentsBoundingBox(true);

        // Water cells: under the sheet, with the ground at least MinDepthCm below its surface.
        const int32 Columns = FMath::CeilToInt32(SheetBounds.GetSize().X / CellCm);
        const int32 Rows = FMath::CeilToInt32(SheetBounds.GetSize().Y / CellCm);
        // Triangles by 10 x 10 cell bucket, so each cell tests only the triangles near it.
        constexpr int32 Bucket = 10;
        const int32 BucketColumns = Columns / Bucket + 1;
        const int32 BucketRows = Rows / Bucket + 1;
        TArray<TArray<int32>> Buckets;
        Buckets.SetNum(BucketColumns * BucketRows);
        for (int32 Index = 0; Index < Triangles.Num(); ++Index)
        {
            const FBox2D& B = Triangles[Index].Bounds;
            const int32 X0 = FMath::Clamp(FMath::FloorToInt32((B.Min.X - SheetBounds.Min.X) / (CellCm * Bucket)), 0, BucketColumns - 1);
            const int32 X1 = FMath::Clamp(FMath::FloorToInt32((B.Max.X - SheetBounds.Min.X) / (CellCm * Bucket)), 0, BucketColumns - 1);
            const int32 Y0 = FMath::Clamp(FMath::FloorToInt32((B.Min.Y - SheetBounds.Min.Y) / (CellCm * Bucket)), 0, BucketRows - 1);
            const int32 Y1 = FMath::Clamp(FMath::FloorToInt32((B.Max.Y - SheetBounds.Min.Y) / (CellCm * Bucket)), 0, BucketRows - 1);
            for (int32 Y = Y0; Y <= Y1; ++Y)
            {
                for (int32 X = X0; X <= X1; ++X)
                {
                    Buckets[Y * BucketColumns + X].Add(Index);
                }
            }
        }
        TArray<double> Bed;
        Bed.Init(TNumericLimits<double>::Max(), Columns * Rows);
        int32 WaterCells = 0;
        for (int32 Row = 0; Row < Rows; ++Row)
        {
            for (int32 Column = 0; Column < Columns; ++Column)
            {
                const FVector2D Centre(SheetBounds.Min.X + (Column + 0.5) * CellCm, SheetBounds.Min.Y + (Row + 0.5) * CellCm);
                bool bUnderSheet = false;
                for (const int32 Index : Buckets[(Row / Bucket) * BucketColumns + Column / Bucket])
                {
                    const FTriangle2D& Triangle = Triangles[Index];
                    if (Triangle.Bounds.IsInsideOrOn(Centre) && InTriangle(Triangle, Centre))
                    {
                        bUnderSheet = true;
                        break;
                    }
                }
                if (!bUnderSheet)
                {
                    continue;
                }
                TOptional<float> Ground;
                for (const ALandscapeProxy* Landscape : Landscapes)
                {
                    Ground = Landscape->GetHeightAtLocation(FVector(Centre, SurfaceZ), EHeightfieldSource::Complex);
                    if (Ground.IsSet())
                    {
                        break;
                    }
                }
                if (Ground.IsSet() && Ground.GetValue() < SurfaceZ - MinDepthCm)
                {
                    Bed[Row * Columns + Column] = Ground.GetValue();
                    ++WaterCells;
                }
            }
        }

        // Merge the cells into boxes: runs along X, then identical runs on following rows.
        TArray<bool> Taken;
        Taken.Init(false, Columns * Rows);
        const auto IsWater = [&](int32 Column, int32 Row) { return Bed[Row * Columns + Column] < TNumericLimits<double>::Max(); };
        int32 Volumes = 0;
        double Deepest = SurfaceZ;
        for (int32 Row = 0; Row < Rows; ++Row)
        {
            for (int32 Column = 0; Column < Columns; ++Column)
            {
                if (!IsWater(Column, Row) || Taken[Row * Columns + Column])
                {
                    continue;
                }
                int32 End = Column;
                while (End + 1 < Columns && IsWater(End + 1, Row) && !Taken[Row * Columns + End + 1])
                {
                    ++End;
                }
                int32 Last = Row;
                for (int32 Next = Row + 1; Next < Rows; ++Next)
                {
                    bool bSame = (Column == 0 || !IsWater(Column - 1, Next)) && (End + 1 >= Columns || !IsWater(End + 1, Next));
                    for (int32 Cell = Column; bSame && Cell <= End; ++Cell)
                    {
                        bSame = IsWater(Cell, Next) && !Taken[Next * Columns + Cell];
                    }
                    if (!bSame)
                    {
                        break;
                    }
                    Last = Next;
                }
                double Lowest = SurfaceZ;
                for (int32 R = Row; R <= Last; ++R)
                {
                    for (int32 Cell = Column; Cell <= End; ++Cell)
                    {
                        Taken[R * Columns + Cell] = true;
                        Lowest = FMath::Min(Lowest, Bed[R * Columns + Cell]);
                    }
                }
                Deepest = FMath::Min(Deepest, Lowest);
                const FBox Box(
                    FVector(SheetBounds.Min.X + Column * CellCm, SheetBounds.Min.Y + Row * CellCm, Lowest - BelowBedCm),
                    FVector(SheetBounds.Min.X + (End + 1) * CellCm, SheetBounds.Min.Y + (Last + 1) * CellCm, SurfaceZ));
                if (!SpawnWaterVolume(World, Box, Volumes))
                {
                    UE_LOG(LogMarburgumSwim, Error, TEXT("MB2SWIM could not spawn a water volume at %s"), *Box.ToString());
                    return false;
                }
                ++Volumes;
            }
        }
        if (!UEditorLoadingAndSavingUtils::SaveMap(World, LentMap))
        {
            UE_LOG(LogMarburgumSwim, Error, TEXT("MB2SWIM could not save %s"), LentMap);
            return false;
        }
        UE_LOG(LogMarburgumSwim, Display, TEXT("MB2SWIM water surface_z=%.1f cells=%d volumes=%d replaced=%d deepest_bed_z=%.1f sheet_triangles=%d"),
            SurfaceZ, WaterCells, Volumes, OldVolumes.Num(), Deepest, Triangles.Num());
        return true;
    }

    void InspectLent()
    {
        UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(LentMap);
        if (!World)
        {
            UE_LOG(LogMarburgumSwim, Error, TEXT("MB2SWIM cannot load %s"), LentMap);
            return;
        }
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            FString Mesh;
            if (const AStaticMeshActor* MeshActor = Cast<AStaticMeshActor>(Actor))
            {
                const UStaticMesh* Static = MeshActor->GetStaticMeshComponent()->GetStaticMesh();
                Mesh = Static ? Static->GetPathName() : TEXT("-");
                if (!Mesh.Contains(TEXT("Water")) && !Mesh.Contains(TEXT("Bridges")) && !Mesh.Contains(TEXT("Roads")))
                {
                    continue;
                }
            }
            else if (!Actor->IsA<ALandscape>() && !Actor->IsA<APhysicsVolume>() && !Actor->GetClass()->GetName().Contains(TEXT("Start")))
            {
                continue;
            }
            FVector Origin;
            FVector Extent;
            Actor->GetActorBounds(false, Origin, Extent);
            UE_LOG(LogMarburgumSwim, Display, TEXT("MB2SWIM actor %s class=%s mesh=%s loc=%s rot=%s scale=%s min=%s max=%s"),
                *Actor->GetActorLabel(), *Actor->GetClass()->GetName(), *Mesh, *Actor->GetActorLocation().ToString(),
                *Actor->GetActorRotation().ToString(), *Actor->GetActorScale3D().ToString(), *(Origin - Extent).ToString(),
                *(Origin + Extent).ToString());
        }
    }
}

UMarburgumSwimContentCommandlet::UMarburgumSwimContentCommandlet()
{
    IsClient = false;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
}

int32 UMarburgumSwimContentCommandlet::Main(const FString& Params)
{
    FString Steps = TEXT("animation,water");
    FParse::Value(*Params, TEXT("Steps="), Steps, false);
    TArray<FString> List;
    Steps.ParseIntoArray(List, TEXT(","));
    bool bOk = true;
    for (const FString& Step : List)
    {
        if (Step == TEXT("inspect"))
        {
            InspectLent();
        }
        else if (Step == TEXT("animation"))
        {
            bOk &= BuildSwimAnimation({ SwimPackage, SwimCycleSeconds, SwimTracks(), HipsBobMetres });
            bOk &= BuildSwimAnimation({ TreadPackage, TreadCycleSeconds, TreadTracks(), TreadBobMetres });
        }
        else if (Step == TEXT("water"))
        {
            bOk &= BuildWaterVolumes();
        }
        else
        {
            UE_LOG(LogMarburgumSwim, Error, TEXT("MB2SWIM unknown step %s"), *Step);
            bOk = false;
        }
    }
    UE_LOG(LogMarburgumSwim, Display, TEXT("MB2SWIM result=%d"), bOk ? 0 : 1);
    return bOk ? 0 : 1;
}
