#pragma once

#include "CoreMinimal.h"
#include "Components/ContentWidget.h"
#include "Components/Widget.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "MarburgumUIWidgets.generated.h"

/**
 * UI-01 vector primitives drawn by Slate itself: no textures to blur at 4K or to licence. Glyphs are a small line-icon
 * family on a 24-unit grid; meters, scrims and the selectable row take their colours from the theme tokens.
 */
enum class EMarburgumGlyph : uint8
{
    None,
    Talk, Inspect, Use, Fish, Car, Door, Item, Coin, Skill, Mission, Journal, Character, Inventory, Settings, Chat,
    Warning, Check, Close, Heart, Breath, Swim, Clock, Pin, Signal, Impact, Lock, ChevronRight, ChevronLeft, ChevronUp,
    ChevronDown, Info, Menu, Exit, Sound, Display, Keyboard, Gamepad, Eye, Star, Clue, Shop, Fast, Trade, Quote, Person
};

/** A line glyph sized to its allotted box, stroked in one colour. */
class MARBURGUMIICLIENT_API SMarburgumGlyph : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SMarburgumGlyph) : _Glyph(EMarburgumGlyph::None), _Color(FLinearColor::White), _Size(20.f) {}
        SLATE_ARGUMENT(EMarburgumGlyph, Glyph)
        SLATE_ATTRIBUTE(FSlateColor, Color)
        SLATE_ARGUMENT(float, Size)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void SetGlyph(EMarburgumGlyph InGlyph);
    void SetColor(const TAttribute<FSlateColor>& InColor);
    void SetSize(float InSize);

    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
        FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Size, Size); }

    /** The glyph's strokes in 24-unit space (cached), for tests. */
    static const TArray<TArray<FVector2f>>& Strokes(EMarburgumGlyph Glyph);

private:
    EMarburgumGlyph Glyph = EMarburgumGlyph::None;
    TAttribute<FSlateColor> Color;
    float Size = 20.f;
};

/** A horizontal meter: rounded track, toned fill, optional segment ticks and a lagging ghost of the previous value. */
class MARBURGUMIICLIENT_API SMarburgumMeter : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SMarburgumMeter) : _Percent(1.f), _FillColor(FLinearColor::White), _Height(6.f), _Segments(0) {}
        SLATE_ARGUMENT(float, Percent)
        SLATE_ARGUMENT(FLinearColor, FillColor)
        SLATE_ARGUMENT(float, Height)
        SLATE_ARGUMENT(int32, Segments)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void SetPercent(float InPercent);
    void SetGhost(float InGhost);
    void SetFillColor(const FLinearColor& InColor);
    void SetSegments(int32 InSegments);
    void SetHeight(float InHeight);

    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
        FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(24.f, Height); }

private:
    float Percent = 1.f;
    float Ghost = -1.f;
    FLinearColor FillColor;
    float Height = 6.f;
    int32 Segments = 0;
    FSlateBrush Track;
    FSlateBrush Fill;
};

/** A soft gradient behind text over the street: a band fading out at its ends, or a vertical fade. */
enum class EMarburgumScrimShape : uint8
{
    HorizontalBand, // Transparent at the left and right ends, solid in the middle.
    VerticalBand,   // Transparent at the top and bottom, solid in the middle.
    FadeDown,       // Solid at the top, transparent at the bottom.
    FadeUp,         // Transparent at the top, solid at the bottom.
    FadeRight,      // Solid at the left, transparent at the right (left-anchored HUD).
    FadeLeft        // Transparent at the left, solid at the right (right-anchored HUD).
};

class MARBURGUMIICLIENT_API SMarburgumScrim : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SMarburgumScrim) : _Shape(EMarburgumScrimShape::HorizontalBand), _Color(FLinearColor(0.f, 0.f, 0.f, 0.6f)) {}
        SLATE_ARGUMENT(EMarburgumScrimShape, Shape)
        SLATE_ARGUMENT(FLinearColor, Color)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void SetColor(const FLinearColor& InColor) { Color = InColor; Invalidate(EInvalidateWidgetReason::Paint); }

    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
        FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1.f, 1.f); }

private:
    EMarburgumScrimShape Shape = EMarburgumScrimShape::HorizontalBand;
    FLinearColor Color;
};

DECLARE_DELEGATE(FMarburgumSelectableEvent);
DECLARE_DELEGATE_OneParam(FMarburgumSelectableStep, int32 /*Direction: -1 left, +1 right*/);

/**
 * A focusable row, tile or tab: the one interactive primitive of the management screens and menus. Mouse click,
 * Enter, Space or the gamepad's accept button activate it; it draws its own hover, selected, pressed and focus states
 * (a copper ring for keyboard and gamepad focus, never the engine's dotted rectangle). With OnStep bound, Left/Right
 * change its value instead of moving focus (settings rows).
 */
class MARBURGUMIICLIENT_API SMarburgumSelectable : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SMarburgumSelectable) : _Radius(4.f), _bQuiet(false) {}
        SLATE_DEFAULT_SLOT(FArguments, Content)
        SLATE_ARGUMENT(float, Radius)
        SLATE_ARGUMENT(bool, bQuiet)
        SLATE_EVENT(FMarburgumSelectableEvent, OnActivated)
        SLATE_EVENT(FMarburgumSelectableEvent, OnFocused)
        SLATE_EVENT(FMarburgumSelectableStep, OnStep)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void SetContent(const TSharedRef<SWidget>& InContent);
    void SetSelected(bool bInSelected);
    bool IsSelected() const { return bSelected; }
    void SetQuiet(bool bInQuiet) { bQuiet = bInQuiet; Invalidate(EInvalidateWidgetReason::Paint); }
    void SetOnActivated(FMarburgumSelectableEvent In) { OnActivated = In; }
    void SetOnFocused(FMarburgumSelectableEvent In) { OnFocused = In; }
    void SetOnStep(FMarburgumSelectableStep In) { OnStep = In; }
    void Activate();

    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual const FSlateBrush* GetFocusBrush() const override { return nullptr; }
    virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;
    virtual void OnFocusLost(const FFocusEvent& InFocusEvent) override;
    virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
    virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
    virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
    virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
    virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
    virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
        FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
    FMarburgumSelectableEvent OnActivated;
    FMarburgumSelectableEvent OnFocused;
    FMarburgumSelectableStep OnStep;
    float Radius = 4.f;
    bool bQuiet = false;
    bool bSelected = false;
    bool bPressed = false;
    bool bHasFocus = false;
};

/** UMG face of SMarburgumGlyph. */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumGlyph : public UWidget
{
    GENERATED_BODY()

public:
    void SetGlyph(EMarburgumGlyph InGlyph);
    void SetColor(const FLinearColor& InColor);
    void SetSize(float InSize);
    EMarburgumGlyph GetGlyph() const { return Glyph; }

    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;

private:
    TSharedPtr<SMarburgumGlyph> Slate;
    EMarburgumGlyph Glyph = EMarburgumGlyph::None;
    FLinearColor Color = FLinearColor::White;
    float Size = 20.f;
};

/** UMG face of SMarburgumMeter. */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumMeter : public UWidget
{
    GENERATED_BODY()

public:
    void SetPercent(float InPercent);
    void SetGhost(float InGhost);
    void SetFillColor(const FLinearColor& InColor);
    void SetSegments(int32 InSegments);
    void SetHeight(float InHeight);
    float GetPercent() const { return Percent; }

    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;

private:
    TSharedPtr<SMarburgumMeter> Slate;
    float Percent = 1.f;
    float Ghost = -1.f;
    FLinearColor FillColor = FLinearColor::White;
    int32 Segments = 0;
    float Height = 6.f;
};

/** UMG face of SMarburgumScrim. */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumScrim : public UWidget
{
    GENERATED_BODY()

public:
    void Setup(EMarburgumScrimShape InShape, const FLinearColor& InColor);

    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;

private:
    TSharedPtr<SMarburgumScrim> Slate;
    EMarburgumScrimShape Shape = EMarburgumScrimShape::HorizontalBand;
    FLinearColor Color = FLinearColor(0.f, 0.f, 0.f, 0.6f);
};

/** UMG face of SMarburgumSelectable; its one child is the row's content. */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumSelectable : public UContentWidget
{
    GENERATED_BODY()

public:
    FMarburgumSelectableEvent OnActivated;
    FMarburgumSelectableEvent OnFocused;
    FMarburgumSelectableStep OnStep;

    void SetSelected(bool bInSelected);
    bool IsSelected() const { return bSelected; }
    /** Quiet: no resting fill, only hover/selected/focus (tabs, menu entries over a panel). */
    void SetQuiet(bool bInQuiet);
    void SetRadius(float InRadius) { Radius = InRadius; }
    /** Gives keyboard focus to this row for the owning player. */
    void Focus();
    /** Activates as if clicked. */
    void Activate();

    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void OnSlotAdded(UPanelSlot* InSlot) override;
    virtual void OnSlotRemoved(UPanelSlot* InSlot) override;

private:
    TSharedPtr<SMarburgumSelectable> Slate;
    bool bSelected = false;
    bool bQuiet = false;
    float Radius = 4.f;
};
