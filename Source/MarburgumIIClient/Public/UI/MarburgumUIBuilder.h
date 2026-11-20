#pragma once

#include "CoreMinimal.h"
#include "UI/MarburgumUITheme.h"
#include "UI/MarburgumUIWidgets.h"

class UBorder;
class UButton;
class UEditableTextBox;
class UHorizontalBox;
class UImage;
class UPanelWidget;
class UProgressBar;
class UScrollBox;
class USizeBox;
class UTextBlock;
class UVerticalBox;
class UWidget;
class UWidgetTree;

namespace MarburgumUI
{
    enum class EButtonKind : uint8
    {
        Primary,   // One per surface: copper fill.
        Secondary, // Outlined.
        Quiet      // Text-only, low emphasis.
    };

    /**
     * UI-01 component primitives. Constructs themed UMG widgets inside a UserWidget's WidgetTree so
     * every screen shares one visual grammar. Builders only style and assemble; they never bind
     * gameplay data or issue commands.
     */
    class MARBURGUMIICLIENT_API FBuilder
    {
    public:
        explicit FBuilder(UWidgetTree& InTree) : Tree(InTree) {}

        UTextBlock* Text(const FText& Value, ETypeRole Role, const FLinearColor& Color) const;
        UTextBlock* Text(const FText& Value, ETypeRole Role = ETypeRole::Body) const;

        /** Rounded surface with padding around optional content. */
        UBorder* Surface(ESurfaceKind Kind, const FMargin& Padding, UWidget* Content = nullptr, bool bOutlined = true) const;

        UVerticalBox* Column() const;
        UHorizontalBox* Row() const;

        /** Vertical scroll region around Content. Stacked with bFill, it shrinks to the screen and scrolls when the
         *  content is taller than the screen, and keeps its natural height otherwise. */
        UScrollBox* Scroll(UWidget* Content) const;
        /** Scrolls Box by Lines lines of body text (negative is up), clamped to the content. */
        static void ScrollBy(UScrollBox* Box, float Lines);

        /** Adds a child to a vertical box with a bottom gap from the spacing scale. */
        static void Stack(UVerticalBox* Box, UWidget* Child, float GapAfter, bool bFill = false);
        /** Adds a child to a horizontal box with a right gap from the spacing scale. */
        static void Inline(UHorizontalBox* Box, UWidget* Child, float GapAfter, bool bFill = false, bool bCenterVertically = true);
        /** Inline, top-aligned: a card beside a taller list keeps its own height instead of stretching. */
        static void InlineTop(UHorizontalBox* Box, UWidget* Child, float GapAfter, bool bFill = false);

        /** Thin horizontal rule (accent or divider). */
        UImage* Rule(const FLinearColor& Color, float Thickness, float Width = 0.f) const;
        /** Fixed-size empty space. */
        USizeBox* Spacer(float Width, float Height) const;

        /** Button with a themed label; OutLabel receives the label for later text changes. */
        UButton* Button(const FText& Label, EButtonKind Kind, UTextBlock** OutLabel = nullptr) const;

        /** Small capsule label; text + tone, never color alone. */
        UBorder* Pill(const FText& Label, ETone Tone, UTextBlock** OutLabel = nullptr) const;

        /** Themed horizontal meter; marquee for honest indeterminate progress. */
        UProgressBar* Meter(ETone Tone, float Percent, bool bMarquee = false) const;
        /** Vector meter of a fixed height: rounded track, toned fill, optional segment ticks. */
        UMarburgumMeter* Bar(ETone Tone, float Percent, float Height = 6.f, int32 Segments = 0) const;

        /** A line glyph from the UI-01 icon family. */
        UMarburgumGlyph* Glyph(EMarburgumGlyph Icon, const FLinearColor& Color, float Size = 0.f) const;
        /** Glyph and text on one line. */
        UHorizontalBox* GlyphText(EMarburgumGlyph Icon, const FText& Value, ETypeRole Role, const FLinearColor& Color,
            UTextBlock** OutText = nullptr, float GlyphSize = 0.f) const;
        /** A keyboard key cap ("E", "Esc"); inactive caps are drawn hollow. */
        UBorder* KeyCap(const FText& Key, bool bActive = true, UTextBlock** OutLabel = nullptr) const;
        /** A key cap and what it does: "[Esc] Close". */
        UHorizontalBox* KeyHint(const FText& Key, const FText& Action) const;
        /** A soft gradient behind text over the street. */
        UMarburgumScrim* Scrim(EMarburgumScrimShape Shape, const FLinearColor& Color) const;
        /** A focusable row/tile/tab around Content (padded). Bind its events before adding it to a parent. */
        UMarburgumSelectable* Selectable(UWidget* Content, const FMargin& Padding, bool bQuiet = false) const;
        /** Section title: spaced capitals followed by a hairline. */
        UHorizontalBox* SectionTitle(const FText& Title, const FLinearColor& Color) const;
        /** A chip: glyph and spaced capitals on a toned capsule. */
        UBorder* Chip(EMarburgumGlyph Icon, const FText& Label, ETone Tone, UTextBlock** OutLabel = nullptr) const;
        /** Width-constrained box: at most MaxWidth, shrinking with the screen (no fixed pixel layouts). */
        USizeBox* Bounded(UWidget* Content, float MaxWidth, float MinWidth = 0.f) const;

        /** Single-line themed text field. */
        UEditableTextBox* TextField(const FText& Initial, const FText& Hint) const;

        /** Key/value line used on cards and diagnostics. */
        UHorizontalBox* Field(const FText& Key, const FText& Value, UTextBlock** OutValue = nullptr) const;

        /** Notification card: tone accent bar, kind overline and message (notification_feed item). */
        UBorder* Toast(const FText& Kind, const FText& Message, ETone Tone, EMarburgumGlyph Icon = EMarburgumGlyph::None) const;

        /** Designed empty/error/unsupported state card; never a bare empty string. */
        UBorder* StateCard(const FText& Title, const FText& Body, ETone Tone) const;

        /** Visible marker for design fixtures (ontology invariant: fixtures are always labeled). */
        UBorder* SampleDataBadge() const;

        UWidgetTree& GetTree() const { return Tree; }

        /** Aligns a border's content. UBorder copies alignment into its slot only when content is added,
         *  so changing it afterwards must go through the slot as well. */
        static void Align(UBorder* Border, EHorizontalAlignment Horizontal, EVerticalAlignment Vertical);

        static void StyleButton(UButton* Button, EButtonKind Kind);
        static void SetPillTone(UBorder* Pill, UTextBlock* Label, ETone Tone);

    private:
        UWidgetTree& Tree;
    };
}
