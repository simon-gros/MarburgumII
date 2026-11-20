#include "UI/MarburgumUISettings.h"

#include "AudioDevice.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UI/MarburgumUITheme.h"

UMarburgumUISettings& UMarburgumUISettings::Get()
{
    UMarburgumUISettings* Settings = GetMutableDefault<UMarburgumUISettings>();
    Settings->Sanitize();
    return *Settings;
}

float UMarburgumUISettings::TextScale() const
{
    return TextSize <= 0 ? 1.f : TextSize == 1 ? 1.12f : 1.25f;
}

float UMarburgumUISettings::NotificationTimeScale() const
{
    return NotificationTime <= 0 ? 0.7f : NotificationTime == 1 ? 1.f : 1.6f;
}

void UMarburgumUISettings::Sanitize()
{
    InterfaceScale = FMath::IsFinite(InterfaceScale) ? FMath::Clamp(InterfaceScale, MinInterfaceScale, MaxInterfaceScale) : 1.f;
    TextSize = FMath::Clamp(TextSize, 0, 2);
    HudOpacity = FMath::IsFinite(HudOpacity) ? FMath::Clamp(HudOpacity, 0.4f, 1.f) : 1.f;
    ConditionDisplay = FMath::Clamp(ConditionDisplay, 0, 1);
    NotificationTime = FMath::Clamp(NotificationTime, 0, 2);
    MasterVolume = FMath::IsFinite(MasterVolume) ? FMath::Clamp(MasterVolume, 0.f, 1.f) : 1.f;
}

void UMarburgumUISettings::Persist()
{
    Sanitize();
    SaveConfig();
}

void UMarburgumUISettings::ResetToDefaults()
{
    InterfaceScale = 1.f;
    TextSize = 0;
    bHighContrast = false;
    bReducedMotion = false;
    HudOpacity = 1.f;
    ConditionDisplay = 0;
    NotificationTime = 1;
    bChatTimestamps = true;
    MasterVolume = 1.f;
}

float UMarburgumUISettings::EffectiveInterfaceScale() const
{
    float Override = 0.f;
    if (FParse::Value(FCommandLine::Get(), TEXT("MB2UIScale="), Override) && Override > 0.f)
    {
        return FMath::Clamp(Override, 0.5f, 3.f);
    }
    return InterfaceScale;
}

bool UMarburgumUISettings::ApplyToRuntime(UWorld* World) const
{
    if (FSlateApplication::IsInitialized())
    {
        FSlateApplication::Get().SetApplicationScale(EffectiveInterfaceScale());
    }
    if (GEngine)
    {
        FAudioDeviceHandle Audio = World ? World->GetAudioDevice() : GEngine->GetMainAudioDevice();
        if (Audio)
        {
            Audio->SetTransientPrimaryVolume(MasterVolume);
        }
    }
    MarburgumUI::FPresentationOptions Options;
    Options.bHighContrast = bHighContrast;
    Options.bReducedMotion = bReducedMotion;
    Options.TextScale = TextScale();
    Options.NotificationTimeScale = NotificationTimeScale();
    return MarburgumUI::SetPresentationOptions(Options);
}
