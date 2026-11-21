#include "UI/MarburgumUIBuilder.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

#define LOCTEXT_NAMESPACE "MarburgumUI"

namespace MarburgumUI
{
    UTextBlock* FBuilder::Text(const FText& Value, ETypeRole Role, const FLinearColor& Color) const
    {
        UTextBlock* Block = Tree.ConstructWidget<UTextBlock>();
        Block->SetText(Value);
        Block->SetFont(Font(Role));
        Block->SetColorAndOpacity(FSlateColor(Color));
        return Block;
    }

    UTextBlock* FBuilder::Text(const FText& Value, ETypeRole Role) const
    {
        const FColors& C = Tokens().Color;
        const bool bSecondary = Role == ETypeRole::Caption || Role == ETypeRole::Overline;
        return Text(Value, Role, bSecondary ? C.TextSecondary : C.TextPrimary);
    }

    UBorder* FBuilder::Surface(ESurfaceKind Kind, const FMargin& Padding, UWidget* Content, bool bOutlined) const
    {
        UBorder* Border = Tree.ConstructWidget<UBorder>();
        Border->SetBrush(SurfaceBrush(Kind, bOutlined));
        Border->SetPadding(Padding);
        if (Content)
        {
            Border->SetContent(Content);
        }
        return Border;
    }

    void FBuilder::Align(UBorder* Border, EHorizontalAlignment Horizontal, EVerticalAlignment Vertical)
    {
        Border->SetHorizontalAlignment(Horizontal);
        Border->SetVerticalAlignment(Vertical);
        if (UBorderSlot* Slot = Cast<UBorderSlot>(Border->GetContentSlot()))
        {
            Slot->SetHorizontalAlignment(Horizontal);
            Slot->SetVerticalAlignment(Vertical);
        }
    }

    UVerticalBox* FBuilder::Column() const
    {
        return Tree.ConstructWidget<UVerticalBox>();
    }

    UHorizontalBox* FBuilder::Row() const
    {
        return Tree.ConstructWidget<UHorizontalBox>();
    }

    UScrollBox* FBuilder::Scroll(UWidget* Content) const
    {
        UScrollBox* Box = Tree.ConstructWidget<UScrollBox>();
        Box->SetOrientation(Orient_Vertical);
        Box->SetScrollBarVisibility(ESlateVisibility::Visible);
        Box->SetAlwaysShowScrollbar(false);
        Box->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);
        if (Content)
        {
            Box->AddChild(Content);
        }
        return Box;
    }

    void FBuilder::ScrollBy(UScrollBox* Box, float Lines)
    {
        if (!Box)
        {
            return;
        }
        const float Line = Font(ETypeRole::Body).Size * 1.5f;
        Box->SetScrollOffset(FMath::Clamp(Box->GetScrollOffset() + Lines * Line, 0.f, Box->GetScrollOffsetOfEnd()));
    }

    void FBuilder::Stack(UVerticalBox* Box, UWidget* Child, float GapAfter, bool bFill)
    {
        UVerticalBoxSlot* Slot = Box->AddChildToVerticalBox(Child);
        Slot->SetPadding(FMargin(0.f, 0.f, 0.f, GapAfter));
        Slot->SetHorizontalAlignment(HAlign_Fill);
        if (bFill)
        {
            Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        }
    }

    void FBuilder::Inline(UHorizontalBox* Box, UWidget* Child, float GapAfter, bool bFill, bool bCenterVertically)
    {
        UHorizontalBoxSlot* Slot = Box->AddChildToHorizontalBox(Child);
        Slot->SetPadding(FMargin(0.f, 0.f, GapAfter, 0.f));
        Slot->SetVerticalAlignment(bCenterVertically ? VAlign_Center : VAlign_Fill);
        if (bFill)
        {
            Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        }
    }

    void FBuilder::InlineTop(UHorizontalBox* Box, UWidget* Child, float GapAfter, bool bFill)
    {
        Inline(Box, Child, GapAfter, bFill, false);
        if (UHorizontalBoxSlot* Slot = Cast<UHorizontalBoxSlot>(Child->Slot))
        {
            Slot->SetVerticalAlignment(VAlign_Top);
        }
    }

    UImage* FBuilder::Rule(const FLinearColor& Color, float Thickness, float Width) const
    {
        UImage* Image = Tree.ConstructWidget<UImage>();
        FSlateBrush Brush = SolidBrush(Color);
        Brush.ImageSize = FVector2D(Width > 0.f ? Width : 1.f, Thickness);
        Image->SetBrush(Brush);
        return Image;
    }

    USizeBox* FBuilder::Spacer(float Width, float Height) const
    {
        USizeBox* Box = Tree.ConstructWidget<USizeBox>();
        Box->SetWidthOverride(Width);
        Box->SetHeightOverride(Height);
        return Box;
    }

    void FBuilder::Fade(UWidget* Widget, float Opacity)
    {
        if (!Widget)
        {
            return;
        }
        Widget->SetRenderOpacity(Opacity);
        const ESlateVisibility Current = Widget->GetVisibility();
        if (Opacity <= 0.01f)
        {
            if (Current != ESlateVisibility::Collapsed && Current != ESlateVisibility::Hidden)
            {
                Widget->SetVisibility(ESlateVisibility::Hidden);
            }
        }
        else if (Current == ESlateVisibility::Hidden)
        {
            Widget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        }
    }

    void FBuilder::StyleButton(UButton* Button, EButtonKind Kind)
    {
        const FTokens& T = Tokens();
        const FColors& C = T.Color;
        const float R = T.Radius.Medium;
        FButtonStyle Style;
        switch (Kind)
        {
        case EButtonKind::Primary:
            Style.SetNormal(RoundedBrush(C.Accent, R))
                .SetHovered(RoundedBrush(C.AccentHover, R, C.Focus, T.BorderWidth))
                .SetPressed(RoundedBrush(C.AccentPressed, R))
                .SetDisabled(RoundedBrush(C.Disabled, R));
            break;
        case EButtonKind::Secondary:
            Style.SetNormal(RoundedBrush(FLinearColor::Transparent, R, C.BorderStrong, T.BorderWidth))
                .SetHovered(RoundedBrush(C.Raised, R, C.Accent, T.BorderWidth))
                .SetPressed(RoundedBrush(C.Panel, R, C.AccentPressed, T.BorderWidth))
                .SetDisabled(RoundedBrush(FLinearColor::Transparent, R, C.Disabled, T.BorderWidth));
            break;
        case EButtonKind::Quiet:
            Style.SetNormal(RoundedBrush(FLinearColor::Transparent, R))
                .SetHovered(RoundedBrush(C.Raised, R))
                .SetPressed(RoundedBrush(C.Panel, R))
                .SetDisabled(RoundedBrush(FLinearColor::Transparent, R));
            break;
        }
        const FMargin Padding(T.Space.XL, T.Space.M);
        Style.SetNormalPadding(Padding).SetPressedPadding(FMargin(T.Space.XL, T.Space.M + 1.f, T.Space.XL, T.Space.M - 1.f));
        Button->SetStyle(Style);
    }

    UButton* FBuilder::Button(const FText& Label, EButtonKind Kind, UTextBlock** OutLabel) const
    {
        const FColors& C = Tokens().Color;
        UButton* NewButton = Tree.ConstructWidget<UButton>();
        StyleButton(NewButton, Kind);

        UTextBlock* LabelBlock = Text(Label, ETypeRole::BodyStrong,
            Kind == EButtonKind::Primary ? C.TextOnAccent : C.TextPrimary);
        LabelBlock->SetJustification(ETextJustify::Center);
        if (UButtonSlot* Slot = Cast<UButtonSlot>(NewButton->AddChild(LabelBlock)))
        {
            Slot->SetHorizontalAlignment(HAlign_Center);
            Slot->SetVerticalAlignment(VAlign_Center);
        }
        if (OutLabel)
        {
            *OutLabel = LabelBlock;
        }
        return NewButton;
    }

    void FBuilder::SetPillTone(UBorder* Pill, UTextBlock* Label, ETone Tone)
    {
        const FTokens& T = Tokens();
        const FLinearColor Color = ToneColor(Tone);
        Pill->SetBrush(RoundedBrush(Color.CopyWithNewOpacity(0.16f), T.Radius.Pill, Color.CopyWithNewOpacity(0.7f), T.BorderWidth));
        if (Label)
        {
            Label->SetColorAndOpacity(FSlateColor(Tone == ETone::Neutral ? T.Color.TextPrimary : Color));
        }
    }

    UBorder* FBuilder::Pill(const FText& Label, ETone Tone, UTextBlock** OutLabel) const
    {
        const FTokens& T = Tokens();
        UTextBlock* LabelBlock = Text(Label, ETypeRole::Overline);
        UBorder* Capsule = Surface(ESurfaceKind::Raised, FMargin(T.Space.M, T.Space.XS), LabelBlock, false);
        Align(Capsule, HAlign_Center, VAlign_Center);
        SetPillTone(Capsule, LabelBlock, Tone);
        if (OutLabel)
        {
            *OutLabel = LabelBlock;
        }
        return Capsule;
    }

    UProgressBar* FBuilder::Meter(ETone Tone, float Percent, bool bMarquee) const
    {
        const FTokens& T = Tokens();
        UProgressBar* Bar = Tree.ConstructWidget<UProgressBar>();
        FProgressBarStyle Style;
        Style.SetBackgroundImage(RoundedBrush(T.Color.Raised, T.Radius.Small, T.Color.Border, T.BorderWidth))
            .SetFillImage(RoundedBrush(FLinearColor::White, T.Radius.Small))
            .SetMarqueeImage(RoundedBrush(FLinearColor::White, T.Radius.Small));
        Bar->SetWidgetStyle(Style);
        Bar->SetFillColorAndOpacity(ToneColor(Tone));
        Bar->SetPercent(Percent);
        Bar->SetIsMarquee(bMarquee);
        Bar->SetBorderPadding(FVector2D(0.f, 0.f));
        return Bar;
    }

    UEditableTextBox* FBuilder::TextField(const FText& Initial, const FText& Hint) const
    {
        const FTokens& T = Tokens();
        const FColors& C = T.Color;
        UEditableTextBox* Field = Tree.ConstructWidget<UEditableTextBox>();
        FEditableTextBoxStyle Style;
        Style.SetBackgroundImageNormal(RoundedBrush(C.Raised, T.Radius.Medium, C.Border, T.BorderWidth))
            .SetBackgroundImageHovered(RoundedBrush(C.Raised, T.Radius.Medium, C.BorderStrong, T.BorderWidth))
            .SetBackgroundImageFocused(RoundedBrush(C.Raised, T.Radius.Medium, C.Focus, T.FocusWidth))
            .SetBackgroundImageReadOnly(RoundedBrush(C.Panel, T.Radius.Medium, C.Border, T.BorderWidth))
            .SetPadding(FMargin(T.Space.L, T.Space.M))
            .SetFont(Font(ETypeRole::Body))
            .SetForegroundColor(FSlateColor(C.TextPrimary))
            .SetFocusedForegroundColor(FSlateColor(C.TextPrimary))
            .SetReadOnlyForegroundColor(FSlateColor(C.TextSecondary))
            .SetBackgroundColor(FSlateColor(FLinearColor::White));
        Field->SetWidgetStyle(Style);
        Field->SetText(Initial);
        Field->SetHintText(Hint);
        Field->SetSelectAllTextWhenFocused(true);
        Field->SetRevertTextOnEscape(true);
        return Field;
    }

    UHorizontalBox* FBuilder::Field(const FText& Key, const FText& Value, UTextBlock** OutValue) const
    {
        const FTokens& T = Tokens();
        UHorizontalBox* Line = Row();
        UTextBlock* KeyBlock = Text(Key, ETypeRole::Caption, T.Color.TextMuted);
        UTextBlock* ValueBlock = Text(Value, ETypeRole::Numeric, T.Color.TextPrimary);
        Inline(Line, KeyBlock, T.Space.L, true);
        Inline(Line, ValueBlock, 0.f);
        if (OutValue)
        {
            *OutValue = ValueBlock;
        }
        return Line;
    }

    UBorder* FBuilder::Toast(const FText& Kind, const FText& Message, ETone Tone, EMarburgumGlyph Icon) const
    {
        const FTokens& T = Tokens();
        UHorizontalBox* Line = Row();
        UImage* Accent = Rule(ToneColor(Tone), 1.f, 3.f);
        UHorizontalBoxSlot* AccentSlot = Line->AddChildToHorizontalBox(Accent);
        AccentSlot->SetVerticalAlignment(VAlign_Fill);
        AccentSlot->SetPadding(FMargin(0.f, 0.f, T.Space.M, 0.f));
        if (Icon != EMarburgumGlyph::None)
        {
            UMarburgumGlyph* Mark = Glyph(Icon, ToneColor(Tone), T.Icon.Medium);
            UHorizontalBoxSlot* MarkSlot = Line->AddChildToHorizontalBox(Mark);
            MarkSlot->SetVerticalAlignment(VAlign_Top);
            MarkSlot->SetPadding(FMargin(0.f, 1.f, T.Space.M, 0.f));
        }

        UVerticalBox* TextColumn = Column();
        Stack(TextColumn, Text(Kind, ETypeRole::Overline, ToneColor(Tone)), T.Space.XXS);
        UTextBlock* MessageBlock = Text(Message, ETypeRole::Body);
        MessageBlock->SetAutoWrapText(true);
        Stack(TextColumn, MessageBlock, 0.f);
        Inline(Line, TextColumn, 0.f, true, false);

        return Surface(ESurfaceKind::Overlay, FMargin(T.Space.M, T.Space.M, T.Space.L, T.Space.M), Line);
    }

    UMarburgumMeter* FBuilder::Bar(ETone Tone, float Percent, float Height, int32 Segments) const
    {
        UMarburgumMeter* Meter = Tree.ConstructWidget<UMarburgumMeter>();
        Meter->SetFillColor(Tone == ETone::Neutral ? Tokens().Color.TextMuted : ToneColor(Tone));
        Meter->SetPercent(Percent);
        Meter->SetHeight(Height);
        Meter->SetSegments(Segments);
        return Meter;
    }

    UMarburgumGlyph* FBuilder::Glyph(EMarburgumGlyph Icon, const FLinearColor& Color, float Size) const
    {
        UMarburgumGlyph* Mark = Tree.ConstructWidget<UMarburgumGlyph>();
        Mark->SetGlyph(Icon);
        Mark->SetColor(Color);
        // Icons follow the text size preference with the type they sit beside.
        Mark->SetSize((Size > 0.f ? Size : Tokens().Icon.Medium) * Tokens().TextScale);
        return Mark;
    }

    UHorizontalBox* FBuilder::GlyphText(EMarburgumGlyph Icon, const FText& Value, ETypeRole Role, const FLinearColor& Color,
        UTextBlock** OutText, float GlyphSize) const
    {
        const FTokens& T = Tokens();
        UHorizontalBox* Line = Row();
        Inline(Line, Glyph(Icon, Color, GlyphSize > 0.f ? GlyphSize : T.Icon.Small), T.Space.S);
        UTextBlock* Label = Text(Value, Role, Color);
        Inline(Line, Label, 0.f, true);
        if (OutText)
        {
            *OutText = Label;
        }
        return Line;
    }

    UBorder* FBuilder::KeyCap(const FText& Key, bool bActive, UTextBlock** OutLabel) const
    {
        const FTokens& T = Tokens();
        const FColors& C = T.Color;
        UTextBlock* Label = Text(Key, ETypeRole::Key, bActive ? C.TextOnAccent : C.TextSecondary);
        Label->SetJustification(ETextJustify::Center);
        UBorder* Cap = Surface(ESurfaceKind::Raised, FMargin(T.Space.S, T.Space.XXS), Label, false);
        Cap->SetBrush(bActive ? RoundedBrush(C.Accent, T.Radius.Medium, C.AccentPressed, T.BorderWidth)
                              : RoundedBrush(C.Sunken, T.Radius.Medium, C.BorderStrong, T.BorderWidth));
        Align(Cap, HAlign_Center, VAlign_Center);
        if (OutLabel)
        {
            *OutLabel = Label;
        }
        return Cap;
    }

    UHorizontalBox* FBuilder::KeyHint(const FText& Key, const FText& Action, UTextBlock** OutKey) const
    {
        const FTokens& T = Tokens();
        UHorizontalBox* Line = Row();
        UTextBlock* KeyLabel = Text(Key, ETypeRole::Caption, T.Color.TextPrimary);
        KeyLabel->SetFont(Font(ETypeRole::Label));
        UBorder* Cap = Surface(ESurfaceKind::Sunken, FMargin(T.Space.XS + 2.f, 0.f), KeyLabel, false);
        Cap->SetBrush(RoundedBrush(T.Color.Sunken, T.Radius.Small, T.Color.BorderStrong, T.BorderWidth));
        Align(Cap, HAlign_Center, VAlign_Center);
        Inline(Line, Cap, T.Space.XS + 2.f);
        Inline(Line, Text(Action, ETypeRole::Caption, T.Color.TextSecondary), 0.f);
        if (OutKey)
        {
            *OutKey = KeyLabel;
        }
        return Line;
    }

    UMarburgumScrim* FBuilder::Scrim(EMarburgumScrimShape Shape, const FLinearColor& Color) const
    {
        UMarburgumScrim* Backing = Tree.ConstructWidget<UMarburgumScrim>();
        Backing->Setup(Shape, Color);
        return Backing;
    }

    UMarburgumSelectable* FBuilder::Selectable(UWidget* Content, const FMargin& Padding, bool bQuiet) const
    {
        UMarburgumSelectable* Row = Tree.ConstructWidget<UMarburgumSelectable>();
        Row->SetQuiet(bQuiet);
        UBorder* Inner = Tree.ConstructWidget<UBorder>();
        Inner->SetBrush(SolidBrush(FLinearColor::Transparent));
        Inner->SetPadding(Padding);
        Inner->SetContent(Content);
        Row->SetContent(Inner);
        return Row;
    }

    UHorizontalBox* FBuilder::SectionTitle(const FText& Title, const FLinearColor& Color) const
    {
        const FTokens& T = Tokens();
        UHorizontalBox* Line = Row();
        Inline(Line, Text(Title, ETypeRole::Overline, Color), T.Space.M);
        UImage* Hair = Rule(T.Color.Border, 1.f);
        UHorizontalBoxSlot* HairSlot = Line->AddChildToHorizontalBox(Hair);
        HairSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        HairSlot->SetVerticalAlignment(VAlign_Center);
        return Line;
    }

    UBorder* FBuilder::Chip(EMarburgumGlyph Icon, const FText& Label, ETone Tone, UTextBlock** OutLabel) const
    {
        const FTokens& T = Tokens();
        const FLinearColor Color = Tone == ETone::Neutral ? T.Color.TextSecondary : ToneColor(Tone);
        UHorizontalBox* Line = Row();
        if (Icon != EMarburgumGlyph::None)
        {
            Inline(Line, Glyph(Icon, Color, 14.f), T.Space.XS);
        }
        UTextBlock* LabelBlock = Text(Label, ETypeRole::Overline, Color);
        Inline(Line, LabelBlock, 0.f);
        UBorder* Capsule = Surface(ESurfaceKind::Raised, FMargin(T.Space.S + 2.f, T.Space.XXS + 1.f), Line, false);
        Capsule->SetBrush(RoundedBrush(Color.CopyWithNewOpacity(T.bHighContrast ? 0.26f : 0.14f), T.Radius.Pill, Color.CopyWithNewOpacity(0.7f),
            T.BorderWidth));
        Align(Capsule, HAlign_Center, VAlign_Center);
        if (OutLabel)
        {
            *OutLabel = LabelBlock;
        }
        return Capsule;
    }

    USizeBox* FBuilder::Bounded(UWidget* Content, float MaxWidth, float MinWidth) const
    {
        USizeBox* Box = Tree.ConstructWidget<USizeBox>();
        Box->SetMaxDesiredWidth(MaxWidth);
        if (MinWidth > 0.f)
        {
            Box->SetMinDesiredWidth(MinWidth);
        }
        Box->SetContent(Content);
        return Box;
    }

    UBorder* FBuilder::StateCard(const FText& Title, const FText& Body, ETone Tone) const
    {
        const FTokens& T = Tokens();
        UVerticalBox* Content = Column();
        Stack(Content, Rule(ToneColor(Tone), 2.f, 32.f), T.Space.M);
        Stack(Content, Text(Title, ETypeRole::BodyStrong), T.Space.XS);
        UTextBlock* BodyBlock = Text(Body, ETypeRole::Caption);
        BodyBlock->SetAutoWrapText(true);
        Stack(Content, BodyBlock, 0.f);
        // Left-align the short accent rule instead of stretching it.
        if (UVerticalBoxSlot* RuleSlot = Cast<UVerticalBoxSlot>(Content->GetSlots()[0]))
        {
            RuleSlot->SetHorizontalAlignment(HAlign_Left);
        }
        return Surface(ESurfaceKind::Raised, Pad(T.Space.L), Content);
    }

    UBorder* FBuilder::SampleDataBadge() const
    {
        const FTokens& T = Tokens();
        UTextBlock* Label = Text(LOCTEXT("SampleData", "SAMPLE DATA · NOT LIVE GAMEPLAY"), ETypeRole::Overline, T.Color.SampleData);
        UBorder* Badge = Surface(ESurfaceKind::Raised, FMargin(T.Space.M, T.Space.XS), Label, false);
        Badge->SetBrush(RoundedBrush(T.Color.SampleData.CopyWithNewOpacity(0.14f), T.Radius.Pill, T.Color.SampleData, T.BorderWidth));
        return Badge;
    }
}

#undef LOCTEXT_NAMESPACE
