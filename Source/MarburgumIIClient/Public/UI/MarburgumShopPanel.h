#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MarburgumShopPanel.generated.h"

class AMarburgumInteractable;
class UTextBlock;
class UVerticalBox;

/**
 * A shop counter (ADR-060): the keeper's goods with the price, what the counter pays back, the copies on the shelf
 * and how many the character holds, as the server last replicated them. A number key buys one of that line, the
 * same key with Shift sells one back. The server prices and decides every request and answers with a receipt; the
 * panel stays open for the next one. B, walking out of reach or another screen opening closes it. Lines the
 * character cannot afford, that are sold out or that it cannot sell are shown muted; the server checks again.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumShopPanel : public UUserWidget
{
    GENERATED_BODY()

public:
    void Open(AMarburgumInteractable* Keeper);
    void Close();
    bool IsOpen() const;
    AMarburgumInteractable* GetKeeper() const { return Keeper.Get(); }

    /** Buy (or, with bSell, sell back) one of line Number (1-based). False when closed or the line does not exist. */
    bool Trade(int32 Number, bool bSell);

    /** What the panel shows, for evidence capture. */
    FName GetShownShop() const { return Shop; }
    int32 GetShownLineCount() const { return LineCount; }
    bool IsShownOpen() const { return bShownOpen; }

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    void Rebuild();
    /** What the panel was built from, so it rebuilds only when that changes. */
    FString Snapshot() const;

    UPROPERTY(Transient) TObjectPtr<UTextBlock> ShopLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> HoursLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MoneyLabel;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> Rows;

    TWeakObjectPtr<AMarburgumInteractable> Keeper;
    FName Shop;
    int32 LineCount = 0;
    bool bShownOpen = false;
    FString Shown;
};
