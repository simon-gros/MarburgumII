#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MarburgumSkillsPanel.generated.h"

class AMarburgumPlayerState;
class UMarburgumGlyph;
class UMarburgumMeter;
class UMarburgumSelectable;
class UTextBlock;
class UScrollBox;
class UVerticalBox;

/**
 * UI-04 `skills_panel` (ADR-047), a page of the management shell (K): every registered skill as a row with its glyph,
 * level on the original's 0-1990 curve and progress to the next level, and a detail pane for the focused skill (XP into
 * the level, to the next, in all, where the level stands on the 0-1990 scale, and what raises it in this build). Levels
 * are derived on the client from the owner-only replicated XP with the same integer curve the server uses; the server
 * alone awards XP. Designed states for waiting and no character. Rebuilt on the progress-changed event.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumSkillsPanel : public UUserWidget
{
    GENERATED_BODY()

public:
    void SetOpen(bool bOpen);
    bool IsOpen() const;
    /** Scrolls the list by Lines lines (mouse wheel, Page Up/Down). */
    void Scroll(float Lines);
    UWidget* GetPreferredFocus() const;

    /** "Fishing 1 · 24 / 20 XP"-style line for one skill as shown (evidence capture); empty while closed. */
    FText GetShownLine(FName SkillId) const;

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

private:
    void HandleProgressChanged(AMarburgumPlayerState* State);
    void Rebuild();
    void ShowDetail(int32 Index);

    UPROPERTY(Transient) TObjectPtr<UVerticalBox> Rows;
    UPROPERTY(Transient) TObjectPtr<UScrollBox> Scroller;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MoneyLabel;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> DetailBox;
    UPROPERTY(Transient) TArray<TObjectPtr<UMarburgumSelectable>> RowWidgets;

    TMap<FName, FText> ShownLines;
    int32 Selected = 0;
    FDelegateHandle ProgressHandle;
};
