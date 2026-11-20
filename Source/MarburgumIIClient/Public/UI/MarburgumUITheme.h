#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Layout/Margin.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateColor.h"

// UI-01 presentation tokens (ontology type `presentation_token`). Every Marburgum widget takes its
// colours, type, spacing and radii from here; per-screen ad-hoc values are not allowed. Values follow
// Docs/UI_HUD_DESIGN.md §3 and are open to art-direction review.
//
// Version 2 (UI/HUD overhaul): Fira Sans for the interface, Fira Sans Condensed for signage-style labels and Noto
// Serif for editorial titles (place names, the journal), all OFL and loaded from Content/Marburgum/Fonts; a refined
// charcoal/copper palette whose text colours meet 4.5:1 on every surface they sit on (QA U13); a high-contrast set;
// and the player's text size and reduced-motion preferences, which every token follows.
namespace MarburgumUI
{
    inline constexpr int32 TokenVersion = 2;

    enum class ETypeRole : uint8
    {
        Display,      // Title-screen wordmark (serif).
        Title,        // Screen titles.
        PlaceTitle,   // Location banner and archival headings (serif).
        Heading,      // Panel and section headers.
        Subheading,   // Card titles.
        Body,
        BodyStrong,
        Quote,        // What somebody says; journal entries (serif italic).
        Caption,      // Secondary detail, metadata.
        Overline,     // Small spaced capitals above titles and on pills (condensed).
        Label,        // Control labels, list rows.
        Numeric,      // Tabular values: counters, quantities.
        NumericLarge, // Big readouts: speed, levels.
        Key           // Key caps.
    };

    // Semantic tones; never the only carrier of meaning (always paired with text or a glyph).
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
        Sunken,  // Wells: list backgrounds, tracks.
        Overlay  // HUD modules drawn over the 3D world: translucent.
    };

    struct FColors
    {
        FLinearColor Backdrop;
        FLinearColor Panel;
        FLinearColor Raised;
        FLinearColor Sunken;
        FLinearColor Overlay;
        FLinearColor Scrim;        // Dims the street behind a screen that has the keys.
        FLinearColor Border;
        FLinearColor BorderStrong;
        FLinearColor Hairline;     // Copper rules and frames.
        FLinearColor TextPrimary;
        FLinearColor TextSecondary;
        FLinearColor TextMuted;
        FLinearColor TextOnAccent;
        FLinearColor Accent;
        FLinearColor AccentHover;
        FLinearColor AccentPressed;
        FLinearColor AccentSoft;   // Selected rows, active tabs.
        FLinearColor Info;
        FLinearColor Success;
        FLinearColor Warning;
        FLinearColor Danger;
        FLinearColor Focus;
        FLinearColor Disabled;
        FLinearColor SampleData;
        FLinearColor Paper;        // Archival surfaces (journal entries).
        FLinearColor PaperText;
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
        float ScreenGutter = 64.f;
    };

    struct FRadii
    {
        float Small = 2.f;
        float Medium = 4.f;
        float Large = 6.f;
        float Pill = 12.f;
    };

    struct FIconSizes
    {
        float Small = 16.f;
        float Medium = 20.f;
        float Large = 28.f;
    };

    struct FTokens
    {
        FColors Color;
        FSpacing Space;
        FRadii Radius;
        FIconSizes Icon;
        float BorderWidth = 1.f;
        float FocusWidth = 2.f;
        float FadeSeconds = 0.18f;
        float ToastSeconds = 4.5f;
        /** Multiplies every type size (Settings: text size). */
        float TextScale = 1.f;
        /** Multiplies how long a notification stays (Settings: notification time). */
        float NotificationTimeScale = 1.f;
        bool bHighContrast = false;
        /** No slides, pulses or long fades (Settings: reduce motion). */
        bool bReducedMotion = false;
    };

    /** The player's presentation preferences the tokens follow; client-local, never sent to a server. */
    struct FPresentationOptions
    {
        bool bHighContrast = false;
        bool bReducedMotion = false;
        float TextScale = 1.f;
        float NotificationTimeScale = 1.f;

        bool operator==(const FPresentationOptions& Other) const
        {
            return bHighContrast == Other.bHighContrast && bReducedMotion == Other.bReducedMotion
                && FMath::IsNearlyEqual(TextScale, Other.TextScale) && FMath::IsNearlyEqual(NotificationTimeScale, Other.NotificationTimeScale);
        }
    };

    /** Active token set. Widgets read it when they are built; after a change the UI subsystem rebuilds them. */
    MARBURGUMIICLIENT_API const FTokens& Tokens();
    /** Replaces the active token set; returns true when anything changed. */
    MARBURGUMIICLIENT_API bool SetPresentationOptions(const FPresentationOptions& Options);
    MARBURGUMIICLIENT_API const FPresentationOptions& GetPresentationOptions();

    MARBURGUMIICLIENT_API FSlateFontInfo Font(ETypeRole Role);
    /** True when the bundled interface fonts were found (false: engine Roboto stands in). */
    MARBURGUMIICLIENT_API bool HasBrandFonts();
    MARBURGUMIICLIENT_API FLinearColor ToneColor(ETone Tone);

    /** Rounded, optionally outlined box brush for a surface. */
    MARBURGUMIICLIENT_API FSlateBrush SurfaceBrush(ESurfaceKind Surface, bool bOutlined = true);
    MARBURGUMIICLIENT_API FSlateBrush RoundedBrush(const FLinearColor& Fill, float Radius,
        const FLinearColor& Outline = FLinearColor::Transparent, float OutlineWidth = 0.f);
    MARBURGUMIICLIENT_API FSlateBrush SolidBrush(const FLinearColor& Fill);

    /** WCAG 2.x relative luminance and contrast ratio, for the token tests (QA U13). */
    MARBURGUMIICLIENT_API float RelativeLuminance(const FLinearColor& Color);
    MARBURGUMIICLIENT_API float ContrastRatio(const FLinearColor& Foreground, const FLinearColor& Background);
    /** Foreground with alpha composited over an opaque background (and that over the backdrop when translucent). */
    MARBURGUMIICLIENT_API FLinearColor Composite(const FLinearColor& Top, const FLinearColor& Below);

    /** Padding helpers on the spacing scale. */
    inline FMargin Pad(float Uniform) { return FMargin(Uniform); }
    inline FMargin Pad(float Horizontal, float Vertical) { return FMargin(Horizontal, Vertical); }

    /**
     * Text in capitals for headings, overlines and name labels. FString::ToUpper changes ASCII letters only, so
     * "Kočar" would read "KOčAR"; FText::ToUpper is culture-aware (Č, Š, Ž, Ć, Đ, Ä, Ö, Ü).
     */
    /** The Marburgum calendar date in the interface language: "Tuesday 18 June 1996" / "torek, 18. junij 1996", or the
     *  short form "Tue 18 Jun 1996" / "18. jun. 1996". Times stay 24-hour ("20:15") in both languages. */
    MARBURGUMIICLIENT_API FText GameDate(int32 Year, int32 Month, int32 Day, int32 Weekday, bool bLong);
    MARBURGUMIICLIENT_API FText GameTime(int32 Hour, int32 Minute);

    inline FText Caps(const FString& Text) { return FText::FromString(Text).ToUpper(); }
    inline FText Caps(const FText& Text) { return Text.ToUpper(); }

    /**
     * Developer detail on player-facing screens (build and contract versions, server addresses, raw error text):
     * only with -MB2UIDiagnostics, and never in Shipping. The log always has it.
     */
    MARBURGUMIICLIENT_API bool ShowsDiagnostics();
}
