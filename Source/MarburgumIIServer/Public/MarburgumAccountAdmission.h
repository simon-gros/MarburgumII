#pragma once

#include "CoreMinimal.h"
#include "MarburgumAdmissionService.h"
#include "MarburgumAccountAdmission.generated.h"

/**
 * Spends admission tickets at the account service (ADR-055). Configured with -MB2Accounts=<https url> and
 * -MB2AccountsKeyFile=<file>; the key never leaves this process except to the service. A shipping server
 * requires accounts even without configuration, so it admits nobody until it has them.
 */
UCLASS()
class MARBURGUMIISERVER_API UMarburgumAccountAdmission : public UMarburgumAdmissionService
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    virtual bool RequiresAccounts() const override;
    virtual void Redeem(const FString& Ticket, FMarburgumTicketRedeemed Done) override;

private:
    FString ServiceUrl;
    FString ServerKey;
};
