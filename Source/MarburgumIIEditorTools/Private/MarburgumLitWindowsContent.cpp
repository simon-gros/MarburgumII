#include "MarburgumLitWindowsContent.h"

#include "FileHelpers.h"
#include "MarburgumLitWindows.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionEyeAdaptationInverse.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumLitWindowsContent, Log, All);

namespace MarburgumLitWindowsContent
{
    namespace LitWindowsRules = MarburgumII::LitWindows;
    namespace Lights = MarburgumII::StreetLights;

    namespace
    {
        // Fixed, so a material compiled against the collection keeps finding its parameters.
        const FGuid EveningId(0x6D62324C, 0x57494E44, 0x4F570001, 0x00000066);
        const FGuid MorningId(0x6D62324C, 0x57494E44, 0x4F570002, 0x00000066);
        const FGuid GroundId(0x6D62324C, 0x57494E44, 0x4F570003, 0x00000066);
        const FGuid DuskId(0x6D62324C, 0x57494E44, 0x4F570004, 0x00000066);

        UPackage* OpenPackage(const TCHAR* PackageName)
        {
            UPackage* Package = FPackageName::DoesPackageExist(PackageName) ? LoadPackage(nullptr, PackageName, LOAD_None) : CreatePackage(PackageName);
            if (Package)
            {
                Package->FullyLoad();
            }
            return Package;
        }

        template <typename T>
        T* Expression(UMaterial* Material)
        {
            T* Node = NewObject<T>(Material);
            Material->GetExpressionCollection().AddExpression(Node);
            return Node;
        }

        UMaterialParameterCollection* WriteCollection(TArray<UPackage*>& Dirty)
        {
            UPackage* Package = OpenPackage(LitWindowsRules::CollectionPackage);
            const FString Name = FPackageName::GetShortName(LitWindowsRules::CollectionPackage);
            UMaterialParameterCollection* Collection = Package ? FindObject<UMaterialParameterCollection>(Package, *Name) : nullptr;
            if (!Collection && Package)
            {
                Collection = NewObject<UMaterialParameterCollection>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
            }
            if (!Collection)
            {
                return nullptr;
            }
            struct FWanted
            {
                const TCHAR* Name;
                FGuid Id;
            };
            bool bChanged = false;
            for (const FWanted& Wanted : { FWanted{ LitWindowsRules::EveningParameter, EveningId }, FWanted{ LitWindowsRules::MorningParameter, MorningId },
                     FWanted{ LitWindowsRules::GroundParameter, GroundId }, FWanted{ LitWindowsRules::DuskParameter, DuskId } })
            {
                FCollectionScalarParameter* Existing = Collection->ScalarParameters.FindByPredicate(
                    [&Wanted](const FCollectionScalarParameter& Parameter) { return Parameter.ParameterName == FName(Wanted.Name); });
                if (Existing && Existing->Id == Wanted.Id && Existing->DefaultValue == 0.0f)
                {
                    continue;
                }
                if (!bChanged)
                {
                    Collection->PreEditChange(nullptr);
                    bChanged = true;
                }
                if (!Existing)
                {
                    Existing = &Collection->ScalarParameters.AddDefaulted_GetRef();
                    Existing->ParameterName = FName(Wanted.Name);
                }
                // Dark windows until a client says otherwise: the editor, a server and a world with no clock see the day.
                Existing->Id = Wanted.Id;
                Existing->DefaultValue = 0.0f;
            }
            if (bChanged)
            {
                Collection->PostEditChange();
                Collection->MarkPackageDirty();
                Dirty.AddUnique(Package);
            }
            return Collection;
        }

        FString Float3(const FLinearColor& C)
        {
            return FString::Printf(TEXT("float3(%.6f, %.6f, %.6f)"), C.R, C.G, C.B);
        }

        /** The household's rule of MarburgumLitWindows.cpp (IsLit, RoomOf, CurtainOf, GlowOf), on the GPU. */
        FString GlowCode()
        {
            FString Code;
            Code += TEXT("float E = Tag.x;\nfloat M = Tag.y;\nfloat K = Kind.x;\nbool Ground = Kind.y > 0.5;\n");
            Code += TEXT("float Lit = Ground ? (E < GroundShare ? 1.0 : 0.0) : ((E < EveningShare || M < MorningShare) ? 1.0 : 0.0);\n");
            Code += TEXT("float X = E * 7.0 + M * 3.0;\nfloat Strength = 0.55 + 0.45 * (X - floor(X));\n");
            Code += FString::Printf(TEXT("float3 Colour = %s;\n"), *Float3(Lights::KelvinColour(LitWindowsRules::TungstenKelvin)));
            Code += FString::Printf(TEXT("if (Ground) { Colour = %s; Strength = %.4f; }\n"), *Float3(Lights::KelvinColour(LitWindowsRules::ShopKelvin)), LitWindowsRules::ShopGlow);
            Code += FString::Printf(TEXT("else if (K >= %.6f) { Colour = %s; Strength *= %.4f; }\n"), LitWindowsRules::TungstenShare + LitWindowsRules::FluorescentShare,
                *Float3(Lights::KelvinColour(LitWindowsRules::TelevisionKelvin)), LitWindowsRules::TelevisionGlow);
            Code += FString::Printf(TEXT("else if (K >= %.6f) { Colour = %s; }\n"), LitWindowsRules::TungstenShare, *Float3(Lights::KelvinColour(LitWindowsRules::FluorescentKelvin)));
            Code += FString::Printf(TEXT("return Colour * (Strength * %.4f * Lit * saturate(Dusk));\n"), LitWindowsRules::GlowOnScreen);
            return Code;
        }

        UMaterial* WriteGlass(UMaterialParameterCollection* Collection, TArray<UPackage*>& Dirty, FString& OutError)
        {
            UPackage* Package = OpenPackage(LitWindowsRules::GlassMaterialPackage);
            const FString Name = FPackageName::GetShortName(LitWindowsRules::GlassMaterialPackage);
            UMaterial* Material = Package ? FindObject<UMaterial>(Package, *Name) : nullptr;
            if (Material)
            {
                Material->Modify();
                Material->GetExpressionCollection().Empty();
                UMaterialEditorOnlyData* Ed = Material->GetEditorOnlyData();
                Ed->BaseColor = FColorMaterialInput();
                Ed->Metallic = FScalarMaterialInput();
                Ed->Roughness = FScalarMaterialInput();
                Ed->EmissiveColor = FColorMaterialInput();
            }
            else if (Package)
            {
                Material = NewObject<UMaterial>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
            }
            if (!Material)
            {
                OutError = FString::Printf(TEXT("could not create %s"), LitWindowsRules::GlassMaterialPackage);
                return nullptr;
            }
            Material->MaterialDomain = MD_Surface;
            Material->BlendMode = BLEND_Opaque;
            Material->SetShadingModel(MSM_DefaultLit);

            // By day: the glass MI_facade_glass has always set, dark and smooth.
            UMaterialExpressionVectorParameter* Tint = Expression<UMaterialExpressionVectorParameter>(Material);
            Tint->ParameterName = TEXT("BaseColorTint");
            Tint->DefaultValue = FLinearColor(0.16f, 0.20f, 0.22f);
            UMaterialExpressionScalarParameter* Rough = Expression<UMaterialExpressionScalarParameter>(Material);
            Rough->ParameterName = TEXT("RoughnessDefault");
            Rough->DefaultValue = 0.06f;

            // By night: the pane's household, and the clock's four numbers.
            UMaterialExpressionTextureCoordinate* Tag = Expression<UMaterialExpressionTextureCoordinate>(Material);
            Tag->CoordinateIndex = LitWindowsRules::TagChannel;
            UMaterialExpressionTextureCoordinate* Kind = Expression<UMaterialExpressionTextureCoordinate>(Material);
            Kind->CoordinateIndex = LitWindowsRules::KindChannel;
            const auto Parameter = [&](const TCHAR* ParameterName)
            {
                UMaterialExpressionCollectionParameter* E = Expression<UMaterialExpressionCollectionParameter>(Material);
                E->Collection = Collection;
                E->ParameterName = ParameterName;
                E->ParameterId = Collection->GetParameterId(FName(ParameterName));
                return E;
            };
            UMaterialExpressionCustom* Glow = Expression<UMaterialExpressionCustom>(Material);
            Glow->Description = TEXT("LitWindowGlow");
            Glow->Code = GlowCode();
            Glow->OutputType = CMOT_Float3;
            Glow->Inputs.Empty();
            const auto Input = [&](const TCHAR* InputName, UMaterialExpression* From)
            {
                FCustomInput& In = Glow->Inputs.AddDefaulted_GetRef();
                In.InputName = FName(InputName);
                In.Input.Connect(0, From);
            };
            Input(TEXT("Tag"), Tag);
            Input(TEXT("Kind"), Kind);
            Input(TEXT("EveningShare"), Parameter(LitWindowsRules::EveningParameter));
            Input(TEXT("MorningShare"), Parameter(LitWindowsRules::MorningParameter));
            Input(TEXT("GroundShare"), Parameter(LitWindowsRules::GroundParameter));
            Input(TEXT("Dusk"), Parameter(LitWindowsRules::DuskParameter));
            // Bright on screen whatever the eye's exposure, as the lamps' glass is.
            UMaterialExpressionEyeAdaptationInverse* OnScreen = Expression<UMaterialExpressionEyeAdaptationInverse>(Material);
            OnScreen->LightValueInput.Connect(0, Glow);

            UMaterialEditorOnlyData* Ed = Material->GetEditorOnlyData();
            Ed->BaseColor.Connect(0, Tint);
            Ed->Roughness.Connect(0, Rough);
            Ed->EmissiveColor.Connect(0, OnScreen);
            Material->PreEditChange(nullptr);
            Material->PostEditChange();
            Material->MarkPackageDirty();
            Dirty.AddUnique(Package);
            return Material;
        }

        /** What the glass's old parent did, for the record: its expressions and parameters. */
        void DescribeOldParent()
        {
            const UMaterial* Building = LoadObject<UMaterial>(nullptr, TEXT("/Game/Marburgum/Materials/Masters/M_Building.M_Building"), nullptr, LOAD_NoWarn | LOAD_Quiet);
            if (!Building)
            {
                return;
            }
            for (const UMaterialExpression* Node : Building->GetExpressions())
            {
                FString Detail;
                if (const UMaterialExpressionParameter* P = Cast<UMaterialExpressionParameter>(Node))
                {
                    Detail = P->ParameterName.ToString();
                }
                else if (const UMaterialExpressionCollectionParameter* C = Cast<UMaterialExpressionCollectionParameter>(Node))
                {
                    Detail = FString::Printf(TEXT("%s.%s"), C->Collection ? *C->Collection->GetName() : TEXT("?"), *C->ParameterName.ToString());
                }
                else if (const UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Node))
                {
                    Detail = Custom->Code.Left(160).Replace(TEXT("\n"), TEXT(" "));
                }
                UE_LOG(LogMarburgumLitWindowsContent, Display, TEXT("MB2WINDOWCONTENT M_Building %s %s"), *Node->GetClass()->GetName(), *Detail);
            }
        }
    }

    bool Write(FString& OutError)
    {
        DescribeOldParent();
        TArray<UPackage*> Dirty;
        UMaterialParameterCollection* Collection = WriteCollection(Dirty);
        if (!Collection)
        {
            OutError = FString::Printf(TEXT("could not create %s"), LitWindowsRules::CollectionPackage);
            return false;
        }
        if (!WriteGlass(Collection, Dirty, OutError))
        {
            return false;
        }
        if (!Dirty.IsEmpty() && !UEditorLoadingAndSavingUtils::SavePackages(Dirty, false))
        {
            OutError = TEXT("could not save the lit windows");
            return false;
        }
        UE_LOG(LogMarburgumLitWindowsContent, Display, TEXT("MB2WINDOWCONTENT saved=%d material=%s collection=%s"), Dirty.Num(), LitWindowsRules::GlassMaterial, LitWindowsRules::Collection);
        UE_LOG(LogMarburgumLitWindowsContent, Display, TEXT("MB2WINDOWCONTENT glow:\n%s"), *GlowCode());
        return true;
    }
}
