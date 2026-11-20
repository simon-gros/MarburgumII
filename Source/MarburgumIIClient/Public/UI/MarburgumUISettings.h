#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MarburgumUISettings.generated.h"

/**
 * The player's client-local interface preferences (ontology `player_preference`), saved in this machine's
 * GameUserSettings.ini so they survive a restart. Nothing here reaches a server or an account. Display settings that
 * the engine already persists (VSync, frame-rate cap) stay in UGameUserSettings; this object only reads and applies
 * them through the same Settings screen.
 */
UCLASS(Config = GameUserSettings)
class MARBURGUMIICLIENT_API UMarburgumUISettings : public UObject
{
    GENERATED_BODY()

public:
    /** The live, persisted preferences (the class default object, loaded from GameUserSettings.ini). */
    static UMarburgumUISettings& Get();

    /** Interface scale on top of the display's DPI scale: 0.8 to 2.0. */
    UPROPERTY(Config) float InterfaceScale = 1.f;
    /** 0 standard, 1 large (112 %), 2 larger (125 %). */
    UPROPERTY(Config) int32 TextSize = 0;
    UPROPERTY(Config) bool bHighContrast = false;
    UPROPERTY(Config) bool bReducedMotion = false;
    /** How strongly the exploration HUD is drawn over the street: 0.4 to 1.0. */
    UPROPERTY(Config) float HudOpacity = 1.f;
    /** 0: the condition shows when it changes or matters; 1: always. */
    UPROPERTY(Config) int32 ConditionDisplay = 0;
    /** 0 short, 1 standard, 2 long. */
    UPROPERTY(Config) int32 NotificationTime = 1;
    /** Chat lines carry their local time. */
    UPROPERTY(Config) bool bChatTimestamps = true;
    /** The game's overall loudness, 0 to 1 (the engine's transient primary volume). */
    UPROPERTY(Config) float MasterVolume = 1.f;
    UPROPERTY(Config) int32 Version = 1;

    static constexpr float MinInterfaceScale = 0.8f;
    static constexpr float MaxInterfaceScale = 2.f;

    float TextScale() const;
    float NotificationTimeScale() const;

    /** Clamps every value to its range (a hand-edited ini cannot break the interface). */
    void Sanitize();
    /** Writes the preferences to GameUserSettings.ini. */
    void Persist();
    /** Back to the shipped defaults (not persisted until Persist). */
    void ResetToDefaults();

    /** The interface scale actually in use: -MB2UIScale for this run when given (captures), else the preference. */
    float EffectiveInterfaceScale() const;
    /** Pushes scale, theme options and volume into Slate, the theme tokens and the audio device. Returns true when
     *  the theme changed and widgets built before must be rebuilt. */
    bool ApplyToRuntime(class UWorld* World) const;
};
