#include "Render/MarburgumRenderFeatureSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "RHIGlobals.h"
#include "UObject/SoftObjectPath.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumRender, Log, All);

using namespace MarburgumII::Render;

namespace
{
    const TCHAR* const SettingsSection = TEXT("MarburgumII.Render");
    const TCHAR* const ScreenPercentageCVar = TEXT("r.ScreenPercentage");

    template <typename EnumType>
    void ReadSaved(const TCHAR* Key, EnumType& Value)
    {
        FString Token;
        if (GConfig && GConfig->GetString(SettingsSection, Key, Token, GGameUserSettingsIni))
        {
            // An unknown token (a removed mode, a hand edit) keeps the default rather than failing.
            FromToken(Token, Value);
        }
    }

    template <typename EnumType>
    void ReadOverride(const TCHAR* Switch, EnumType& Value)
    {
        FString Token;
        if (FParse::Value(FCommandLine::Get(), Switch, Token) && !FromToken(Token, Value))
        {
            UE_LOG(LogMarburgumRender, Warning, TEXT("MB2RENDER event=bad_option option=%s value=%s"), Switch, *Token);
        }
    }

    FRenderRequest ReadSavedRequest()
    {
        FRenderRequest Saved;
        ReadSaved(TEXT("Upscaler"), Saved.Upscaler);
        ReadSaved(TEXT("DlssMode"), Saved.DlssMode);
        ReadSaved(TEXT("FrameGeneration"), Saved.FrameGeneration);
        ReadSaved(TEXT("Reflex"), Saved.Reflex);
        ReadSaved(TEXT("NeuralRendering"), Saved.NeuralRendering);
        return Saved;
    }

    template <typename EnumType>
    bool ParseConsoleToken(const TArray<FString>& Args, EnumType& Value)
    {
        return Args.Num() == 1 && FromToken(Args[0], Value);
    }
}

bool UMarburgumRenderFeatureSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UGameInstance* GameInstance = Cast<UGameInstance>(Outer);
    return GameInstance && !GameInstance->IsDedicatedServerInstance() && FApp::CanEverRender();
}

void UMarburgumRenderFeatureSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    // A launching game creates its game instance before PostEngineInit, when the NVIDIA blueprint
    // modules load and their support queries become valid. PIE instances start after that.
    if (GIsRunning)
    {
        Start();
    }
    else
    {
        InitCompleteHandle = FCoreDelegates::OnFEngineLoopInitComplete.AddUObject(this, &UMarburgumRenderFeatureSubsystem::Start);
    }
}

void UMarburgumRenderFeatureSubsystem::Start()
{
    FCoreDelegates::OnFEngineLoopInitComplete.Remove(InitCompleteHandle);
    if (const IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(ScreenPercentageCVar))
    {
        BaselineScreenPercentage = Var->GetString();
    }
    ProbeCapabilities();
    LoadRequest();
    Resolution = Resolve(Request, Capabilities);
    Apply(false);
    RegisterConsoleCommands();
}

void UMarburgumRenderFeatureSubsystem::Deinitialize()
{
    FCoreDelegates::OnFEngineLoopInitComplete.Remove(InitCompleteHandle);
    for (IConsoleObject* Object : ConsoleObjects)
    {
        IConsoleManager::Get().UnregisterConsoleObject(Object);
    }
    ConsoleObjects.Reset();
    Super::Deinitialize();
}

void UMarburgumRenderFeatureSubsystem::SetRequest(const FRenderRequest& NewRequest)
{
    Request = NewRequest;
    // Safe mode belongs to this launch; it is never written over the player's saved choice.
    if (!Request.bSafeMode)
    {
        SaveRequest();
    }
    const FRenderResolution Previous = Resolution;
    Resolution = Resolve(Request, Capabilities);
    Apply(!(Previous == Resolution));
}

void UMarburgumRenderFeatureSubsystem::ProbeCapabilities()
{
    Capabilities = FRenderCapabilities();
    Capabilities.DlssUnavailable = ProbeFeature(DlssPlugin, { DlssEnableCVar }, DlssSupportQuery);
    Capabilities.FrameGenerationUnavailable =
        ProbeFeature(FrameGenerationPlugin, { FrameGenerationEnableCVar }, FrameGenerationSupportQuery);
    Capabilities.ReflexUnavailable = ProbeFeature(ReflexPlugin, { ReflexEnableCVar, ReflexModeCVar }, ReflexSupportQuery);
    // DLSS5ForUE5 has no support query; its runtime is supplied separately and never committed (ADR-030).
    Capabilities.NeuralRenderingUnavailable =
        ProbeFeature(NeuralRenderingPlugin, { NeuralRenderingEnableCVar }, FString(), NeuralRenderingRuntimeFile);
    // NVIDIA Smooth Motion is driver-side (NvPresent); the game can only see that it was asked for.
    Capabilities.bExternalFrameGeneration =
        FPlatformMisc::GetEnvironmentVariable(TEXT("NVPRESENT_ENABLE_SMOOTH_MOTION")).TrimStartAndEnd() == TEXT("1");
}

FString UMarburgumRenderFeatureSubsystem::ProbeFeature(
    const FString& Plugin, const TArray<FString>& CVars, const FString& SupportQuery, const FString& RequiredFile) const
{
    if (Plugin.IsEmpty())
    {
        return TEXT("not_configured");
    }
    const TSharedPtr<IPlugin> Found = IPluginManager::Get().FindPlugin(Plugin);
    if (!Found.IsValid())
    {
        return FString::Printf(TEXT("plugin_not_installed:%s"), *Plugin);
    }
    if (!Found->IsEnabled())
    {
        return FString::Printf(TEXT("plugin_disabled:%s"), *Plugin);
    }
    if (!IsRHIDeviceNVIDIA())
    {
        return TEXT("not_nvidia_gpu");
    }
    if (!RequiredFile.IsEmpty() && !FPaths::FileExists(FPaths::Combine(Found->GetBaseDir(), RequiredFile)))
    {
        return FString::Printf(TEXT("runtime_missing:%s"), *FPaths::GetCleanFilename(RequiredFile));
    }
    // The plugin's own verdict comes before its variables: on a platform it does not support, its
    // blueprint module still loads and says so, while the modules that register variables do not.
    if (!SupportQuery.IsEmpty())
    {
        const FString Verdict = QuerySupport(SupportQuery);
        if (!Verdict.IsEmpty())
        {
            return Verdict;
        }
    }
    for (const FString& Name : CVars)
    {
        if (Name.IsEmpty() || !IConsoleManager::Get().FindConsoleVariable(*Name))
        {
            return FString::Printf(TEXT("cvar_missing:%s"), Name.IsEmpty() ? TEXT("(unset)") : *Name);
        }
    }
    return FString();
}

FString UMarburgumRenderFeatureSubsystem::QuerySupport(const FString& SupportQuery)
{
    // The query is bound by name so the plugin stays optional: no headers, no link dependency.
    FString ClassPath;
    FString FunctionName;
    const UClass* Class = nullptr;
    UFunction* Function = nullptr;
    if (SupportQuery.Split(TEXT(":"), &ClassPath, &FunctionName))
    {
        Class = FSoftClassPath(ClassPath).ResolveClass();
        Function = Class ? Class->FindFunctionByName(*FunctionName) : nullptr;
    }
    const FBoolProperty* Result = Function ? CastField<FBoolProperty>(Function->GetReturnProperty()) : nullptr;
    if (!Result || Function->NumParms != 1 || !Function->HasAnyFunctionFlags(FUNC_Static))
    {
        return FString::Printf(TEXT("support_query_missing:%s"), *SupportQuery);
    }
    TArray<uint8> Parameters;
    Parameters.SetNumZeroed(Function->ParmsSize);
    Class->GetDefaultObject()->ProcessEvent(Function, Parameters.GetData());
    return Result->GetPropertyValue_InContainer(Parameters.GetData()) ? FString() : TEXT("unsupported");
}

void UMarburgumRenderFeatureSubsystem::LoadRequest()
{
    Request = ReadSavedRequest();
    ReadOverride(TEXT("MB2Upscaler="), Request.Upscaler);
    ReadOverride(TEXT("MB2DlssMode="), Request.DlssMode);
    ReadOverride(TEXT("MB2FrameGeneration="), Request.FrameGeneration);
    ReadOverride(TEXT("MB2Reflex="), Request.Reflex);
    ReadOverride(TEXT("MB2NeuralRendering="), Request.NeuralRendering);
    Request.bSafeMode = FParse::Param(FCommandLine::Get(), TEXT("MB2SafeRender"));
}

void UMarburgumRenderFeatureSubsystem::SaveRequest() const
{
    if (!GConfig)
    {
        return;
    }
    GConfig->SetString(SettingsSection, TEXT("Upscaler"), ToToken(Request.Upscaler), GGameUserSettingsIni);
    GConfig->SetString(SettingsSection, TEXT("DlssMode"), ToToken(Request.DlssMode), GGameUserSettingsIni);
    GConfig->SetString(SettingsSection, TEXT("FrameGeneration"), ToToken(Request.FrameGeneration), GGameUserSettingsIni);
    GConfig->SetString(SettingsSection, TEXT("Reflex"), ToToken(Request.Reflex), GGameUserSettingsIni);
    GConfig->SetString(SettingsSection, TEXT("NeuralRendering"), ToToken(Request.NeuralRendering), GGameUserSettingsIni);
    GConfig->Flush(false, GGameUserSettingsIni);
}

void UMarburgumRenderFeatureSubsystem::Apply(bool bCutHistory)
{
    // A feature that is unavailable is never touched: its variables may not exist, and the engine's
    // own defaults (TSR, no generated frames) are the fallback.
    if (Capabilities.HasDlss())
    {
        const bool bDlss = Resolution.Upscaler == EUpscaler::Dlss;
        SetCVar(DlssEnableCVar, bDlss ? TEXT("1") : TEXT("0"));
        // DLAA is DLSS at full resolution; the other modes render below it and DLSS reconstructs.
        SetCVar(ScreenPercentageCVar,
            bDlss ? FString::SanitizeFloat(DlssScreenPercentage(Resolution.DlssMode)) : BaselineScreenPercentage);
    }
    if (Capabilities.HasFrameGeneration())
    {
        SetCVar(FrameGenerationEnableCVar, Resolution.FrameGeneration == EFrameGeneration::Dlss ? TEXT("1") : TEXT("0"));
    }
    if (Capabilities.HasReflex())
    {
        SetCVar(ReflexEnableCVar, Resolution.Reflex == EReflex::Off ? TEXT("0") : TEXT("1"));
        SetCVar(ReflexModeCVar, Resolution.Reflex == EReflex::Boost ? TEXT("2") : TEXT("1"));
    }
    if (Capabilities.HasNeuralRendering())
    {
        // One pass with scene guides generated; Guided also feeds depth and motion to the network.
        const bool bOn = Resolution.NeuralRendering != ENeuralRendering::Off;
        const bool bGuided = Resolution.NeuralRendering == ENeuralRendering::Guided;
        SetCVar(TEXT("r.DLSS5.PassCount"), TEXT("1"));
        SetCVar(TEXT("r.DLSS5.EnableSceneGuides"), TEXT("1"));
        SetCVar(TEXT("r.DLSS5.FeedSceneGuides"), bGuided ? TEXT("1") : TEXT("0"));
        SetCVar(TEXT("r.DLSS5.UseDepth"), TEXT("1"));
        SetCVar(TEXT("r.DLSS5.UseMotionVectors"), bGuided ? TEXT("1") : TEXT("0"));
        SetCVar(NeuralRenderingEnableCVar, bOn ? TEXT("1") : TEXT("0"));
    }

    // A higher-priority setter (command line, device profile, console) wins over a game setting.
    if (Capabilities.HasDlss())
    {
        const IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*DlssEnableCVar);
        if (Var && Var->GetBool() != (Resolution.Upscaler == EUpscaler::Dlss))
        {
            Resolution.Notes.AddUnique(FString::Printf(TEXT("dlss:overridden:%s"), *DlssEnableCVar));
        }
    }
    if (Capabilities.HasNeuralRendering())
    {
        // DLSS5ForUE5's own editor window and crash recovery set its variables from the console.
        const IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*NeuralRenderingEnableCVar);
        if (Var && Var->GetBool() != (Resolution.NeuralRendering != ENeuralRendering::Off))
        {
            Resolution.Notes.AddUnique(FString::Printf(TEXT("neural_rendering:overridden:%s"), *NeuralRenderingEnableCVar));
        }
    }

    if (bCutHistory)
    {
        // Upscaler history from the previous configuration would ghost into the next frames.
        const UGameInstance* GameInstance = GetGameInstance();
        for (const ULocalPlayer* Player : GameInstance->GetLocalPlayers())
        {
            const APlayerController* Controller = Player ? Player->PlayerController.Get() : nullptr;
            if (Controller && Controller->PlayerCameraManager)
            {
                Controller->PlayerCameraManager->SetGameCameraCutThisFrame();
            }
        }
    }
    UE_LOG(LogMarburgumRender, Display, TEXT("%s"), *DescribeState());
    bApplied = true;
}

void UMarburgumRenderFeatureSubsystem::SetCVar(const FString& Name, const FString& Value) const
{
    if (IConsoleVariable* Var = Name.IsEmpty() ? nullptr : IConsoleManager::Get().FindConsoleVariable(*Name))
    {
        Var->Set(*Value, ECVF_SetByGameSetting);
    }
}

FString UMarburgumRenderFeatureSubsystem::DescribeState() const
{
    const bool bDlss = Resolution.Upscaler == EUpscaler::Dlss;
    const FString ScreenPercentage = bDlss ? FString::Printf(TEXT("%.1f"), DlssScreenPercentage(Resolution.DlssMode)) : TEXT("engine");
    return FString::Printf(
        TEXT("MB2RENDER event=%s rhi=%s gpu=\"%s\" driver=%s nvidia=%d safe_mode=%d ")
        TEXT("request=upscaler:%s,dlss_mode:%s,frame_generation:%s,reflex:%s ")
        TEXT("active=upscaler:%s,dlss_mode:%s,screen_pct:%s,frame_generation:%s,reflex:%s ")
        TEXT("available=dlss:%s,frame_generation:%s,reflex:%s,neural_rendering:%s external_frame_generation=%s ")
        TEXT("neural_rendering=%s/%s notes=%s"),
        bApplied ? TEXT("changed") : TEXT("resolved"),
        *FApp::GetGraphicsRHI(), *GRHIAdapterName, *GRHIAdapterUserDriverVersion, IsRHIDeviceNVIDIA() ? 1 : 0,
        Request.bSafeMode ? 1 : 0,
        ToToken(Request.Upscaler), ToToken(Request.DlssMode), ToToken(Request.FrameGeneration), ToToken(Request.Reflex),
        ToToken(Resolution.Upscaler), bDlss ? ToToken(Resolution.DlssMode) : TEXT("-"), *ScreenPercentage,
        ToToken(Resolution.FrameGeneration), ToToken(Resolution.Reflex),
        Capabilities.HasDlss() ? TEXT("yes") : *Capabilities.DlssUnavailable,
        Capabilities.HasFrameGeneration() ? TEXT("yes") : *Capabilities.FrameGenerationUnavailable,
        Capabilities.HasReflex() ? TEXT("yes") : *Capabilities.ReflexUnavailable,
        Capabilities.HasNeuralRendering() ? TEXT("yes") : *Capabilities.NeuralRenderingUnavailable,
        Capabilities.bExternalFrameGeneration ? TEXT("nvidia-smooth-motion(requested-unverified)") : TEXT("none"),
        ToToken(Request.NeuralRendering), ToToken(Resolution.NeuralRendering),
        Resolution.Notes.IsEmpty() ? TEXT("-") : *FString::Join(Resolution.Notes, TEXT(";")));
}

void UMarburgumRenderFeatureSubsystem::RegisterConsoleCommands()
{
    IConsoleManager& Console = IConsoleManager::Get();
    // A second game instance (multi-client PIE) keeps the first one's commands.
    if (Console.FindConsoleObject(TEXT("mb2.Render.Status")))
    {
        return;
    }
    ConsoleObjects.Add(Console.RegisterConsoleCommand(TEXT("mb2.Render.Status"),
        TEXT("Log the active render features and why any requested feature does not run."),
        FConsoleCommandDelegate::CreateWeakLambda(this, [this]
        {
            UE_LOG(LogMarburgumRender, Display, TEXT("%s"), *DescribeState());
        })));
    const struct
    {
        const TCHAR* Name;
        const TCHAR* Help;
    } Settings[] = {
        { TEXT("Upscaler"), TEXT("auto | tsr | dlss") },
        { TEXT("DlssMode"), TEXT("dlaa | quality | balanced | performance | ultra_performance") },
        { TEXT("FrameGeneration"), TEXT("off | dlss") },
        { TEXT("Reflex"), TEXT("auto | off | on | boost") },
        { TEXT("NeuralRendering"), TEXT("off | safe | guided (DLSS 5 through DLSS5ForUE5; needs DLSS on)") },
    };
    for (const auto& Setting : Settings)
    {
        ConsoleObjects.Add(Console.RegisterConsoleCommand(*FString::Printf(TEXT("mb2.Render.%s"), Setting.Name),
            *FString::Printf(TEXT("Set and save the %s render setting: %s."), Setting.Name, Setting.Help),
            FConsoleCommandWithArgsDelegate::CreateUObject(this, &UMarburgumRenderFeatureSubsystem::HandleConsoleSetting, Setting.Name)));
    }
}

void UMarburgumRenderFeatureSubsystem::HandleConsoleSetting(const TArray<FString>& Args, const TCHAR* Setting)
{
    // Start from the saved choice: this launch's -MB2... overrides and safe mode are never saved.
    FRenderRequest Next = ReadSavedRequest();
    const FString Key(Setting);
    const bool bParsed =
        Key == TEXT("Upscaler") ? ParseConsoleToken(Args, Next.Upscaler) :
        Key == TEXT("DlssMode") ? ParseConsoleToken(Args, Next.DlssMode) :
        Key == TEXT("FrameGeneration") ? ParseConsoleToken(Args, Next.FrameGeneration) :
        Key == TEXT("Reflex") ? ParseConsoleToken(Args, Next.Reflex) :
        ParseConsoleToken(Args, Next.NeuralRendering);
    if (!bParsed)
    {
        UE_LOG(LogMarburgumRender, Warning, TEXT("MB2RENDER event=bad_setting setting=%s value=%s"),
            Setting, *FString::Join(Args, TEXT(" ")));
        return;
    }
    SetRequest(Next);
}
