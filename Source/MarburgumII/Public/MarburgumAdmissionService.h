#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MarburgumAdmissionService.generated.h"

/** The character a spent admission ticket admits (ADR-055). */
struct FMarburgumAdmittedCharacter
{
    FString AccountId;
    /** Opaque, server-assigned ("c_..."); also the character's key in the world store. */
    FString CharacterId;
    FString FirstName;
    FString LastName;
    FName Model;
};

/** Error is empty on success. Always called on the game thread. */
DECLARE_DELEGATE_TwoParams(FMarburgumTicketRedeemed, const FMarburgumAdmittedCharacter& /*Who*/, const FString& /*Error*/);

/**
 * Boundary between the game mode (this module) and the server-only account admission (MarburgumIIServer), which
 * holds the service key and spends tickets. A world without a concrete service has no account path.
 */
UCLASS(Abstract)
class MARBURGUMII_API UMarburgumAdmissionService : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    /** Whether joining needs a ticket. A shipping server always does. */
    virtual bool RequiresAccounts() const PURE_VIRTUAL(UMarburgumAdmissionService::RequiresAccounts, return true;);

    /** Spend a ticket at the account service. */
    virtual void Redeem(const FString& Ticket, FMarburgumTicketRedeemed Done)
        PURE_VIRTUAL(UMarburgumAdmissionService::Redeem, );

    static UMarburgumAdmissionService* Get(const UWorld* World);
};
