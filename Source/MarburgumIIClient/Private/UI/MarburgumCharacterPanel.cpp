#include "UI/MarburgumCharacterPanel.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "MarburgumAccountContract.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumGameState.h"
#include "MarburgumPlaces.h"
#include "MarburgumPlayerState.h"
#include "MarburgumSkillCurve.h"
#include "MarburgumWorldClock.h"
#include "Session/MarburgumSessionSubsystem.h"
#include "UI/MarburgumUIBuilder.h"
#include "UI/MarburgumUISubsystem.h"

#define LOCTEXT_NAMESPACE "MarburgumCharacterPanel"

using namespace MarburgumUI;

namespace
{
    UVerticalBox* Section(const FBuilder& B, UVerticalBox* Parent, const FText& Title)
    {
        const FTokens& T = Tokens();
        FBuilder::Stack(Parent, B.SectionTitle(Title, T.Color.Accent), T.Space.M);
        UVerticalBox* Body = B.Column();
        FBuilder::Stack(Parent, Body, T.Space.XL);
        return Body;
    }

    FText Points(int32 Value, int32 Max)
    {
        return FText::Format(LOCTEXT("Points", "{0} / {1}"), FText::AsNumber(Value), FText::AsNumber(Max));
    }

    FText Initials(const FString& First, const FString& Last)
    {
        FString Out;
        if (!First.IsEmpty())
        {
            Out.AppendChar(First[0]);
        }
        if (!Last.IsEmpty())
        {
            Out.AppendChar(Last[0]);
        }
        return Caps(Out.IsEmpty() ? FString(TEXT("?")) : Out);
    }
}

void UMarburgumCharacterPanel::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);

    UHorizontalBox* Columns = B.Row();
    IdentityBox = B.Column();
    UBorder* IdentityCard = B.Surface(ESurfaceKind::Panel, Pad(T.Space.XL), IdentityBox);
    FBuilder::InlineTop(Columns, B.Bounded(IdentityCard, 440.f, 320.f), T.Space.XXL);

    Sections = B.Column();
    Scroller = B.Scroll(Sections);
    FBuilder::Inline(Columns, B.Bounded(Scroller, 900.f, 420.f), 0.f, true, false);

    WidgetTree->RootWidget = Columns;
    SetVisibility(ESlateVisibility::Collapsed);
}

void UMarburgumCharacterPanel::NativeConstruct()
{
    Super::NativeConstruct();
    for (FMarburgumPlayerStateChanged* Event : { &AMarburgumPlayerState::OnAreaChanged(), &AMarburgumPlayerState::OnMissionsChanged(),
             &AMarburgumPlayerState::OnInventoryChanged(), &AMarburgumPlayerState::OnProgressChanged(), &AMarburgumPlayerState::OnIdentityChanged() })
    {
        Handles.Add(Event->AddUObject(this, &UMarburgumCharacterPanel::HandleStateChanged));
    }
    ConditionHandle = AMarburgumPlayerState::OnConditionChanged().AddUObject(this, &UMarburgumCharacterPanel::HandleConditionChanged);
    Rebuild();
}

void UMarburgumCharacterPanel::NativeDestruct()
{
    FMarburgumPlayerStateChanged* Events[] = { &AMarburgumPlayerState::OnAreaChanged(), &AMarburgumPlayerState::OnMissionsChanged(),
        &AMarburgumPlayerState::OnInventoryChanged(), &AMarburgumPlayerState::OnProgressChanged(), &AMarburgumPlayerState::OnIdentityChanged() };
    for (int32 Index = 0; Index < Handles.Num() && Index < UE_ARRAY_COUNT(Events); ++Index)
    {
        Events[Index]->Remove(Handles[Index]);
    }
    Handles.Reset();
    AMarburgumPlayerState::OnConditionChanged().Remove(ConditionHandle);
    Super::NativeDestruct();
}

void UMarburgumCharacterPanel::SetOpen(bool bOpen)
{
    SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    if (bOpen)
    {
        Rebuild();
        if (Scroller)
        {
            Scroller->ScrollToStart();
        }
    }
}

bool UMarburgumCharacterPanel::IsOpen() const
{
    return GetVisibility() != ESlateVisibility::Collapsed;
}

void UMarburgumCharacterPanel::Scroll(float Lines)
{
    FBuilder::ScrollBy(Scroller, Lines);
}

UWidget* UMarburgumCharacterPanel::GetPreferredFocus() const
{
    return FirstLink;
}

FText UMarburgumCharacterPanel::GetShownStaminaText() const
{
    return IsOpen() && StaminaValue ? StaminaValue->GetText() : FText::GetEmpty();
}

void UMarburgumCharacterPanel::HandleStateChanged(AMarburgumPlayerState* State)
{
    const APlayerController* Owner = GetOwningPlayer();
    if (IsOpen() && Owner && State == Owner->PlayerState)
    {
        const bool bHadFocus = HasFocusedDescendants();
        Rebuild();
        if (bHadFocus && FirstLink)
        {
            FirstLink->SetFocus();
        }
    }
}

void UMarburgumCharacterPanel::HandleConditionChanged(AMarburgumPlayerState* State)
{
    const APlayerController* Owner = GetOwningPlayer();
    if (IsOpen() && Owner && State == Owner->PlayerState)
    {
        if (StaminaMeter)
        {
            UpdateCondition();
        }
        else
        {
            Rebuild(); // The condition just became known.
        }
    }
}

void UMarburgumCharacterPanel::UpdateCondition()
{
    const APlayerController* Owner = GetOwningPlayer();
    const AMarburgumPlayerState* State = Owner ? Owner->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    if (!State || !State->IsConditionKnown() || !StaminaMeter)
    {
        return;
    }
    using namespace MarburgumII::Gameplay;
    const FTokens& T = Tokens();
    const FMarburgumCondition& Condition = State->GetCondition();
    VitalityMeter->SetPercent(Condition.MaxVitality > 0 ? static_cast<float>(Condition.Vitality) / Condition.MaxVitality : 0.f);
    VitalityMeter->SetFillColor(Condition.Vitality * 4 <= Condition.MaxVitality ? T.Color.Danger : T.Color.Success);
    VitalityValue->SetText(Points(Condition.Vitality, Condition.MaxVitality));
    StaminaMeter->SetPercent(Condition.MaxStamina > 0 ? static_cast<float>(Condition.Stamina) / Condition.MaxStamina : 0.f);
    StaminaMeter->SetFillColor(Condition.bWinded ? T.Color.Warning : T.Color.Info);
    StaminaValue->SetText(Points(Condition.Stamina, Condition.MaxStamina));
    const FText Breath = Condition.Vitality == 0
        ? FText::Format(LOCTEXT("Down", "Down: back on your feet in {0} s"), FText::AsNumber(Condition.RecoverySeconds))
        : Condition.bSubmerged ? LOCTEXT("Submerged", "Under water")
        : Condition.bSwimming ? LOCTEXT("Swimming", "Swimming")
        : Condition.bWinded
            ? FText::Format(LOCTEXT("Winded", "Winded: walking until stamina is back to {0}"), FText::AsNumber(FMath::RoundToInt(RunResumeStamina)))
        : Condition.bRunning ? LOCTEXT("Running", "Running")
        : Condition.Stamina < Condition.MaxStamina ? LOCTEXT("Recovering", "Getting breath back")
        : LOCTEXT("Rested", "Rested");
    BreathValue->SetText(Breath);
}

void UMarburgumCharacterPanel::Rebuild()
{
    if (!Sections)
    {
        return;
    }
    using namespace MarburgumII::Gameplay;
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);
    Sections->ClearChildren();
    IdentityBox->ClearChildren();
    StaminaValue = nullptr;
    VitalityValue = nullptr;
    StaminaMeter = nullptr;
    VitalityMeter = nullptr;
    BreathValue = nullptr;
    FirstLink = nullptr;

    const APlayerController* Owner = GetOwningPlayer();
    const AMarburgumPlayerState* State = Owner ? Owner->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    const UGameInstance* GameInstance = GetGameInstance();
    const UMarburgumSessionSubsystem* Session = GameInstance ? GameInstance->GetSubsystem<UMarburgumSessionSubsystem>() : nullptr;
    const FString Key = Session ? Session->GetCharacterKey() : FString();

    // Identity card: monogram, name, body, and where the character is kept.
    const bool bNamed = State && State->HasIdentity();
    UBorder* Monogram = B.Surface(ESurfaceKind::Sunken, Pad(T.Space.M),
        B.Text(bNamed ? Initials(State->GetFirstName(), State->GetLastName()) : FText::AsCultureInvariant(TEXT("—")), ETypeRole::Display, T.Color.Accent), false);
    Monogram->SetBrush(RoundedBrush(T.Color.Sunken, 48.f, T.Color.Hairline, T.BorderWidth));
    FBuilder::Align(Monogram, HAlign_Center, VAlign_Center);
    USizeBox* MonogramSize = WidgetTree->ConstructWidget<USizeBox>();
    MonogramSize->SetWidthOverride(96.f);
    MonogramSize->SetHeightOverride(96.f);
    MonogramSize->SetContent(Monogram);
    FBuilder::Stack(IdentityBox, MonogramSize, T.Space.L);
    if (UVerticalBoxSlot* MonogramSlot = Cast<UVerticalBoxSlot>(IdentityBox->GetSlots().Last()))
    {
        MonogramSlot->SetHorizontalAlignment(HAlign_Left);
    }
    FBuilder::Stack(IdentityBox, B.Text(LOCTEXT("Overline", "CHARACTER"), ETypeRole::Overline, T.Color.Accent), T.Space.XXS);
    UTextBlock* Name = B.Text(bNamed
        ? FText::Format(LOCTEXT("FullName", "{0} {1}"), FText::FromString(State->GetFirstName()), FText::FromString(State->GetLastName()))
        : LOCTEXT("Unnamed", "Development character"), ETypeRole::PlaceTitle, T.Color.TextPrimary);
    Name->SetAutoWrapText(true);
    FBuilder::Stack(IdentityBox, Name, T.Space.S);
    if (bNamed)
    {
        const MarburgumII::Accounts::FCharacterModel* Model = MarburgumII::Accounts::FindModel(State->GetModel());
        FBuilder::Stack(IdentityBox, B.Field(LOCTEXT("Body", "Body"), Model ? FText::FromString(Model->NameEn) : FText::FromName(State->GetModel())),
            T.Space.XS);
        FBuilder::Stack(IdentityBox, B.Field(LOCTEXT("Kept", "Kept"), LOCTEXT("WithAccount", "With your account")), T.Space.L);
    }
    else
    {
        UTextBlock* Dev = B.Text(Key.IsEmpty() ? LOCTEXT("NoKey", "No character on this connection.")
            : LOCTEXT("DevKey", "A test identity for development, not an account."), ETypeRole::Caption, T.Color.TextSecondary);
        Dev->SetAutoWrapText(true);
        FBuilder::Stack(IdentityBox, Dev, T.Space.L);
    }

    FBuilder::Stack(IdentityBox, B.SectionTitle(LOCTEXT("PlaceTitle", "WHEREABOUTS"), T.Color.Accent), T.Space.M);
    if (!State || !State->IsLocationKnown())
    {
        FBuilder::Stack(IdentityBox, B.Field(LOCTEXT("Area", "Area"), LOCTEXT("AreaUnknown", "Not yet known")), 0.f);
    }
    else
    {
        const MarburgumII::Places::FDistrict* District = State->GetPlace().District.IsEmpty() ? nullptr
            : MarburgumII::Places::Get().FindDistrictById(State->GetPlace().District);
        if (District)
        {
            FBuilder::Stack(IdentityBox, B.Field(LOCTEXT("Quarter", "Quarter"), FText::FromString(MarburgumII::Gameplay::Localized(District->DisplayName))), T.Space.XS);
        }
        if (!State->GetPlace().Street.IsEmpty())
        {
            FBuilder::Stack(IdentityBox, B.Field(LOCTEXT("Street", "Street"), FText::FromString(State->GetPlace().Street)), T.Space.XS);
        }
        const FAreaDefinition* Area = State->GetAreaId().IsNone() ? nullptr : FindArea(State->GetAreaId().ToString());
        const FText AreaName = Area ? FText::FromString(MarburgumII::Gameplay::Localized(Area->DisplayName))
            : State->GetAreaId().IsNone() ? FText::FromString(OutsideAreaName) : FText::FromName(State->GetAreaId());
        FBuilder::Stack(IdentityBox, B.Field(LOCTEXT("Area", "Area"), AreaName), T.Space.XS);
    }
    FDateTime Utc;
    if (AMarburgumGameState::WorldUtc(GetWorld(), Utc))
    {
        const MarburgumII::Clock::FCivil Now = MarburgumII::Clock::MarburgumAt(Utc);
        FBuilder::Stack(IdentityBox, B.Field(LOCTEXT("Date", "Date"), GameDate(Now.Year, Now.Month, Now.Day, Now.Weekday, true)), T.Space.XS);
        FBuilder::Stack(IdentityBox, B.Field(LOCTEXT("Time", "Time"), GameTime(Now.Hour, Now.Minute)), 0.f);
    }

    // Condition: the body as the server simulates it.
    UVerticalBox* Body = Section(B, Sections, LOCTEXT("ConditionTitle", "CONDITION"));
    if (!State || !State->IsConditionKnown())
    {
        FBuilder::Stack(Body, B.StateCard(LOCTEXT("WaitingTitle", "Waiting for the server"),
            LOCTEXT("WaitingBody", "Condition appears once the server has restored this character."), ETone::Info), 0.f);
    }
    else
    {
        auto MeterLine = [&](EMarburgumGlyph Icon, const FText& Label, UMarburgumMeter*& OutMeter, UTextBlock*& OutValue)
        {
            UHorizontalBox* Head = B.Row();
            FBuilder::Inline(Head, B.GlyphText(Icon, Label, ETypeRole::Label, T.Color.TextPrimary), T.Space.M, true);
            OutValue = B.Text(FText::GetEmpty(), ETypeRole::Numeric, T.Color.TextPrimary);
            FBuilder::Inline(Head, OutValue, 0.f);
            FBuilder::Stack(Body, Head, T.Space.XS);
            OutMeter = B.Bar(ETone::Success, 1.f, 10.f, 10);
            FBuilder::Stack(Body, OutMeter, T.Space.L);
        };
        UMarburgumMeter* Meter = nullptr;
        UTextBlock* Value = nullptr;
        MeterLine(EMarburgumGlyph::Heart, LOCTEXT("Vitality", "Vitality"), Meter, Value);
        VitalityMeter = Meter;
        VitalityValue = Value;
        MeterLine(EMarburgumGlyph::Breath, LOCTEXT("Stamina", "Stamina"), Meter, Value);
        StaminaMeter = Meter;
        StaminaValue = Value;
        UTextBlock* Breath = nullptr;
        FBuilder::Stack(Body, B.Field(LOCTEXT("Breath", "State"), FText::GetEmpty(), &Breath), T.Space.S);
        BreathValue = Breath;
        UTextBlock* Note = B.Text(LOCTEXT("ConditionNote",
            "Saved by the server when you leave. Vitality comes back in full when you get up after going down."), ETypeRole::Caption, T.Color.TextMuted);
        Note->SetAutoWrapText(true);
        FBuilder::Stack(Body, Note, 0.f);
        UpdateCondition();
    }

    // The saved record in brief, each line a way to the screen that holds the details.
    UVerticalBox* Record = Section(B, Sections, LOCTEXT("RecordTitle", "SAVED RECORD"));
    if (!State || !State->IsInventoryKnown() || !State->AreMissionsKnown())
    {
        FBuilder::Stack(Record, B.Field(LOCTEXT("Record", "Items and missions"), LOCTEXT("RecordWaiting", "Waiting for the server")), 0.f);
    }
    else if (!State->HasCharacter())
    {
        FBuilder::Stack(Record, B.StateCard(LOCTEXT("NoCharacterTitle", "Nothing is saved"),
            LOCTEXT("NoCharacterBody", "Join with a character to hold items and take missions."), ETone::Warning), 0.f);
    }
    else
    {
        int32 Carried = 0;
        for (const FMarburgumItemStack& Stack : State->GetInventory())
        {
            Carried += Stack.Quantity;
        }
        int32 Open = 0;
        for (const FMarburgumMissionState& Mission : State->GetMissions())
        {
            Open += Mission.bCompleted ? 0 : 1;
        }
        UMarburgumUISubsystem* UI = GameInstance ? GameInstance->GetSubsystem<UMarburgumUISubsystem>() : nullptr;
        auto Link = [&](EMarburgumGlyph Icon, const FText& Label, const FText& Value, const TCHAR* KeyName, TFunction<void()> Open)
        {
            UHorizontalBox* Line = B.Row();
            FBuilder::Inline(Line, B.GlyphText(Icon, Label, ETypeRole::Label, T.Color.TextPrimary), T.Space.M, true);
            FBuilder::Inline(Line, B.Text(Value, ETypeRole::Numeric, T.Color.TextPrimary), T.Space.L);
            FBuilder::Inline(Line, B.KeyCap(FText::FromString(KeyName), false), 0.f);
            UMarburgumSelectable* Row = B.Selectable(Line, FMargin(T.Space.L, T.Space.M));
            Row->OnActivated.BindLambda(MoveTemp(Open));
            FBuilder::Stack(Record, Row, T.Space.XS);
            if (!FirstLink)
            {
                FirstLink = Row;
            }
        };
        TWeakObjectPtr<UMarburgumUISubsystem> WeakUI = UI;
        Link(EMarburgumGlyph::Inventory, LOCTEXT("Carried", "Carried"),
            FText::Format(LOCTEXT("ItemCount", "{0} {0}|plural(one=item,other=items)"), FText::AsNumber(Carried)), TEXT("I"),
            [WeakUI]() { if (WeakUI.IsValid()) { WeakUI->ToggleInventory(); } });
        Link(EMarburgumGlyph::Journal, LOCTEXT("Missions", "Missions under way"), FText::AsNumber(Open), TEXT("J"),
            [WeakUI]() { if (WeakUI.IsValid()) { WeakUI->ToggleJournal(); } });
        if (State->IsProgressKnown())
        {
            Link(EMarburgumGlyph::Coin, LOCTEXT("Money", "Money"), FText::AsNumber(State->GetMoney()), TEXT("K"),
                [WeakUI]() { if (WeakUI.IsValid()) { WeakUI->ToggleSkills(); } });
        }
    }

    if (State && State->IsProgressKnown() && State->HasCharacter())
    {
        UVerticalBox* SkillBox = Section(B, Sections, LOCTEXT("SkillsTitle", "SKILLS"));
        UHorizontalBox* Line = B.Row();
        for (const FSkillDefinition& Skill : MarburgumII::Gameplay::Skills)
        {
            const int64 Level = MarburgumII::SkillCurve::LevelForXp(State->GetSkillXp(FName(Skill.Id)));
            UVerticalBox* Cell = B.Column();
            FBuilder::Stack(Cell, B.Text(FText::AsNumber(Level), ETypeRole::NumericLarge, Level > 0 ? T.Color.TextPrimary : T.Color.TextMuted), 0.f);
            FBuilder::Stack(Cell, B.Text(Caps(FString(MarburgumII::Gameplay::Localized(Skill.DisplayName))), ETypeRole::Overline, T.Color.TextSecondary), 0.f);
            FBuilder::Inline(Line, Cell, T.Space.XL);
        }
        FBuilder::Stack(SkillBox, Line, 0.f);
    }

    UVerticalBox* Gear = Section(B, Sections, LOCTEXT("EquipmentTitle", "EQUIPMENT"));
    UTextBlock* GearNote = B.Text(LOCTEXT("EquipmentBody", "Clothing and tools in hand arrive with the systems that use them; nothing is worn or held "
        "in this build."), ETypeRole::Caption, T.Color.TextMuted);
    GearNote->SetAutoWrapText(true);
    FBuilder::Stack(Gear, GearNote, 0.f);
}

#undef LOCTEXT_NAMESPACE
