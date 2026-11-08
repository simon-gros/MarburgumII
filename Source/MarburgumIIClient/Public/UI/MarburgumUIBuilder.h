#pragma once

#include "CoreMinimal.h"
#include "UI/MarburgumUITheme.h"

class UBorder;
class UButton;
class UEditableTextBox;
class UHorizontalBox;
class UImage;
class UPanelWidget;
class UProgressBar;
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

        /** Adds a child to a vertical box with a bottom gap from the spacing scale. */
        static void Stack(UVerticalBox* Box, UWidget* Child, float GapAfter, bool bFill = false);
        /** Adds a child to a horizontal box with a right gap from the spacing scale. */
        static void Inline(UHorizontalBox* Box, UWidget* Child, float GapAfter, bool bFill = false, bool bCenterVertically = true);

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

        /** Single-line themed text field. */
        UEditableTextBox* TextField(const FText& Initial, const FText& Hint) const;

        /** Key/value line used on cards and diagnostics. */
        UHorizontalBox* Field(const FText& Key, const FText& Value, UTextBlock** OutValue = nullptr) const;

        /** Notification card: tone accent bar, kind overline and message (notification_feed item). */
        UBorder* Toast(const FText& Kind, const FText& Message, ETone Tone) const;

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
