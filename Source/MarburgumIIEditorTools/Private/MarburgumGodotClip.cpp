#include "MarburgumGodotClip.h"

#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SkeletalMesh.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumGodotClip, Log, All);

namespace MarburgumGodotClip
{
    namespace
    {
        constexpr const TCHAR* SkeletonPath = TEXT("/Game/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin");
        constexpr const TCHAR* PreviewMeshPath = TEXT("/Game/Characters/Protagonist/SKM_Protagonist.SKM_Protagonist");

        // The Godot humanoid's chain for the bones the original's clips move: a bone not listed follows its parent.
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

        // Parents before children.
        const TCHAR* const GodotOrder[] = {
            TEXT("Hips"), TEXT("Spine"), TEXT("UpperChest"), TEXT("Head"), TEXT("LeftUpperArm"), TEXT("LeftLowerArm"),
            TEXT("RightUpperArm"), TEXT("RightLowerArm"), TEXT("LeftUpperLeg"), TEXT("LeftLowerLeg"), TEXT("RightUpperLeg"),
            TEXT("RightLowerLeg"),
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
         * the same skeleton), Godot model space, from each joint to the next.
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

        UPackage* PreparePackage(const TCHAR* PackageName, UClass* Class, UObject*& OutExisting)
        {
            UPackage* Package = CreatePackage(PackageName);
            Package->FullyLoad();
            OutExisting = StaticFindObject(Class, Package, *FPackageName::GetShortName(PackageName));
            return Package;
        }
    }

    TArray<FVector> Shift(const TArray<FVector>& Values, int32 Steps)
    {
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
        for (FVector& Value : Values)
        {
            Value = FVector(Value.X, -Value.Y, -Value.Z);
        }
        return Values;
    }

    TArray<FVector> Cycle(std::initializer_list<double> Values)
    {
        TArray<FVector> Out;
        const TArray<double> List(Values);
        for (int32 Index = 0; Index <= List.Num(); ++Index)
        {
            Out.Add(FVector(List[Index % List.Num()], 0.0, 0.0));
        }
        return Out;
    }

    FVector Sample(const TArray<FVector>& Keys, double T, double LengthSeconds, bool bLoop)
    {
        if (Keys.Num() == 0)
        {
            return FVector::ZeroVector;
        }
        if (Keys.Num() == 1)
        {
            return Keys[0];
        }
        const int32 Segments = Keys.Num() - 1;
        const double Phase = bLoop ? FMath::Fmod(T / LengthSeconds, 1.0) : FMath::Clamp(T / LengthSeconds, 0.0, 1.0);
        const double Position = Phase * Segments;
        const int32 Segment = FMath::Clamp(FMath::FloorToInt32(Position), 0, Segments - 1);
        const double U = Position - Segment;
        const auto At = [&Keys, Segments, bLoop](int32 Index)
        {
            return bLoop ? Keys[((Index % Segments) + Segments) % Segments] : Keys[FMath::Clamp(Index, 0, Segments)];
        };
        const FVector P0 = At(Segment - 1);
        const FVector P1 = At(Segment);
        const FVector P2 = At(Segment + 1);
        const FVector P3 = At(Segment + 2);
        return 0.5 * ((2.0 * P1) + (-P0 + P2) * U + (2.0 * P0 - 5.0 * P1 + 4.0 * P2 - P3) * U * U
            + (-P0 + 3.0 * P1 - 3.0 * P2 + P3) * U * U * U);
    }

    FVector SampleTimed(const TArray<FVector>& Keys, const TArray<double>& Times, double T)
    {
        if (Keys.Num() == 0 || Keys.Num() != Times.Num())
        {
            return Keys.Num() > 0 ? Keys[0] : FVector::ZeroVector;
        }
        const int32 Last = Keys.Num() - 1;
        if (T <= Times[0] || Last == 0)
        {
            return Keys[0];
        }
        if (T >= Times[Last])
        {
            return Keys[Last];
        }
        int32 From = 0;
        while (From < Last - 1 && T >= Times[From + 1])
        {
            ++From;
        }
        const int32 To = From + 1;
        const int32 Pre = FMath::Max(From - 1, 0);
        const int32 Post = FMath::Min(To + 1, Last);
        // Math::cubic_interpolate_in_time, times relative to the key the segment starts from.
        const double ToT = Times[To] - Times[From];
        const double PreT = Times[Pre] - Times[From];
        const double PostT = Times[Post] - Times[From];
        const double Local = T - Times[From];
        const FVector A1 = FMath::Lerp(Keys[Pre], Keys[From], PreT == 0.0 ? 0.0 : (Local - PreT) / -PreT);
        const FVector A2 = FMath::Lerp(Keys[From], Keys[To], ToT == 0.0 ? 0.5 : Local / ToT);
        const FVector A3 = FMath::Lerp(Keys[To], Keys[Post], PostT - ToT == 0.0 ? 1.0 : (Local - ToT) / (PostT - ToT));
        const FVector B1 = FMath::Lerp(A1, A2, ToT - PreT == 0.0 ? 0.0 : (Local - PreT) / (ToT - PreT));
        const FVector B2 = FMath::Lerp(A2, A3, PostT == 0.0 ? 1.0 : Local / PostT);
        return FMath::Lerp(B1, B2, ToT == 0.0 ? 0.5 : Local / ToT);
    }

    UAnimSequence* Build(const FClip& Clip, const TCHAR* LogTag)
    {
        if (Clip.KeyTimes.Num() > 0)
        {
            for (const FTrack& Track : Clip.Tracks)
            {
                if (Track.Keys.Num() != Clip.KeyTimes.Num())
                {
                    UE_LOG(LogMarburgumGodotClip, Error, TEXT("%s track %s has %d keys for %d key times"), LogTag, Track.GodotBone,
                        Track.Keys.Num(), Clip.KeyTimes.Num());
                    return nullptr;
                }
            }
        }
        USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, SkeletonPath);
        USkeletalMesh* Preview = LoadObject<USkeletalMesh>(nullptr, PreviewMeshPath);
        if (!Skeleton)
        {
            UE_LOG(LogMarburgumGodotClip, Error, TEXT("%s missing skeleton %s"), LogTag, SkeletonPath);
            return nullptr;
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
                UE_LOG(LogMarburgumGodotClip, Error, TEXT("%s mannequin has no bone %s"), LogTag, *Pair.Key.ToString());
                return nullptr;
            }
            const FVector At = GlobalRest[Bone].GetLocation();
            UE_LOG(LogMarburgumGodotClip, Display, TEXT("%s bone %s <- %s rest=(%.1f, %.1f, %.1f)"), LogTag, *Pair.Key.ToString(), *Pair.Value,
                At.X, At.Y, At.Z);
        }

        // Rest correction: turn each listed limb onto the original's rest direction (its parent's turn
        // carried down first), so the clip starts from the pose it was keyed on.
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
                    UE_LOG(LogMarburgumGodotClip, Error, TEXT("%s mannequin has no bone %s"), LogTag, *Aim.Child.ToString());
                    return nullptr;
                }
                const FVector Current = Correction[Bone].RotateVector(GlobalRest[Child].GetLocation() - GlobalRest[Bone].GetLocation()).GetSafeNormal();
                const FVector Target = GodotToMannequin(Aim.GodotDirection).GetSafeNormal();
                Correction[Bone] = FQuat::FindBetweenNormals(Current, Target) * Correction[Bone];
                UE_LOG(LogMarburgumGodotClip, Display, TEXT("%s rest aim %s turned %.1f deg"), LogTag, *Aim.Bone.ToString(),
                    FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Current, Target), -1.0, 1.0))));
            }
        }

        TMap<FString, const FTrack*> TrackByBone;
        for (const FTrack& Track : Clip.Tracks)
        {
            TrackByBone.Add(Track.GodotBone, &Track);
        }

        const int32 Frames = FMath::RoundToInt32(Clip.LengthSeconds * FramesPerSecond);
        TMap<FName, TArray<FVector>> Positions;
        TMap<FName, TArray<FQuat>> Rotations;
        TMap<FName, TArray<FVector>> Scales;
        for (int32 Frame = 0; Frame <= Frames; ++Frame)
        {
            const double T = static_cast<double>(Frame) / FramesPerSecond;

            // Model-space turn of each Godot bone: its parent's turn, then its own key (_bone_pose).
            TMap<FString, FQuat> GodotTurn;
            for (const TCHAR* GodotBone : GodotOrder)
            {
                const FString* Parent = GodotParents.Find(GodotBone);
                const FQuat ParentTurn = Parent ? GodotTurn.FindChecked(*Parent) : FQuat::Identity;
                const FTrack* const* Track = TrackByBone.Find(GodotBone);
                const FVector Euler = !Track ? FVector::ZeroVector
                    : Clip.KeyTimes.Num() > 0 ? SampleTimed((*Track)->Keys, Clip.KeyTimes, T)
                    : Sample((*Track)->Keys, T, Clip.LengthSeconds, Clip.bLoop);
                const FQuat Own = Track ? GodotEulerToMannequin(Euler) : FQuat::Identity;
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
                if (Pair.Value == TEXT("Hips") && Clip.HipsOffsets.Num() > 0)
                {
                    // _position_track: rest plus a model-space displacement, in metres.
                    const FVector Offset = GodotToMannequin(Sample(Clip.HipsOffsets, T, Clip.LengthSeconds, Clip.bLoop)) * 100.0;
                    Position += Parent == INDEX_NONE ? Offset : GlobalRest[Parent].GetRotation().UnrotateVector(Offset);
                }
                Positions.FindOrAdd(Pair.Key).Add(Position);
                Rotations.FindOrAdd(Pair.Key).Add(Local);
                Scales.FindOrAdd(Pair.Key).Add(LocalRest[Bone].GetScale3D());
            }
        }

        // The template's foot IK pulls each foot to its IK bone, so those bones follow the drawn feet (ADR-042).
        if (!FollowFeetWithIkBones(Reference, Frames, Positions, Rotations, Scales, LogTag))
        {
            return nullptr;
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
        Controller.OpenBracket(FText::FromString(Clip.Title), false);
        Controller.RemoveAllBoneTracks(false);
        Controller.SetFrameRate(FFrameRate(FramesPerSecond, 1), false);
        Controller.SetNumberOfFrames(FFrameNumber(Frames), false);
        for (const TPair<FName, TArray<FQuat>>& Pair : Rotations)
        {
            Controller.AddBoneCurve(Pair.Key, false);
            if (!Controller.SetBoneTrackKeys(Pair.Key, Positions[Pair.Key], Rotations[Pair.Key], Scales[Pair.Key], false))
            {
                UE_LOG(LogMarburgumGodotClip, Error, TEXT("%s could not key %s"), LogTag, *Pair.Key.ToString());
                Controller.CloseBracket(false);
                return nullptr;
            }
        }
        Controller.NotifyPopulated();
        Controller.CloseBracket(false);
        // In place, like every clip the original had: the movement component owns where the body goes.
        Sequence->bEnableRootMotion = false;
        Sequence->bLoop = Clip.bLoop;
        Package->MarkPackageDirty();
        if (!UEditorLoadingAndSavingUtils::SavePackages({ Package }, false))
        {
            UE_LOG(LogMarburgumGodotClip, Error, TEXT("%s could not save %s"), LogTag, Clip.Package);
            return nullptr;
        }
        UE_LOG(LogMarburgumGodotClip, Display, TEXT("%s saved %s frames=%d length=%.2fs tracks=%d"), LogTag, Clip.Package, Frames,
            Sequence->GetPlayLength(), Rotations.Num());
        return Sequence;
    }

    void LogLimbCheck(const UAnimSequence& Sequence, const TArray<double>& Times, const TCHAR* LogTag)
    {
        const USkeleton* Skeleton = Sequence.GetSkeleton();
        if (!Skeleton)
        {
            return;
        }
        const FReferenceSkeleton& Reference = Skeleton->GetReferenceSkeleton();
        const int32 BoneCount = Reference.GetRawBoneNum();
        const TArray<FTransform>& LocalRest = Reference.GetRawRefBonePose();
        const auto Direction = [](const TArray<FTransform>& Pose, int32 From, int32 To)
        {
            const FVector D = (Pose[To].GetLocation() - Pose[From].GetLocation()).GetSafeNormal();
            return FVector(D.X, D.Z, -D.Y);
        };
        for (const double T : Times)
        {
            TArray<FTransform> Pose;
            Pose.SetNum(BoneCount);
            for (int32 Bone = 0; Bone < BoneCount; ++Bone)
            {
                FTransform Local = LocalRest[Bone];
                if (MannequinToGodot.Contains(Reference.GetBoneName(Bone)))
                {
                    Sequence.GetBoneTransform(Local, FSkeletonPoseBoneIndex(Bone), FAnimExtractContext(T), true);
                }
                const int32 Parent = Reference.GetRawParentIndex(Bone);
                Pose[Bone] = Parent == INDEX_NONE ? Local : Local * Pose[Parent];
            }
            const auto Index = [&Reference](const TCHAR* Name) { return Reference.FindRawBoneIndex(Name); };
            const FVector Torso = Direction(Pose, Index(TEXT("pelvis")), Index(TEXT("head")));
            const FVector ArmL = Direction(Pose, Index(TEXT("upperarm_l")), Index(TEXT("hand_l")));
            const FVector ArmR = Direction(Pose, Index(TEXT("upperarm_r")), Index(TEXT("hand_r")));
            const FVector LegL = Direction(Pose, Index(TEXT("thigh_l")), Index(TEXT("foot_l")));
            UE_LOG(LogMarburgumGodotClip, Display,
                TEXT("%s check t=%.1f torso=(%.2f,%.2f,%.2f) arm_l=(%.2f,%.2f,%.2f) arm_r=(%.2f,%.2f,%.2f) leg_l=(%.2f,%.2f,%.2f)"), LogTag, T,
                Torso.X, Torso.Y, Torso.Z, ArmL.X, ArmL.Y, ArmL.Z, ArmR.X, ArmR.Y, ArmR.Z, LegL.X, LegL.Y, LegL.Z);
        }
    }

    bool FollowFeetWithIkBones(const FReferenceSkeleton& Reference, int32 Frames, TMap<FName, TArray<FVector>>& Positions,
        TMap<FName, TArray<FQuat>>& Rotations, TMap<FName, TArray<FVector>>& Scales, const TCHAR* LogTag)
    {
        const int32 BoneCount = Reference.GetRawBoneNum();
        const TArray<FTransform>& LocalRest = Reference.GetRawRefBonePose();
        const int32 IkRoot = Reference.FindRawBoneIndex(TEXT("ik_foot_root"));
        const TPair<FName, FName> Feet[] = { { TEXT("foot_l"), TEXT("ik_foot_l") }, { TEXT("foot_r"), TEXT("ik_foot_r") } };
        if (IkRoot == INDEX_NONE)
        {
            UE_LOG(LogMarburgumGodotClip, Error, TEXT("%s mannequin has no bone ik_foot_root"), LogTag);
            return false;
        }
        for (const TPair<FName, FName>& Foot : Feet)
        {
            if (Reference.FindRawBoneIndex(Foot.Key) == INDEX_NONE || Reference.FindRawBoneIndex(Foot.Value) == INDEX_NONE)
            {
                UE_LOG(LogMarburgumGodotClip, Error, TEXT("%s mannequin has no bone %s or %s"), LogTag, *Foot.Key.ToString(),
                    *Foot.Value.ToString());
                return false;
            }
        }
        // Any keys the IK bones had are replaced; they are written after every frame is read.
        for (const TPair<FName, FName>& Foot : Feet)
        {
            Positions.Remove(Foot.Value);
            Rotations.Remove(Foot.Value);
            Scales.Remove(Foot.Value);
        }
        const int32 Pelvis = Reference.FindRawBoneIndex(TEXT("pelvis"));
        TMap<FName, TArray<FVector>> IkPositions;
        TMap<FName, TArray<FQuat>> IkRotations;
        // Evidence: how far each foot the clip draws reaches ahead of the pelvis (component +Y is the body's
        // forward) and above the ground (component Z 0), so the smokes can compare the drawn pose with it.
        TMap<FName, FVector2D> MostAhead;
        TMap<FName, FVector2D> Highest;
        for (int32 Frame = 0; Frame <= Frames; ++Frame)
        {
            // Component space at this frame: keyed bones take their keys, the rest their reference pose.
            TArray<FTransform> Global;
            Global.SetNum(BoneCount);
            for (int32 Bone = 0; Bone < BoneCount; ++Bone)
            {
                const FName Name = Reference.GetBoneName(Bone);
                FTransform Local = LocalRest[Bone];
                if (const TArray<FQuat>* Keyed = Rotations.Find(Name))
                {
                    Local = FTransform((*Keyed)[Frame], Positions[Name][Frame], Scales[Name][Frame]);
                }
                const int32 Parent = Reference.GetRawParentIndex(Bone);
                Global[Bone] = Parent == INDEX_NONE ? Local : Local * Global[Parent];
            }
            for (const TPair<FName, FName>& Foot : Feet)
            {
                const int32 Bone = Reference.FindRawBoneIndex(Foot.Value);
                const int32 Parent = Reference.GetRawParentIndex(Bone);
                const FTransform Local = Global[Reference.FindRawBoneIndex(Foot.Key)].GetRelativeTransform(Global[Parent]);
                IkPositions.FindOrAdd(Foot.Value).Add(Local.GetLocation());
                IkRotations.FindOrAdd(Foot.Value).Add(Local.GetRotation().GetNormalized());
                const FVector At = Global[Reference.FindRawBoneIndex(Foot.Key)].GetLocation();
                const double Ahead = Pelvis == INDEX_NONE ? At.Y : At.Y - Global[Pelvis].GetLocation().Y;
                FVector2D& Ahead2 = MostAhead.FindOrAdd(Foot.Key, FVector2D(-TNumericLimits<double>::Max(), 0.0));
                if (Ahead > Ahead2.X)
                {
                    Ahead2 = FVector2D(Ahead, Frame);
                }
                FVector2D& High = Highest.FindOrAdd(Foot.Key, FVector2D(-TNumericLimits<double>::Max(), 0.0));
                if (At.Z > High.X)
                {
                    High = FVector2D(At.Z, Frame);
                }
            }
        }
        for (const TPair<FName, FName>& Foot : Feet)
        {
            UE_LOG(LogMarburgumGodotClip, Display, TEXT("%s drawn %s most_ahead_of_pelvis=%.0f at frame %.0f highest=%.0f at frame %.0f"),
                LogTag, *Foot.Key.ToString(), MostAhead[Foot.Key].X, MostAhead[Foot.Key].Y, Highest[Foot.Key].X, Highest[Foot.Key].Y);
        }
        for (const TPair<FName, FName>& Foot : Feet)
        {
            const FVector Scale = LocalRest[Reference.FindRawBoneIndex(Foot.Value)].GetScale3D();
            Positions.Add(Foot.Value, IkPositions[Foot.Value]);
            Rotations.Add(Foot.Value, IkRotations[Foot.Value]);
            Scales.Add(Foot.Value, TArray<FVector>());
            Scales[Foot.Value].Init(Scale, Frames + 1);
        }
        return true;
    }
}
