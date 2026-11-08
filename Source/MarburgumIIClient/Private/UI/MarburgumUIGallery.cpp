#include "UI/MarburgumUIGallery.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/EditableTextBox.h"
#include "Components/ProgressBar.h"
#include "Components/SafeZone.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "MarburgumGameState.h"
#include "Session/MarburgumSessionSubsystem.h"
#include "UI/MarburgumUIBuilder.h"

#define LOCTEXT_NAMESPACE "MarburgumGallery"

using namespace MarburgumUI;

namespace
{
    UTextBlock* SectionTitle(const FBuilder& B, const FText& Title)
    {
        return B.Text(Title, ETypeRole::Heading, Tokens().Color.TextPrimary);
    }

    UWidget* LeftAligned(UWidget* Widget)
    {
        if (UVerticalBoxSlot* Slot = Cast<UVerticalBoxSlot>(Widget->Slot))
        {
            Slot->SetHorizontalAlignment(HAlign_Left);
        }
        return Widget;
    }
}

void UMarburgumUIGallery::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);

    UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>();
    WidgetTree->RootWidget = Root;
    UOverlaySlot* BackdropSlot = Root->AddChildToOverlay(B.Surface(ESurfaceKind::Base, FMargin(0.f)));
    BackdropSlot->SetHorizontalAlignment(HAlign_Fill);
    BackdropSlot->SetVerticalAlignment(VAlign_Fill);

    UVerticalBox* Page = B.Column();
    UHorizontalBox* Header = B.Row();
    UVerticalBox* Titles = B.Column();
    FBuilder::Stack(Titles, B.Text(LOCTEXT("Overline", "UI-01  ·  DESIGN SYSTEM"), ETypeRole::Overline, T.Color.Accent), T.Space.S);
    FBuilder::Stack(Titles, B.Text(LOCTEXT("Title", "Component gallery"), ETypeRole::Title), 0.f);
    FBuilder::Inline(Header, Titles, T.Space.XL, true);
    FBuilder::Inline(Header, B.SampleDataBadge(), 0.f);
    FBuilder::Stack(Page, Header, T.Space.XL);
    FBuilder::Stack(Page, B.Rule(T.Color.Border, 1.f), T.Space.XL);

    UHorizontalBox* Columns = B.Row();
    UVerticalBox* Sections[] = { BuildTypography(), BuildColors(), BuildControls(), BuildFeedback() };
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Sections); ++Index)
    {
        FBuilder::Inline(Columns, Sections[Index], Index + 1 < UE_ARRAY_COUNT(Sections) ? T.Space.XXL : 0.f, true, false);
    }
    FBuilder::Stack(Page, Columns, 0.f, true);

    UBorder* Gutter = B.Surface(ESurfaceKind::Base, FMargin(T.Space.XXXL + T.Space.L, T.Space.XXXL), Page, false);
    Gutter->SetBrushColor(FLinearColor::Transparent);
    USafeZone* Safe = WidgetTree->ConstructWidget<USafeZone>();
    Safe->SetContent(Gutter);
    UOverlaySlot* SafeSlot = Root->AddChildToOverlay(Safe);
    SafeSlot->SetHorizontalAlignment(HAlign_Fill);
    SafeSlot->SetVerticalAlignment(VAlign_Fill);
}

UVerticalBox* UMarburgumUIGallery::BuildTypography()
{
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);
    UVerticalBox* Box = B.Column();
    FBuilder::Stack(Box, SectionTitle(B, LOCTEXT("TypeTitle", "TYPOGRAPHY")), T.Space.L);

    UTextBlock* Display = B.Text(LOCTEXT("TypeDisplay", "Marburgum"), ETypeRole::Display);
    FSlateFontInfo DisplayFont = Display->GetFont();
    DisplayFont.Size = 40;
    Display->SetFont(DisplayFont);
    FBuilder::Stack(Box, Display, T.Space.M);
    FBuilder::Stack(Box, B.Text(LOCTEXT("TypeTitleSample", "Lent ob Dravi"), ETypeRole::Title), T.Space.M);
    FBuilder::Stack(Box, B.Text(LOCTEXT("TypeHeading", "JOURNAL  ·  ACTIVE"), ETypeRole::Heading), T.Space.M);
    UTextBlock* Body = B.Text(LOCTEXT("TypeBody", "Body text carries Slovene diacritics cleanly: čevljar, šola, žaga, Tezno, Studenci, Šentilj."), ETypeRole::Body);
    Body->SetAutoWrapText(true);
    FBuilder::Stack(Box, Body, T.Space.M);
    FBuilder::Stack(Box, B.Text(LOCTEXT("TypeStrong", "Strong body for action verbs"), ETypeRole::BodyStrong), T.Space.M);
    FBuilder::Stack(Box, B.Text(LOCTEXT("TypeCaption", "Caption  ·  secondary detail and metadata"), ETypeRole::Caption), T.Space.M);
    FBuilder::Stack(Box, B.Text(LOCTEXT("TypeOverline", "OVERLINE  ·  PILLS AND KIND LABELS"), ETypeRole::Overline), T.Space.M);
    FBuilder::Stack(Box, B.Text(LOCTEXT("TypeNumeric", "1 990  ·  12 345 678  ·  07:45"), ETypeRole::Numeric), 0.f);
    return Box;
}

UVerticalBox* UMarburgumUIGallery::BuildColors()
{
    const FTokens& T = Tokens();
    const FColors& C = T.Color;
    FBuilder B(*WidgetTree);
    UVerticalBox* Box = B.Column();
    FBuilder::Stack(Box, SectionTitle(B, LOCTEXT("ColorTitle", "COLOUR ROLES")), T.Space.L);

    struct FSwatch { FText Name; FLinearColor Color; };
    const FSwatch Swatches[] = {
        { LOCTEXT("SwBackdrop", "Backdrop"), C.Backdrop },
        { LOCTEXT("SwPanel", "Panel"), C.Panel },
        { LOCTEXT("SwRaised", "Raised"), C.Raised },
        { LOCTEXT("SwText", "Text primary"), C.TextPrimary },
        { LOCTEXT("SwText2", "Text secondary"), C.TextSecondary },
        { LOCTEXT("SwAccent", "Accent  ·  copper"), C.Accent },
        { LOCTEXT("SwInfo", "Info  ·  teal"), C.Info },
        { LOCTEXT("SwSuccess", "Success"), C.Success },
        { LOCTEXT("SwWarning", "Warning"), C.Warning },
        { LOCTEXT("SwDanger", "Danger"), C.Danger },
        { LOCTEXT("SwFocus", "Focus"), C.Focus },
        { LOCTEXT("SwSample", "Sample-data marker"), C.SampleData },
    };
    for (const FSwatch& Swatch : Swatches)
    {
        UHorizontalBox* Line = B.Row();
        UBorder* Chip = B.Surface(ESurfaceKind::Raised, FMargin(0.f), B.Spacer(40.f, 24.f));
        Chip->SetBrush(RoundedBrush(Swatch.Color, T.Radius.Medium, C.BorderStrong, T.BorderWidth));
        FBuilder::Inline(Line, Chip, T.Space.M);
        FBuilder::Inline(Line, B.Text(Swatch.Name, ETypeRole::Caption, C.TextPrimary), 0.f, true);
        FBuilder::Stack(Box, Line, T.Space.S);
    }
    return Box;
}

UVerticalBox* UMarburgumUIGallery::BuildControls()
{
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);
    UVerticalBox* Box = B.Column();
    FBuilder::Stack(Box, SectionTitle(B, LOCTEXT("ControlsTitle", "CONTROLS")), T.Space.L);

    UHorizontalBox* Buttons = B.Row();
    FBuilder::Inline(Buttons, B.Button(LOCTEXT("BtnPrimary", "Primary"), EButtonKind::Primary), T.Space.S);
    FBuilder::Inline(Buttons, B.Button(LOCTEXT("BtnSecondary", "Secondary"), EButtonKind::Secondary), T.Space.S);
    FBuilder::Inline(Buttons, B.Button(LOCTEXT("BtnQuiet", "Quiet"), EButtonKind::Quiet), 0.f);
    FBuilder::Stack(Box, Buttons, T.Space.M);
    LeftAligned(Buttons);

    UButton* Disabled = B.Button(LOCTEXT("BtnDisabled", "Disabled  ·  not yet available"), EButtonKind::Primary);
    Disabled->SetIsEnabled(false);
    FBuilder::Stack(Box, Disabled, T.Space.L);
    LeftAligned(Disabled);

    FBuilder::Stack(Box, B.TextField(LOCTEXT("FieldValue", "Glavni trg 1"), LOCTEXT("FieldHint", "Search")), T.Space.L);

    UHorizontalBox* PillsA = B.Row();
    FBuilder::Inline(PillsA, B.Pill(LOCTEXT("PillNeutral", "OFFLINE"), ETone::Neutral), T.Space.S);
    FBuilder::Inline(PillsA, B.Pill(LOCTEXT("PillInfo", "JOINING"), ETone::Info), T.Space.S);
    FBuilder::Inline(PillsA, B.Pill(LOCTEXT("PillSuccess", "CONNECTED"), ETone::Success), 0.f);
    FBuilder::Stack(Box, PillsA, T.Space.S);
    LeftAligned(PillsA);
    UHorizontalBox* PillsB = B.Row();
    FBuilder::Inline(PillsB, B.Pill(LOCTEXT("PillWarning", "RECONNECTING"), ETone::Warning), T.Space.S);
    FBuilder::Inline(PillsB, B.Pill(LOCTEXT("PillDanger", "REFUSED"), ETone::Danger), T.Space.S);
    FBuilder::Inline(PillsB, B.Pill(LOCTEXT("PillAccent", "PINNED"), ETone::Accent), 0.f);
    FBuilder::Stack(Box, PillsB, T.Space.XL);
    LeftAligned(PillsB);

    FBuilder::Stack(Box, B.Text(LOCTEXT("MeterTitle", "METERS"), ETypeRole::Overline), T.Space.S);
    FBuilder::Stack(Box, B.Field(LOCTEXT("MeterVitality", "Vitality (sample)"), LOCTEXT("MeterVitalityValue", "72 / 100")), T.Space.XS);
    USizeBox* VitalityBox = B.Spacer(0.f, 6.f);
    VitalityBox->ClearWidthOverride();
    VitalityBox->SetContent(B.Meter(ETone::Success, 0.72f));
    FBuilder::Stack(Box, VitalityBox, T.Space.M);
    FBuilder::Stack(Box, B.Field(LOCTEXT("MeterStamina", "Stamina (sample)"), LOCTEXT("MeterStaminaValue", "38 / 100")), T.Space.XS);
    USizeBox* StaminaBox = B.Spacer(0.f, 6.f);
    StaminaBox->ClearWidthOverride();
    StaminaBox->SetContent(B.Meter(ETone::Warning, 0.38f));
    FBuilder::Stack(Box, StaminaBox, 0.f);
    return Box;
}

UVerticalBox* UMarburgumUIGallery::BuildFeedback()
{
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);
    UVerticalBox* Box = B.Column();
    FBuilder::Stack(Box, SectionTitle(B, LOCTEXT("FeedbackTitle", "FEEDBACK & STATES")), T.Space.L);
    FBuilder::Stack(Box, B.Toast(LOCTEXT("ToastSkillKind", "SKILL  ·  SAMPLE"), LOCTEXT("ToastSkill", "Fishing +40 XP"), ETone::Accent), T.Space.S);
    FBuilder::Stack(Box, B.Toast(LOCTEXT("ToastObjKind", "OBJECTIVE  ·  SAMPLE"), LOCTEXT("ToastObj", "Ask the angler on the Drava bank about the missing boat"), ETone::Info), T.Space.S);
    FBuilder::Stack(Box, B.Toast(LOCTEXT("ToastConnKind", "CONNECTION"), LOCTEXT("ToastConn", "Reconnected to the server"), ETone::Success), T.Space.S);
    FBuilder::Stack(Box, B.Toast(LOCTEXT("ToastRefuseKind", "REFUSED"), LOCTEXT("ToastRefuse", "The server declined this action. Nothing was changed."), ETone::Danger), T.Space.L);
    FBuilder::Stack(Box, B.StateCard(LOCTEXT("EmptyTitle", "Inventory is empty"),
        LOCTEXT("EmptyBody", "Items appear here once the server confirms you have them."), ETone::Neutral), T.Space.S);
    FBuilder::Stack(Box, B.StateCard(LOCTEXT("UnsupportedTitle", "Skills are not available yet"),
        LOCTEXT("UnsupportedBody", "This build has no authoritative skill system, so nothing is shown."), ETone::Warning), 0.f);
    return Box;
}

void UMarburgumDiagnosticsOverlay::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);

    UVerticalBox* Body = B.Column();
    FBuilder::Stack(Body, B.Text(LOCTEXT("DiagTitle", "DIAGNOSTIC  ·  LIVE REPLICATED STATE"), ETypeRole::Overline, T.Color.Info), T.Space.M);
    UTextBlock* Value = nullptr;
    FBuilder::Stack(Body, B.Field(LOCTEXT("DiagServer", "Server"), FText::GetEmpty(), &Value), T.Space.XS); ServerValue = Value;
    FBuilder::Stack(Body, B.Field(LOCTEXT("DiagContract", "Network contract"), FText::GetEmpty(), &Value), T.Space.XS); ContractValue = Value;
    FBuilder::Stack(Body, B.Field(LOCTEXT("DiagHeartbeat", "Authority heartbeat"), FText::GetEmpty(), &Value), T.Space.XS); HeartbeatValue = Value;
    FBuilder::Stack(Body, B.Field(LOCTEXT("DiagPlayers", "Players in session"), FText::GetEmpty(), &Value), T.Space.XS); PlayersValue = Value;
    FBuilder::Stack(Body, B.Field(LOCTEXT("DiagOutcomes", "Applied / refused"), FText::GetEmpty(), &Value), 0.f); OutcomesValue = Value;

    USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>();
    Width->SetWidthOverride(360.f);
    Width->SetContent(B.Surface(ESurfaceKind::Overlay, Pad(T.Space.L), Body));

    UBorder* Anchor = B.Surface(ESurfaceKind::Base, FMargin(T.Space.XXL, T.Space.XXL), Width, false);
    Anchor->SetBrushColor(FLinearColor::Transparent);
    FBuilder::Align(Anchor, HAlign_Left, VAlign_Top);
    USafeZone* Safe = WidgetTree->ConstructWidget<USafeZone>();
    Safe->SetContent(Anchor);
    WidgetTree->RootWidget = Safe;
    SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UMarburgumDiagnosticsOverlay::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    const double Now = FPlatformTime::Seconds();
    if (Now < NextRefreshSeconds)
    {
        return;
    }
    NextRefreshSeconds = Now + 0.5;

    const UWorld* World = GetWorld();
    const AMarburgumGameState* GameState = World ? World->GetGameState<AMarburgumGameState>() : nullptr;
    if (!UMarburgumSessionSubsystem::IsAdmitted(World) || !GameState)
    {
        const FText Unknown = LOCTEXT("DiagUnknown", "—");
        ServerValue->SetText(LOCTEXT("DiagNotAdmitted", "not admitted"));
        ContractValue->SetText(Unknown);
        HeartbeatValue->SetText(Unknown);
        PlayersValue->SetText(Unknown);
        OutcomesValue->SetText(Unknown);
        return;
    }
    ServerValue->SetText(FText::FromString(World->URL.GetHostPortString()));
    ContractValue->SetText(FText::Format(LOCTEXT("DiagContractValue", "v{0}"), FText::AsNumber(GameState->NetworkContractVersion)));
    HeartbeatValue->SetText(FText::AsNumber(GameState->AuthorityHeartbeat));
    PlayersValue->SetText(FText::AsNumber(GameState->PlayerArray.Num()));
    OutcomesValue->SetText(FText::Format(LOCTEXT("DiagOutcomesValue", "{0} / {1}"),
        FText::AsNumber(GameState->AppliedRequestCount), FText::AsNumber(GameState->RejectedRequestCount)));
}

#undef LOCTEXT_NAMESPACE
