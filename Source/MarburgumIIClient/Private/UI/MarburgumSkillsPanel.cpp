#include "UI/MarburgumSkillsPanel.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/SafeZone.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumPlayerState.h"
#include "MarburgumSkillCurve.h"
#include "UI/MarburgumUIBuilder.h"

#define LOCTEXT_NAMESPACE "MarburgumSkillsPanel"

using namespace MarburgumUI;

namespace
{
    constexpr float PanelWidth = 520.f;
}

void UMarburgumSkillsPanel::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);

    UVerticalBox* Column = B.Column();
    UHorizontalBox* Header = B.Row();
    UVerticalBox* Titles = B.Column();
    FBuilder::Stack(Titles, B.Text(LOCTEXT("Overline", "SKILLS"), ETypeRole::Overline, T.Color.Accent), T.Space.XXS);
    FBuilder::Stack(Titles, B.Text(LOCTEXT("Title", "What you have learned"), ETypeRole::Title), 0.f);
    FBuilder::Inline(Header, Titles, T.Space.M, true);
    FBuilder::Inline(Header, B.Text(LOCTEXT("Close", "[K] Close"), ETypeRole::Caption, T.Color.TextMuted), 0.f, false, false);
    FBuilder::Stack(Column, Header, T.Space.M);
    FBuilder::Stack(Column, B.Rule(T.Color.Border, 1.f), T.Space.M);

    Rows = B.Column();
    FBuilder::Stack(Column, Rows, T.Space.M);

    MoneyLabel = B.Text(FText::GetEmpty(), ETypeRole::Body, T.Color.TextPrimary);
    FBuilder::Stack(Column, MoneyLabel, T.Space.XS);
    FBuilder::Stack(Column, B.Text(LOCTEXT("Footnote", "Levels 0–1990. Only the server awards XP and money; both are saved with your character."),
        ETypeRole::Caption, T.Color.TextMuted), 0.f);

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

void UMarburgumSkillsPanel::Toggle()
{
    SetVisibility(IsOpen() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    if (IsOpen())
    {
        Rebuild();
    }
}

bool UMarburgumSkillsPanel::IsOpen() const
{
    return GetVisibility() != ESlateVisibility::Collapsed;
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
        Rebuild();
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
    ShownLines.Reset();

    const APlayerController* Owner = GetOwningPlayer();
    const AMarburgumPlayerState* State = Owner ? Owner->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    if (!State || !State->IsProgressKnown())
    {
        FBuilder::Stack(Rows, B.StateCard(LOCTEXT("WaitingTitle", "Waiting for the server"),
            LOCTEXT("WaitingBody", "Skills appear once the server has loaded this character's saved record."), ETone::Info), 0.f);
        MoneyLabel->SetText(FText::GetEmpty());
        return;
    }
    if (!State->HasCharacter())
    {
        FBuilder::Stack(Rows, B.StateCard(LOCTEXT("NoCharacterTitle", "No character selected"),
            LOCTEXT("NoCharacterBody", "A connection without a character learns nothing that lasts."), ETone::Warning), 0.f);
        MoneyLabel->SetText(FText::GetEmpty());
        return;
    }

    for (const Gameplay::FSkillDefinition& Skill : Gameplay::Skills)
    {
        const FName Id(Skill.Id);
        const int64 Xp = State->GetSkillXp(Id);
        const int64 Level = SkillCurve::LevelForXp(Xp);
        const int64 Into = SkillCurve::XpIntoLevel(Xp);
        const int64 Next = SkillCurve::XpToNextLevel(Level);

        UVerticalBox* Card = B.Column();
        UHorizontalBox* Header = B.Row();
        FBuilder::Inline(Header, B.Text(FText::FromString(Skill.DisplayName), ETypeRole::Heading), T.Space.S, true);
        FBuilder::Inline(Header, B.Text(FText::Format(LOCTEXT("Level", "Level {0}"), FText::AsNumber(Level)), ETypeRole::Body,
            Xp > 0 ? T.Color.Accent : T.Color.TextMuted), 0.f);
        FBuilder::Stack(Card, Header, T.Space.XS);
        FBuilder::Stack(Card, B.Meter(Xp > 0 ? ETone::Accent : ETone::Neutral, Next > 0 ? static_cast<float>(Into) / Next : 1.f), T.Space.XS);
        const FText Line = Next > 0
            ? FText::Format(LOCTEXT("Progress", "{0} / {1} XP to level {2}  ·  {3} XP in all"), FText::AsNumber(Into), FText::AsNumber(Next),
                FText::AsNumber(Level + 1), FText::AsNumber(Xp))
            : FText::Format(LOCTEXT("Top", "Top level  ·  {0} XP in all"), FText::AsNumber(Xp));
        FBuilder::Stack(Card, B.Text(Line, ETypeRole::Caption), 0.f);
        ShownLines.Add(Id, FText::Format(LOCTEXT("Shown", "{0} {1}  ·  {2}"), FText::FromString(Skill.DisplayName), FText::AsNumber(Level), Line));
        FBuilder::Stack(Rows, B.Surface(ESurfaceKind::Raised, Pad(T.Space.L, T.Space.M), Card), T.Space.S);
    }
    MoneyLabel->SetText(FText::Format(LOCTEXT("Money", "Money: {0}"), FText::AsNumber(State->GetMoney())));
}

#undef LOCTEXT_NAMESPACE
