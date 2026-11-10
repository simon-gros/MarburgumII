#pragma once

#include "CoreMinimal.h"
#include "Render/MarburgumRenderPolicy.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MarburgumRenderFeatureSubsystem.generated.h"

class IConsoleObject;

/**
 * Client-local NVIDIA render features (ADR-029): DLSS Super Resolution / DLAA, DLSS Frame Generation,
 * Reflex and DLSS 5 Neural Rendering through DLSS5ForUE5 (ADR-030), each its own setting, with TSR and no frame generation as the always-available
 * fallback. Not created for dedicated servers or -nullrhi processes; nothing here reaches gameplay,
 * the server or the protocol, so two clients may run different features in the same world.
 *
 * The NVIDIA plugins are never a compile-time or packaging dependency. At startup the subsystem
 * looks for each plugin, its console variables and its support query (all named in the
 * [/Script/MarburgumIIClient.MarburgumRenderFeatureSubsystem] section of DefaultEngine.ini), and any
 * missing piece makes that feature unavailable with a stated reason instead of a crash.
 *
 * The player's choice is saved in GameUserSettings.ini [MarburgumII.Render] and kept even while a
 * plugin is absent, so it applies again once the plugin is installed.
 *
 * Every resolution logs one "MB2RENDER" line naming the RHI, GPU, driver, the request, the active
 * features and the reason for each fallback, so captures and profiles record what drew them.
 *
 * Options (this launch only, not saved): -MB2SafeRender forces every vendor feature off;
 * -MB2Upscaler=auto|tsr|dlss -MB2DlssMode=dlaa|quality|balanced|performance|ultra_performance
 * -MB2FrameGeneration=off|dlss -MB2Reflex=auto|off|on|boost -MB2NeuralRendering=off|safe|guided.
 * Console: mb2.Render.Status, and mb2.Render.Upscaler/DlssMode/FrameGeneration/Reflex/NeuralRendering
 * <token>, which save.
 */
UCLASS(config = Engine)
class MARBURGUMIICLIENT_API UMarburgumRenderFeatureSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    const MarburgumII::Render::FRenderRequest& GetRequest() const { return Request; }
    const MarburgumII::Render::FRenderCapabilities& GetCapabilities() const { return Capabilities; }
    const MarburgumII::Render::FRenderResolution& GetResolution() const { return Resolution; }

    /** Applies a new player choice, saves it unless safe mode is on, and cuts temporal history. */
    void SetRequest(const MarburgumII::Render::FRenderRequest& NewRequest);

    /** The MB2RENDER line for the current state. */
    FString DescribeState() const;

private:
    /** Plugin that provides DLSS Super Resolution and DLAA. */
    UPROPERTY(config)
    FString DlssPlugin;
    UPROPERTY(config)
    FString DlssEnableCVar;
    /** Static, parameterless UFUNCTION returning bool: "/Script/Module.Class:Function". Empty skips the query. */
    UPROPERTY(config)
    FString DlssSupportQuery;

    UPROPERTY(config)
    FString FrameGenerationPlugin;
    UPROPERTY(config)
    FString FrameGenerationEnableCVar;
    UPROPERTY(config)
    FString FrameGenerationSupportQuery;

    UPROPERTY(config)
    FString ReflexPlugin;
    UPROPERTY(config)
    FString ReflexEnableCVar;
    /** Takes 1 for On and 2 for Boost. */
    UPROPERTY(config)
    FString ReflexModeCVar;
    UPROPERTY(config)
    FString ReflexSupportQuery;

    /** DLSS5ForUE5 (vendored source) and the runtime it loads, relative to the plugin directory. */
    UPROPERTY(config)
    FString NeuralRenderingPlugin;
    UPROPERTY(config)
    FString NeuralRenderingEnableCVar;
    UPROPERTY(config)
    FString NeuralRenderingRuntimeFile;

    /** Probe, resolve, apply and register commands once every plugin module has loaded. */
    void Start();
    void ProbeCapabilities();
    FString ProbeFeature(const FString& Plugin, const TArray<FString>& CVars, const FString& SupportQuery,
        const FString& RequiredFile = FString()) const;
    /** Calls "/Script/Module.Class:Function"; empty if it returned true, otherwise the reason. */
    static FString QuerySupport(const FString& SupportQuery);
    void LoadRequest();
    void SaveRequest() const;
    void Apply(bool bCutHistory);
    void SetCVar(const FString& Name, const FString& Value) const;
    void RegisterConsoleCommands();
    void HandleConsoleSetting(const TArray<FString>& Args, const TCHAR* Setting);

    MarburgumII::Render::FRenderRequest Request;
    MarburgumII::Render::FRenderCapabilities Capabilities;
    MarburgumII::Render::FRenderResolution Resolution;
    bool bApplied = false;
    /** r.ScreenPercentage before DLSS changed it, restored for TSR. */
    FString BaselineScreenPercentage;
    TArray<IConsoleObject*> ConsoleObjects;
    FDelegateHandle InitCompleteHandle;
};
