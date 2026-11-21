#include "MarburgumVisualFixesCommandlet.h"

#include "AssetCompilingManager.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Font.h"
#include "Engine/TextRenderActor.h"
#include "EngineUtils.h"
#include "Factories/TrueTypeFontFactory.h"
#include "FileHelpers.h"
#include "GameFramework/PlayerStart.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumVisualFixes, Log, All);

namespace MarburgumVisualFixes
{
    constexpr const TCHAR* LentMap = TEXT("/Game/World/lent");
    constexpr const TCHAR* FontPackage = TEXT("/Game/Marburgum/Fonts/F_SignRoboto");
    constexpr const TCHAR* EngineFont = TEXT("/Engine/EngineFonts/RobotoDistanceField.RobotoDistanceField");
    // A shop sign's lettering stands this high above the street in front of its wall, as MarburgumCreateMaps first placed it.
    constexpr double SignHeight = 340.0;
    constexpr double SignProud = 3.0;
    constexpr double SignSearch = 2000.0;
    constexpr double PasserBy = 800.0;
    constexpr double StartAbove = 100.0;
    constexpr double StartTooHigh = 150.0;
    // -Explain logs why each candidate spot for a sign was turned down.
    bool bExplain = false;

    TOptional<FHitResult> Trace(UWorld* World, const FVector& From, const FVector& To, const AActor* Ignore = nullptr)
    {
        FCollisionQueryParams Params(SCENE_QUERY_STAT(MarburgumVisualFixes), true, Ignore);
        Params.bReturnFaceIndex = true;
        FHitResult Hit;
        if (World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params))
        {
            return Hit;
        }
        return {};
    }

    bool IsBuilding(const AActor* Actor)
    {
        return Actor && Actor->GetFolderPath().ToString().StartsWith(TEXT("Buildings"));
    }

    /**
     * Whether a point is out in the open: building shells collide on both faces, so a ray that starts inside one finds
     * its walls from within, and the only sign of it is the roof straight overhead.
     */
    bool IsOutdoors(UWorld* World, const FVector& Point)
    {
        const TOptional<FHitResult> Over = Trace(World, Point, Point + FVector(0, 0, 10000.0));
        return !Over.IsSet() || !IsBuilding(Over->GetActor());
    }

    /** A hit on plain wall render, not a window's frame or glass, a door or a band (ADR-034, ADR-054). */
    bool IsPlainWall(const FHitResult& Hit)
    {
        int32 Section = 0;
        const UMaterialInterface* Material = Hit.GetComponent() ? Hit.GetComponent()->GetMaterialFromCollisionFaceIndex(Hit.FaceIndex, Section) : nullptr;
        return Material && Material->GetName().Contains(TEXT("plaster"));
    }

    UFont* MakeSignFont(bool bDryRun, TArray<UPackage*>& Dirty)
    {
        const UFont* Source = LoadObject<UFont>(nullptr, EngineFont);
        const FString Ttf = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts/Roboto-Bold.ttf")));
        if (!Source || !FPaths::FileExists(Ttf))
        {
            UE_LOG(LogMarburgumVisualFixes, Error, TEXT("MB2FIX font: missing %s or %s"), EngineFont, *Ttf);
            return nullptr;
        }
        // Printable ASCII, Latin-1 and Latin Extended-A: every letter of Slovene, its neighbours' and German place names.
        FString Chars;
        for (int32 Code = 0x20; Code <= 0x7E; ++Code)
        {
            Chars.AppendChar(TCHAR(Code));
        }
        for (int32 Code = 0xA0; Code <= 0x17F; ++Code)
        {
            Chars.AppendChar(TCHAR(Code));
        }
        Chars += TEXT("–—‘’‚“”„€");
        if (bDryRun)
        {
            UE_LOG(LogMarburgumVisualFixes, Display, TEXT("MB2FIX font would build %s from %s (%d characters)"), FontPackage, *Ttf, Chars.Len());
            return LoadObject<UFont>(nullptr, *FString::Printf(TEXT("%s.%s"), FontPackage, *FPackageName::GetShortName(FontPackage)));
        }
        UPackage* Package = FPackageName::DoesPackageExist(FontPackage) ? LoadPackage(nullptr, FontPackage, LOAD_None) : CreatePackage(FontPackage);
        Package->FullyLoad();
        // Rebuilt from scratch every run: the previous font and its pages go to the transient package.
        if (UFont* Previous = FindObject<UFont>(Package, *FPackageName::GetShortName(FontPackage)))
        {
            Previous->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
            Previous->ClearFlags(RF_Public | RF_Standalone);
        }
        UTrueTypeFontFactory* Factory = NewObject<UTrueTypeFontFactory>();
        Factory->AddToRoot();
        Factory->ImportOptions->Data = Source->ImportOptions;
        FFontImportOptionsData& Options = Factory->ImportOptions->Data;
        Options.FontName = Ttf;
        Options.Chars = Chars;
        Options.UnicodeRange.Reset();
        Options.CharsFilePath.Reset();
        Options.bIncludeASCIIRange = true;
        // The list above is printable already; the factory's own test is iswprint, which in the C locale of a Linux
        // commandlet rejects everything past ASCII and leaves those glyphs empty.
        Options.bCreatePrintableOnly = false;
        Options.bUseDistanceFieldAlpha = true;
        UE_LOG(LogMarburgumVisualFixes, Display, TEXT("MB2FIX font engine source: height %.0f, distance-field scale %d, page %dx%d"), Options.Height,
            Options.DistanceFieldScaleFactor, Options.TexturePageWidth, Options.TexturePageMaxHeight);
        Options.Height = Options.Height > 0.f ? Options.Height : 48.f;
        // The factory renders each page at page size x scale in one 32-bit bitmap, which must stay well under 2 GiB:
        // 1024 x 8 is an 8192-pixel square (256 MiB) and holds all 328 glyphs on one or two pages.
        Options.DistanceFieldScaleFactor = 8;
        Options.TexturePageWidth = 1024;
        Options.TexturePageMaxHeight = 1024;
        Factory->bPropertiesConfigured = false;
        UFont* Font = Cast<UFont>(Factory->FactoryCreateNew(UFont::StaticClass(), Package, *FPackageName::GetShortName(FontPackage), RF_Public | RF_Standalone, nullptr, GWarn));
        Factory->RemoveFromRoot();
        if (!Font)
        {
            UE_LOG(LogMarburgumVisualFixes, Error, TEXT("MB2FIX font: the TrueType factory did not build %s"), FontPackage);
            return nullptr;
        }
        Font->PostEditChange();
        Package->MarkPackageDirty();
        Dirty.Add(Package);
        // The glyph for s-caron must exist and have a size, not just a slot.
        const uint16* Slot = Font->IsRemapped ? Font->CharRemap.Find(0x0161) : nullptr;
        const int32 Index = Slot ? *Slot : 0x0161;
        const bool bShin = (!Font->IsRemapped || Slot) && Font->Characters.IsValidIndex(Index) && Font->Characters[Index].USize > 0;
        UE_LOG(LogMarburgumVisualFixes, Display, TEXT("MB2FIX font built %s: %d glyphs on %d pages, has s-caron=%d"), FontPackage, Font->Characters.Num(), Font->Textures.Num(),
            bShin ? 1 : 0);
        return bShin ? Font : nullptr;
    }

    /**
     * Whether lettering of this size, centred at Centre on Wall's face (Normal pointing out), lies on one flat stretch of
     * that wall and can be read by someone standing PasserBy out at eye height: nothing in between, seen from either end
     * (a ray that starts inside another closed building does not hit that building's walls).
     */
    bool Readable(UWorld* World, const ATextRenderActor* Sign, const AActor* Wall, const FVector& Centre, const FVector& Normal, const FVector& Size, double StreetZ)
    {
        const FVector Tangent(-Normal.Y, Normal.X, 0.0);
        const double HalfWidth = FMath::Max(Size.Y, 100.0) * 0.5 + 10.0;
        // Every 30 cm across and at the top, middle and bottom: the same wall, flat, and plain render (no window or band).
        for (double Across = -HalfWidth; Across <= HalfWidth + 1.0; Across += FMath::Min(30.0, HalfWidth))
        {
            for (const double Up : { -Size.Z * 0.5 - 5.0, 0.0, Size.Z * 0.5 + 5.0 })
            {
                const FVector Point = Centre + Tangent * Across + FVector(0, 0, Up);
                const TOptional<FHitResult> Probe = Trace(World, Point + Normal * 300.0, Point - Normal * 100.0, Sign);
                if (!Probe.IsSet() || Probe->GetActor() != Wall || !IsPlainWall(*Probe) || FMath::Abs(FVector::DotProduct(Probe->ImpactPoint - Centre, Normal)) > 8.0)
                {
                    UE_CLOG(bExplain, LogMarburgumVisualFixes, Display, TEXT("MB2FIX explain %s at %s: not plain flat wall at %.0f across, %.0f up"), *Sign->GetActorLabel(),
                        *Centre.ToString(), Across, Up);
                    return false;
                }
            }
        }
        const FVector Eye(Centre.X + Normal.X * PasserBy, Centre.Y + Normal.Y * PasserBy, StreetZ + 170.0);
        if (!IsOutdoors(World, Centre + Normal * 150.0) || !IsOutdoors(World, Eye))
        {
            UE_CLOG(bExplain, LogMarburgumVisualFixes, Display, TEXT("MB2FIX explain %s at %s: under a roof (front %d, eye %d)"), *Sign->GetActorLabel(), *Centre.ToString(),
                IsOutdoors(World, Centre + Normal * 150.0) ? 0 : 1, IsOutdoors(World, Eye) ? 0 : 1);
            return false;
        }
        const TOptional<FHitResult> Sight = Trace(World, Eye, Centre - Normal * 20.0, Sign);
        const TOptional<FHitResult> Back = Trace(World, Centre + Normal * 10.0, Eye, Sign);
        const bool bSeen = Sight.IsSet() && Sight->GetActor() == Wall && FVector::Dist(Sight->ImpactPoint, Centre) < 60.0 && !Back.IsSet();
        UE_CLOG(bExplain && !bSeen, LogMarburgumVisualFixes, Display, TEXT("MB2FIX explain %s at %s: hidden (sight %s, back %s)"), *Sign->GetActorLabel(), *Centre.ToString(),
            Sight.IsSet() && Sight->GetActor() ? *Sight->GetActor()->GetActorLabel() : TEXT("-"), Back.IsSet() && Back->GetActor() ? *Back->GetActor()->GetActorLabel() : TEXT("-"));
        return bSeen;
    }

    /**
     * Where a shop sign goes: where it is, if that already reads; otherwise on the wall in front of it, 3.4 m above
     * the street there (lower under eaves that come down to it), slid along the wall until it reads.
     */
    TOptional<FTransform> SignOnFacade(UWorld* World, ATextRenderActor* Sign)
    {
        const FVector Origin = Sign->GetActorLocation();
        const FVector Out = Sign->GetActorForwardVector().GetSafeNormal2D();
        const FVector Along(-Out.Y, Out.X, 0.0);
        const FVector Size = Sign->GetTextRender()->GetTextLocalSize();
        const auto StreetUnder = [World](const FVector& Point) -> TOptional<FHitResult>
        {
            TOptional<FHitResult> Street = Trace(World, Point + FVector(0, 0, 600.0), Point - FVector(0, 0, 3000.0));
            return Street.IsSet() && !IsBuilding(Street->GetActor()) ? Street : TOptional<FHitResult>();
        };
        if (const TOptional<FHitResult> Wall = Trace(World, Origin + Out * 300.0, Origin - Out * 100.0, Sign);
            Wall.IsSet() && IsBuilding(Wall->GetActor()) && FVector::Dist(Wall->ImpactPoint, Origin - Out * SignProud) < 5.0)
        {
            const TOptional<FHitResult> Street = StreetUnder(Wall->ImpactPoint + Out * 150.0);
            if (Street.IsSet() && Readable(World, Sign, Wall->GetActor(), Wall->ImpactPoint, Out, Size, Street->ImpactPoint.Z))
            {
                return Sign->GetActorTransform();
            }
        }
        // A sign left floating far above sign height (a ground rebuild that measured through unbuilt meshes lifted three,
        // ADR-090) is looked for at its own height first, then at the sign height over the street below it.
        TArray<double, TInlineAllocator<2>> Levels = { Origin.Z };
        if (const TOptional<FHitResult> Below = StreetUnder(Origin + Out * 150.0); Below.IsSet() && Origin.Z - Below->ImpactPoint.Z > SignHeight + 200.0)
        {
            Levels.Add(Below->ImpactPoint.Z + SignHeight);
        }
        for (const double Level : Levels)
        {
            for (double Step = 0.0; Step <= SignSearch; Step += 25.0)
            {
                for (const double Side : { 1.0, -1.0 })
                {
                    if (Step == 0.0 && Side < 0.0)
                    {
                        continue;
                    }
                    const FVector At = FVector(Origin.X, Origin.Y, Level) + Along * Step * Side;
                    // The wall just behind the old spot first, then the first wall seen from further out over the street.
                    for (const double From : { 400.0, 1500.0 })
                    {
                        const TOptional<FHitResult> Wall = Trace(World, At + Out * From, At - Out * 1500.0, Sign);
                        if (!Wall.IsSet() || !IsBuilding(Wall->GetActor()) || FVector::DotProduct(Wall->ImpactNormal.GetSafeNormal2D(), Out) < 0.8)
                        {
                            continue;
                        }
                        const FVector Normal = Wall->ImpactNormal.GetSafeNormal2D();
                        const TOptional<FHitResult> Street = StreetUnder(Wall->ImpactPoint + Normal * 150.0);
                        if (!Street.IsSet())
                        {
                            continue;
                        }
                        double Ceiling = SignHeight + 80.0;
                        // Eaves or a cornice that come down near the lettering: keep 40 cm clear of them, never under 2.6 m.
                        const FVector Foot(Wall->ImpactPoint.X + Normal.X * 30.0, Wall->ImpactPoint.Y + Normal.Y * 30.0, Street->ImpactPoint.Z + 200.0);
                        if (const TOptional<FHitResult> Over = Trace(World, Foot, Foot + FVector(0, 0, Ceiling + Size.Z), Sign); Over.IsSet())
                        {
                            Ceiling = Over->ImpactPoint.Z - Street->ImpactPoint.Z - 40.0 - Size.Z * 0.5;
                        }
                        // 3.4 m first, then a little lower or higher, to find the band between the ground floor's windows and the next.
                        for (const double Height : { SignHeight, SignHeight - 40.0, SignHeight + 40.0, SignHeight - 70.0, SignHeight + 80.0 })
                        {
                            const double Clamped = FMath::Max(260.0, FMath::Min(Height, Ceiling));
                            const FVector Centre(Wall->ImpactPoint.X, Wall->ImpactPoint.Y, Street->ImpactPoint.Z + Clamped);
                            if (Readable(World, Sign, Wall->GetActor(), Centre, Normal, Size, Street->ImpactPoint.Z))
                            {
                                return FTransform(Normal.Rotation(), Centre + Normal * SignProud);
                            }
                        }
                    }
                }
            }
        }
        return {};
    }
}

UMarburgumVisualFixesCommandlet::UMarburgumVisualFixesCommandlet()
{
    IsClient = false;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
}

int32 UMarburgumVisualFixesCommandlet::Main(const FString& Params)
{
    using namespace MarburgumVisualFixes;
    FString StepsText = TEXT("font,signs,people,starts");
    FParse::Value(*Params, TEXT("Steps="), StepsText);
    TArray<FString> Steps;
    StepsText.ToLower().ParseIntoArray(Steps, TEXT(","));
    const bool bDryRun = FParse::Param(*Params, TEXT("DryRun"));
    bExplain = FParse::Param(*Params, TEXT("Explain"));
    int32 Failures = 0;
    TArray<UPackage*> Dirty;

    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(LentMap);
    if (!World)
    {
        return 1;
    }
    World->UpdateWorldComponents(true, false);
    FAssetCompilingManager::Get().FinishAllCompilation();

    UFont* Font = nullptr;
    if (Steps.Contains(TEXT("font")))
    {
        Font = MakeSignFont(bDryRun, Dirty);
        Failures += Font || bDryRun ? 0 : 1;
    }
    int32 Changed = 0;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        const FString Label = Actor->GetActorLabel();
        const FVector At = Actor->GetActorLocation();
        if (ATextRenderActor* Sign = Cast<ATextRenderActor>(Actor); Sign && Actor->GetFolderPath() == FName(TEXT("Marburgum/Opening")))
        {
            if (Font)
            {
                Sign->GetTextRender()->SetFont(Font);
                UE_LOG(LogMarburgumVisualFixes, Display, TEXT("MB2FIX font %s now draws in %s"), *Label, *Font->GetPathName());
                ++Changed;
            }
            if (Steps.Contains(TEXT("signs")) && Label.StartsWith(TEXT("MB2Sign_shop_")))
            {
                const TOptional<FTransform> Placed = SignOnFacade(World, Sign);
                if (!Placed.IsSet())
                {
                    UE_LOG(LogMarburgumVisualFixes, Error, TEXT("MB2FIX sign %s: no flat, visible wall within %.0f m of %s"), *Label, SignSearch / 100.0, *At.ToString());
                    ++Failures;
                    continue;
                }
                if (FVector::Dist(At, Placed->GetLocation()) < 1.0)
                {
                    continue;
                }
                UE_LOG(LogMarburgumVisualFixes, Display, TEXT("MB2FIX sign %s from %s yaw %.1f to %s yaw %.1f (moved %.0f cm)"), *Label, *At.ToString(), Sign->GetActorRotation().Yaw,
                    *Placed->GetLocation().ToString(), Placed->Rotator().Yaw, FVector::Dist(At, Placed->GetLocation()));
                Sign->SetActorTransform(*Placed);
                ++Changed;
            }
            continue;
        }
        if (Steps.Contains(TEXT("people")) && Label.StartsWith(TEXT("MB2Npc_")))
        {
            const TOptional<FHitResult> Ground = Trace(World, At + FVector(0, 0, 50.0), At - FVector(0, 0, 3000.0), Actor);
            if (!Ground.IsSet())
            {
                UE_LOG(LogMarburgumVisualFixes, Error, TEXT("MB2FIX person %s has no ground under %s"), *Label, *At.ToString());
                ++Failures;
                continue;
            }
            if (FMath::Abs(At.Z - Ground->ImpactPoint.Z) > 1.0)
            {
                UE_LOG(LogMarburgumVisualFixes, Display, TEXT("MB2FIX person %s feet from %.1f to %.1f cm on %s"), *Label, At.Z, Ground->ImpactPoint.Z,
                    Ground->GetActor() ? *Ground->GetActor()->GetActorLabel() : TEXT("?"));
                Actor->SetActorLocation(FVector(At.X, At.Y, Ground->ImpactPoint.Z));
                ++Changed;
            }
            continue;
        }
        if (Steps.Contains(TEXT("starts")) && Actor->IsA<APlayerStart>())
        {
            const TOptional<FHitResult> Ground = Trace(World, At + FVector(0, 0, 50.0), At - FVector(0, 0, 3000.0), Actor);
            if (Ground.IsSet() && At.Z - Ground->ImpactPoint.Z > StartTooHigh)
            {
                UE_LOG(LogMarburgumVisualFixes, Display, TEXT("MB2FIX start %s from %.1f cm (%.0f cm above %s) to %.1f cm"), *Label, At.Z, At.Z - Ground->ImpactPoint.Z,
                    Ground->GetActor() ? *Ground->GetActor()->GetActorLabel() : TEXT("?"), Ground->ImpactPoint.Z + StartAbove);
                Actor->SetActorLocation(FVector(At.X, At.Y, Ground->ImpactPoint.Z + StartAbove));
                ++Changed;
            }
        }
    }
    UE_LOG(LogMarburgumVisualFixes, Display, TEXT("MB2FIX changes=%d failures=%d dry_run=%d"), Changed, Failures, bDryRun ? 1 : 0);
    if (bDryRun || Changed == 0)
    {
        return Failures > 0 ? 1 : 0;
    }
    if (!Dirty.IsEmpty() && !UEditorLoadingAndSavingUtils::SavePackages(Dirty, false))
    {
        UE_LOG(LogMarburgumVisualFixes, Error, TEXT("MB2FIX could not save the font"));
        return 1;
    }
    if (!UEditorLoadingAndSavingUtils::SaveMap(World, LentMap))
    {
        UE_LOG(LogMarburgumVisualFixes, Error, TEXT("MB2FIX could not save %s"), LentMap);
        return 1;
    }
    UE_LOG(LogMarburgumVisualFixes, Display, TEXT("MB2FIX saved %s"), LentMap);
    return Failures > 0 ? 1 : 0;
}
