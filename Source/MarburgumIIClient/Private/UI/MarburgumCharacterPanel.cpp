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
#include "Components/WrapBox.h"
#include "Engine/GameInstance.h"
#include "MarburgumAccountContract.h"
#include "MarburgumCharacterSheet.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumGameState.h"
#include "MarburgumPlaces.h"
#include "MarburgumPlayerController.h"
#include "MarburgumPlayerState.h"
#include "MarburgumSkillCurve.h"
#include "MarburgumWorldClock.h"
#include "Session/MarburgumSessionSubsystem.h"
#include "UI/MarburgumCharacterPreview.h"
#include "UI/MarburgumUIBuilder.h"
#include "UI/MarburgumUISubsystem.h"

#define LOCTEXT_NAMESPACE "MarburgumCharacterPanel"

using namespace MarburgumUI;

/** What one rebuild of the sheet reads, gathered once. */
class FMarburgumSheetContext
{
public:
    FBuilder& B;
    const AMarburgumPlayerState* State = nullptr;
    FString Key;
    UGameInstance* GameInstance = nullptr;
};

namespace
{
    // Card widths in layout units: two cards side by side fit the shell's widest page beside the identity card; at large
    // text or a narrow window the wrap box puts them under each other instead of squeezing them.
    constexpr float IdentityWidth = 380.f;
    constexpr float WideCard = 560.f;
    constexpr float NarrowCard = 420.f;
    constexpr float MinCard = 300.f;
    constexpr float PreviewHeight = 340.f;

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

    UVerticalBox* Card(const FBuilder& B, const FText& Overline, const FText& Title, UBorder*& OutCard)
    {
        const FTokens& T = Tokens();
        UVerticalBox* Body = B.Column();
        FBuilder::Stack(Body, B.Text(Overline, ETypeRole::Overline, T.Color.Accent), T.Space.XXS);
        if (!Title.IsEmpty())
        {
            UTextBlock* Heading = B.Text(Title, ETypeRole::Subheading, T.Color.TextPrimary);
            Heading->SetAutoWrapText(true);
            FBuilder::Stack(Body, Heading, T.Space.L);
        }
        OutCard = B.Surface(ESurfaceKind::Panel, Pad(T.Space.XL), Body);
        return Body;
    }

    UTextBlock* Caption(const FBuilder& B, const FText& Text, const FLinearColor& Color)
    {
        UTextBlock* Line = B.Text(Text, ETypeRole::Caption, Color);
        Line->SetAutoWrapText(true);
        return Line;
    }

    /** The sheet's word for a slot's state; never colour alone. */
    struct FSlotLook
    {
        FText State;
        ETone Tone = ETone::Neutral;
        FText Content;
        FText Tooltip;
    };
}

void UMarburgumCharacterPanel::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);

    Cards = WidgetTree->ConstructWidget<UWrapBox>();
    Cards->SetInnerSlotPadding(FVector2D(T.Space.XL, T.Space.XL));
    Scroller = B.Scroll(Cards);
    WidgetTree->RootWidget = Scroller;

    // The identity card is made once and stays first: the body in it is the creator's stage (keyed so it never lights the
    // city, ADR-085), and moving that widget between rebuilt cards would tear its stage down and spawn it again.
    Preview = CreateWidget<UMarburgumCharacterPreview>(this, UMarburgumCharacterPreview::StaticClass());
    Preview->SetWorldSafe(true);
    USizeBox* Stage = WidgetTree->ConstructWidget<USizeBox>();
    Stage->SetHeightOverride(PreviewHeight);
    Stage->SetContent(Preview);
    PreviewFrame = B.Surface(ESurfaceKind::Sunken, Pad(T.Space.XS), Stage, false);
    PreviewFrame->SetBrush(RoundedBrush(T.Color.Sunken.CopyWithNewOpacity(0.6f), T.Radius.Large, T.Color.Hairline, T.BorderWidth));
    UVerticalBox* Identity = B.Column();
    FBuilder::Stack(Identity, PreviewFrame, T.Space.L);
    IdentityInfo = B.Column();
    FBuilder::Stack(Identity, IdentityInfo, 0.f);
    Cards->AddChildToWrapBox(B.Bounded(B.Surface(ESurfaceKind::Panel, Pad(T.Space.XL), Identity), IdentityWidth, MinCard));
    SetVisibility(ESlateVisibility::Collapsed);
}

void UMarburgumCharacterPanel::NativeConstruct()
{
    Super::NativeConstruct();
    for (FMarburgumPlayerStateChanged* Event : { &AMarburgumPlayerState::OnAreaChanged(), &AMarburgumPlayerState::OnMissionsChanged(),
             &AMarburgumPlayerState::OnInventoryChanged(), &AMarburgumPlayerState::OnProgressChanged(), &AMarburgumPlayerState::OnIdentityChanged(),
             &AMarburgumPlayerState::OnAttributesChanged() })
    {
        Handles.Add(Event->AddUObject(this, &UMarburgumCharacterPanel::HandleStateChanged));
    }
    ConditionHandle = AMarburgumPlayerState::OnConditionChanged().AddUObject(this, &UMarburgumCharacterPanel::HandleConditionChanged);
    BindReceipts();
    Rebuild();
}

void UMarburgumCharacterPanel::NativeDestruct()
{
    FMarburgumPlayerStateChanged* Events[] = { &AMarburgumPlayerState::OnAreaChanged(), &AMarburgumPlayerState::OnMissionsChanged(),
        &AMarburgumPlayerState::OnInventoryChanged(), &AMarburgumPlayerState::OnProgressChanged(), &AMarburgumPlayerState::OnIdentityChanged(),
        &AMarburgumPlayerState::OnAttributesChanged() };
    for (int32 Index = 0; Index < Handles.Num() && Index < UE_ARRAY_COUNT(Events); ++Index)
    {
        Events[Index]->Remove(Handles[Index]);
    }
    Handles.Reset();
    AMarburgumPlayerState::OnConditionChanged().Remove(ConditionHandle);
    if (AMarburgumPlayerController* Source = ReceiptSource.Get())
    {
        Source->OnInteractionReceipt.Remove(ReceiptHandle);
    }
    ReceiptSource.Reset();
    if (Preview)
    {
        Preview->SetActive(false);
    }
    Super::NativeDestruct();
}

void UMarburgumCharacterPanel::BindReceipts()
{
    AMarburgumPlayerController* Controller = Cast<AMarburgumPlayerController>(GetOwningPlayer());
    if (Controller == ReceiptSource.Get())
    {
        return;
    }
    if (AMarburgumPlayerController* Old = ReceiptSource.Get())
    {
        Old->OnInteractionReceipt.Remove(ReceiptHandle);
    }
    ReceiptSource = Controller;
    if (Controller)
    {
        ReceiptHandle = Controller->OnInteractionReceipt.AddUObject(this, &UMarburgumCharacterPanel::HandleReceipt);
    }
}

void UMarburgumCharacterPanel::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    if (Pending.IsPending() && Pending.Expire())
    {
        SlotNotices.Add(Pending.Slot, { LOCTEXT("NoAnswer", "No answer from the server. Nothing changed that the server has confirmed; try again."),
            ETone::Warning });
        Rebuild();
    }
}

void UMarburgumCharacterPanel::SetOpen(bool bOpen)
{
    SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    if (bOpen)
    {
        BindReceipts();
        Rebuild();
        if (Scroller)
        {
            Scroller->ScrollToStart();
        }
    }
    else if (Preview)
    {
        // The stage renders only while the sheet is on screen.
        Preview->SetActive(false);
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
    const int32 Index = SlotIds.IndexOfByKey(SelectedSlot);
    if (SlotTiles.IsValidIndex(Index))
    {
        return SlotTiles[Index];
    }
    return SlotTiles.Num() > 0 ? SlotTiles[0].Get() : static_cast<UWidget*>(FirstLink.Get());
}

FText UMarburgumCharacterPanel::GetShownStaminaText() const
{
    return IsOpen() && StaminaValue ? StaminaValue->GetText() : FText::GetEmpty();
}

FText UMarburgumCharacterPanel::GetShownSlotState(FName SlotId) const
{
    const int32 Index = SlotIds.IndexOfByKey(SlotId);
    return SlotStates.IsValidIndex(Index) && SlotStates[Index] ? SlotStates[Index]->GetText() : FText::GetEmpty();
}

void UMarburgumCharacterPanel::SelectSlot(FName SlotId)
{
    SelectedSlot = SlotId;
    Rebuild();
}

bool UMarburgumCharacterPanel::RequestForTest(FName ItemId)
{
    if (!OfferedItems.Contains(ItemId))
    {
        return false;
    }
    Request(SelectedSlot, ItemId);
    return Pending.IsPending();
}

void UMarburgumCharacterPanel::HandleStateChanged(AMarburgumPlayerState* State)
{
    const APlayerController* Owner = GetOwningPlayer();
    if (IsOpen() && Owner && State == Owner->PlayerState)
    {
        const bool bHadFocus = HasFocusedDescendants();
        Rebuild();
        if (bHadFocus)
        {
            if (UWidget* Focus = GetPreferredFocus())
            {
                Focus->SetFocus();
            }
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

void UMarburgumCharacterPanel::HandleReceipt(const FMarburgumInteractionReceipt& Receipt)
{
    if (!Pending.Accept(Receipt))
    {
        return;
    }
    // The slot shows the server's answer until it changes again; the change itself arrives as replicated equipment.
    ETone Tone = ETone::Neutral;
    const FText Said = EquipReceiptText(Receipt, Tone);
    SlotNotices.Add(Pending.Slot, { Said, Tone });
    if (IsOpen())
    {
        const bool bHadFocus = HasFocusedDescendants();
        Rebuild();
        if (bHadFocus)
        {
            if (UWidget* Focus = GetPreferredFocus())
            {
                Focus->SetFocus();
            }
        }
    }
}

void UMarburgumCharacterPanel::Request(FName SlotId, FName ItemId)
{
    if (Pending.Submit(Cast<AMarburgumPlayerController>(GetOwningPlayer()), SlotId, ItemId))
    {
        SlotNotices.Remove(SlotId);
        Rebuild();
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
    if (!Cards)
    {
        return;
    }
    FBuilder B(*WidgetTree);
    // Everything after the identity card is rebuilt; the identity card's words are refilled in place.
    while (Cards->GetChildrenCount() > 1)
    {
        Cards->RemoveChildAt(Cards->GetChildrenCount() - 1);
    }
    StaminaValue = nullptr;
    VitalityValue = nullptr;
    StaminaMeter = nullptr;
    VitalityMeter = nullptr;
    BreathValue = nullptr;
    FirstLink = nullptr;
    SlotDetail = nullptr;
    SlotTiles.Reset();
    SlotStates.Reset();
    SlotIds.Reset();
    ShownScores.Reset();
    OfferedItems.Reset();

    FMarburgumSheetContext Context{ B };
    const APlayerController* Owner = GetOwningPlayer();
    Context.State = Owner ? Owner->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    Context.GameInstance = GetGameInstance();
    const UMarburgumSessionSubsystem* Session = Context.GameInstance ? Context.GameInstance->GetSubsystem<UMarburgumSessionSubsystem>() : nullptr;
    Context.Key = Session ? Session->GetCharacterKey() : FString();

    auto Add = [this](UWidget* Content, float Width)
    {
        Cards->AddChildToWrapBox(FBuilder(*WidgetTree).Bounded(Content, Width, FMath::Min(Width, MinCard)));
    };
    BuildIdentity(Context);
    Add(BuildEquipment(Context), WideCard);
    Add(BuildAttributes(Context), NarrowCard);
    Add(BuildCondition(Context), NarrowCard);
    Add(BuildRecord(Context), WideCard);
    if (SlotDetail)
    {
        ShowSlot(SlotIds.Contains(SelectedSlot) ? SelectedSlot : SlotIds.IsEmpty() ? NAME_None : SlotIds[0]);
    }
}

void UMarburgumCharacterPanel::BuildIdentity(const FMarburgumSheetContext& Context)
{
    using namespace MarburgumII::Gameplay;
    const FTokens& T = Tokens();
    FBuilder& B = Context.B;
    const AMarburgumPlayerState* State = Context.State;
    UVerticalBox* Box = IdentityInfo;
    Box->ClearChildren();

    // Who: the body in 3D when the character has one, else a monogram; then the name and how the character is kept.
    const bool bNamed = State && State->HasIdentity();
    int32 ModelIndex = INDEX_NONE;
    if (bNamed)
    {
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(MarburgumII::Accounts::CharacterModels); ++Index)
        {
            if (State->GetModel() == FName(MarburgumII::Accounts::CharacterModels[Index].Id))
            {
                ModelIndex = Index;
            }
        }
    }
    PreviewFrame->SetVisibility(ModelIndex != INDEX_NONE ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    if (ModelIndex != INDEX_NONE)
    {
        Preview->ShowModel(ModelIndex);
        Preview->SetActive(IsOpen());
    }
    else
    {
        Preview->SetActive(false);
        UBorder* Monogram = B.Surface(ESurfaceKind::Sunken, Pad(T.Space.M),
            B.Text(bNamed ? Initials(State->GetFirstName(), State->GetLastName()) : FText::AsCultureInvariant(TEXT("—")), ETypeRole::Display, T.Color.Accent), false);
        Monogram->SetBrush(RoundedBrush(T.Color.Sunken, 48.f, T.Color.Hairline, T.BorderWidth));
        FBuilder::Align(Monogram, HAlign_Center, VAlign_Center);
        USizeBox* MonogramSize = WidgetTree->ConstructWidget<USizeBox>();
        MonogramSize->SetWidthOverride(96.f);
        MonogramSize->SetHeightOverride(96.f);
        MonogramSize->SetContent(Monogram);
        FBuilder::Stack(Box, MonogramSize, T.Space.L);
        if (UVerticalBoxSlot* MonogramSlot = Cast<UVerticalBoxSlot>(Box->GetSlots().Last()))
        {
            MonogramSlot->SetHorizontalAlignment(HAlign_Left);
        }
    }
    FBuilder::Stack(Box, B.Text(LOCTEXT("Overline", "CHARACTER"), ETypeRole::Overline, T.Color.Accent), T.Space.XXS);
    UTextBlock* Name = B.Text(bNamed
        ? FText::Format(LOCTEXT("FullName", "{0} {1}"), FText::FromString(State->GetFirstName()), FText::FromString(State->GetLastName()))
        : LOCTEXT("Unnamed", "Development character"), ETypeRole::PlaceTitle, T.Color.TextPrimary);
    Name->SetAutoWrapText(true);
    FBuilder::Stack(Box, Name, T.Space.S);
    if (bNamed)
    {
        const MarburgumII::Accounts::FCharacterModel* Model = MarburgumII::Accounts::FindModel(State->GetModel());
        FBuilder::Stack(Box, B.Field(LOCTEXT("Body", "Body"), Model ? FText::FromString(Model->NameEn) : FText::FromName(State->GetModel())),
            T.Space.XS);
        FBuilder::Stack(Box, B.Field(LOCTEXT("Kept", "Kept"), LOCTEXT("WithAccount", "With your account")), T.Space.L);
    }
    else
    {
        FBuilder::Stack(Box, Caption(B, Context.Key.IsEmpty() ? LOCTEXT("NoKey", "No character on this connection.")
            : LOCTEXT("DevKey", "A test identity for development, not an account."), T.Color.TextSecondary), T.Space.L);
    }

    FBuilder::Stack(Box, B.SectionTitle(LOCTEXT("PlaceTitle", "WHEREABOUTS"), T.Color.Accent), T.Space.M);
    if (!State || !State->IsLocationKnown())
    {
        FBuilder::Stack(Box, B.Field(LOCTEXT("Area", "Area"), LOCTEXT("AreaUnknown", "Not yet known")), 0.f);
    }
    else
    {
        const MarburgumII::Places::FDistrict* District = State->GetPlace().District.IsEmpty() ? nullptr
            : MarburgumII::Places::Get().FindDistrictById(State->GetPlace().District);
        if (District)
        {
            FBuilder::Stack(Box, B.Field(LOCTEXT("Quarter", "Quarter"), FText::FromString(Localized(District->DisplayName))), T.Space.XS);
        }
        if (!State->GetPlace().Street.IsEmpty())
        {
            FBuilder::Stack(Box, B.Field(LOCTEXT("Street", "Street"), FText::FromString(State->GetPlace().Street)), T.Space.XS);
        }
        const FAreaDefinition* Area = State->GetAreaId().IsNone() ? nullptr : FindArea(State->GetAreaId().ToString());
        const FText AreaName = Area ? FText::FromString(Localized(Area->DisplayName))
            : State->GetAreaId().IsNone() ? FText::FromString(OutsideAreaName) : FText::FromName(State->GetAreaId());
        FBuilder::Stack(Box, B.Field(LOCTEXT("Area", "Area"), AreaName), T.Space.XS);
    }
    FDateTime Utc;
    if (AMarburgumGameState::WorldUtc(GetWorld(), Utc))
    {
        const MarburgumII::Clock::FCivil Now = MarburgumII::Clock::MarburgumAt(Utc);
        FBuilder::Stack(Box, B.Field(LOCTEXT("Date", "Date"), GameDate(Now.Year, Now.Month, Now.Day, Now.Weekday, true)), T.Space.XS);
        FBuilder::Stack(Box, B.Field(LOCTEXT("Time", "Time"), GameTime(Now.Hour, Now.Minute)), 0.f);
    }
}

UWidget* UMarburgumCharacterPanel::BuildEquipment(const FMarburgumSheetContext& Context)
{
    using namespace MarburgumII::Gameplay;
    const FTokens& T = Tokens();
    FBuilder& B = Context.B;
    const AMarburgumPlayerState* State = Context.State;
    UBorder* Frame = nullptr;
    UVerticalBox* Body = Card(B, LOCTEXT("EquipmentOverline", "EQUIPMENT"), LOCTEXT("EquipmentTitle", "Worn and held"), Frame);

    const bool bKnown = State && State->IsInventoryKnown();
    if (bKnown && !State->HasCharacter())
    {
        FBuilder::Stack(Body, B.StateCard(LOCTEXT("NoCharacterGearTitle", "Nothing to wear"),
            LOCTEXT("NoCharacterGearBody", "Join with a character to wear or hold things."), ETone::Warning), 0.f);
        return Frame;
    }

    // The original's two columns: clothes on the left, hands and the bag on the right.
    UHorizontalBox* Columns = B.Row();
    UVerticalBox* Left = B.Column();
    UVerticalBox* Right = B.Column();
    FBuilder::InlineTop(Columns, Left, T.Space.M, true);
    FBuilder::InlineTop(Columns, Right, 0.f, true);
    for (const FEquipmentSlotDefinition& Def : EquipmentSlots)
    {
        const FName SlotId(Def.Id);
        const FName Worn = bKnown ? State->GetEquipped(SlotId) : NAME_None;
        FSlotLook Look;
        const TPair<FText, ETone>* Notice = SlotNotices.Find(SlotId);
        if (!bKnown)
        {
            Look.State = LOCTEXT("SlotLoading", "LOADING");
            Look.Tone = ETone::Info;
            Look.Content = FText::AsCultureInvariant(TEXT("—"));
            Look.Tooltip = LOCTEXT("SlotLoadingTip", "Waiting for the server to load this character's things.");
        }
        else if (Pending.IsPending() && Pending.Slot == SlotId)
        {
            Look.State = LOCTEXT("SlotPending", "WAITING");
            Look.Tone = ETone::Info;
            Look.Content = Worn.IsNone() ? LOCTEXT("SlotEmptyContent", "Empty") : ItemName(Worn);
            Look.Tooltip = LOCTEXT("SlotPendingTip", "Asked the server; nothing changes here until it answers.");
        }
        else if (Notice && Notice->Value == ETone::Warning)
        {
            Look.State = LOCTEXT("SlotRefused", "REFUSED");
            Look.Tone = ETone::Warning;
            Look.Content = Worn.IsNone() ? LOCTEXT("SlotEmptyContent", "Empty") : ItemName(Worn);
            Look.Tooltip = Notice->Key;
        }
        else if (!Worn.IsNone())
        {
            Look.State = LOCTEXT("SlotEquipped", "IN USE");
            Look.Tone = ETone::Success;
            Look.Content = ItemName(Worn);
            Look.Tooltip = FText::Format(LOCTEXT("SlotEquippedTip", "{0} is in your {1}. It is still one of the things you carry."), ItemName(Worn),
                SlotName(SlotId).ToLower());
        }
        else if (!AnyItemFitsSlot(Def.Id))
        {
            Look.State = LOCTEXT("SlotNone", "NONE YET");
            Look.Tone = ETone::Neutral;
            Look.Content = LOCTEXT("SlotNoneContent", "Nothing to wear here yet");
            Look.Tooltip = LOCTEXT("SlotNoneTip", "No item in this build can be worn here yet. The slot is kept for the clothes and gear that will.");
        }
        else
        {
            Look.State = LOCTEXT("SlotEmpty", "EMPTY");
            Look.Tone = ETone::Neutral;
            Look.Content = LOCTEXT("SlotEmptyContent", "Empty");
            Look.Tooltip = LOCTEXT("SlotEmptyTip", "Choose the slot to see what you carry that fits it.");
        }

        UHorizontalBox* Line = B.Row();
        FBuilder::Inline(Line, B.Glyph(SlotGlyph(SlotId), Worn.IsNone() ? T.Color.TextMuted : T.Color.Accent, T.Icon.Medium), T.Space.M);
        UVerticalBox* Words = B.Column();
        FBuilder::Stack(Words, B.Text(Caps(SlotName(SlotId)), ETypeRole::Overline, T.Color.TextSecondary), T.Space.XXS);
        UTextBlock* Content = B.Text(Look.Content, ETypeRole::Label, Worn.IsNone() ? T.Color.TextMuted : T.Color.TextPrimary);
        Content->SetAutoWrapText(true);
        FBuilder::Stack(Words, Content, T.Space.XXS);
        UTextBlock* StateText = B.Text(Look.State, ETypeRole::Overline, Look.Tone == ETone::Neutral ? T.Color.TextMuted : ToneColor(Look.Tone));
        FBuilder::Stack(Words, StateText, 0.f);
        FBuilder::Inline(Line, Words, 0.f, true);
        UMarburgumSelectable* Tile = B.Selectable(Line, FMargin(T.Space.M, T.Space.S));
        Tile->SetToolTipText(Look.Tooltip);
        Tile->OnFocused.BindWeakLambda(this, [this, SlotId]() { ShowSlot(SlotId); });
        Tile->OnActivated.BindWeakLambda(this, [this, SlotId]() { ShowSlot(SlotId); });
        FBuilder::Stack(Def.bRightColumn ? Right : Left, Tile, T.Space.S);
        SlotTiles.Add(Tile);
        SlotStates.Add(StateText);
        SlotIds.Add(SlotId);
    }
    FBuilder::Stack(Body, Columns, T.Space.L);

    SlotDetail = B.Column();
    UBorder* DetailFrame = B.Surface(ESurfaceKind::Raised, Pad(T.Space.L), SlotDetail);
    FBuilder::Stack(Body, DetailFrame, T.Space.M);
    FBuilder::Stack(Body, Caption(B, LOCTEXT("EquipmentNote",
        "Kept by the server with your inventory: a thing in a slot is still one of the things you carry, never a second copy. "
        "Nothing worn or held changes an attribute yet."), T.Color.TextMuted), 0.f);
    return Frame;
}

void UMarburgumCharacterPanel::ShowSlot(FName SlotId)
{
    using namespace MarburgumII::Gameplay;
    SelectedSlot = SlotId;
    for (int32 Index = 0; Index < SlotTiles.Num(); ++Index)
    {
        SlotTiles[Index]->SetSelected(SlotIds[Index] == SlotId);
    }
    if (!SlotDetail)
    {
        return;
    }
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);
    SlotDetail->ClearChildren();
    OfferedItems.Reset();
    const APlayerController* Owner = GetOwningPlayer();
    const AMarburgumPlayerState* State = Owner ? Owner->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    if (SlotId.IsNone() || !State || !State->IsInventoryKnown())
    {
        FBuilder::Stack(SlotDetail, Caption(B, LOCTEXT("DetailWaiting", "Waiting for the server."), T.Color.TextSecondary), 0.f);
        return;
    }
    const FName Worn = State->GetEquipped(SlotId);
    UHorizontalBox* Head = B.Row();
    FBuilder::Inline(Head, B.Glyph(SlotGlyph(SlotId), T.Color.Accent, T.Icon.Medium), T.Space.S);
    FBuilder::Inline(Head, B.Text(SlotName(SlotId), ETypeRole::BodyStrong, T.Color.TextPrimary), 0.f, true);
    FBuilder::Stack(SlotDetail, Head, T.Space.S);
    FBuilder::Stack(SlotDetail, B.Field(LOCTEXT("InSlot", "In it"), Worn.IsNone() ? LOCTEXT("SlotEmptyContent", "Empty") : ItemName(Worn)), T.Space.M);

    const bool bBusy = Pending.IsPending();
    auto Action = [&](const FText& Label, FName ItemId, bool bEnabled, const FText& Why)
    {
        UHorizontalBox* Line = B.Row();
        FBuilder::Inline(Line, B.Glyph(ItemId.IsNone() ? EMarburgumGlyph::ChevronDown : EMarburgumGlyph::ChevronRight,
            bEnabled ? T.Color.Accent : T.Color.TextMuted, T.Icon.Small), T.Space.S);
        UVerticalBox* Words = B.Column();
        UTextBlock* Text = B.Text(Label, ETypeRole::Label, bEnabled ? T.Color.TextPrimary : T.Color.TextMuted);
        Text->SetAutoWrapText(true);
        FBuilder::Stack(Words, Text, 0.f);
        if (!Why.IsEmpty())
        {
            FBuilder::Stack(Words, Caption(B, Why, T.Color.TextMuted), 0.f);
        }
        FBuilder::Inline(Line, Words, 0.f, true);
        if (!bEnabled)
        {
            FBuilder::Stack(SlotDetail, Line, T.Space.S);
            return;
        }
        UMarburgumSelectable* Row = B.Selectable(Line, FMargin(T.Space.M, T.Space.S));
        Row->OnActivated.BindWeakLambda(this, [this, SlotId, ItemId]() { Request(SlotId, ItemId); });
        FBuilder::Stack(SlotDetail, Row, T.Space.XS);
        OfferedItems.Add(ItemId);
    };

    bool bAnyFits = false;
    for (const FMarburgumItemStack& Stack : State->GetInventory())
    {
        const FString Item = Stack.ItemId.ToString();
        if (!FitsSlot(Item, SlotId.ToString()) || Stack.ItemId == Worn)
        {
            bAnyFits |= Stack.ItemId == Worn;
            continue;
        }
        bAnyFits = true;
        const TArray<FName> Elsewhere = State->GetSlotsHolding(Stack.ItemId);
        const bool bFree = Stack.Quantity > Elsewhere.Num();
        const FText Label = Worn.IsNone() ? FText::Format(LOCTEXT("EquipItem", "{0}: {1}"), EquipVerb(SlotId), ItemName(Stack.ItemId))
            : FText::Format(LOCTEXT("SwapItem", "{0}: {1}, instead of {2}"), EquipVerb(SlotId), ItemName(Stack.ItemId), ItemName(Worn));
        Action(Label, Stack.ItemId, bFree && !bBusy,
            !bFree ? FText::Format(LOCTEXT("InUseElsewhere", "Your only one is in your {0}; put it away there first."),
                         Elsewhere.IsEmpty() ? FText::GetEmpty() : SlotName(Elsewhere[0]).ToLower())
                   : FText::GetEmpty());
    }
    if (!Worn.IsNone())
    {
        Action(FText::Format(LOCTEXT("UnequipItem", "{0}: {1}"), UnequipVerb(SlotId), ItemName(Worn)), NAME_None, !bBusy, FText::GetEmpty());
    }
    if (!bAnyFits && Worn.IsNone())
    {
        FBuilder::Stack(SlotDetail, Caption(B, AnyItemFitsSlot(SlotId.ToString())
            ? LOCTEXT("NothingCarriedFits", "You carry nothing that fits here.")
            : LOCTEXT("NothingExistsFits", "No item in this build can be worn here yet."), T.Color.TextSecondary), T.Space.S);
    }

    if (bBusy)
    {
        FBuilder::Stack(SlotDetail, B.GlyphText(EMarburgumGlyph::Clock, Pending.Slot == SlotId
            ? LOCTEXT("PendingHere", "Asked the server; waiting for its answer.")
            : FText::Format(LOCTEXT("PendingElsewhere", "Waiting for the server's answer about your {0}."), SlotName(Pending.Slot).ToLower()),
            ETypeRole::Caption, ToneColor(ETone::Info)), 0.f);
    }
    else if (const TPair<FText, ETone>* Notice = SlotNotices.Find(SlotId))
    {
        UTextBlock* Said = Caption(B, Notice->Key, ToneColor(Notice->Value));
        UHorizontalBox* Line = B.Row();
        FBuilder::Inline(Line, B.Glyph(Notice->Value == ETone::Success ? EMarburgumGlyph::Check : EMarburgumGlyph::Info, ToneColor(Notice->Value),
            T.Icon.Small), T.Space.S, false, false);
        FBuilder::Inline(Line, Said, 0.f, true);
        FBuilder::Stack(SlotDetail, Line, 0.f);
    }
}

UWidget* UMarburgumCharacterPanel::BuildAttributes(const FMarburgumSheetContext& Context)
{
    using namespace MarburgumII::CharacterSheet;
    const FTokens& T = Tokens();
    FBuilder& B = Context.B;
    const AMarburgumPlayerState* State = Context.State;
    UBorder* Frame = nullptr;
    UVerticalBox* Body = Card(B, LOCTEXT("AttributesOverline", "ATTRIBUTES"), FText::GetEmpty(), Frame);
    if (!State || !State->AreAttributesKnown())
    {
        FBuilder::Stack(Body, B.StateCard(LOCTEXT("AttributesWaitingTitle", "Waiting for the server"),
            LOCTEXT("AttributesWaitingBody", "Might, Endurance, Eye and Poise appear once the server has loaded this character."), ETone::Info), 0.f);
        return Frame;
    }
    const FMarburgumAttributes& Scores = State->GetAttributes();
    for (int32 Index = 0; Index < AttributeCount; ++Index)
    {
        const int32 Score = Scores.Get(Index);
        const int32 Mod = Modifier(Score);
        ShownScores.Add(Score);
        UHorizontalBox* Head = B.Row();
        FBuilder::Inline(Head, B.Text(AttributeName(Index), ETypeRole::BodyStrong, T.Color.TextPrimary), T.Space.M, true);
        FBuilder::Inline(Head, B.Text(FText::AsNumber(Score), ETypeRole::NumericLarge, T.Color.TextPrimary), T.Space.S);
        UBorder* ModPill = B.Pill(FText::Format(LOCTEXT("ModifierPill", "MOD {0}"), SignedModifier(Mod)),
            Mod > 0 ? ETone::Success : Mod < 0 ? ETone::Warning : ETone::Neutral);
        FBuilder::Inline(Head, ModPill, 0.f);
        FBuilder::Stack(Body, Head, T.Space.XS);
        UMarburgumMeter* Meter = B.Bar(ETone::Accent, static_cast<float>(Score) / MarburgumII::Gameplay::AttributeMaxScore, 6.f,
            MarburgumII::Gameplay::AttributeMaxScore);
        FBuilder::Stack(Body, Meter, T.Space.XS);
        // Base, equipment and other effects are kept apart; none but the base exists yet, and the line says so.
        FBuilder::Stack(Body, B.Text(FText::Format(LOCTEXT("Breakdown", "Base {0}  ·  equipment {1}  ·  other {1}  ·  total {0}"),
            FText::AsNumber(Score), SignedModifier(0)), ETypeRole::Caption, T.Color.TextSecondary), T.Space.XXS);
        FBuilder::Stack(Body, Caption(B, AttributeSummary(Index), T.Color.TextMuted), Index + 1 < AttributeCount ? T.Space.L : T.Space.M);
    }
    FBuilder::Stack(Body, Caption(B, LOCTEXT("AttributesNote",
        "Kept by the server; 10 is ordinary, 1 to 20 the range. No score changes a blow, a cast, a price, a run or a swim yet."), T.Color.TextMuted), 0.f);
    return Frame;
}

UWidget* UMarburgumCharacterPanel::BuildCondition(const FMarburgumSheetContext& Context)
{
    const FTokens& T = Tokens();
    FBuilder& B = Context.B;
    const AMarburgumPlayerState* State = Context.State;
    UBorder* Frame = nullptr;
    UVerticalBox* Body = Card(B, LOCTEXT("ConditionTitle", "CONDITION"), FText::GetEmpty(), Frame);
    if (!State || !State->IsConditionKnown())
    {
        FBuilder::Stack(Body, B.StateCard(LOCTEXT("WaitingTitle", "Waiting for the server"),
            LOCTEXT("WaitingBody", "Condition appears once the server has restored this character."), ETone::Info), 0.f);
        return Frame;
    }
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
    FBuilder::Stack(Body, Caption(B, LOCTEXT("ConditionNote",
        "Saved by the server when you leave. Vitality comes back in full when you get up after going down. Stamina is the breath a run "
        "spends; Endurance is a lasting attribute and does not move with it."), T.Color.TextMuted), 0.f);
    UpdateCondition();
    return Frame;
}

UWidget* UMarburgumCharacterPanel::BuildRecord(const FMarburgumSheetContext& Context)
{
    using namespace MarburgumII::Gameplay;
    const FTokens& T = Tokens();
    FBuilder& B = Context.B;
    const AMarburgumPlayerState* State = Context.State;
    UBorder* Frame = nullptr;
    UVerticalBox* Record = Card(B, LOCTEXT("RecordTitle", "SAVED RECORD"), FText::GetEmpty(), Frame);
    if (!State || !State->IsInventoryKnown() || !State->AreMissionsKnown())
    {
        FBuilder::Stack(Record, B.Field(LOCTEXT("Record", "Items and missions"), LOCTEXT("RecordWaiting", "Waiting for the server")), 0.f);
        return Frame;
    }
    if (!State->HasCharacter())
    {
        FBuilder::Stack(Record, B.StateCard(LOCTEXT("NoCharacterTitle", "Nothing is saved"),
            LOCTEXT("NoCharacterBody", "Join with a character to hold items and take missions."), ETone::Warning), 0.f);
        return Frame;
    }
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
    UMarburgumUISubsystem* UI = Context.GameInstance ? Context.GameInstance->GetSubsystem<UMarburgumUISubsystem>() : nullptr;
    auto Link = [&](EMarburgumGlyph Icon, const FText& Label, const FText& Value, const TCHAR* KeyName, TFunction<void()> OpenScreen)
    {
        UHorizontalBox* Line = B.Row();
        FBuilder::Inline(Line, B.GlyphText(Icon, Label, ETypeRole::Label, T.Color.TextPrimary), T.Space.M, true);
        FBuilder::Inline(Line, B.Text(Value, ETypeRole::Numeric, T.Color.TextPrimary), T.Space.L);
        FBuilder::Inline(Line, B.KeyCap(FText::FromString(KeyName), false), 0.f);
        UMarburgumSelectable* Row = B.Selectable(Line, FMargin(T.Space.L, T.Space.M));
        Row->OnActivated.BindLambda(MoveTemp(OpenScreen));
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

        // Skill levels in brief, the way the Skills screen counts them.
        FBuilder::Stack(Record, B.Spacer(1.f, T.Space.M), 0.f);
        FBuilder::Stack(Record, B.SectionTitle(LOCTEXT("SkillsTitle", "SKILLS"), T.Color.Accent), T.Space.M);
        UWrapBox* Line = WidgetTree->ConstructWidget<UWrapBox>();
        Line->SetInnerSlotPadding(FVector2D(T.Space.XL, T.Space.S));
        for (const FSkillDefinition& Skill : Skills)
        {
            const int64 Level = MarburgumII::SkillCurve::LevelForXp(State->GetSkillXp(FName(Skill.Id)));
            UVerticalBox* Cell = B.Column();
            FBuilder::Stack(Cell, B.Text(FText::AsNumber(Level), ETypeRole::NumericLarge, Level > 0 ? T.Color.TextPrimary : T.Color.TextMuted), 0.f);
            FBuilder::Stack(Cell, B.Text(Caps(FString(Localized(Skill.DisplayName))), ETypeRole::Overline, T.Color.TextSecondary), 0.f);
            Line->AddChildToWrapBox(Cell);
        }
        FBuilder::Stack(Record, Line, 0.f);
    }
    return Frame;
}

#undef LOCTEXT_NAMESPACE
