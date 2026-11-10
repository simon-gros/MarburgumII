#include "Render/MarburgumRenderPolicy.h"

namespace MarburgumII::Render
{
    FRenderResolution Resolve(const FRenderRequest& Request, const FRenderCapabilities& Capabilities)
    {
        FRenderResolution Out;
        if (Request.bSafeMode)
        {
            Out.Notes.Add(TEXT("safe_mode"));
            return Out;
        }

        if (Request.Upscaler != EUpscaler::Tsr)
        {
            if (Capabilities.HasDlss())
            {
                Out.Upscaler = EUpscaler::Dlss;
                Out.DlssMode = Request.DlssMode;
            }
            else
            {
                Out.Notes.Add(FString::Printf(TEXT("dlss:%s"), *Capabilities.DlssUnavailable));
            }
        }

        if (Request.FrameGeneration == EFrameGeneration::Dlss)
        {
            if (Capabilities.bExternalFrameGeneration)
            {
                // Two generated-frame paths would interpolate already generated frames.
                Out.Notes.Add(TEXT("frame_generation:conflict_smooth_motion"));
            }
            else if (!Capabilities.HasFrameGeneration())
            {
                Out.Notes.Add(FString::Printf(TEXT("frame_generation:%s"), *Capabilities.FrameGenerationUnavailable));
            }
            else if (!Capabilities.HasReflex())
            {
                // DLSS Frame Generation requires Reflex to hold the added latency down.
                Out.Notes.Add(FString::Printf(TEXT("frame_generation:reflex_%s"), *Capabilities.ReflexUnavailable));
            }
            else
            {
                Out.FrameGeneration = EFrameGeneration::Dlss;
            }
        }

        switch (Request.Reflex)
        {
        case EReflex::Auto:
            Out.Reflex = Capabilities.HasReflex() ? EReflex::On : EReflex::Off;
            break;
        case EReflex::Off:
            if (Out.FrameGeneration == EFrameGeneration::Dlss)
            {
                Out.Reflex = EReflex::On;
                Out.Notes.Add(TEXT("reflex:required_by_frame_generation"));
            }
            break;
        case EReflex::On:
        case EReflex::Boost:
            if (Capabilities.HasReflex())
            {
                Out.Reflex = Request.Reflex;
            }
            else
            {
                Out.Notes.Add(FString::Printf(TEXT("reflex:%s"), *Capabilities.ReflexUnavailable));
            }
            break;
        }
        return Out;
    }

    float DlssScreenPercentage(EDlssMode Mode)
    {
        switch (Mode)
        {
        case EDlssMode::Dlaa: return 100.f;
        case EDlssMode::Quality: return 66.667f;
        case EDlssMode::Balanced: return 58.f;
        case EDlssMode::Performance: return 50.f;
        case EDlssMode::UltraPerformance: return 33.333f;
        }
        return 100.f;
    }

    const TCHAR* ToToken(EUpscaler Value)
    {
        switch (Value)
        {
        case EUpscaler::Auto: return TEXT("auto");
        case EUpscaler::Tsr: return TEXT("tsr");
        case EUpscaler::Dlss: return TEXT("dlss");
        }
        return TEXT("auto");
    }

    const TCHAR* ToToken(EDlssMode Value)
    {
        switch (Value)
        {
        case EDlssMode::Dlaa: return TEXT("dlaa");
        case EDlssMode::Quality: return TEXT("quality");
        case EDlssMode::Balanced: return TEXT("balanced");
        case EDlssMode::Performance: return TEXT("performance");
        case EDlssMode::UltraPerformance: return TEXT("ultra_performance");
        }
        return TEXT("quality");
    }

    const TCHAR* ToToken(EFrameGeneration Value)
    {
        return Value == EFrameGeneration::Dlss ? TEXT("dlss") : TEXT("off");
    }

    const TCHAR* ToToken(EReflex Value)
    {
        switch (Value)
        {
        case EReflex::Auto: return TEXT("auto");
        case EReflex::Off: return TEXT("off");
        case EReflex::On: return TEXT("on");
        case EReflex::Boost: return TEXT("boost");
        }
        return TEXT("auto");
    }

    namespace
    {
        template <typename EnumType, SIZE_T Count>
        bool ParseToken(const FString& Token, const EnumType (&Values)[Count], EnumType& Out)
        {
            const FString Key = Token.TrimStartAndEnd().ToLower();
            for (const EnumType Value : Values)
            {
                if (Key == ToToken(Value))
                {
                    Out = Value;
                    return true;
                }
            }
            return false;
        }
    }

    bool FromToken(const FString& Token, EUpscaler& Out)
    {
        static constexpr EUpscaler Values[] = { EUpscaler::Auto, EUpscaler::Tsr, EUpscaler::Dlss };
        return ParseToken(Token, Values, Out);
    }

    bool FromToken(const FString& Token, EDlssMode& Out)
    {
        static constexpr EDlssMode Values[] = {
            EDlssMode::Dlaa, EDlssMode::Quality, EDlssMode::Balanced, EDlssMode::Performance, EDlssMode::UltraPerformance };
        return ParseToken(Token, Values, Out);
    }

    bool FromToken(const FString& Token, EFrameGeneration& Out)
    {
        static constexpr EFrameGeneration Values[] = { EFrameGeneration::Off, EFrameGeneration::Dlss };
        return ParseToken(Token, Values, Out);
    }

    bool FromToken(const FString& Token, EReflex& Out)
    {
        static constexpr EReflex Values[] = { EReflex::Auto, EReflex::Off, EReflex::On, EReflex::Boost };
        return ParseToken(Token, Values, Out);
    }
}
