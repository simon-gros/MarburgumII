#include "UI/MarburgumSettingsPanel.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/UserInterfaceSettings.h"
#include "GameFramework/GameUserSettings.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumSessionContract.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Render/MarburgumRenderFeatureSubsystem.h"
#include "UI/MarburgumUIBuilder.h"
#include "UI/MarburgumUISettings.h"
#include "UnrealClient.h"

#define LOCTEXT_NAMESPACE "MarburgumSettingsPanel"

using namespace MarburgumUI;

namespace
{
    constexpr float ScaleSteps[] = { 0.8f, 0.9f, 1.f, 1.1f, 1.25f, 1.5f, 1.75f, 2.f };
    constexpr float FrameSteps[] = { 0.f, 30.f, 60.f, 90.f, 120.f, 144.f };

    template <int32 N>
    int32 NearestStep(const float (&Steps)[N], float Value)
    {
        int32 Best = 0;
        for (int32 Index = 1; Index < N; ++Index)
        {
            if (FMath::Abs(Steps[Index] - Value) < FMath::Abs(Steps[Best] - Value))
            {
                Best = Index;
            }
        }
        return Best;
    }

    /** Next index Direction away, clamped for sliders and wrapped for choices. */
    int32 StepIndex(int32 Index, int32 Count, int32 Direction, bool bWrap)
    {
        const int32 Next = Index + Direction;
        return bWrap ? (Next % Count + Count) % Count : FMath::Clamp(Next, 0, Count - 1);
    }

    FText Percent(float Value)
    {
        return FText::Format(LOCTEXT("Percent", "{0} %"), FText::AsNumber(FMath::RoundToInt(Value * 100.f)));
    }

    FText OnOff(bool bValue)
    {
        return bValue ? LOCTEXT("On", "On") : LOCTEXT("Off", "Off");
    }

    bool ScaleFixedByCommandLine()
    {
        float Override = 0.f;
        return FParse::Value(FCommandLine::Get(), TEXT("MB2UIScale="), Override) && Override > 0.f;
    }

    FIntPoint ViewportSize()
    {
        return GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport ? GEngine->GameViewport->Viewport->GetSizeXY()
            : FIntPoint(0, 0);
    }

    bool IsEditable(EMarburgumSetting Setting)
    {
        return Setting != EMarburgumSetting::Resolution && Setting != EMarburgumSetting::WindowMode
            && Setting != EMarburgumSetting::DisplayScale;
    }

    /** Settings whose change alters the theme tokens, so the whole interface is rebuilt. */
    bool ChangesPresentation(EMarburgumSetting Setting)
    {
        return Setting == EMarburgumSetting::Language || Setting == EMarburgumSetting::TextSize || Setting == EMarburgumSetting::HighContrast
            || Setting == EMarburgumSetting::ReducedMotion || Setting == EMarburgumSetting::NotificationTime;
    }

    /** What the screen opened with. File-scope so it survives the rebuild a theme change causes. */
    bool bSnapshotTaken = false;

    /** Absent on dedicated servers and -nullrhi processes, which draw nothing. */
    UMarburgumRenderFeatureSubsystem* RenderFeatures(const UUserWidget& Widget)
    {
        const UGameInstance* GameInstance = Widget.GetGameInstance();
        return GameInstance ? GameInstance->GetSubsystem<UMarburgumRenderFeatureSubsystem>() : nullptr;
    }

    FText NeuralRenderingText(MarburgumII::Render::ENeuralRendering Value)
    {
        switch (Value)
        {
        case MarburgumII::Render::ENeuralRendering::Safe: return LOCTEXT("NeuralSafe", "Safe");
        case MarburgumII::Render::ENeuralRendering::Guided: return LOCTEXT("NeuralGuided", "Guided");
        default: return LOCTEXT("Off", "Off");
        }
    }

    /** Saves a new profile through the subsystem, which re-resolves and cuts temporal history; unchanged is left alone. */
    void SetNeuralRendering(UMarburgumRenderFeatureSubsystem* Render, MarburgumII::Render::ENeuralRendering Value)
    {
        if (Render && Render->GetRequest().NeuralRendering != Value)
        {
            MarburgumII::Render::FRenderRequest Request = Render->GetRequest();
            Request.NeuralRendering = Value;
            Render->SetRequest(Request);
        }
    }
}

void UMarburgumSettingsPanel::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    Build();
    SetVisibility(ESlateVisibility::Collapsed);
}

void UMarburgumSettingsPanel::Build()
{
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);

    UHorizontalBox* Columns = B.Row();

    // Left: every setting in one list, so Up/Down reach all of them and Left/Right change the focused one.
    UVerticalBox* ListColumn = B.Column();
    FBuilder::Stack(ListColumn, B.Text(LOCTEXT("Overline", "SETTINGS"), ETypeRole::Overline, T.Color.Accent), T.Space.XXS);
    FBuilder::Stack(ListColumn, B.Text(LOCTEXT("Title", "How the game looks, sounds and answers"), ETypeRole::Title), T.Space.L);
    List = B.Column();
    Scroller = B.Scroll(List);
    FBuilder::Stack(ListColumn, Scroller, 0.f, true);
    Rows.Reset();
    Labels.Reset();
    Descriptions.Reset();

    AddSection(LOCTEXT("Interface", "INTERFACE"));
    AddRow(EMarburgumSetting::Language, LOCTEXT("LanguageLabel", "Language"),
        LOCTEXT("LanguageDesc", "The language of menus, screens and notices. Place names are always Slovene; what people in the city say, and mission texts, are still in English in this build."), true);
    AddRow(EMarburgumSetting::InterfaceScale, LOCTEXT("ScaleLabel", "Interface scale"),
        ScaleFixedByCommandLine()
            ? LOCTEXT("ScaleFixed", "The size of every panel and text on top of the display's own scaling. Fixed for this run by -MB2UIScale.")
            : LOCTEXT("ScaleDesc", "The size of every panel and text on top of the display's own scaling. 100 % suits most screens; "
                "raise it on a large television or a high-density laptop."), true);
    AddRow(EMarburgumSetting::TextSize, LOCTEXT("TextLabel", "Text size"),
        LOCTEXT("TextDesc", "Larger reading text in panels, the journal, chat and subtitles, without enlarging the frames."), true);
    AddRow(EMarburgumSetting::HighContrast, LOCTEXT("ContrastLabel", "High contrast"),
        LOCTEXT("ContrastDesc", "Opaque panels, brighter secondary text and stronger outlines that read over any street."), true);
    AddRow(EMarburgumSetting::ReducedMotion, LOCTEXT("MotionLabel", "Reduce motion"),
        LOCTEXT("MotionDesc", "No slides, pulses or long fades: titles and notifications appear and leave at once."), true);
    AddRow(EMarburgumSetting::HudOpacity, LOCTEXT("HudLabel", "HUD opacity"),
        LOCTEXT("HudDesc", "How strongly the exploration HUD is drawn over the street. Screens and warnings stay opaque."), true);
    AddRow(EMarburgumSetting::ConditionDisplay, LOCTEXT("ConditionLabel", "Condition"),
        LOCTEXT("ConditionDesc", "Show vitality and stamina only when they change or matter, or keep them on screen."), true);
    AddRow(EMarburgumSetting::NotificationTime, LOCTEXT("NoticeLabel", "Notification time"),
        LOCTEXT("NoticeDesc", "How long mission, item and server notices stay before they fade. Refusals always stay a little longer."), true);
    AddRow(EMarburgumSetting::ChatTimestamps, LOCTEXT("StampLabel", "Chat timestamps"),
        LOCTEXT("StampDesc", "Each chat line carries the time this computer received it."), true);

    AddSection(LOCTEXT("Audio", "AUDIO"));
    AddRow(EMarburgumSetting::MasterVolume, LOCTEXT("VolumeLabel", "Master volume"),
        LOCTEXT("VolumeDesc", "The loudness of everything the game plays. Separate music and effects levels arrive when the game has "
            "separate music and effects channels."), true);

    AddSection(LOCTEXT("Display", "DISPLAY"));
    AddRow(EMarburgumSetting::Resolution, LOCTEXT("ResolutionLabel", "Resolution"),
        LOCTEXT("ResolutionDesc", "The size of the game's window. Changing it in game is not offered yet; start the client with "
            "-ResX and -ResY to choose."), false);
    AddRow(EMarburgumSetting::WindowMode, LOCTEXT("WindowLabel", "Window mode"),
        LOCTEXT("WindowDesc", "Full screen, borderless or windowed, as the client was started."), false);
    AddRow(EMarburgumSetting::DisplayScale, LOCTEXT("DpiLabel", "Display scaling"),
        LOCTEXT("DpiDesc", "The interface is designed at 1080p and follows the window's height: 80 % at 720p, 133 % at 1440p and 200 % "
            "at 4K. Interface scale multiplies on top."), false);
    AddRow(EMarburgumSetting::VSync, LOCTEXT("VSyncLabel", "Vertical sync"),
        LOCTEXT("VSyncDesc", "Waits for the display before showing each frame: no tearing, slightly more input delay."), true);
    AddRow(EMarburgumSetting::FrameRateLimit, LOCTEXT("FpsLabel", "Frame-rate limit"),
        LOCTEXT("FpsDesc", "Caps how many frames the game draws per second, to keep a laptop cool and quiet."), true);

    AddSection(LOCTEXT("Graphics", "GRAPHICS"));
    AddRow(EMarburgumSetting::NeuralRendering, LOCTEXT("NeuralLabel", "DLSS 5 neural rendering"),
        LOCTEXT("NeuralDesc", "Optional. NVIDIA's neural rendering redraws each finished frame for richer light and materials. Safe "
            "works from colour alone; Guided also uses depth and motion and holds detail better while moving. It needs an NVIDIA RTX "
            "card, Windows with DirectX 12, DLSS upscaling and NVIDIA's DLSS 5 runtime, which the game does not include. It can "
            "change faces and signs, and only this computer's picture changes."), true);

    AddSection(LOCTEXT("Controls", "CONTROLS"));
    const auto K = [](const TCHAR* Key) { return FText::FromString(Key); };
    AddBinding(LOCTEXT("Move", "Walk"), { K(TEXT("W")), K(TEXT("A")), K(TEXT("S")), K(TEXT("D")) });
    AddBinding(LOCTEXT("Run", "Run  ·  handbrake when driving"), { K(TEXT("Shift")) });
    AddBinding(LOCTEXT("Jump", "Jump"), { K(TEXT("Space")) });
    AddBinding(LOCTEXT("Interact", "Talk, inspect, fish, enter or leave a car"), { K(TEXT("E")) });
    AddBinding(LOCTEXT("Shop", "Open a shopkeeper's counter"), { K(TEXT("B")) });
    AddBinding(LOCTEXT("Fight", "Jab  ·  heavy swing  ·  guard (hold)"), { K(TEXT("F")), K(TEXT("R")), K(TEXT("G")) });
    AddBinding(LOCTEXT("Chat", "Chat"), { K(TEXT("Enter")) });
    AddBinding(LOCTEXT("Screens", "Character  ·  Skills  ·  Inventory  ·  Journal"), { K(TEXT("C")), K(TEXT("K")), K(TEXT("I")), K(TEXT("J")) });
    AddBinding(LOCTEXT("Choose", "Pick an answer in a decision"), { K(TEXT("1")), K(TEXT("–")), K(TEXT("6")) });
    AddBinding(LOCTEXT("Menu", "Close a screen  ·  game menu"), { K(TEXT("Esc")) });
    AddBinding(LOCTEXT("Photo", "Take a photograph with the camera"), { K(TEXT("P")) });
    AddBinding(LOCTEXT("Pad", "Gamepad in menus: move, select, back, switch screen"), { K(TEXT("D-pad")), K(TEXT("A")), K(TEXT("B")), K(TEXT("LB/RB")) });
    AddBinding(LOCTEXT("PadWalk", "Gamepad: walk  ·  look and aim  ·  run (hold)  ·  jump"), { K(TEXT("L")), K(TEXT("R")), K(TEXT("LT")), K(TEXT("X")) });
    AddBinding(LOCTEXT("PadAct", "Gamepad: interact  ·  photograph  ·  Journal  ·  Inventory  ·  close  ·  game menu"),
        { K(TEXT("A")), K(TEXT("RT")), K(TEXT("View")), K(TEXT("Y")), K(TEXT("B")), K(TEXT("Menu")) });
    UTextBlock* Rebind = B.Text(LOCTEXT("RebindPad", "Keys cannot be changed yet; rebinding arrives with the input-mapping design. Driving, "
        "fighting, chat and the counter are not on the gamepad in this build. Hints name the keys of the device you used last."), ETypeRole::Caption, T.Color.TextMuted);
    Rebind->SetAutoWrapText(true);
    FBuilder::Stack(List, Rebind, T.Space.XL);

    AddSection(LOCTEXT("Restore", "RESTORE"));
    AddAction(EMarburgumSetting::UndoChanges, LOCTEXT("UndoLabel", "Undo changes"),
        LOCTEXT("UndoDesc", "Puts every setting back the way it was when this screen opened."));
    AddAction(EMarburgumSetting::ResetDefaults, LOCTEXT("ResetLabel", "Reset to defaults"),
        LOCTEXT("ResetDesc", "Puts every interface, audio, display and graphics setting here back to the shipped values."));

    USizeBox* ListWidth = B.Bounded(ListColumn, 820.f, 480.f);
    FBuilder::Inline(Columns, ListWidth, T.Space.XXL, true, false);

    // Right: what the focused setting does.
    UVerticalBox* Detail = B.Column();
    FBuilder::Stack(Detail, B.Text(LOCTEXT("About", "ABOUT THIS SETTING"), ETypeRole::Overline, T.Color.TextSecondary), T.Space.M);
    DetailTitle = B.Text(FText::GetEmpty(), ETypeRole::Heading);
    DetailTitle->SetAutoWrapText(true);
    FBuilder::Stack(Detail, DetailTitle, T.Space.S);
    DetailBody = B.Text(FText::GetEmpty(), ETypeRole::Body, T.Color.TextSecondary);
    DetailBody->SetAutoWrapText(true);
    FBuilder::Stack(Detail, DetailBody, T.Space.L);
    UTextBlock* ValueOut = nullptr;
    FBuilder::Stack(Detail, B.Field(LOCTEXT("Current", "Now"), FText::GetEmpty(), &ValueOut), T.Space.XS);
    DetailValue = ValueOut;
    UTextBlock* DefaultOut = nullptr;
    FBuilder::Stack(Detail, B.Field(LOCTEXT("Default", "Default"), FText::GetEmpty(), &DefaultOut), T.Space.XL);
    DetailDefault = DefaultOut;
    FBuilder::Stack(Detail, B.Rule(T.Color.Border, 1.f), T.Space.M);
    UTextBlock* Local = B.Text(LOCTEXT("Local", "Saved on this computer as soon as you change it. Nothing here is sent to the server or "
        "to your account."), ETypeRole::Caption, T.Color.TextMuted);
    Local->SetAutoWrapText(true);
    FBuilder::Stack(Detail, Local, T.Space.L);
    FBuilder::Stack(Detail, B.KeyHint(FText::AsCultureInvariant(TEXT("← →")), LOCTEXT("Change", "Change value")), T.Space.XS);
    FBuilder::Stack(Detail, B.KeyHint(FText::AsCultureInvariant(TEXT("Enter")), LOCTEXT("Next", "Next value")), 0.f);
    UBorder* DetailCard = B.Surface(ESurfaceKind::Panel, Pad(T.Space.XL), Detail);
    USizeBox* DetailWidth = B.Bounded(DetailCard, 520.f, 320.f);
    FBuilder::InlineTop(Columns, DetailWidth, 0.f);

    WidgetTree->RootWidget = Columns;
}

void UMarburgumSettingsPanel::AddSection(const FText& Title)
{
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);
    if (List->GetChildrenCount() > 0)
    {
        FBuilder::Stack(List, B.Spacer(1.f, T.Space.L), 0.f);
    }
    FBuilder::Stack(List, B.SectionTitle(Title, T.Color.Accent), T.Space.S);
}

void UMarburgumSettingsPanel::AddRow(EMarburgumSetting Setting, const FText& Label, const FText& Description, bool bEditable)
{
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);
    UHorizontalBox* Line = B.Row();
    UTextBlock* LabelBlock = B.Text(Label, ETypeRole::Label, bEditable ? T.Color.TextPrimary : T.Color.TextSecondary);
    FBuilder::Inline(Line, LabelBlock, T.Space.L, true);

    UHorizontalBox* Control = B.Row();
    if (bEditable)
    {
        FBuilder::Inline(Control, B.Glyph(EMarburgumGlyph::ChevronLeft, T.Color.TextMuted, T.Icon.Small), T.Space.S);
    }
    UTextBlock* ValueBlock = B.Text(ValueText(Setting), ETypeRole::Label, bEditable ? T.Color.Accent : T.Color.TextSecondary);
    ValueBlock->SetJustification(ETextJustify::Center);
    USizeBox* ValueWidth = WidgetTree->ConstructWidget<USizeBox>();
    ValueWidth->SetMinDesiredWidth(150.f);
    ValueWidth->SetContent(ValueBlock);
    FBuilder::Inline(Control, ValueWidth, bEditable ? T.Space.S : 0.f);
    if (bEditable)
    {
        FBuilder::Inline(Control, B.Glyph(EMarburgumGlyph::ChevronRight, T.Color.TextMuted, T.Icon.Small), 0.f);
    }
    FBuilder::Inline(Line, Control, 0.f);

    UMarburgumSelectable* Row = B.Selectable(Line, FMargin(T.Space.L, T.Space.M));
    const int32 Index = Rows.Num();
    Row->OnFocused.BindWeakLambda(this, [this, Index]() { ShowDetail(Index); });
    if (bEditable)
    {
        Row->OnStep.BindWeakLambda(this, [this, Setting](int32 Direction) { Change(Setting, Direction); });
        Row->OnActivated.BindWeakLambda(this, [this, Setting]() { Change(Setting, 0); });
    }
    FBuilder::Stack(List, Row, T.Space.XS);
    Rows.Add({ Setting, Row, ValueBlock });
    Labels.Add(Index, Label);
    Descriptions.Add(Index, Description);
}

void UMarburgumSettingsPanel::AddAction(EMarburgumSetting Setting, const FText& Label, const FText& Description)
{
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);
    UHorizontalBox* Line = B.Row();
    FBuilder::Inline(Line, B.Glyph(Setting == EMarburgumSetting::UndoChanges ? EMarburgumGlyph::ChevronLeft : EMarburgumGlyph::Close,
        T.Color.Accent, T.Icon.Small), T.Space.M);
    FBuilder::Inline(Line, B.Text(Label, ETypeRole::Label, T.Color.TextPrimary), 0.f, true);
    UMarburgumSelectable* Row = B.Selectable(Line, FMargin(T.Space.L, T.Space.M));
    const int32 Index = Rows.Num();
    Row->OnFocused.BindWeakLambda(this, [this, Index]() { ShowDetail(Index); });
    Row->OnActivated.BindWeakLambda(this, [this, Setting]() { Change(Setting, 0); });
    FBuilder::Stack(List, Row, T.Space.XS);
    Rows.Add({ Setting, Row, nullptr });
    Labels.Add(Index, Label);
    Descriptions.Add(Index, Description);
}

void UMarburgumSettingsPanel::AddBinding(const FText& Action, const TArray<FText>& Keys)
{
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);
    UHorizontalBox* Line = B.Row();
    UTextBlock* ActionBlock = B.Text(Action, ETypeRole::Body, T.Color.TextSecondary);
    ActionBlock->SetAutoWrapText(true);
    FBuilder::Inline(Line, ActionBlock, T.Space.L, true);
    for (int32 Index = 0; Index < Keys.Num(); ++Index)
    {
        FBuilder::Inline(Line, B.KeyCap(Keys[Index]), Index + 1 < Keys.Num() ? T.Space.XS : 0.f);
    }
    UBorder* Holder = B.Surface(ESurfaceKind::Base, FMargin(T.Space.L, T.Space.XS + 1.f), Line, false);
    Holder->SetBrushColor(FLinearColor::Transparent);
    FBuilder::Stack(List, Holder, 0.f);
}

FText UMarburgumSettingsPanel::ValueText(EMarburgumSetting Setting) const
{
    const UMarburgumUISettings& S = UMarburgumUISettings::Get();
    const UGameUserSettings* Game = GEngine ? GEngine->GetGameUserSettings() : nullptr;
    switch (Setting)
    {
    case EMarburgumSetting::InterfaceScale: return Percent(S.EffectiveInterfaceScale());
    case EMarburgumSetting::TextSize:
        return S.TextSize == 0 ? LOCTEXT("Standard", "Standard") : S.TextSize == 1 ? LOCTEXT("Large", "Large") : LOCTEXT("Larger", "Larger");
    case EMarburgumSetting::HighContrast: return OnOff(S.bHighContrast);
    case EMarburgumSetting::ReducedMotion: return OnOff(S.bReducedMotion);
    case EMarburgumSetting::HudOpacity: return Percent(S.HudOpacity);
    case EMarburgumSetting::ConditionDisplay: return S.ConditionDisplay == 1 ? LOCTEXT("Always", "Always") : LOCTEXT("WhenItMatters", "When it matters");
    case EMarburgumSetting::NotificationTime:
        return S.NotificationTime == 0 ? LOCTEXT("Short", "Short") : S.NotificationTime == 1 ? LOCTEXT("Standard", "Standard") : LOCTEXT("Long", "Long");
    case EMarburgumSetting::ChatTimestamps: return OnOff(S.bChatTimestamps);
    // Each language by its own name, never translated.
    case EMarburgumSetting::Language: return S.IsSlovene() ? FText::AsCultureInvariant(TEXT("Slovenščina")) : FText::AsCultureInvariant(TEXT("English"));
    case EMarburgumSetting::MasterVolume: return Percent(S.MasterVolume);
    case EMarburgumSetting::VSync: return Game ? OnOff(Game->IsVSyncEnabled()) : LOCTEXT("Unavailable", "Unavailable");
    case EMarburgumSetting::FrameRateLimit:
        if (!Game)
        {
            return LOCTEXT("Unavailable", "Unavailable");
        }
        return Game->GetFrameRateLimit() <= 0.f ? LOCTEXT("Unlimited", "Unlimited")
            : FText::Format(LOCTEXT("Fps", "{0} fps"), FText::AsNumber(FMath::RoundToInt(Game->GetFrameRateLimit())));
    case EMarburgumSetting::Resolution:
    {
        const FIntPoint Size = ViewportSize();
        return FText::Format(LOCTEXT("Size", "{0} × {1}"), FText::AsNumber(Size.X, &FNumberFormattingOptions::DefaultNoGrouping()),
            FText::AsNumber(Size.Y, &FNumberFormattingOptions::DefaultNoGrouping()));
    }
    case EMarburgumSetting::WindowMode:
        if (!Game)
        {
            return LOCTEXT("Unavailable", "Unavailable");
        }
        switch (Game->GetFullscreenMode())
        {
        case EWindowMode::Fullscreen: return LOCTEXT("Fullscreen", "Full screen");
        case EWindowMode::WindowedFullscreen: return LOCTEXT("Borderless", "Borderless");
        default: return LOCTEXT("Windowed", "Windowed");
        }
    case EMarburgumSetting::DisplayScale:
    {
        const FIntPoint Size = ViewportSize();
        const float Dpi = Size.X > 0 ? GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(Size) : 1.f;
        return Percent(Dpi);
    }
    // The saved choice, which is kept while it cannot run; the detail says why it does not.
    case EMarburgumSetting::NeuralRendering:
    {
        const UMarburgumRenderFeatureSubsystem* Render = RenderFeatures(*this);
        return Render ? NeuralRenderingText(Render->GetRequest().NeuralRendering) : LOCTEXT("Unavailable", "Unavailable");
    }
    default: return FText::GetEmpty();
    }
}

FText UMarburgumSettingsPanel::NeuralRenderingBlocked() const
{
    const UMarburgumRenderFeatureSubsystem* Render = RenderFeatures(*this);
    if (!Render || Render->GetRequest().NeuralRendering == MarburgumII::Render::ENeuralRendering::Off)
    {
        return FText::GetEmpty();
    }
    // The policy's notes name the reason (ADR-029/030), e.g. "neural_rendering:runtime_missing:nvngx_dlssnr.dll".
    FString Reason;
    for (const FString& Note : Render->GetResolution().Notes)
    {
        if (Note == TEXT("safe_mode") || Note.StartsWith(TEXT("neural_rendering:")))
        {
            Reason = Note;
            break;
        }
    }
    if (Reason.Contains(TEXT(":overridden:")))
    {
        return LOCTEXT("NeuralOverridden", "DLSS 5's own controls have overridden this choice for now.");
    }
    if (Render->GetResolution().NeuralRendering != MarburgumII::Render::ENeuralRendering::Off)
    {
        return FText::GetEmpty();
    }
    if (Reason == TEXT("safe_mode"))
    {
        return LOCTEXT("NeuralSafeMode", "Not running: the game was started in safe render mode (-MB2SafeRender).");
    }
    if (Reason.Contains(TEXT("requires_dlss")))
    {
        return LOCTEXT("NeuralNeedsDlss", "Not running: DLSS upscaling is not active on this computer.");
    }
    if (Reason.Contains(TEXT("plugin_not_installed")) || Reason.Contains(TEXT("plugin_disabled")))
    {
        return LOCTEXT("NeuralNoPlugin", "Not running: DLSS 5 is only part of Windows builds running on DirectX 12.");
    }
    if (Reason.Contains(TEXT("not_nvidia_gpu")))
    {
        return LOCTEXT("NeuralNoNvidia", "Not running: it needs an NVIDIA RTX graphics card.");
    }
    if (Reason.Contains(TEXT("runtime_missing")))
    {
        return LOCTEXT("NeuralNoRuntime", "Not running: NVIDIA's DLSS 5 runtime is not installed.");
    }
    return LOCTEXT("NeuralNotStarted", "Not running: the DLSS 5 plugin did not start.");
}

void UMarburgumSettingsPanel::Change(EMarburgumSetting Setting, int32 Direction)
{
    UMarburgumUISettings& S = UMarburgumUISettings::Get();
    UGameUserSettings* Game = GEngine ? GEngine->GetGameUserSettings() : nullptr;
    // Enter (Direction 0) steps forward and wraps; Left/Right step and stop at the ends of a range.
    const bool bWrap = Direction == 0;
    const int32 Dir = Direction == 0 ? 1 : Direction;
    switch (Setting)
    {
    case EMarburgumSetting::InterfaceScale:
        if (ScaleFixedByCommandLine())
        {
            return;
        }
        S.InterfaceScale = ScaleSteps[StepIndex(NearestStep(ScaleSteps, S.InterfaceScale), UE_ARRAY_COUNT(ScaleSteps), Dir, bWrap)];
        break;
    case EMarburgumSetting::TextSize: S.TextSize = StepIndex(S.TextSize, 3, Dir, bWrap); break;
    case EMarburgumSetting::HighContrast: S.bHighContrast = !S.bHighContrast; break;
    case EMarburgumSetting::ReducedMotion: S.bReducedMotion = !S.bReducedMotion; break;
    case EMarburgumSetting::HudOpacity:
        S.HudOpacity = 0.4f + 0.1f * StepIndex(FMath::RoundToInt((S.HudOpacity - 0.4f) * 10.f), 7, Dir, bWrap);
        break;
    case EMarburgumSetting::ConditionDisplay: S.ConditionDisplay = S.ConditionDisplay == 1 ? 0 : 1; break;
    case EMarburgumSetting::NotificationTime: S.NotificationTime = StepIndex(S.NotificationTime, 3, Dir, bWrap); break;
    case EMarburgumSetting::ChatTimestamps: S.bChatTimestamps = !S.bChatTimestamps; break;
    case EMarburgumSetting::Language: S.Language = S.IsSlovene() ? TEXT("en") : TEXT("sl"); break;
    case EMarburgumSetting::MasterVolume:
        S.MasterVolume = 0.1f * StepIndex(FMath::RoundToInt(S.MasterVolume * 10.f), 11, Dir, bWrap);
        break;
    case EMarburgumSetting::VSync:
        if (Game)
        {
            Game->SetVSyncEnabled(!Game->IsVSyncEnabled());
        }
        break;
    case EMarburgumSetting::FrameRateLimit:
        if (Game)
        {
            Game->SetFrameRateLimit(FrameSteps[StepIndex(NearestStep(FrameSteps, Game->GetFrameRateLimit()), UE_ARRAY_COUNT(FrameSteps), Dir, bWrap)]);
        }
        break;
    case EMarburgumSetting::NeuralRendering:
        if (UMarburgumRenderFeatureSubsystem* Render = RenderFeatures(*this))
        {
            const int32 Current = static_cast<int32>(Render->GetRequest().NeuralRendering);
            SetNeuralRendering(Render, static_cast<MarburgumII::Render::ENeuralRendering>(StepIndex(Current, 3, Dir, bWrap)));
        }
        break;
    case EMarburgumSetting::UndoChanges:
        ApplySnapshot(OpenedWith());
        break;
    case EMarburgumSetting::ResetDefaults:
        S.ResetToDefaults();
        if (Game)
        {
            Game->SetVSyncEnabled(false);
            Game->SetFrameRateLimit(0.f);
        }
        SetNeuralRendering(RenderFeatures(*this), MarburgumII::Render::ENeuralRendering::Off);
        break;
    default:
        return;
    }
    S.Persist();
    if (Game && (Setting == EMarburgumSetting::VSync || Setting == EMarburgumSetting::FrameRateLimit
        || Setting == EMarburgumSetting::UndoChanges || Setting == EMarburgumSetting::ResetDefaults))
    {
        Game->ApplyNonResolutionSettings();
        Game->SaveSettings();
    }
    const bool bThemeChanged = S.ApplyToRuntime(GetWorld());
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=client event=setting_changed setting=%d value=\"%s\" theme_changed=%d"),
        MarburgumII::Session::UiLogPrefix, static_cast<int32>(Setting), *ValueText(Setting).ToString(), bThemeChanged ? 1 : 0);
    if (bThemeChanged || ChangesPresentation(Setting))
    {
        // Every widget takes its type and colours when it is built: the subsystem rebuilds them and re-opens this screen.
        OnPresentationChanged.ExecuteIfBound();
        return;
    }
    Refresh();
}

UMarburgumSettingsPanel::FSnapshot UMarburgumSettingsPanel::Capture() const
{
    const UMarburgumUISettings& S = UMarburgumUISettings::Get();
    const UGameUserSettings* Game = GEngine ? GEngine->GetGameUserSettings() : nullptr;
    FSnapshot Out;
    Out.InterfaceScale = S.InterfaceScale;
    Out.TextSize = S.TextSize;
    Out.bHighContrast = S.bHighContrast;
    Out.bReducedMotion = S.bReducedMotion;
    Out.HudOpacity = S.HudOpacity;
    Out.ConditionDisplay = S.ConditionDisplay;
    Out.NotificationTime = S.NotificationTime;
    Out.bChatTimestamps = S.bChatTimestamps;
    Out.Language = S.Language;
    Out.MasterVolume = S.MasterVolume;
    Out.bVSync = Game && Game->IsVSyncEnabled();
    Out.FrameRateLimit = Game ? Game->GetFrameRateLimit() : 0.f;
    if (const UMarburgumRenderFeatureSubsystem* Render = RenderFeatures(*this))
    {
        Out.NeuralRendering = Render->GetRequest().NeuralRendering;
    }
    return Out;
}

void UMarburgumSettingsPanel::ApplySnapshot(const FSnapshot& Snapshot)
{
    UMarburgumUISettings& S = UMarburgumUISettings::Get();
    S.InterfaceScale = Snapshot.InterfaceScale;
    S.TextSize = Snapshot.TextSize;
    S.bHighContrast = Snapshot.bHighContrast;
    S.bReducedMotion = Snapshot.bReducedMotion;
    S.HudOpacity = Snapshot.HudOpacity;
    S.ConditionDisplay = Snapshot.ConditionDisplay;
    S.NotificationTime = Snapshot.NotificationTime;
    S.bChatTimestamps = Snapshot.bChatTimestamps;
    S.Language = Snapshot.Language;
    S.MasterVolume = Snapshot.MasterVolume;
    if (UGameUserSettings* Game = GEngine ? GEngine->GetGameUserSettings() : nullptr)
    {
        Game->SetVSyncEnabled(Snapshot.bVSync);
        Game->SetFrameRateLimit(Snapshot.FrameRateLimit);
    }
    SetNeuralRendering(RenderFeatures(*this), Snapshot.NeuralRendering);
}

void UMarburgumSettingsPanel::Refresh()
{
    for (const FRow& Row : Rows)
    {
        if (UTextBlock* Value = Row.Value.Get())
        {
            Value->SetText(ValueText(Row.Setting));
        }
    }
    ShowDetail(FocusedRow);
}

void UMarburgumSettingsPanel::ShowDetail(int32 RowIndex)
{
    if (!Rows.IsValidIndex(RowIndex) || !DetailTitle)
    {
        return;
    }
    FocusedRow = RowIndex;
    const EMarburgumSetting Setting = Rows[RowIndex].Setting;
    DetailTitle->SetText(Labels.FindRef(RowIndex));
    const FText Blocked = Setting == EMarburgumSetting::NeuralRendering ? NeuralRenderingBlocked() : FText::GetEmpty();
    DetailBody->SetText(Blocked.IsEmpty() ? Descriptions.FindRef(RowIndex)
        : FText::Format(LOCTEXT("DescAndStatus", "{0}\n\n{1}"), Descriptions.FindRef(RowIndex), Blocked));
    const bool bValued = Rows[RowIndex].Value.IsValid();
    DetailValue->SetText(bValued ? ValueText(Setting) : FText::GetEmpty());
    FText Default;
    switch (Setting)
    {
    case EMarburgumSetting::InterfaceScale: Default = Percent(1.f); break;
    case EMarburgumSetting::TextSize: Default = LOCTEXT("Standard", "Standard"); break;
    case EMarburgumSetting::HighContrast:
    case EMarburgumSetting::ReducedMotion:
    case EMarburgumSetting::VSync: Default = OnOff(false); break;
    case EMarburgumSetting::ChatTimestamps: Default = OnOff(true); break;
    case EMarburgumSetting::Language: Default = LOCTEXT("SystemLanguage", "The system's"); break;
    case EMarburgumSetting::HudOpacity:
    case EMarburgumSetting::MasterVolume: Default = Percent(1.f); break;
    case EMarburgumSetting::ConditionDisplay: Default = LOCTEXT("WhenItMatters", "When it matters"); break;
    case EMarburgumSetting::NotificationTime: Default = LOCTEXT("Standard", "Standard"); break;
    case EMarburgumSetting::FrameRateLimit: Default = LOCTEXT("Unlimited", "Unlimited"); break;
    case EMarburgumSetting::NeuralRendering: Default = NeuralRenderingText(MarburgumII::Render::ENeuralRendering::Off); break;
    default: Default = LOCTEXT("NotApplicable", "—"); break;
    }
    DetailDefault->SetText(Default);
}

UMarburgumSettingsPanel::FSnapshot& UMarburgumSettingsPanel::OpenedWith()
{
    static FSnapshot Snapshot;
    return Snapshot;
}

void UMarburgumSettingsPanel::SetOpen(bool bOpen)
{
    if (bOpen && !bSnapshotTaken)
    {
        OpenedWith() = Capture();
        bSnapshotTaken = true;
    }
    if (!bOpen)
    {
        bSnapshotTaken = false;
    }
    SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    if (bOpen)
    {
        Refresh();
    }
}

bool UMarburgumSettingsPanel::IsOpen() const
{
    return GetVisibility() != ESlateVisibility::Collapsed;
}

void UMarburgumSettingsPanel::Scroll(float Lines)
{
    FBuilder::ScrollBy(Scroller, Lines);
}

UWidget* UMarburgumSettingsPanel::GetPreferredFocus() const
{
    return Rows.IsValidIndex(FocusedRow) ? Rows[FocusedRow].Widget.Get() : nullptr;
}

void UMarburgumSettingsPanel::RestoreFocus(int32 Index)
{
    FocusedRow = Rows.IsValidIndex(Index) ? Index : 0;
    if (UMarburgumSelectable* Row = Rows.IsValidIndex(FocusedRow) ? Rows[FocusedRow].Widget.Get() : nullptr)
    {
        Row->Focus();
        if (Scroller)
        {
            Scroller->ScrollWidgetIntoView(Row, false);
        }
    }
}

FText UMarburgumSettingsPanel::GetShownValue(EMarburgumSetting Setting) const
{
    return ValueText(Setting);
}

void UMarburgumSettingsPanel::StepSetting(EMarburgumSetting Setting, int32 Direction)
{
    Change(Setting, Direction);
}

bool UMarburgumSettingsPanel::FocusSetting(EMarburgumSetting Setting)
{
    const int32 Index = Rows.IndexOfByPredicate([Setting](const FRow& Row) { return Row.Setting == Setting; });
    if (Index == INDEX_NONE)
    {
        return false;
    }
    RestoreFocus(Index);
    ShowDetail(Index);
    return true;
}

#undef LOCTEXT_NAMESPACE
