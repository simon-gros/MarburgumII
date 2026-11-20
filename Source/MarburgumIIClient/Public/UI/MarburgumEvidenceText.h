#pragma once

#include "CoreMinimal.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumMissionLogic.h"

class AMarburgumPlayerState;

/**
 * How the interface names kinds of evidence (ADR-083), kept apart the way the original storyline keeps them apart, and
 * the replicated progress views the shared mission rules read on the client.
 */
namespace MarburgumUI
{
    /** "Original", "Print", "Copy", "Testimony", "Information", "Inference"; empty for None. */
    MARBURGUMIICLIENT_API FText EvidenceClassName(MarburgumII::Gameplay::EEvidenceClass Class);

    /** One sentence on what that kind of evidence can and cannot prove. */
    MARBURGUMIICLIENT_API FText EvidenceClassMeaning(MarburgumII::Gameplay::EEvidenceClass Class);

    /** The owner's missions as the shared rules see them (stage, completed, outcome), from the replicated copy. */
    MARBURGUMIICLIENT_API TMap<FString, MarburgumII::Missions::FProgressView> ProgressViews(const AMarburgumPlayerState* State);
}
