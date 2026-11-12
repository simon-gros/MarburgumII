#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/Actor.h"
#include "MarburgumCharacterPreview.generated.h"

class UAnimSequence;
class UButton;
class UImage;
class UPointLightComponent;
class USceneCaptureComponent2D;
class USkeletalMesh;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UTextBlock;
class UTextureRenderTarget2D;

/**
 * The first-character creator's stage (ADR-057): one roster body on a plinth before a backdrop, three lights and a
 * camera that renders only those into a texture. Client-only and never replicated; it stands far below whatever
 * world the front end has loaded and shows nothing but its own parts. Bodies come from the account contract's
 * roster, the same meshes AMarburgumCharacter wears in the world.
 */
UCLASS(NotPlaceable, Transient)
class MARBURGUMIICLIENT_API AMarburgumCharacterPreviewStage : public AActor
{
    GENERATED_BODY()

public:
    AMarburgumCharacterPreviewStage();

    /** Wear the roster body at Index (the account contract's CharacterModels order); false if it did not load. */
    bool ShowModel(int32 Index);
    /** Turn the body to face this many degrees away from the camera. */
    void SetYaw(float Degrees);
    /** Frame for a picture this wide over this tall, and this close: 0 the whole body, 1 head and shoulders. */
    void SetFraming(float Aspect, float Zoom);
    void SetTarget(UTextureRenderTarget2D* Target);
    /** Render every frame while true; idle and costless otherwise. */
    void SetCapturing(bool bCapturing);

protected:
    virtual void BeginPlay() override;

private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Turntable;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USkeletalMeshComponent> Body;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Floor;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Backdrop;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Ground;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UDirectionalLightComponent> Key;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> Fill;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> Rim;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneCaptureComponent2D> Capture;

    /** One per roster entry, in CharacterModels order; null where an asset is missing. */
    UPROPERTY() TArray<TObjectPtr<USkeletalMesh>> ModelMeshes;
    UPROPERTY() TObjectPtr<UAnimSequence> Idle;
    UPROPERTY() TObjectPtr<UMaterialInterface> PlainMaterial;
};

/**
 * The creator's 3D preview: the stage's picture, full height beside the form, turned by dragging and brought closer
 * with the mouse wheel. The account screen tells it which roster body to show and when the creator is on screen; it
 * spawns its stage on first use in the current world, sizes the picture to the space it is given and renders only
 * while active.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumCharacterPreview : public UUserWidget
{
    GENERATED_BODY()

public:
    void ShowModel(int32 Index);
    void SetActive(bool bActive);

    /** Test evidence: the roster index the stage last showed, or INDEX_NONE before it has shown one. */
    int32 GetShownModel() const { return ShownModel; }
    float GetYaw() const { return Yaw; }
    float GetZoom() const { return Zoom; }
    /** Test drivers: the same paths as a horizontal drag of this many pixels, and wheel notches. */
    void TurnForTest(float Pixels) { Turn(Pixels); }
    void ZoomForTest(float Notches) { ZoomBy(Notches); }

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;

private:
    AMarburgumCharacterPreviewStage* EnsureStage();
    void Refresh();
    void Turn(float Pixels);
    void ZoomBy(float Notches);

    UPROPERTY(Transient) TObjectPtr<UImage> Picture;
    UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> Target;
    TWeakObjectPtr<AMarburgumCharacterPreviewStage> Stage;

    int32 WantedModel = 0;
    int32 ShownModel = INDEX_NONE;
    float Yaw = -20.f;
    float Zoom = 0.f;
    float Aspect = 2.f / 3.f;
    bool bActive = false;
    bool bDragging = false;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FMarburgumModelPicked, int32 /*RosterIndex*/);

/**
 * One body the creator offers, as a selectable card: its name, and "Selected" on the chosen one, so the choice
 * never rests on colour alone.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumModelCard : public UUserWidget
{
    GENERATED_BODY()

public:
    void Setup(int32 InIndex, const FText& Name);
    void SetSelected(bool bSelected);
    UButton* GetButton() const { return Button; }

    FMarburgumModelPicked OnPicked;

protected:
    virtual void NativeOnInitialized() override;

private:
    UFUNCTION() void HandleClicked();

    UPROPERTY(Transient) TObjectPtr<UButton> Button;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> NameText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StateText;
    int32 Index = INDEX_NONE;
};
