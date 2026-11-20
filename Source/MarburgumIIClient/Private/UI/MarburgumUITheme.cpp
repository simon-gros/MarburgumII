#include "UI/MarburgumUITheme.h"

#include "Fonts/CompositeFont.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "MarburgumWorldClock.h"
#include "HAL/FileManager.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumSessionContract.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"

namespace MarburgumUI
{
    namespace
    {
        FLinearColor Hex(const TCHAR* Value, float Alpha = 1.f)
        {
            FLinearColor Color = FLinearColor::FromSRGBColor(FColor::FromHex(Value));
            Color.A = Alpha;
            return Color;
        }

        FTokens MakeTokens(const FPresentationOptions& Options)
        {
            FTokens T;
            FColors& C = T.Color;
            // Charcoal/slate surfaces, warm off-white type, muted copper accent, desaturated teal. Every text colour
            // meets 4.5:1 on Panel, Raised and Sunken (Marburgum.Client.UIContrast).
            C.Backdrop = Hex(TEXT("0E1114"));
            C.Panel = Hex(TEXT("15191C"), 0.97f);
            C.Raised = Hex(TEXT("1D2226"));
            C.Sunken = Hex(TEXT("101316"));
            C.Overlay = Hex(TEXT("111518"), 0.84f);
            C.Scrim = Hex(TEXT("07090B"), 0.74f);
            C.Border = Hex(TEXT("2C3338"));
            C.BorderStrong = Hex(TEXT("46515A"));
            C.Hairline = Hex(TEXT("C8834B"), 0.45f);
            C.TextPrimary = Hex(TEXT("F2EBDF"));
            C.TextSecondary = Hex(TEXT("C2B9AC"));
            C.TextMuted = Hex(TEXT("959AA0"));
            C.TextOnAccent = Hex(TEXT("1A120B"));
            C.Accent = Hex(TEXT("C8834B"));
            C.AccentHover = Hex(TEXT("D9985F"));
            C.AccentPressed = Hex(TEXT("A86A3B"));
            C.AccentSoft = Hex(TEXT("C8834B"), 0.16f);
            C.Info = Hex(TEXT("72A7A3"));
            C.Success = Hex(TEXT("86B57F"));
            C.Warning = Hex(TEXT("E2B65E"));
            C.Danger = Hex(TEXT("E06B5D"));
            C.Focus = Hex(TEXT("F2CB96"));
            C.Disabled = Hex(TEXT("4B5257"));
            C.SampleData = Hex(TEXT("B48AD6"));
            C.Paper = Hex(TEXT("1C1A17"));
            C.PaperText = Hex(TEXT("EDE3D0"));
            if (Options.bHighContrast)
            {
                // Opaque surfaces, brighter secondary text and outlines that read over any street.
                C.Panel = Hex(TEXT("101315"));
                C.Raised = Hex(TEXT("1A1F23"));
                C.Overlay = Hex(TEXT("0D1012"), 0.96f);
                C.Scrim = Hex(TEXT("050607"), 0.88f);
                C.Border = Hex(TEXT("6A7680"));
                C.BorderStrong = Hex(TEXT("A3AEB7"));
                C.Hairline = Hex(TEXT("D9985F"), 0.9f);
                C.TextSecondary = Hex(TEXT("E2DACE"));
                C.TextMuted = Hex(TEXT("C1C6CB"));
                C.Focus = Hex(TEXT("FFE0B0"));
                C.AccentSoft = Hex(TEXT("C8834B"), 0.3f);
            }
            T.TextScale = FMath::Clamp(Options.TextScale, 0.8f, 1.6f);
            T.NotificationTimeScale = FMath::Clamp(Options.NotificationTimeScale, 0.5f, 3.f);
            T.bHighContrast = Options.bHighContrast;
            T.bReducedMotion = Options.bReducedMotion;
            T.FocusWidth = Options.bHighContrast ? 3.f : 2.f;
            T.BorderWidth = Options.bHighContrast ? 1.5f : 1.f;
            T.FadeSeconds = Options.bReducedMotion ? 0.f : 0.18f;
            return T;
        }

        FPresentationOptions& ActiveOptions()
        {
            static FPresentationOptions Options;
            return Options;
        }

        FTokens& ActiveTokens()
        {
            static FTokens Active = MakeTokens(ActiveOptions());
            return Active;
        }

        FString FontFile(const TCHAR* Name)
        {
            return FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Marburgum/Fonts"), FString(Name) + TEXT(".ttf"));
        }

        /** Three composite families: the interface sans, its condensed cut and the editorial serif. */
        struct FFamilies
        {
            TSharedPtr<const FCompositeFont> Sans;
            TSharedPtr<const FCompositeFont> Condensed;
            TSharedPtr<const FCompositeFont> Serif;
            bool bBrand = false;
        };

        const FFamilies& Families()
        {
            static const FFamilies Built = []()
            {
                FFamilies Out;
                const TCHAR* Required[] = { TEXT("FiraSans-Light"), TEXT("FiraSans-Regular"), TEXT("FiraSans-Medium"),
                    TEXT("FiraSans-SemiBold"), TEXT("FiraSans-Bold"), TEXT("FiraSans-Italic"), TEXT("FiraSansCondensed-SemiBold"),
                    TEXT("NotoSerif-Regular"), TEXT("NotoSerif-Italic") };
                TArray<FString> Missing;
                for (const TCHAR* Name : Required)
                {
                    if (!IFileManager::Get().FileExists(*FontFile(Name)))
                    {
                        Missing.Add(Name);
                    }
                }
                if (!Missing.IsEmpty())
                {
                    UE_LOG(LogMarburgumNet, Warning, TEXT("%s role=client event=font_fallback missing=\"%s\" dir=\"%s\""),
                        MarburgumII::Session::UiLogPrefix, *FString::Join(Missing, TEXT(",")), *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Marburgum/Fonts")));
                    return Out;
                }
                // Glyphs a face lacks (arrows, symbols, scripts beyond Latin/Greek/Cyrillic) come from the engine faces.
                auto AddFallback = [](FCompositeFont& Font)
                {
                    const FString Engine = FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts"));
                    Font.FallbackTypeface.Typeface.AppendFont(TEXT("Regular"), Engine / TEXT("Roboto-Regular.ttf"), EFontHinting::Default,
                        EFontLoadingPolicy::LazyLoad);
                };
                auto Face = [](FCompositeFont& Font, const TCHAR* Typeface, const TCHAR* File)
                {
                    Font.DefaultTypeface.AppendFont(Typeface, FontFile(File), EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
                };
                TSharedRef<FCompositeFont> Sans = MakeShared<FCompositeFont>();
                Face(*Sans, TEXT("Light"), TEXT("FiraSans-Light"));
                Face(*Sans, TEXT("Regular"), TEXT("FiraSans-Regular"));
                Face(*Sans, TEXT("Medium"), TEXT("FiraSans-Medium"));
                Face(*Sans, TEXT("SemiBold"), TEXT("FiraSans-SemiBold"));
                Face(*Sans, TEXT("Bold"), TEXT("FiraSans-Bold"));
                Face(*Sans, TEXT("Italic"), TEXT("FiraSans-Italic"));
                AddFallback(*Sans);
                TSharedRef<FCompositeFont> Condensed = MakeShared<FCompositeFont>();
                Face(*Condensed, TEXT("SemiBold"), TEXT("FiraSansCondensed-SemiBold"));
                AddFallback(*Condensed);
                TSharedRef<FCompositeFont> Serif = MakeShared<FCompositeFont>();
                Face(*Serif, TEXT("Regular"), TEXT("NotoSerif-Regular"));
                Face(*Serif, TEXT("Italic"), TEXT("NotoSerif-Italic"));
                AddFallback(*Serif);
                Out.Sans = Sans;
                Out.Condensed = Condensed;
                Out.Serif = Serif;
                Out.bBrand = true;
                return Out;
            }();
            return Built;
        }

        FSlateFontInfo Make(const TSharedPtr<const FCompositeFont>& Family, const TCHAR* Typeface, float Size, const char* FallbackStyle)
        {
            const float Scaled = FMath::RoundToFloat(Size * ActiveTokens().TextScale);
            if (Family.IsValid())
            {
                return FSlateFontInfo(Family, Scaled, FName(Typeface));
            }
            return FCoreStyle::GetDefaultFontStyle(FallbackStyle, FMath::RoundToInt(Scaled));
        }
    }

    const FTokens& Tokens()
    {
        return ActiveTokens();
    }

    bool SetPresentationOptions(const FPresentationOptions& Options)
    {
        if (Options == ActiveOptions())
        {
            return false;
        }
        ActiveOptions() = Options;
        ActiveTokens() = MakeTokens(Options);
        return true;
    }

    const FPresentationOptions& GetPresentationOptions()
    {
        return ActiveOptions();
    }

    bool HasBrandFonts()
    {
        return Families().bBrand;
    }

    FSlateFontInfo Font(ETypeRole Role)
    {
        const FFamilies& F = Families();
        FSlateFontInfo Info;
        switch (Role)
        {
        case ETypeRole::Display:
            Info = Make(F.Serif, TEXT("Regular"), 52.f, "Light");
            Info.LetterSpacing = 10;
            break;
        case ETypeRole::Title:
            Info = Make(F.Sans, TEXT("Light"), 28.f, "Light");
            break;
        case ETypeRole::PlaceTitle:
            Info = Make(F.Serif, TEXT("Regular"), 36.f, "Regular");
            break;
        case ETypeRole::Heading:
            Info = Make(F.Sans, TEXT("SemiBold"), 18.f, "Bold");
            break;
        case ETypeRole::Subheading:
            Info = Make(F.Sans, TEXT("Medium"), 15.f, "Bold");
            break;
        case ETypeRole::Body:
            Info = Make(F.Sans, TEXT("Regular"), 14.f, "Regular");
            break;
        case ETypeRole::BodyStrong:
            Info = Make(F.Sans, TEXT("SemiBold"), 14.f, "Bold");
            break;
        case ETypeRole::Quote:
            Info = Make(F.Serif, TEXT("Italic"), 16.f, "Italic");
            break;
        case ETypeRole::Caption:
            Info = Make(F.Sans, TEXT("Regular"), 12.f, "Regular");
            break;
        case ETypeRole::Overline:
            Info = Make(F.Condensed, TEXT("SemiBold"), 12.f, "Bold");
            Info.LetterSpacing = 110;
            break;
        case ETypeRole::Label:
            Info = Make(F.Sans, TEXT("Medium"), 13.f, "Regular");
            break;
        case ETypeRole::Numeric:
            Info = Make(F.Sans, TEXT("Medium"), 13.f, "Mono");
            break;
        case ETypeRole::NumericLarge:
            Info = Make(F.Sans, TEXT("Light"), 40.f, "Light");
            break;
        case ETypeRole::Key:
            Info = Make(F.Sans, TEXT("SemiBold"), 14.f, "Bold");
            break;
        }
        return Info;
    }

    bool ShowsDiagnostics()
    {
#if UE_BUILD_SHIPPING
        return false;
#else
        static const bool bShows = FParse::Param(FCommandLine::Get(), TEXT("MB2UIDiagnostics"));
        return bShows;
#endif
    }

    FLinearColor ToneColor(ETone Tone)
    {
        const FColors& C = Tokens().Color;
        switch (Tone)
        {
        case ETone::Neutral: return C.TextSecondary;
        case ETone::Accent: return C.Accent;
        case ETone::Info: return C.Info;
        case ETone::Success: return C.Success;
        case ETone::Warning: return C.Warning;
        case ETone::Danger: return C.Danger;
        }
        return C.TextSecondary;
    }

    FSlateBrush RoundedBrush(const FLinearColor& Fill, float Radius, const FLinearColor& Outline, float OutlineWidth)
    {
        FSlateBrush Brush;
        Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
        Brush.TintColor = FSlateColor(Fill);
        Brush.OutlineSettings = FSlateBrushOutlineSettings(FVector4(Radius, Radius, Radius, Radius), FSlateColor(Outline), OutlineWidth);
        Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
        return Brush;
    }

    FSlateBrush SolidBrush(const FLinearColor& Fill)
    {
        FSlateBrush Brush;
        Brush.DrawAs = ESlateBrushDrawType::Box;
        Brush.TintColor = FSlateColor(Fill);
        return Brush;
    }

    FSlateBrush SurfaceBrush(ESurfaceKind Surface, bool bOutlined)
    {
        const FTokens& T = Tokens();
        const FLinearColor Outline = bOutlined ? T.Color.Border : FLinearColor::Transparent;
        const float Width = bOutlined ? T.BorderWidth : 0.f;
        switch (Surface)
        {
        case ESurfaceKind::Base: return SolidBrush(T.Color.Backdrop);
        case ESurfaceKind::Panel: return RoundedBrush(T.Color.Panel, T.Radius.Large, Outline, Width);
        case ESurfaceKind::Raised: return RoundedBrush(T.Color.Raised, T.Radius.Medium, Outline, Width);
        case ESurfaceKind::Sunken: return RoundedBrush(T.Color.Sunken, T.Radius.Medium, Outline, Width);
        case ESurfaceKind::Overlay: return RoundedBrush(T.Color.Overlay, T.Radius.Large, Outline, Width);
        }
        return SolidBrush(T.Color.Panel);
    }

    float RelativeLuminance(const FLinearColor& Color)
    {
        // FLinearColor channels are already linear, which is what the WCAG formula takes after its sRGB decode.
        return 0.2126f * Color.R + 0.7152f * Color.G + 0.0722f * Color.B;
    }

    float ContrastRatio(const FLinearColor& Foreground, const FLinearColor& Background)
    {
        const float A = RelativeLuminance(Foreground);
        const float B = RelativeLuminance(Background);
        return (FMath::Max(A, B) + 0.05f) / (FMath::Min(A, B) + 0.05f);
    }

    FLinearColor Composite(const FLinearColor& Top, const FLinearColor& Below)
    {
        const float A = FMath::Clamp(Top.A, 0.f, 1.f);
        FLinearColor Out = Top * A + Below * (1.f - A);
        Out.A = 1.f;
        return Out;
    }

    FText GameDate(int32 Year, int32 Month, int32 Day, int32 Weekday, bool bLong)
    {
        MarburgumII::Clock::FCivil Civil;
        Civil.Year = Year;
        Civil.Month = Month;
        Civil.Day = Day;
        Civil.Weekday = Weekday;
        const bool bSlovene = FInternationalization::Get().GetCurrentLanguage()->GetTwoLetterISOLanguageName() == TEXT("sl");
        if (!bSlovene || !FDateTime::Validate(Year, Month, Day, 0, 0, 0, 0))
        {
            // English keeps the shared clock's own wording (the label the server and the smokes log).
            const FString Long = MarburgumII::Clock::DateLabel(Civil);
            if (bLong)
            {
                return FText::AsCultureInvariant(Long);
            }
            const FString Short = MarburgumII::Clock::ShortLabel(Civil);
            int32 Dot = INDEX_NONE;
            return FText::AsCultureInvariant(Short.FindChar(TEXT('\xB7'), Dot) ? Short.Left(Dot).TrimEnd() : Short);
        }
        // Slovene from the culture's calendar names (ICU): "torek, 18. junij 1996", "18. jun. 1996".
        return FText::AsDate(FDateTime(Year, Month, Day), bLong ? EDateTimeStyle::Full : EDateTimeStyle::Medium, FText::GetInvariantTimeZone());
    }

    FText GameTime(int32 Hour, int32 Minute)
    {
        return FText::AsCultureInvariant(FString::Printf(TEXT("%02d:%02d"), Hour, Minute));
    }
}
