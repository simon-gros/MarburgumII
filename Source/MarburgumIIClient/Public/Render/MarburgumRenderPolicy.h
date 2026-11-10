#pragma once

#include "CoreMinimal.h"

/**
 * Client-local render-feature policy (ADR-029). Pure: it turns the player's request and what this
 * process can actually do into the features that run, with a reason for every fallback. The
 * subsystem probes capabilities and applies the result; nothing here touches the engine.
 *
 * Each lane is its own setting, never one "DLSS" master toggle: upscaler (TSR or DLSS Super
 * Resolution, whose modes include DLAA), native frame generation (DLSS FG), and Reflex. DLSS 5
 * Neural Rendering has no lane: there is no supported UE 5.8 package for it.
 */
namespace MarburgumII::Render
{
    enum class EUpscaler : uint8
    {
        Auto, // DLSS Quality where supported, TSR elsewhere
        Tsr,
        Dlss,
    };

    enum class EDlssMode : uint8
    {
        Dlaa, // DLSS at full resolution: anti-aliasing only
        Quality,
        Balanced,
        Performance,
        UltraPerformance,
    };

    enum class EFrameGeneration : uint8
    {
        Off,
        Dlss, // native DLSS Frame Generation through the Streamline plugin
    };

    enum class EReflex : uint8
    {
        Auto, // On where supported
        Off,
        On,
        Boost,
    };

    struct FRenderRequest
    {
        EUpscaler Upscaler = EUpscaler::Auto;
        EDlssMode DlssMode = EDlssMode::Quality;
        EFrameGeneration FrameGeneration = EFrameGeneration::Off;
        EReflex Reflex = EReflex::Auto;
        /** -MB2SafeRender: every vendor feature off, whatever is saved. */
        bool bSafeMode = false;
    };

    /** What this process can run. An empty reason means the feature is available. */
    struct FRenderCapabilities
    {
        FString DlssUnavailable;
        FString FrameGenerationUnavailable;
        FString ReflexUnavailable;
        /** NVIDIA Smooth Motion was requested for this process (NVPRESENT_ENABLE_SMOOTH_MOTION=1). */
        bool bExternalFrameGeneration = false;

        bool HasDlss() const { return DlssUnavailable.IsEmpty(); }
        bool HasFrameGeneration() const { return FrameGenerationUnavailable.IsEmpty(); }
        bool HasReflex() const { return ReflexUnavailable.IsEmpty(); }
    };

    struct FRenderResolution
    {
        EUpscaler Upscaler = EUpscaler::Tsr; // never Auto
        EDlssMode DlssMode = EDlssMode::Quality; // meaningful only when Upscaler is Dlss
        EFrameGeneration FrameGeneration = EFrameGeneration::Off;
        EReflex Reflex = EReflex::Off; // never Auto
        /** Why a requested feature does not run, e.g. "dlss:plugin_not_installed". */
        TArray<FString> Notes;

        bool operator==(const FRenderResolution& Other) const
        {
            return Upscaler == Other.Upscaler && DlssMode == Other.DlssMode &&
                FrameGeneration == Other.FrameGeneration && Reflex == Other.Reflex;
        }
    };

    MARBURGUMIICLIENT_API FRenderResolution Resolve(const FRenderRequest& Request, const FRenderCapabilities& Capabilities);

    /** Render scale for a DLSS mode, in percent of the output resolution (NVIDIA's ratios). */
    MARBURGUMIICLIENT_API float DlssScreenPercentage(EDlssMode Mode);

    /** Lower-case tokens used in GameUserSettings.ini, console commands and log lines. */
    MARBURGUMIICLIENT_API const TCHAR* ToToken(EUpscaler Value);
    MARBURGUMIICLIENT_API const TCHAR* ToToken(EDlssMode Value);
    MARBURGUMIICLIENT_API const TCHAR* ToToken(EFrameGeneration Value);
    MARBURGUMIICLIENT_API const TCHAR* ToToken(EReflex Value);
    MARBURGUMIICLIENT_API bool FromToken(const FString& Token, EUpscaler& Out);
    MARBURGUMIICLIENT_API bool FromToken(const FString& Token, EDlssMode& Out);
    MARBURGUMIICLIENT_API bool FromToken(const FString& Token, EFrameGeneration& Out);
    MARBURGUMIICLIENT_API bool FromToken(const FString& Token, EReflex& Out);
}
