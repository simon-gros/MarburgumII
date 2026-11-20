#include "UI/MarburgumSkillsPanel.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumGameplayCatalogue.h"
#include "MarburgumPlayerState.h"
#include "MarburgumSkillCurve.h"
#include "UI/MarburgumUIBuilder.h"

#define LOCTEXT_NAMESPACE "MarburgumSkillsPanel"

using namespace MarburgumUI;

namespace
{
    EMarburgumGlyph SkillGlyph(const FString& Id)
    {
        if (Id == TEXT("fishing")) return EMarburgumGlyph::Fish;
        if (Id == TEXT("photography")) return EMarburgumGlyph::Eye;
        if (Id == TEXT("housing")) return EMarburgumGlyph::Door;
        if (Id == TEXT("trading")) return EMarburgumGlyph::Trade;
        if (Id == TEXT("reputation")) return EMarburgumGlyph::Star;
        return EMarburgumGlyph::Skill;
    }

    /** What raises the skill in this build, from the server's actual awards; nothing is promised beyond them. */
    FText SkillSource(const FString& Id)
    {
        if (Id == TEXT("fishing")) return LOCTEXT("FishingSource", "Landing a fish at a Drava fishing spot. Bigger species give more.");
        if (Id == TEXT("trading")) return LOCTEXT("TradingSource", "Your first purchase at each shopkeeper's counter, and some mission decisions.");
        if (Id == TEXT("reputation")) return LOCTEXT("ReputationSource", "Decisions at the end of missions. Reputation can be lost as well as earned.");
        return LOCTEXT("NoSource", "Nothing in this build raises it yet; it is kept for the systems that will.");
    }

    FText SkillName(const MarburgumII::Gameplay::FSkillDefinition& Skill)
    {
        return FText::FromString(Skill.DisplayName);
    }

    struct FSkillView
    {
        int64 Xp = 0;
        int64 Level = 0;
        int64 Into = 0;
        int64 Next = 0;
    };

    FSkillView ViewOf(const AMarburgumPlayerState& State, const MarburgumII::Gameplay::FSkillDefinition& Skill)
    {
        using namespace MarburgumII;
        FSkillView View;
        View.Xp = State.GetSkillXp(FName(Skill.Id));
        View.Level = SkillCurve::LevelForXp(View.Xp);
        View.Into = SkillCurve::XpIntoLevel(View.Xp);
        View.Next = SkillCurve::XpToNextLevel(View.Level);
        return View;
    }

    float Fraction(const FSkillView& View)
    {
        return View.Next > 0 ? FMath::Clamp(static_cast<float>(View.Into) / View.Next, 0.f, 1.f) : 1.f;
    }
}

void UMarburgumSkillsPanel::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);

    UHorizontalBox* Columns = B.Row();

    UVerticalBox* ListColumn = B.Column();
    UHorizontalBox* Header = B.Row();
    UVerticalBox* Titles = B.Column();
    FBuilder::Stack(Titles, B.Text(LOCTEXT("Overline", "SKILLS"), ETypeRole::Overline, T.Color.Accent), T.Space.XXS);
    FBuilder::Stack(Titles, B.Text(LOCTEXT("Title", "What you have learned"), ETypeRole::Title), 0.f);
    FBuilder::Inline(Header, Titles, T.Space.L, true);
    UTextBlock* MoneyText = nullptr;
    UHorizontalBox* Money = B.GlyphText(EMarburgumGlyph::Coin, FText::GetEmpty(), ETypeRole::Numeric, T.Color.TextPrimary, &MoneyText);
    MoneyLabel = MoneyText;
    FBuilder::Inline(Header, Money, 0.f);
    FBuilder::Stack(ListColumn, Header, T.Space.L);

    Rows = B.Column();
    Scroller = B.Scroll(Rows);
    FBuilder::Stack(ListColumn, Scroller, T.Space.M, true);
    UTextBlock* Footnote = B.Text(LOCTEXT("Footnote", "Levels run from 0 to 1990. Only the server awards XP and money; both are saved with your character."),
        ETypeRole::Caption, T.Color.TextMuted);
    Footnote->SetAutoWrapText(true);
    FBuilder::Stack(ListColumn, Footnote, 0.f);
    FBuilder::Inline(Columns, B.Bounded(ListColumn, 760.f, 420.f), T.Space.XXL, true, false);

    DetailBox = B.Column();
    UBorder* DetailCard = B.Surface(ESurfaceKind::Panel, Pad(T.Space.XL), DetailBox);
    FBuilder::InlineTop(Columns, B.Bounded(DetailCard, 520.f, 320.f), 0.f);

    WidgetTree->RootWidget = Columns;
    SetVisibility(ESlateVisibility::Collapsed);
}

void UMarburgumSkillsPanel::NativeConstruct()
{
    Super::NativeConstruct();
    ProgressHandle = AMarburgumPlayerState::OnProgressChanged().AddUObject(this, &UMarburgumSkillsPanel::HandleProgressChanged);
    Rebuild();
}

void UMarburgumSkillsPanel::NativeDestruct()
{
    AMarburgumPlayerState::OnProgressChanged().Remove(ProgressHandle);
    Super::NativeDestruct();
}

void UMarburgumSkillsPanel::SetOpen(bool bOpen)
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

bool UMarburgumSkillsPanel::IsOpen() const
{
    return GetVisibility() != ESlateVisibility::Collapsed;
}

void UMarburgumSkillsPanel::Scroll(float Lines)
{
    FBuilder::ScrollBy(Scroller, Lines);
}

UWidget* UMarburgumSkillsPanel::GetPreferredFocus() const
{
    return RowWidgets.IsValidIndex(Selected) ? RowWidgets[Selected].Get() : nullptr;
}

FText UMarburgumSkillsPanel::GetShownLine(FName SkillId) const
{
    const FText* Line = IsOpen() ? ShownLines.Find(SkillId) : nullptr;
    return Line ? *Line : FText::GetEmpty();
}

void UMarburgumSkillsPanel::HandleProgressChanged(AMarburgumPlayerState* State)
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

void UMarburgumSkillsPanel::Rebuild()
{
    if (!Rows)
    {
        return;
    }
    using namespace MarburgumII;
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);
    Rows->ClearChildren();
    RowWidgets.Reset();
    ShownLines.Reset();

    const APlayerController* Owner = GetOwningPlayer();
    const AMarburgumPlayerState* State = Owner ? Owner->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    if (!State || !State->IsProgressKnown() || !State->HasCharacter())
    {
        const bool bWaiting = !State || !State->IsProgressKnown();
        FBuilder::Stack(Rows, bWaiting
            ? B.StateCard(LOCTEXT("WaitingTitle", "Waiting for the server"),
                LOCTEXT("WaitingBody", "Skills appear once the server has loaded this character's saved record."), ETone::Info)
            : B.StateCard(LOCTEXT("NoCharacterTitle", "No character selected"),
                LOCTEXT("NoCharacterBody", "A connection without a character learns nothing that lasts."), ETone::Warning), 0.f);
        MoneyLabel->SetText(FText::GetEmpty());
        DetailBox->ClearChildren();
        return;
    }

    int32 Index = 0;
    for (const Gameplay::FSkillDefinition& Skill : Gameplay::Skills)
    {
        const FSkillView View = ViewOf(*State, Skill);
        const bool bStarted = View.Xp > 0;

        UHorizontalBox* Line = B.Row();
        UBorder* Tile = B.Surface(ESurfaceKind::Sunken, Pad(T.Space.S), B.Glyph(SkillGlyph(Skill.Id), bStarted ? T.Color.Accent : T.Color.TextMuted,
            T.Icon.Large), false);
        Tile->SetBrush(RoundedBrush(T.Color.Sunken, T.Radius.Medium, T.Color.Border, T.BorderWidth));
        FBuilder::Inline(Line, Tile, T.Space.L);

        UVerticalBox* Middle = B.Column();
        FBuilder::Stack(Middle, B.Text(SkillName(Skill), ETypeRole::Subheading), T.Space.XS);
        FBuilder::Stack(Middle, B.Bar(bStarted ? ETone::Accent : ETone::Neutral, Fraction(View), 6.f), T.Space.XS);
        const FText Progress = View.Next > 0
            ? FText::Format(LOCTEXT("Progress", "{0} / {1} XP to level {2}"), FText::AsNumber(View.Into), FText::AsNumber(View.Next),
                FText::AsNumber(View.Level + 1))
            : LOCTEXT("TopLevel", "Top level reached");
        FBuilder::Stack(Middle, B.Text(Progress, ETypeRole::Caption, T.Color.TextSecondary), 0.f);
        FBuilder::Inline(Line, Middle, T.Space.XL, true);

        UVerticalBox* LevelBox = B.Column();
        UTextBlock* LevelCaption = B.Text(LOCTEXT("LevelOverline", "LEVEL"), ETypeRole::Overline, T.Color.TextMuted);
        LevelCaption->SetJustification(ETextJustify::Right);
        FBuilder::Stack(LevelBox, LevelCaption, 0.f);
        UTextBlock* LevelValue = B.Text(FText::AsNumber(View.Level), ETypeRole::NumericLarge, bStarted ? T.Color.TextPrimary : T.Color.TextMuted);
        LevelValue->SetJustification(ETextJustify::Right);
        FBuilder::Stack(LevelBox, LevelValue, 0.f);
        USizeBox* LevelWidth = WidgetTree->ConstructWidget<USizeBox>();
        LevelWidth->SetMinDesiredWidth(72.f);
        LevelWidth->SetContent(LevelBox);
        FBuilder::Inline(Line, LevelWidth, 0.f);

        UMarburgumSelectable* Row = B.Selectable(Line, FMargin(T.Space.L, T.Space.M));
        Row->OnFocused.BindWeakLambda(this, [this, Index]() { ShowDetail(Index); });
        Row->OnActivated.BindWeakLambda(this, [this, Index]() { ShowDetail(Index); });
        FBuilder::Stack(Rows, Row, T.Space.S);
        RowWidgets.Add(Row);

        const FText Line2 = View.Next > 0
            ? FText::Format(LOCTEXT("Shown", "{0} {1}  ·  {2} / {3} XP to level {4}  ·  {5} XP in all"), SkillName(Skill),
                FText::AsNumber(View.Level), FText::AsNumber(View.Into), FText::AsNumber(View.Next), FText::AsNumber(View.Level + 1), FText::AsNumber(View.Xp))
            : FText::Format(LOCTEXT("ShownTop", "{0} {1}  ·  Top level  ·  {2} XP in all"), SkillName(Skill), FText::AsNumber(View.Level),
                FText::AsNumber(View.Xp));
        ShownLines.Add(FName(Skill.Id), Line2);
        ++Index;
    }
    Selected = FMath::Clamp(Selected, 0, FMath::Max(0, RowWidgets.Num() - 1));
    MoneyLabel->SetText(FText::Format(LOCTEXT("Money", "{0} money"), FText::AsNumber(State->GetMoney())));
    ShowDetail(Selected);
}

void UMarburgumSkillsPanel::ShowDetail(int32 Index)
{
    using namespace MarburgumII;
    if (!DetailBox || Index < 0 || Index >= UE_ARRAY_COUNT(Gameplay::Skills))
    {
        return;
    }
    Selected = Index;
    for (int32 Row = 0; Row < RowWidgets.Num(); ++Row)
    {
        RowWidgets[Row]->SetSelected(Row == Index);
    }
    const APlayerController* Owner = GetOwningPlayer();
    const AMarburgumPlayerState* State = Owner ? Owner->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    if (!State)
    {
        return;
    }
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);
    const Gameplay::FSkillDefinition& Skill = Gameplay::Skills[Index];
    const FSkillView View = ViewOf(*State, Skill);
    DetailBox->ClearChildren();

    UHorizontalBox* Head = B.Row();
    FBuilder::Inline(Head, B.Glyph(SkillGlyph(Skill.Id), T.Color.Accent, 40.f), T.Space.L);
    UVerticalBox* Names = B.Column();
    FBuilder::Stack(Names, B.Text(Skill.bReversible ? LOCTEXT("Standing", "STANDING") : LOCTEXT("Craft", "CRAFT"), ETypeRole::Overline,
        T.Color.TextSecondary), T.Space.XXS);
    FBuilder::Stack(Names, B.Text(SkillName(Skill), ETypeRole::Title), 0.f);
    FBuilder::Inline(Head, Names, 0.f, true);
    FBuilder::Stack(DetailBox, Head, T.Space.XL);

    UHorizontalBox* LevelLine = B.Row();
    FBuilder::Inline(LevelLine, B.Text(FText::AsNumber(View.Level), ETypeRole::Display, T.Color.TextPrimary), T.Space.M, false, false);
    UTextBlock* Of = B.Text(FText::Format(LOCTEXT("OfMax", "level, of {0}"), FText::AsNumber(SkillCurve::MaxLevel, &FNumberFormattingOptions::DefaultNoGrouping())), ETypeRole::Caption,
        T.Color.TextSecondary);
    FBuilder::Inline(LevelLine, Of, 0.f, true, true);
    FBuilder::Stack(DetailBox, LevelLine, T.Space.L);

    FBuilder::Stack(DetailBox, B.Text(LOCTEXT("ToNext", "TO THE NEXT LEVEL"), ETypeRole::Overline, T.Color.TextSecondary), T.Space.XS);
    FBuilder::Stack(DetailBox, B.Bar(View.Xp > 0 ? ETone::Accent : ETone::Neutral, Fraction(View), 10.f), T.Space.S);
    FBuilder::Stack(DetailBox, B.Field(LOCTEXT("Into", "XP in this level"), View.Next > 0
        ? FText::Format(LOCTEXT("IntoValue", "{0} of {1}"), FText::AsNumber(View.Into), FText::AsNumber(View.Next))
        : LOCTEXT("TopValue", "Top level")), T.Space.XS);
    FBuilder::Stack(DetailBox, B.Field(LOCTEXT("Total", "XP in all"), FText::AsNumber(View.Xp)), T.Space.XL);

    FBuilder::Stack(DetailBox, B.Text(LOCTEXT("Scale", "ON THE 0–1990 SCALE"), ETypeRole::Overline, T.Color.TextSecondary), T.Space.XS);
    FBuilder::Stack(DetailBox, B.Bar(ETone::Info, static_cast<float>(View.Level) / SkillCurve::MaxLevel, 6.f, 10), T.Space.XL);

    FBuilder::Stack(DetailBox, B.SectionTitle(LOCTEXT("Raises", "WHAT RAISES IT"), T.Color.Accent), T.Space.S);
    UTextBlock* Source = B.Text(SkillSource(Skill.Id), ETypeRole::Body, T.Color.TextSecondary);
    Source->SetAutoWrapText(true);
    FBuilder::Stack(DetailBox, Source, 0.f);
}

#undef LOCTEXT_NAMESPACE
