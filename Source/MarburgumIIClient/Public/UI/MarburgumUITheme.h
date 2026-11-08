#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Layout/Margin.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateColor.h"

// UI-01 presentation tokens (ontology type `presentation_token`). Every Marburgum widget takes its
// colors, type, spacing and radii from here; per-screen ad-hoc values are not allowed. Values are the
// "proposed initial tokens" of Docs/UI_HUD_DESIGN.md §3 and are open to art-direction review.
namespace MarburgumUI
{
    inline constexpr int32 TokenVersion = 1;

    enum class ETypeRole : uint8
    {
        Display,  // Title-screen wordmark.
        Title,    // Screen titles.
        Heading,  // Panel and section headers.
        Body,
        BodyStrong,
        Caption,  // Secondary detail, metadata.
        Overline, // Small spaced capitals above titles and on pills.
        Numeric   // Tabular values: counters, quantities.
    };

    // Semantic tones; never the only carrier of meaning (always paired with text or an icon glyph).
    enum class ETone : uint8
    {
        Neutral,
        Accent,  // Copper: primary action, brand.
        Info,    // Teal: wayfinding, neutral status.
        Success,
        Warning,
        Danger
    };

    enum class ESurfaceKind : uint8
    {
        Base,    // Full-screen backdrop.
        Panel,   // Cards and panels over the backdrop or the world.
        Raised,  // Inputs, nested cards.
        Overlay  // HUD modules drawn over the 3D world: translucent.
    };

    struct FColors
    {
        FLinearColor Backdrop;
        FLinearColor Panel;
        FLinearColor Raised;
        FLinearColor Overlay;
        FLinearColor Border;
        FLinearColor BorderStrong;
        FLinearColor TextPrimary;
        FLinearColor TextSecondary;
        FLinearColor TextMuted;
        FLinearColor TextOnAccent;
        FLinearColor Accent;
        FLinearColor AccentHover;
        FLinearColor AccentPressed;
        FLinearColor Info;
        FLinearColor Success;
        FLinearColor Warning;
        FLinearColor Danger;
        FLinearColor Focus;
        FLinearColor Disabled;
        FLinearColor SampleData;
    };

    struct FSpacing
    {
        float XXS = 2.f;
        float XS = 4.f;
        float S = 8.f;
        float M = 12.f;
        float L = 16.f;
        float XL = 24.f;
        float XXL = 32.f;
        float XXXL = 48.f;
        float ScreenGutter = 96.f;
    };

    struct FRadii
    {
        float Small = 2.f;
        float Medium = 4.f;
        float Large = 8.f;
        float Pill = 12.f;
    };

    struct FTokens
    {
        FColors Color;
        FSpacing Space;
        FRadii Radius;
        float BorderWidth = 1.f;
        float FocusWidth = 2.f;
        float FadeSeconds = 0.18f;
        float ToastSeconds = 4.5f;
    };

    /** Active token set. A future high-contrast variant swaps the whole set, not single screens. */
    MARBURGUMIICLIENT_API const FTokens& Tokens();

    MARBURGUMIICLIENT_API FSlateFontInfo Font(ETypeRole Role);
    MARBURGUMIICLIENT_API FLinearColor ToneColor(ETone Tone);

    /** Rounded, optionally outlined box brush for a surface. */
    MARBURGUMIICLIENT_API FSlateBrush SurfaceBrush(ESurfaceKind Surface, bool bOutlined = true);
    MARBURGUMIICLIENT_API FSlateBrush RoundedBrush(const FLinearColor& Fill, float Radius,
        const FLinearColor& Outline = FLinearColor::Transparent, float OutlineWidth = 0.f);
    MARBURGUMIICLIENT_API FSlateBrush SolidBrush(const FLinearColor& Fill);

    /** Padding helpers on the spacing scale. */
    inline FMargin Pad(float Uniform) { return FMargin(Uniform); }
    inline FMargin Pad(float Horizontal, float Vertical) { return FMargin(Horizontal, Vertical); }
}
