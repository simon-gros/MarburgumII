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

    UBorder* FBuilder::Toast(const FText& Kind, const FText& Message, ETone Tone) const
    {
        const FTokens& T = Tokens();
        UHorizontalBox* Line = Row();
        UImage* Accent = Rule(ToneColor(Tone), 1.f, 3.f);
        UHorizontalBoxSlot* AccentSlot = Line->AddChildToHorizontalBox(Accent);
        AccentSlot->SetVerticalAlignment(VAlign_Fill);
        AccentSlot->SetPadding(FMargin(0.f, 0.f, T.Space.M, 0.f));

        UVerticalBox* TextColumn = Column();
        Stack(TextColumn, Text(Kind, ETypeRole::Overline, ToneColor(Tone)), T.Space.XS);
        UTextBlock* MessageBlock = Text(Message, ETypeRole::Body);
        MessageBlock->SetAutoWrapText(true);
        Stack(TextColumn, MessageBlock, 0.f);
        Inline(Line, TextColumn, 0.f, true, false);

        return Surface(ESurfaceKind::Overlay, FMargin(T.Space.M, T.Space.M, T.Space.L, T.Space.M), Line);
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
