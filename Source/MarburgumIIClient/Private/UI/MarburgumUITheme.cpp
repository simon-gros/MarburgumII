#include "UI/MarburgumUITheme.h"

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

        FTokens MakeDefaultTokens()
        {
            FTokens T;
            FColors& C = T.Color;
            // Charcoal/slate surfaces, warm off-white type, muted copper accent, desaturated teal.
            C.Backdrop = Hex(TEXT("101315"));
            C.Panel = Hex(TEXT("191D20"), 0.96f);
            C.Raised = Hex(TEXT("23292D"));
            C.Overlay = Hex(TEXT("15191C"), 0.82f);
            C.Border = Hex(TEXT("343C42"));
            C.BorderStrong = Hex(TEXT("4A545B"));
            C.TextPrimary = Hex(TEXT("EEE7DB"));
            C.TextSecondary = Hex(TEXT("B3ACA1"));
            C.TextMuted = Hex(TEXT("7E8388"));
            C.TextOnAccent = Hex(TEXT("17110C"));
            C.Accent = Hex(TEXT("C27D46"));
            C.AccentHover = Hex(TEXT("D48F57"));
            C.AccentPressed = Hex(TEXT("A5683A"));
            C.Info = Hex(TEXT("5E9696"));
            C.Success = Hex(TEXT("74A86F"));
            C.Warning = Hex(TEXT("D6A94F"));
            C.Danger = Hex(TEXT("CC5B4E"));
            C.Focus = Hex(TEXT("E9C08F"));
            C.Disabled = Hex(TEXT("4B5257"));
            C.SampleData = Hex(TEXT("B07FD0"));
            return T;
        }
    }

    const FTokens& Tokens()
    {
        static const FTokens Default = MakeDefaultTokens();
        return Default;
    }

    FSlateFontInfo Font(ETypeRole Role)
    {
        // Engine-bundled Roboto (Apache-2.0; covers Slovene diacritics). A licensed brand face can
        // replace it here without touching any widget.
        FSlateFontInfo Info;
        switch (Role)
        {
        case ETypeRole::Display:
            Info = FCoreStyle::GetDefaultFontStyle("Light", 56);
            Info.LetterSpacing = 20;
            break;
        case ETypeRole::Title:
            Info = FCoreStyle::GetDefaultFontStyle("Regular", 28);
            break;
        case ETypeRole::Heading:
            Info = FCoreStyle::GetDefaultFontStyle("Bold", 17);
            Info.LetterSpacing = 10;
            break;
        case ETypeRole::Body:
            Info = FCoreStyle::GetDefaultFontStyle("Regular", 14);
            break;
        case ETypeRole::BodyStrong:
            Info = FCoreStyle::GetDefaultFontStyle("Bold", 14);
            break;
        case ETypeRole::Caption:
            Info = FCoreStyle::GetDefaultFontStyle("Regular", 12);
            break;
        case ETypeRole::Overline:
            Info = FCoreStyle::GetDefaultFontStyle("Bold", 11);
            Info.LetterSpacing = 160;
            break;
        case ETypeRole::Numeric:
            Info = FCoreStyle::GetDefaultFontStyle("Mono", 13);
            break;
        }
        return Info;
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
        case ESurfaceKind::Overlay: return RoundedBrush(T.Color.Overlay, T.Radius.Large, Outline, Width);
        }
        return SolidBrush(T.Color.Panel);
    }
}
