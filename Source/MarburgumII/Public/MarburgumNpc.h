#pragma once

#include "CoreMinimal.h"
#include "MarburgumInteractable.h"
#include "MarburgumNpc.generated.h"

class UStaticMeshComponent;

/**
 * An authored non-player character the player can talk to (ontology `npc`). Talking is an ordinary
 * interaction request: the server decides what it means for the requesting character, e.g. accepting
 * the mission this NPC offers (MarburgumII::Gameplay::FindMissionOfferedBy). An NPC holds no stock.
 *
 * The body is a placeholder figure built from engine shapes and tinted from the original
 * npc_visual_profiles.json colours; it is not a character model.
 */
UCLASS()
class MARBURGUMII_API AMarburgumNpc : public AMarburgumInteractable
{
    GENERATED_BODY()

public:
    AMarburgumNpc();

    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual bool IsItemSource() const override { return false; }

    /** The NPC's line from the original interactions.json; shown when the server accepts a talk. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marburgum|Npc")
    FText Dialogue;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marburgum|Npc")
    FLinearColor PrimaryColor = FLinearColor(0.1f, 0.13f, 0.09f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marburgum|Npc")
    FLinearColor SecondaryColor = FLinearColor(0.035f, 0.04f, 0.035f);

    /** Show the fishing-rod prop (the riverside angler). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marburgum|Npc")
    bool bHoldsRod = false;

private:
    void Tint(UStaticMeshComponent* Part, const FLinearColor& Color) const;

    UPROPERTY(VisibleAnywhere, Category = "Marburgum|Npc")
    TObjectPtr<USceneComponent> Root;

    UPROPERTY(VisibleAnywhere, Category = "Marburgum|Npc")
    TObjectPtr<UStaticMeshComponent> Head;

    UPROPERTY(VisibleAnywhere, Category = "Marburgum|Npc")
    TObjectPtr<UStaticMeshComponent> Cap;

    UPROPERTY(VisibleAnywhere, Category = "Marburgum|Npc")
    TObjectPtr<UStaticMeshComponent> Rod;
};
