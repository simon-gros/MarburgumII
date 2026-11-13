#include "UI/MarburgumCharacterPanel.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/SafeZone.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/GameInstance.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumPlaces.h"
#include "MarburgumPlayerState.h"
#include "Session/MarburgumSessionSubsystem.h"
#include "UI/MarburgumUIBuilder.h"

#define LOCTEXT_NAMESPACE "MarburgumCharacterPanel"

using namespace MarburgumUI;

namespace
{
    constexpr float PanelWidth = 520.f;

    UVerticalBox* Section(const FBuilder& B, UVerticalBox* Parent, const FText& Title)
    {
        const FTokens& T = Tokens();
        FBuilder::Stack(Parent, B.Text(Title, ETypeRole::Overline, T.Color.TextSecondary), T.Space.S);
        UVerticalBox* Body = B.Column();
        FBuilder::Stack(Parent, Body, T.Space.L);
        return Body;
    }

    FText Points(int32 Value, int32 Max)
    {
        return FText::Format(LOCTEXT("Points", "{0} / {1}"), FText::AsNumber(Value), FText::AsNumber(Max));
    }
}

void UMarburgumCharacterPanel::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);

    UVerticalBox* Column = B.Column();
    UHorizontalBox* Header = B.Row();
    UVerticalBox* Titles = B.Column();
    FBuilder::Stack(Titles, B.Text(LOCTEXT("Overline", "CHARACTER"), ETypeRole::Overline, T.Color.Accent), T.Space.XXS);
    NameLabel = B.Text(FText::GetEmpty(), ETypeRole::Title);
    // Development keys can be long; they wrap rather than run under the close hint.
    NameLabel->SetAutoWrapText(true);
    NameLabel->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
    FBuilder::Stack(Titles, NameLabel, 0.f);
    FBuilder::Inline(Header, Titles, T.Space.M, true);
    FBuilder::Inline(Header, B.Text(LOCTEXT("Close", "[C] Close"), ETypeRole::Caption, T.Color.TextMuted), 0.f, false, false);
    FBuilder::Stack(Column, Header, T.Space.S);

    IdentityLabel = B.Text(FText::GetEmpty(), ETypeRole::Caption);
    IdentityLabel->SetAutoWrapText(true);
    FBuilder::Stack(Column, IdentityLabel, T.Space.M);
    FBuilder::Stack(Column, B.Rule(T.Color.Border, 1.f), T.Space.L);

    Sections = B.Column();
    FBuilder::Stack(Column, Sections, 0.f);

    UBorder* Panel = B.Surface(ESurfaceKind::Panel, Pad(T.Space.XL), Column);
    USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>();
    Width->SetWidthOverride(PanelWidth);
    Width->SetContent(Panel);

    UBorder* Anchor = B.Surface(ESurfaceKind::Base, FMargin(0.f), Width, false);
    Anchor->SetBrushColor(FLinearColor::Transparent);
    FBuilder::Align(Anchor, HAlign_Center, VAlign_Center);

    USafeZone* Safe = WidgetTree->ConstructWidget<USafeZone>();
    Safe->SetContent(Anchor);
    WidgetTree->RootWidget = Safe;
    SetVisibility(ESlateVisibility::Collapsed);
}

void UMarburgumCharacterPanel::NativeConstruct()
{
    Super::NativeConstruct();
    for (FMarburgumPlayerStateChanged* Event : { &AMarburgumPlayerState::OnConditionChanged(), &AMarburgumPlayerState::OnAreaChanged(),
             &AMarburgumPlayerState::OnMissionsChanged() })
    {
        Handles.Add(Event->AddUObject(this, &UMarburgumCharacterPanel::HandleStateChanged));
    }
    Handles.Add(AMarburgumPlayerState::OnInventoryChanged().AddUObject(this, &UMarburgumCharacterPanel::HandleStateChanged));
    Rebuild();
}

void UMarburgumCharacterPanel::NativeDestruct()
{
    AMarburgumPlayerState::OnConditionChanged().Remove(Handles[0]);
    AMarburgumPlayerState::OnAreaChanged().Remove(Handles[1]);
    AMarburgumPlayerState::OnMissionsChanged().Remove(Handles[2]);
    AMarburgumPlayerState::OnInventoryChanged().Remove(Handles[3]);
    Handles.Reset();
    Super::NativeDestruct();
}

void UMarburgumCharacterPanel::Toggle()
{
    SetVisibility(IsOpen() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    if (IsOpen())
    {
        Rebuild();
    }
}

bool UMarburgumCharacterPanel::IsOpen() const
{
    return GetVisibility() != ESlateVisibility::Collapsed;
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
        Rebuild();
    }
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
    StaminaValue = nullptr;

    const APlayerController* Owner = GetOwningPlayer();
    const AMarburgumPlayerState* State = Owner ? Owner->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    const UGameInstance* GameInstance = GetGameInstance();
    const UMarburgumSessionSubsystem* Session = GameInstance ? GameInstance->GetSubsystem<UMarburgumSessionSubsystem>() : nullptr;
    const FString Key = Session ? Session->GetCharacterKey() : FString();
    NameLabel->SetText(Key.IsEmpty() ? LOCTEXT("Nameless", "No character") : FText::FromString(Key));
    IdentityLabel->SetText(Key.IsEmpty()
        ? LOCTEXT("NoKey", "No development character on this connection")
        : LOCTEXT("Key", "Development character  ·  a test identity, not an account. Portraits and names arrive with character creation."));

    // Condition: the body as the server simulates it.
    UVerticalBox* Body = Section(B, Sections, LOCTEXT("ConditionTitle", "CONDITION"));
    if (!State || !State->IsConditionKnown())
    {
        FBuilder::Stack(Body, B.StateCard(LOCTEXT("WaitingTitle", "Waiting for the server"),
            LOCTEXT("WaitingBody", "Condition appears once the server has restored this character."), ETone::Info), 0.f);
    }
    else
    {
        const FMarburgumCondition& Condition = State->GetCondition();
        FBuilder::Stack(Body, B.Field(LOCTEXT("Vitality", "Vitality"), Points(Condition.Vitality, Condition.MaxVitality)), T.Space.XS);
        UTextBlock* Value = nullptr;
        FBuilder::Stack(Body, B.Field(LOCTEXT("Stamina", "Stamina"), Points(Condition.Stamina, Condition.MaxStamina), &Value), T.Space.XS);
        StaminaValue = Value;
        const FText Breath = Condition.bWinded
            ? FText::Format(LOCTEXT("Winded", "Winded: walking until stamina is back to {0}"), FText::AsNumber(FMath::RoundToInt(RunResumeStamina)))
            : Condition.bRunning ? LOCTEXT("Running", "Running")
            : Condition.Stamina < Condition.MaxStamina ? LOCTEXT("Recovering", "Getting breath back")
            : LOCTEXT("Rested", "Rested");
        FBuilder::Stack(Body, B.Field(LOCTEXT("Breath", "Breath"), Breath), T.Space.S);
        UTextBlock* Note = B.Text(LOCTEXT("ConditionNote",
            "Saved by the server when you leave. Nothing in this build injures you yet, so vitality stays full."),
            ETypeRole::Caption, T.Color.TextMuted);
        Note->SetAutoWrapText(true);
        FBuilder::Stack(Body, Note, 0.f);
    }

    // Whereabouts: the area the server placed the pawn in.
    UVerticalBox* Place = Section(B, Sections, LOCTEXT("PlaceTitle", "WHEREABOUTS"));
    if (!State || !State->IsLocationKnown())
    {
        FBuilder::Stack(Place, B.Field(LOCTEXT("Area", "Area"), LOCTEXT("AreaUnknown", "Not yet known")), 0.f);
    }
    else
    {
        const FAreaDefinition* Area = State->GetAreaId().IsNone() ? nullptr : FindArea(State->GetAreaId().ToString());
        const FText Name = Area ? FText::FromString(Area->DisplayName)
            : State->GetAreaId().IsNone() ? FText::FromString(OutsideAreaName) : FText::FromName(State->GetAreaId());
        FBuilder::Stack(Place, B.Field(LOCTEXT("Area", "Area"), Name), 0.f);
        // The register quarter or village and the street (ADR-063); none inside a named area or off every street.
        const MarburgumII::Places::FDistrict* District = State->GetPlace().District.IsEmpty() ? nullptr
            : MarburgumII::Places::Get().FindDistrictById(State->GetPlace().District);
        if (District)
        {
            FBuilder::Stack(Place, B.Field(LOCTEXT("Quarter", "Quarter"), FText::FromString(District->DisplayName)), 0.f);
        }
        if (!State->GetPlace().Street.IsEmpty())
        {
            FBuilder::Stack(Place, B.Field(LOCTEXT("Street", "Street"), FText::FromString(State->GetPlace().Street)), 0.f);
        }
    }

    // Custody and missions: summaries; the Inventory (I) and Journal (J) hold the details.
    UVerticalBox* Record = Section(B, Sections, LOCTEXT("RecordTitle", "SAVED RECORD"));
    if (!State || !State->IsInventoryKnown() || !State->AreMissionsKnown())
    {
        FBuilder::Stack(Record, B.Field(LOCTEXT("Record", "Items and missions"), LOCTEXT("RecordWaiting", "Waiting for the server")), 0.f);
    }
    else if (!State->HasCharacter())
    {
        FBuilder::Stack(Record, B.StateCard(LOCTEXT("NoCharacterTitle", "Nothing is saved"),
            LOCTEXT("NoCharacterBody", "Join with a development character key to hold items and take missions."), ETone::Warning), 0.f);
    }
    else
    {
        int32 Carried = 0;
        for (const FMarburgumItemStack& Stack : State->GetInventory())
        {
            Carried += Stack.Quantity;
        }
        FBuilder::Stack(Record, B.Field(LOCTEXT("Carried", "Carried  ·  [I]"), FText::Format(
            LOCTEXT("ItemCount", "{0} {0}|plural(one=item,other=items)"), FText::AsNumber(Carried))), T.Space.XS);
        FBuilder::Stack(Record, B.Field(LOCTEXT("Missions", "Missions under way  ·  [J]"), FText::AsNumber(State->GetMissions().Num())), 0.f);
    }

    UVerticalBox* Gear = Section(B, Sections, LOCTEXT("EquipmentTitle", "EQUIPMENT"));
    FBuilder::Stack(Gear, B.StateCard(LOCTEXT("EquipmentNone", "Not part of this build"),
        LOCTEXT("EquipmentBody", "Clothing and tools in hand arrive with the systems that use them."), ETone::Neutral), 0.f);
}

#undef LOCTEXT_NAMESPACE
