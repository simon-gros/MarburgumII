#include "UI/MarburgumUIRoot.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"

namespace
{
    constexpr int32 LayerCount = static_cast<int32>(EMarburgumUILayer::Diagnostic) + 1;
}

void UMarburgumUIRoot::NativeOnInitialized()
{
    Super::NativeOnInitialized();

    UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("MarburgumUIRoot"));
    WidgetTree->RootWidget = Root;

    Layers.Reset();
    for (int32 Index = 0; Index < LayerCount; ++Index)
    {
        UOverlay* Layer = WidgetTree->ConstructWidget<UOverlay>();
        UOverlaySlot* LayerSlot = Root->AddChildToOverlay(Layer);
        LayerSlot->SetHorizontalAlignment(HAlign_Fill);
        LayerSlot->SetVerticalAlignment(VAlign_Fill);
        // Empty layers must not swallow clicks meant for layers below or for the game.
        Layer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        Layers.Add(Layer);
    }
}

UOverlay* UMarburgumUIRoot::GetLayer(EMarburgumUILayer Layer) const
{
    const int32 Index = static_cast<int32>(Layer);
    return Layers.IsValidIndex(Index) ? Layers[Index].Get() : nullptr;
}

void UMarburgumUIRoot::AddToLayer(EMarburgumUILayer Layer, UWidget* Content)
{
    if (UOverlay* Target = GetLayer(Layer); Target && Content)
    {
        UOverlaySlot* ContentSlot = Target->AddChildToOverlay(Content);
        ContentSlot->SetHorizontalAlignment(HAlign_Fill);
        ContentSlot->SetVerticalAlignment(VAlign_Fill);
    }
}
