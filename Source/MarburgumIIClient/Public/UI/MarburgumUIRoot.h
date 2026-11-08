#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MarburgumUIRoot.generated.h"

class UOverlay;

/** Presentation surfaces in back-to-front order (ontology `ui_surface`). */
enum class EMarburgumUILayer : uint8
{
    Hud,           // Low-density exploration modules.
    Notifications, // Transient feedback; never steals focus.
    Screens,       // Full-screen front-end and management screens.
    Modal,         // Blocking confirmation / critical refusal.
    Diagnostic     // Opt-in developer overlay; never part of the default HUD.
};

/**
 * Single viewport root that owns every Marburgum surface, so stacking order and focus precedence are
 * decided in one place instead of by ad-hoc viewport z-orders.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumUIRoot : public UUserWidget
{
    GENERATED_BODY()

public:
    void AddToLayer(EMarburgumUILayer Layer, UWidget* Content);
    UOverlay* GetLayer(EMarburgumUILayer Layer) const;

protected:
    virtual void NativeOnInitialized() override;

private:
    UPROPERTY(Transient)
    TArray<TObjectPtr<UOverlay>> Layers;
};
