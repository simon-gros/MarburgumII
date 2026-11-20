#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MarburgumSettingsPanel.generated.h"

class UScrollBox;
class UTextBlock;
class UVerticalBox;
class UMarburgumSelectable;

/** Which preference a settings row edits (the value lives in UMarburgumUISettings or UGameUserSettings). */
enum class EMarburgumSetting : uint8
{
    InterfaceScale,
    TextSize,
    HighContrast,
    ReducedMotion,
    HudOpacity,
    ConditionDisplay,
    NotificationTime,
    ChatTimestamps,
    Language,
    MasterVolume,
    VSync,
    FrameRateLimit,
    Resolution,   // Read-only.
    WindowMode,   // Read-only.
    DisplayScale, // Read-only.
    ResetDefaults,
    UndoChanges
};

DECLARE_DELEGATE(FMarburgumSettingsPresentationChanged);

/**
 * UI-06 `settings_screen`: interface, audio, display and controls, hosted by the management shell (O, or Settings in
 * the Escape menu). Every change applies at once and is saved to this machine's GameUserSettings.ini; nothing reaches
 * the server or the account. Left/Right (or the d-pad) step a value, Enter steps it forward; "Undo changes" restores
 * what the screen opened with and "Reset to defaults" the shipped values. Display settings are limited to what is safe
 * at run time (VSync, frame-rate cap); resolution and window mode are shown, not switched. Key bindings are shown as
 * they are; rebinding arrives with the input-mapping design.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumSettingsPanel : public UUserWidget
{
    GENERATED_BODY()

public:
    /** Fired after a change that alters the theme tokens (text size, contrast, motion, notification time): the
     *  subsystem rebuilds the interface and re-opens this screen. */
    FMarburgumSettingsPresentationChanged OnPresentationChanged;

    void SetOpen(bool bOpen);
    bool IsOpen() const;
    void Scroll(float Lines);
    UWidget* GetPreferredFocus() const;
    /** Focus the row at Index (after a rebuild) and keep the opened-with snapshot from Snapshot. */
    void RestoreFocus(int32 Index);
    int32 GetFocusedRow() const { return FocusedRow; }

    /** The shown value of Setting, for evidence capture. */
    FText GetShownValue(EMarburgumSetting Setting) const;

    /** Steps Setting by Direction as the row would (tests and the capture tour). */
    void StepSetting(EMarburgumSetting Setting, int32 Direction);

protected:
    virtual void NativeOnInitialized() override;

private:
    struct FRow
    {
        EMarburgumSetting Setting;
        TWeakObjectPtr<UMarburgumSelectable> Widget;
        TWeakObjectPtr<UTextBlock> Value;
    };

    void Build();
    void AddSection(const FText& Title);
    void AddRow(EMarburgumSetting Setting, const FText& Label, const FText& Description, bool bEditable);
    void AddAction(EMarburgumSetting Setting, const FText& Label, const FText& Description);
    void AddBinding(const FText& Action, const TArray<FText>& Keys);
    void Change(EMarburgumSetting Setting, int32 Direction);
    void Refresh();
    void ShowDetail(int32 RowIndex);
    FText ValueText(EMarburgumSetting Setting) const;

    UPROPERTY(Transient) TObjectPtr<UVerticalBox> List;
    UPROPERTY(Transient) TObjectPtr<UScrollBox> Scroller;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> DetailTitle;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> DetailBody;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> DetailValue;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> DetailDefault;

    /** What the screen opened with, for "Undo changes". */
    struct FSnapshot
    {
        float InterfaceScale = 1.f;
        int32 TextSize = 0;
        bool bHighContrast = false;
        bool bReducedMotion = false;
        float HudOpacity = 1.f;
        int32 ConditionDisplay = 0;
        int32 NotificationTime = 1;
        bool bChatTimestamps = true;
        FString Language;
        float MasterVolume = 1.f;
        bool bVSync = false;
        float FrameRateLimit = 0.f;
    };
    FSnapshot Capture() const;
    void ApplySnapshot(const FSnapshot& Snapshot);
    static FSnapshot& OpenedWith();

    TArray<FRow> Rows;
    TMap<int32, FText> Descriptions;
    TMap<int32, FText> Labels;
    int32 FocusedRow = 0;
};
