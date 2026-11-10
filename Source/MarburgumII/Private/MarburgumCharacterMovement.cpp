#include "MarburgumCharacterMovement.h"

#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumPlayerState.h"

void UMarburgumCharacterMovement::OnTimeDiscrepancyDetected(float CurrentTimeDiscrepancy, float LifetimeRawTimeDiscrepancy,
    float Lifetime, float CurrentMoveError)
{
    Super::OnTimeDiscrepancyDetected(CurrentTimeDiscrepancy, LifetimeRawTimeDiscrepancy, Lifetime, CurrentMoveError);
    ++Detections;
    ++UnreportedDetections;
    // A client that keeps running fast is detected several times a second; one line a second is enough.
    const double Now = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
    if (LastReportSeconds >= 0.0 && Now - LastReportSeconds < 1.0)
    {
        return;
    }
    const AMarburgumPlayerState* State = CharacterOwner ? CharacterOwner->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    UE_LOG(LogMarburgumNet, Log,
        TEXT("%s role=server event=time_discrepancy player=%d character=%s discrepancy_s=%.3f lifetime_raw_s=%.3f lifetime_s=%.1f detections=%d since_last=%d"),
        MarburgumII::Gameplay::InteractLogPrefix, State ? State->GetPlayerId() : -1,
        State && !State->GetCharacterKey().IsEmpty() ? *State->GetCharacterKey() : TEXT("-"),
        CurrentTimeDiscrepancy, LifetimeRawTimeDiscrepancy, Lifetime, Detections, UnreportedDetections);
    UnreportedDetections = 0;
    LastReportSeconds = Now;
}
