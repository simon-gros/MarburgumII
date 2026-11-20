#include "UI/MarburgumUIWidgets.h"

#include "Components/PanelSlot.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "UI/MarburgumUITheme.h"
#include "Widgets/SNullWidget.h"

namespace
{
    using FStroke = TArray<FVector2f>;
    using FStrokes = TArray<FStroke>;

    FStroke Line(float X0, float Y0, float X1, float Y1)
    {
        return { FVector2f(X0, Y0), FVector2f(X1, Y1) };
    }

    FStroke Poly(std::initializer_list<FVector2f> Points)
    {
        return FStroke(Points);
    }

    /** Arc in degrees, 0 = +X, 90 = +Y (down), from A0 to A1 in that direction. */
    FStroke Arc(float Cx, float Cy, float R, float A0, float A1, int32 Segments = 0)
    {
        if (Segments <= 0)
        {
            Segments = FMath::Max(6, FMath::CeilToInt(FMath::Abs(A1 - A0) / 12.f));
        }
        FStroke Out;
        for (int32 I = 0; I <= Segments; ++I)
        {
            const float A = FMath::DegreesToRadians(FMath::Lerp(A0, A1, static_cast<float>(I) / Segments));
            Out.Add(FVector2f(Cx + R * FMath::Cos(A), Cy + R * FMath::Sin(A)));
        }
        return Out;
    }

    FStroke Circle(float Cx, float Cy, float R)
    {
        return Arc(Cx, Cy, R, 0.f, 360.f, R < 2.f ? 8 : 32);
    }

    FStroke Quad(FVector2f P0, FVector2f P1, FVector2f P2, int32 Segments = 12)
    {
        FStroke Out;
        for (int32 I = 0; I <= Segments; ++I)
        {
            const float T = static_cast<float>(I) / Segments;
            Out.Add((1 - T) * (1 - T) * P0 + 2 * (1 - T) * T * P1 + T * T * P2);
        }
        return Out;
    }

    FStroke RoundRect(float X, float Y, float W, float H, float R)
    {
        FStroke Out;
        auto Corner = [&](float Cx, float Cy, float A0)
        {
            const FStroke Part = Arc(Cx, Cy, R, A0, A0 + 90.f, 4);
            Out.Append(Part);
        };
        Corner(X + W - R, Y + R, -90.f);
        Corner(X + W - R, Y + H - R, 0.f);
        Corner(X + R, Y + H - R, 90.f);
        Corner(X + R, Y + R, 180.f);
        const FVector2f First = Out[0]; // Not Out.Add(Out[0]): the argument would alias the growing array.
        Out.Add(First);
        return Out;
    }

    FStroke Dot(float X, float Y)
    {
        return Circle(X, Y, 0.7f);
    }

    FStrokes Build(EMarburgumGlyph Glyph)
    {
        switch (Glyph)
        {
        case EMarburgumGlyph::Talk:
            return { RoundRect(3, 4, 18, 12, 3), Poly({ { 8, 16 }, { 7, 20.5f }, { 12.5f, 16 } }) };
        case EMarburgumGlyph::Chat:
            return { RoundRect(3, 4, 18, 12, 3), Poly({ { 8, 16 }, { 7, 20.5f }, { 12.5f, 16 } }), Dot(8, 10), Dot(12, 10), Dot(16, 10) };
        case EMarburgumGlyph::Quote:
            return { Poly({ { 5, 15 }, { 5, 11 }, { 8, 8 } }), Poly({ { 13, 15 }, { 13, 11 }, { 16, 8 } }), Circle(6.5f, 15, 1.5f), Circle(14.5f, 15, 1.5f) };
        case EMarburgumGlyph::Inspect:
            return { Circle(10, 10, 6), Line(14.5f, 14.5f, 20.5f, 20.5f) };
        case EMarburgumGlyph::Use:
            return { Circle(12, 12, 8), Circle(12, 12, 2.5f) };
        case EMarburgumGlyph::Fish:
            return { Quad({ 3, 12 }, { 10, 4.5f }, { 17, 12 }), Quad({ 3, 12 }, { 10, 19.5f }, { 17, 12 }),
                Poly({ { 17, 12 }, { 21, 8 }, { 21, 16 }, { 17, 12 } }), Dot(7.5f, 11) };
        case EMarburgumGlyph::Car:
            return { Poly({ { 3, 16 }, { 3, 12.5f }, { 6, 11.5f }, { 8.5f, 7 }, { 15.5f, 7 }, { 18, 11.5f }, { 21, 12.5f }, { 21, 16 }, { 3, 16 } }),
                Circle(7.5f, 16.5f, 2), Circle(16.5f, 16.5f, 2), Line(6, 11.5f, 18, 11.5f) };
        case EMarburgumGlyph::Door:
            return { Poly({ { 6, 21 }, { 6, 3 }, { 18, 3 }, { 18, 21 } }), Line(3, 21, 21, 21), Dot(15, 12.5f) };
        case EMarburgumGlyph::Item:
            return { Poly({ { 4, 8 }, { 12, 4 }, { 20, 8 }, { 20, 16 }, { 12, 20 }, { 4, 16 }, { 4, 8 } }), Poly({ { 4, 8 }, { 12, 12 }, { 20, 8 } }),
                Line(12, 12, 12, 20) };
        case EMarburgumGlyph::Coin:
            return { Circle(12, 12, 8.5f), Circle(12, 12, 5.5f), Line(12, 9.5f, 12, 14.5f) };
        case EMarburgumGlyph::Skill:
            return { Poly({ { 5, 13 }, { 12, 6.5f }, { 19, 13 } }), Poly({ { 5, 19 }, { 12, 12.5f }, { 19, 19 } }) };
        case EMarburgumGlyph::Mission:
            return { Line(6, 21, 6, 3), Poly({ { 6, 4 }, { 18.5f, 4 }, { 15.5f, 8 }, { 18.5f, 12 }, { 6, 12 } }) };
        case EMarburgumGlyph::Journal:
            return { RoundRect(5, 3, 14, 18, 1.5f), Line(8.5f, 3, 8.5f, 21), Line(11.5f, 8, 16, 8), Line(11.5f, 11.5f, 16, 11.5f) };
        case EMarburgumGlyph::Character:
        case EMarburgumGlyph::Person:
            return { Circle(12, 8, 4), Quad({ 4.5f, 21 }, { 12, 9.5f }, { 19.5f, 21 }) };
        case EMarburgumGlyph::Inventory:
            return { RoundRect(4, 8, 16, 13, 2), Arc(12, 8, 4, 180, 360), Line(4, 13, 20, 13) };
        case EMarburgumGlyph::Settings:
        {
            FStrokes Out = { Circle(12, 12, 5.5f), Circle(12, 12, 2.2f) };
            for (int32 I = 0; I < 8; ++I)
            {
                const float A = FMath::DegreesToRadians(I * 45.f + 22.5f);
                Out.Add(Line(12 + 5.5f * FMath::Cos(A), 12 + 5.5f * FMath::Sin(A), 12 + 8.5f * FMath::Cos(A), 12 + 8.5f * FMath::Sin(A)));
            }
            return Out;
        }
        case EMarburgumGlyph::Warning:
            return { Poly({ { 12, 3.5f }, { 21.5f, 20 }, { 2.5f, 20 }, { 12, 3.5f } }), Line(12, 9.5f, 12, 14), Dot(12, 17) };
        case EMarburgumGlyph::Check:
            return { Poly({ { 5, 12.5f }, { 10, 17.5f }, { 19, 7 } }) };
        case EMarburgumGlyph::Close:
            return { Line(6, 6, 18, 18), Line(18, 6, 6, 18) };
        case EMarburgumGlyph::Heart:
        {
            FStroke Heart;
            for (int32 I = 0; I <= 40; ++I)
            {
                const float T = 2.f * PI * I / 40.f;
                const float X = 16.f * FMath::Pow(FMath::Sin(T), 3.f);
                const float Y = 13.f * FMath::Cos(T) - 5.f * FMath::Cos(2.f * T) - 2.f * FMath::Cos(3.f * T) - FMath::Cos(4.f * T);
                Heart.Add(FVector2f(12.f + X * 0.52f, 11.5f - Y * 0.52f));
            }
            return { Heart };
        }
        case EMarburgumGlyph::Breath:
            return { Line(3, 8, 15, 8), Arc(15, 5.5f, 2.5f, 90, -180), Line(3, 12, 19.5f, 12), Line(3, 16, 13, 16), Arc(13, 18.5f, 2.5f, -90, 180) };
        case EMarburgumGlyph::Swim:
        {
            FStrokes Out;
            for (float Base : { 9.f, 14.f, 19.f })
            {
                FStroke Wave;
                for (int32 I = 0; I <= 24; ++I)
                {
                    const float X = 3.f + 18.f * I / 24.f;
                    Wave.Add(FVector2f(X, Base + 1.6f * FMath::Sin((X - 3.f) / 18.f * 4.f * PI)));
                }
                Out.Add(Wave);
            }
            return Out;
        }
        case EMarburgumGlyph::Clock:
            return { Circle(12, 12, 8.5f), Poly({ { 12, 7 }, { 12, 12 }, { 15.5f, 14 } }) };
        case EMarburgumGlyph::Pin:
            return { [] { FStroke S = Arc(12, 10, 6, 150, 390); S.Add(FVector2f(12, 21)); const FVector2f First = S[0]; S.Add(First); return S; }(), Circle(12, 10, 2.2f) };
        case EMarburgumGlyph::Signal:
            return { Arc(12, 19, 4, 225, 315), Arc(12, 19, 8.5f, 225, 315), Arc(12, 19, 13, 230, 310), Dot(12, 19) };
        case EMarburgumGlyph::Impact:
        {
            FStrokes Out = { Circle(12, 12, 2.5f) };
            for (int32 I = 0; I < 8; ++I)
            {
                const float A = FMath::DegreesToRadians(I * 45.f);
                const float Outer = I % 2 == 0 ? 9.5f : 7.f;
                Out.Add(Line(12 + 4.5f * FMath::Cos(A), 12 + 4.5f * FMath::Sin(A), 12 + Outer * FMath::Cos(A), 12 + Outer * FMath::Sin(A)));
            }
            return Out;
        }
        case EMarburgumGlyph::Lock:
            return { RoundRect(5, 11, 14, 10, 2), Arc(12, 11, 4.5f, 180, 360), Dot(12, 16) };
        case EMarburgumGlyph::ChevronRight:
            return { Poly({ { 9, 5 }, { 16, 12 }, { 9, 19 } }) };
        case EMarburgumGlyph::ChevronLeft:
            return { Poly({ { 15, 5 }, { 8, 12 }, { 15, 19 } }) };
        case EMarburgumGlyph::ChevronUp:
            return { Poly({ { 5, 15 }, { 12, 8 }, { 19, 15 } }) };
        case EMarburgumGlyph::ChevronDown:
            return { Poly({ { 5, 9 }, { 12, 16 }, { 19, 9 } }) };
        case EMarburgumGlyph::Info:
            return { Circle(12, 12, 8.5f), Line(12, 11, 12, 16.5f), Dot(12, 8) };
        case EMarburgumGlyph::Menu:
            return { Line(4, 7, 20, 7), Line(4, 12, 20, 12), Line(4, 17, 20, 17) };
        case EMarburgumGlyph::Exit:
            return { Poly({ { 11, 4 }, { 4, 4 }, { 4, 20 }, { 11, 20 } }), Line(9, 12, 20, 12), Poly({ { 16, 8 }, { 20, 12 }, { 16, 16 } }) };
        case EMarburgumGlyph::Sound:
            return { Poly({ { 4, 9 }, { 8, 9 }, { 13, 5 }, { 13, 19 }, { 8, 15 }, { 4, 15 }, { 4, 9 } }), Arc(13, 12, 3.5f, -45, 45),
                Arc(13, 12, 7, -45, 45) };
        case EMarburgumGlyph::Display:
            return { RoundRect(3, 4, 18, 12, 1.5f), Line(8.5f, 20, 15.5f, 20), Line(12, 16, 12, 20) };
        case EMarburgumGlyph::Keyboard:
            return { RoundRect(2.5f, 6, 19, 12, 2), Dot(6, 10), Dot(9.3f, 10), Dot(12.6f, 10), Dot(15.9f, 10), Dot(18, 10), Line(8, 14.5f, 16, 14.5f) };
        case EMarburgumGlyph::Gamepad:
            return { RoundRect(2.5f, 7, 19, 10.5f, 5), Line(7, 10.5f, 7, 14), Line(5.25f, 12.25f, 8.75f, 12.25f), Dot(15.5f, 11), Dot(17.8f, 13.4f) };
        case EMarburgumGlyph::Eye:
            return { Quad({ 2, 12 }, { 12, 3.5f }, { 22, 12 }), Quad({ 2, 12 }, { 12, 20.5f }, { 22, 12 }), Circle(12, 12, 3) };
        case EMarburgumGlyph::Star:
        {
            FStroke Star;
            for (int32 I = 0; I <= 10; ++I)
            {
                const float A = FMath::DegreesToRadians(-90.f + I * 36.f);
                const float R = I % 2 == 0 ? 9.f : 3.8f;
                Star.Add(FVector2f(12 + R * FMath::Cos(A), 12.5f + R * FMath::Sin(A)));
            }
            return { Star };
        }
        case EMarburgumGlyph::Clue:
            return { Poly({ { 6, 3 }, { 14, 3 }, { 19, 8 }, { 19, 21 }, { 6, 21 }, { 6, 3 } }), Poly({ { 14, 3 }, { 14, 8 }, { 19, 8 } }),
                Line(9, 12, 16, 12), Line(9, 15.5f, 16, 15.5f) };
        case EMarburgumGlyph::Shop:
            return { Poly({ { 3, 9 }, { 5, 4 }, { 19, 4 }, { 21, 9 }, { 3, 9 } }), Poly({ { 5, 9 }, { 5, 20 }, { 19, 20 }, { 19, 9 } }),
                Poly({ { 10, 20 }, { 10, 14 }, { 14, 14 }, { 14, 20 } }) };
        case EMarburgumGlyph::Fast:
            return { Poly({ { 5, 6 }, { 11, 12 }, { 5, 18 } }), Poly({ { 12, 6 }, { 18, 12 }, { 12, 18 } }) };
        case EMarburgumGlyph::Trade:
            return { Line(4, 8, 18.5f, 8), Poly({ { 15, 4.5f }, { 18.5f, 8 }, { 15, 11.5f } }), Line(20, 16, 5.5f, 16),
                Poly({ { 9, 12.5f }, { 5.5f, 16 }, { 9, 19.5f } }) };
        case EMarburgumGlyph::None:
            break;
        }
        return {};
    }
}

// ------------------------------------------------------------------------------------------------ SMarburgumGlyph

const TArray<TArray<FVector2f>>& SMarburgumGlyph::Strokes(EMarburgumGlyph Glyph)
{
    // Built once per glyph on first use; a fixed table, so references handed out stay valid.
    static TArray<TOptional<FStrokes>> Cache = [] { TArray<TOptional<FStrokes>> Table; Table.SetNum(256); return Table; }();
    TOptional<FStrokes>& Entry = Cache[static_cast<uint8>(Glyph)];
    if (!Entry.IsSet())
    {
        Entry = Build(Glyph);
    }
    return Entry.GetValue();
}

void SMarburgumGlyph::Construct(const FArguments& InArgs)
{
    Glyph = InArgs._Glyph;
    Color = InArgs._Color;
    Size = InArgs._Size;
}

void SMarburgumGlyph::SetGlyph(EMarburgumGlyph InGlyph)
{
    if (Glyph != InGlyph)
    {
        Glyph = InGlyph;
        Invalidate(EInvalidateWidgetReason::Paint);
    }
}

void SMarburgumGlyph::SetColor(const TAttribute<FSlateColor>& InColor)
{
    Color = InColor;
    Invalidate(EInvalidateWidgetReason::Paint);
}

void SMarburgumGlyph::SetSize(float InSize)
{
    if (!FMath::IsNearlyEqual(Size, InSize))
    {
        Size = InSize;
        Invalidate(EInvalidateWidgetReason::Layout);
    }
}

int32 SMarburgumGlyph::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
    FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
    const FVector2f Local = AllottedGeometry.GetLocalSize();
    const float Box = FMath::Min(Local.X, Local.Y);
    if (Box <= 0.f || Glyph == EMarburgumGlyph::None)
    {
        return LayerId;
    }
    const float Scale = Box / 24.f;
    const FVector2f Offset((Local.X - Box) * 0.5f, (Local.Y - Box) * 0.5f);
    // Strokes thicken with the glyph but stay at least a device pixel wide.
    const float Pixels = AllottedGeometry.Scale;
    const float Thickness = FMath::Max(1.6f * Scale, 1.1f / FMath::Max(Pixels, 0.01f));
    const FLinearColor Tint = Color.Get().GetColor(InWidgetStyle) * InWidgetStyle.GetColorAndOpacityTint();
    for (const FStroke& Stroke : Strokes(Glyph))
    {
        TArray<FVector2f> Points;
        Points.Reserve(Stroke.Num());
        for (const FVector2f& Point : Stroke)
        {
            Points.Add(Offset + Point * Scale);
        }
        FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), MoveTemp(Points), ESlateDrawEffect::None,
            Tint, true, Thickness * Pixels);
    }
    return LayerId;
}

// ------------------------------------------------------------------------------------------------ SMarburgumMeter

void SMarburgumMeter::Construct(const FArguments& InArgs)
{
    Percent = FMath::Clamp(InArgs._Percent, 0.f, 1.f);
    FillColor = InArgs._FillColor;
    Height = InArgs._Height;
    Segments = InArgs._Segments;
    const MarburgumUI::FTokens& T = MarburgumUI::Tokens();
    Track = MarburgumUI::RoundedBrush(T.Color.Sunken, Height * 0.5f, T.Color.Border, T.BorderWidth);
    Fill = MarburgumUI::RoundedBrush(FLinearColor::White, Height * 0.5f);
}

void SMarburgumMeter::SetPercent(float InPercent)
{
    InPercent = FMath::Clamp(InPercent, 0.f, 1.f);
    if (!FMath::IsNearlyEqual(Percent, InPercent, 0.0005f))
    {
        Percent = InPercent;
        Invalidate(EInvalidateWidgetReason::Paint);
    }
}

void SMarburgumMeter::SetGhost(float InGhost)
{
    if (!FMath::IsNearlyEqual(Ghost, InGhost, 0.0005f))
    {
        Ghost = InGhost;
        Invalidate(EInvalidateWidgetReason::Paint);
    }
}

void SMarburgumMeter::SetFillColor(const FLinearColor& InColor)
{
    if (!FillColor.Equals(InColor))
    {
        FillColor = InColor;
        Invalidate(EInvalidateWidgetReason::Paint);
    }
}

void SMarburgumMeter::SetSegments(int32 InSegments)
{
    Segments = InSegments;
    Invalidate(EInvalidateWidgetReason::Paint);
}

void SMarburgumMeter::SetHeight(float InHeight)
{
    Height = InHeight;
    const MarburgumUI::FTokens& T = MarburgumUI::Tokens();
    Track = MarburgumUI::RoundedBrush(T.Color.Sunken, Height * 0.5f, T.Color.Border, T.BorderWidth);
    Fill = MarburgumUI::RoundedBrush(FLinearColor::White, Height * 0.5f);
    Invalidate(EInvalidateWidgetReason::Layout);
}

int32 SMarburgumMeter::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
    FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
    const FVector2f Local = AllottedGeometry.GetLocalSize();
    const float H = FMath::Min(Height, Local.Y);
    const float Y = (Local.Y - H) * 0.5f;
    const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();
    if (Tint.A <= 0.004f)
    {
        return LayerId; // Faded out: a rounded box's outline ignores the draw tint and would stay behind.
    }
    // The outline colour is not multiplied by the draw tint either, so it fades with a tinted copy of the brush.
    FSlateBrush TintedTrack = Track;
    TintedTrack.OutlineSettings.Color = FSlateColor(Track.OutlineSettings.Color.GetSpecifiedColor() * Tint);
    FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(FVector2f(Local.X, H), FSlateLayoutTransform(FVector2f(0.f, Y))),
        &TintedTrack, ESlateDrawEffect::None, Track.GetTint(InWidgetStyle) * Tint);
    const float Inset = 1.f;
    const float Usable = FMath::Max(0.f, Local.X - 2.f * Inset);
    if (Ghost > Percent)
    {
        const float GhostWidth = FMath::Max(H, Usable * FMath::Clamp(Ghost, 0.f, 1.f));
        FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
            AllottedGeometry.ToPaintGeometry(FVector2f(GhostWidth, H - 2.f * Inset), FSlateLayoutTransform(FVector2f(Inset, Y + Inset))), &Fill,
            ESlateDrawEffect::None, FillColor.CopyWithNewOpacity(0.35f) * Tint);
    }
    if (Percent > 0.f)
    {
        // A sliver never drops below a round dot, so 1 % still reads as "some".
        const float Width = FMath::Max(FMath::Min(H, Usable), Usable * Percent);
        FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2,
            AllottedGeometry.ToPaintGeometry(FVector2f(Width, H - 2.f * Inset), FSlateLayoutTransform(FVector2f(Inset, Y + Inset))), &Fill,
            ESlateDrawEffect::None, FillColor * Tint);
    }
    if (Segments > 1)
    {
        const FLinearColor Notch = MarburgumUI::Tokens().Color.Sunken * Tint;
        for (int32 I = 1; I < Segments; ++I)
        {
            const float X = Inset + Usable * I / Segments;
            TArray<FVector2f> Points = { FVector2f(X, Y), FVector2f(X, Y + H) };
            FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 3, AllottedGeometry.ToPaintGeometry(), MoveTemp(Points), ESlateDrawEffect::None,
                Notch, false, 2.f * AllottedGeometry.Scale);
        }
    }
    return LayerId + 3;
}

// ------------------------------------------------------------------------------------------------ SMarburgumScrim

void SMarburgumScrim::Construct(const FArguments& InArgs)
{
    Shape = InArgs._Shape;
    Color = InArgs._Color;
}

int32 SMarburgumScrim::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
    FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
    const FVector2f Local = AllottedGeometry.GetLocalSize();
    const FLinearColor Solid = Color * InWidgetStyle.GetColorAndOpacityTint();
    const FLinearColor Clear = Solid.CopyWithNewOpacity(0.f);
    TArray<FSlateGradientStop> Stops;
    // Orient_Vertical lays the stops out along X (vertical bands of colour); Orient_Horizontal along Y.
    EOrientation Orientation = Orient_Vertical;
    switch (Shape)
    {
    case EMarburgumScrimShape::HorizontalBand:
        Stops = { FSlateGradientStop(FVector2f(0.f, 0.f), Clear), FSlateGradientStop(FVector2f(Local.X * 0.22f, 0.f), Solid),
            FSlateGradientStop(FVector2f(Local.X * 0.78f, 0.f), Solid), FSlateGradientStop(FVector2f(Local.X, 0.f), Clear) };
        Orientation = Orient_Vertical;
        break;
    case EMarburgumScrimShape::VerticalBand:
        Stops = { FSlateGradientStop(FVector2f(0.f, 0.f), Clear), FSlateGradientStop(FVector2f(0.f, Local.Y * 0.25f), Solid),
            FSlateGradientStop(FVector2f(0.f, Local.Y * 0.75f), Solid), FSlateGradientStop(FVector2f(0.f, Local.Y), Clear) };
        Orientation = Orient_Horizontal;
        break;
    case EMarburgumScrimShape::FadeDown:
        Stops = { FSlateGradientStop(FVector2f(0.f, 0.f), Solid), FSlateGradientStop(FVector2f(0.f, Local.Y), Clear) };
        Orientation = Orient_Horizontal;
        break;
    case EMarburgumScrimShape::FadeUp:
        Stops = { FSlateGradientStop(FVector2f(0.f, 0.f), Clear), FSlateGradientStop(FVector2f(0.f, Local.Y), Solid) };
        Orientation = Orient_Horizontal;
        break;
    case EMarburgumScrimShape::FadeRight:
        Stops = { FSlateGradientStop(FVector2f(0.f, 0.f), Solid), FSlateGradientStop(FVector2f(Local.X * 0.55f, 0.f), Solid.CopyWithNewOpacity(Solid.A * 0.7f)),
            FSlateGradientStop(FVector2f(Local.X, 0.f), Clear) };
        Orientation = Orient_Vertical;
        break;
    case EMarburgumScrimShape::FadeLeft:
        Stops = { FSlateGradientStop(FVector2f(0.f, 0.f), Clear), FSlateGradientStop(FVector2f(Local.X * 0.45f, 0.f), Solid.CopyWithNewOpacity(Solid.A * 0.7f)),
            FSlateGradientStop(FVector2f(Local.X, 0.f), Solid) };
        Orientation = Orient_Vertical;
        break;
    case EMarburgumScrimShape::SoftBand:
    case EMarburgumScrimShape::SoftFadeRight:
    {
        // A gradient runs along one axis only, so the soft top and bottom come from horizontal slices, each the
        // horizontal gradient scaled by a smooth vertical falloff (no visible steps at 16 slices).
        constexpr int32 Slices = 16;
        const float SliceHeight = Local.Y / Slices;
        for (int32 Index = 0; Index < Slices; ++Index)
        {
            const float Centre = (Index + 0.5f) / Slices;
            const float Falloff = FMath::SmoothStep(0.f, 0.32f, Centre) * FMath::SmoothStep(0.f, 0.32f, 1.f - Centre);
            const FLinearColor Row = Solid.CopyWithNewOpacity(Solid.A * Falloff);
            const FLinearColor RowClear = Row.CopyWithNewOpacity(0.f);
            TArray<FSlateGradientStop> RowStops = Shape == EMarburgumScrimShape::SoftBand
                ? TArray<FSlateGradientStop>{ FSlateGradientStop(FVector2f(0.f, 0.f), RowClear), FSlateGradientStop(FVector2f(Local.X * 0.22f, 0.f), Row),
                    FSlateGradientStop(FVector2f(Local.X * 0.78f, 0.f), Row), FSlateGradientStop(FVector2f(Local.X, 0.f), RowClear) }
                : TArray<FSlateGradientStop>{ FSlateGradientStop(FVector2f(0.f, 0.f), Row),
                    FSlateGradientStop(FVector2f(Local.X * 0.55f, 0.f), Row.CopyWithNewOpacity(Row.A * 0.7f)), FSlateGradientStop(FVector2f(Local.X, 0.f), RowClear) };
            FSlateDrawElement::MakeGradient(OutDrawElements, LayerId,
                AllottedGeometry.ToPaintGeometry(FVector2f(Local.X, SliceHeight + 0.5f), FSlateLayoutTransform(FVector2f(0.f, SliceHeight * Index))),
                MoveTemp(RowStops), Orient_Vertical);
        }
        return LayerId;
    }
    }
    FSlateDrawElement::MakeGradient(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), MoveTemp(Stops), Orientation);
    return LayerId;
}

// ------------------------------------------------------------------------------------------------ SMarburgumSelectable

void SMarburgumSelectable::Construct(const FArguments& InArgs)
{
    Radius = InArgs._Radius;
    bQuiet = InArgs._bQuiet;
    OnActivated = InArgs._OnActivated;
    OnFocused = InArgs._OnFocused;
    OnStep = InArgs._OnStep;
    ChildSlot[InArgs._Content.Widget];
}

void SMarburgumSelectable::SetContent(const TSharedRef<SWidget>& InContent)
{
    ChildSlot[InContent];
}

void SMarburgumSelectable::SetSelected(bool bInSelected)
{
    if (bSelected != bInSelected)
    {
        bSelected = bInSelected;
        Invalidate(EInvalidateWidgetReason::Paint);
    }
}

void SMarburgumSelectable::Activate()
{
    OnActivated.ExecuteIfBound();
}

FReply SMarburgumSelectable::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
    // A ring for keyboard and gamepad focus; a mouse click already shows where it went.
    bHasFocus = InFocusEvent.GetCause() != EFocusCause::Mouse;
    Invalidate(EInvalidateWidgetReason::Paint);
    OnFocused.ExecuteIfBound();
    return FReply::Handled();
}

void SMarburgumSelectable::OnFocusLost(const FFocusEvent& InFocusEvent)
{
    bHasFocus = false;
    bPressed = false;
    Invalidate(EInvalidateWidgetReason::Paint);
}

FReply SMarburgumSelectable::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
    if (!FSlateApplication::IsInitialized())
    {
        return FReply::Unhandled();
    }
    FSlateApplication& App = FSlateApplication::Get();
    if (OnStep.IsBound())
    {
        const EUINavigation Direction = App.GetNavigationDirectionFromKey(InKeyEvent);
        if (Direction == EUINavigation::Left || Direction == EUINavigation::Right)
        {
            OnStep.Execute(Direction == EUINavigation::Left ? -1 : 1);
            return FReply::Handled();
        }
    }
    if (App.GetNavigationActionFromKey(InKeyEvent) == EUINavigationAction::Accept)
    {
        bPressed = true;
        bHasFocus = true;
        Invalidate(EInvalidateWidgetReason::Paint);
        return FReply::Handled();
    }
    return FReply::Unhandled();
}

FReply SMarburgumSelectable::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
    if (bPressed && FSlateApplication::IsInitialized()
        && FSlateApplication::Get().GetNavigationActionFromKey(InKeyEvent) == EUINavigationAction::Accept)
    {
        bPressed = false;
        Invalidate(EInvalidateWidgetReason::Paint);
        Activate();
        return FReply::Handled();
    }
    return FReply::Unhandled();
}

FReply SMarburgumSelectable::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
    {
        return FReply::Unhandled();
    }
    bPressed = true;
    Invalidate(EInvalidateWidgetReason::Paint);
    return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this), EFocusCause::Mouse);
}

FReply SMarburgumSelectable::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !bPressed)
    {
        return FReply::Unhandled();
    }
    bPressed = false;
    Invalidate(EInvalidateWidgetReason::Paint);
    if (MyGeometry.IsUnderLocation(MouseEvent.GetScreenSpacePosition()))
    {
        Activate();
    }
    return FReply::Handled().ReleaseMouseCapture();
}

void SMarburgumSelectable::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);
    Invalidate(EInvalidateWidgetReason::Paint);
}

void SMarburgumSelectable::OnMouseLeave(const FPointerEvent& MouseEvent)
{
    SCompoundWidget::OnMouseLeave(MouseEvent);
    bPressed = bPressed && HasMouseCapture();
    Invalidate(EInvalidateWidgetReason::Paint);
}

int32 SMarburgumSelectable::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
    FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
    const MarburgumUI::FTokens& T = MarburgumUI::Tokens();
    const MarburgumUI::FColors& C = T.Color;
    const bool bEnabled = bParentEnabled && IsEnabled();
    FLinearColor Fill = bQuiet ? FLinearColor::Transparent : C.Raised;
    FLinearColor Outline = bQuiet ? FLinearColor::Transparent : C.Border;
    if (bSelected)
    {
        Fill = C.AccentSoft;
        Outline = C.Hairline;
    }
    if (bEnabled && IsHovered())
    {
        Fill = bSelected ? C.AccentSoft : MarburgumUI::Composite(C.TextPrimary.CopyWithNewOpacity(0.06f), C.Raised);
        Outline = bSelected ? C.Accent : C.BorderStrong;
    }
    if (bEnabled && bPressed)
    {
        Fill = C.AccentSoft;
        Outline = C.AccentPressed;
    }
    const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();
    const FSlateBrush Background = MarburgumUI::RoundedBrush(Fill, Radius, Outline, T.BorderWidth);
    FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), &Background, ESlateDrawEffect::None,
        Background.GetTint(InWidgetStyle) * Tint);
    if (bSelected)
    {
        // A copper bar on the leading edge marks the selection without colour alone (it is a shape).
        const FSlateBrush Bar = MarburgumUI::RoundedBrush(C.Accent, 1.5f);
        const FVector2f Local = AllottedGeometry.GetLocalSize();
        FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
            AllottedGeometry.ToPaintGeometry(FVector2f(3.f, FMath::Max(0.f, Local.Y - 12.f)), FSlateLayoutTransform(FVector2f(2.f, 6.f))), &Bar,
            ESlateDrawEffect::None, Bar.GetTint(InWidgetStyle) * Tint);
    }
    int32 Layer = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 1, InWidgetStyle,
        bEnabled);
    if (bHasFocus && HasAnyUserFocus().IsSet())
    {
        const FSlateBrush Ring = MarburgumUI::RoundedBrush(FLinearColor::Transparent, Radius + 2.f, C.Focus, T.FocusWidth);
        const FVector2f Local = AllottedGeometry.GetLocalSize();
        FSlateDrawElement::MakeBox(OutDrawElements, Layer + 1,
            AllottedGeometry.ToPaintGeometry(Local + FVector2f(4.f, 4.f), FSlateLayoutTransform(FVector2f(-2.f, -2.f))), &Ring,
            ESlateDrawEffect::None, Ring.GetTint(InWidgetStyle) * Tint);
        ++Layer;
    }
    return Layer;
}

// ------------------------------------------------------------------------------------------------ UMG faces

void UMarburgumGlyph::SetGlyph(EMarburgumGlyph InGlyph)
{
    Glyph = InGlyph;
    if (Slate)
    {
        Slate->SetGlyph(Glyph);
    }
}

void UMarburgumGlyph::SetColor(const FLinearColor& InColor)
{
    Color = InColor;
    if (Slate)
    {
        Slate->SetColor(FSlateColor(Color));
    }
}

void UMarburgumGlyph::SetSize(float InSize)
{
    Size = InSize;
    if (Slate)
    {
        Slate->SetSize(Size);
    }
}

TSharedRef<SWidget> UMarburgumGlyph::RebuildWidget()
{
    Slate = SNew(SMarburgumGlyph).Glyph(Glyph).Color(FSlateColor(Color)).Size(Size);
    return Slate.ToSharedRef();
}

void UMarburgumGlyph::ReleaseSlateResources(bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    Slate.Reset();
}

void UMarburgumMeter::SetPercent(float InPercent)
{
    Percent = InPercent;
    if (Slate)
    {
        Slate->SetPercent(Percent);
    }
}

void UMarburgumMeter::SetGhost(float InGhost)
{
    Ghost = InGhost;
    if (Slate)
    {
        Slate->SetGhost(Ghost);
    }
}

void UMarburgumMeter::SetFillColor(const FLinearColor& InColor)
{
    FillColor = InColor;
    if (Slate)
    {
        Slate->SetFillColor(FillColor);
    }
}

void UMarburgumMeter::SetSegments(int32 InSegments)
{
    Segments = InSegments;
    if (Slate)
    {
        Slate->SetSegments(Segments);
    }
}

void UMarburgumMeter::SetHeight(float InHeight)
{
    Height = InHeight;
    if (Slate)
    {
        Slate->SetHeight(Height);
    }
}

TSharedRef<SWidget> UMarburgumMeter::RebuildWidget()
{
    Slate = SNew(SMarburgumMeter).Percent(Percent).FillColor(FillColor).Height(Height).Segments(Segments);
    Slate->SetGhost(Ghost);
    return Slate.ToSharedRef();
}

void UMarburgumMeter::ReleaseSlateResources(bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    Slate.Reset();
}

void UMarburgumScrim::Setup(EMarburgumScrimShape InShape, const FLinearColor& InColor)
{
    Shape = InShape;
    Color = InColor;
    if (Slate)
    {
        Slate->SetColor(Color);
    }
}

TSharedRef<SWidget> UMarburgumScrim::RebuildWidget()
{
    Slate = SNew(SMarburgumScrim).Shape(Shape).Color(Color);
    return Slate.ToSharedRef();
}

void UMarburgumScrim::ReleaseSlateResources(bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    Slate.Reset();
}

void UMarburgumSelectable::SetSelected(bool bInSelected)
{
    bSelected = bInSelected;
    if (Slate)
    {
        Slate->SetSelected(bSelected);
    }
}

void UMarburgumSelectable::SetQuiet(bool bInQuiet)
{
    bQuiet = bInQuiet;
    if (Slate)
    {
        Slate->SetQuiet(bQuiet);
    }
}

void UMarburgumSelectable::Focus()
{
    SetFocus();
}

void UMarburgumSelectable::Activate()
{
    OnActivated.ExecuteIfBound();
}

TSharedRef<SWidget> UMarburgumSelectable::RebuildWidget()
{
    Slate = SNew(SMarburgumSelectable).Radius(Radius).bQuiet(bQuiet)
        .OnActivated(FMarburgumSelectableEvent::CreateWeakLambda(this, [this]() { OnActivated.ExecuteIfBound(); }))
        .OnFocused(FMarburgumSelectableEvent::CreateWeakLambda(this, [this]() { OnFocused.ExecuteIfBound(); }));
    if (OnStep.IsBound())
    {
        Slate->SetOnStep(FMarburgumSelectableStep::CreateWeakLambda(this, [this](int32 Direction) { OnStep.ExecuteIfBound(Direction); }));
    }
    Slate->SetSelected(bSelected);
    if (UPanelSlot* ContentSlot = GetContentSlot(); ContentSlot && ContentSlot->Content)
    {
        Slate->SetContent(ContentSlot->Content->TakeWidget());
    }
    return Slate.ToSharedRef();
}

void UMarburgumSelectable::OnSlotAdded(UPanelSlot* InSlot)
{
    if (Slate && InSlot && InSlot->Content)
    {
        Slate->SetContent(InSlot->Content->TakeWidget());
    }
}

void UMarburgumSelectable::OnSlotRemoved(UPanelSlot* InSlot)
{
    if (Slate)
    {
        Slate->SetContent(SNullWidget::NullWidget);
    }
}

void UMarburgumSelectable::ReleaseSlateResources(bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    Slate.Reset();
}
