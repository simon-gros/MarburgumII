#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MarburgumAccountSmokeSubsystem.generated.h"

/**
 * First-launch driver (ADR-055), active only with -MB2AccountSmoke. It plays a player on the real account screens
 * against the real account service and a dedicated server, and exits 0 (its expectations held) or 1. Roles (see
 * scripts/account-smoke-linux.sh):
 *
 *   FIRST     a new player: register an email, a wrong code, then the code from the outbox, two passwords that
 *             differ, a username and password, each roster body in the creator's 3D preview (turned once), a
 *             refused first name, then the character; Connect enters the
 *             world and the server shows the character's name and body.
 *   RETURN    the same player again: a wrong password, then the right one; no creator; the same character enters.
 *   REFUSED   started with a join URL the server must refuse; passes when the session ends refused.
 *   ADMITTED  started with a join URL carrying a ticket; passes once in the world as -MB2AccountFirst/Last with
 *             -MB2AccountModel, after holding the character there for -MB2AccountHold=<s>.
 *
 * Options: -MB2AccountRole= -MB2AccountShot=<prefix> -MB2AccountTimeout=<s> -MB2AccountOutbox=<dir>
 * -MB2AccountEmail= -MB2AccountUser= -MB2AccountPasswordFile= -MB2AccountFirst= -MB2AccountLast= -MB2AccountModel=
 * and -MB2Server=<host:port>.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumAccountSmokeSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:
    bool Tick(float DeltaTime);
    void TickFirst(double Now);
    void TickReturn(double Now);
    void TickRefused(double Now);
    void TickAdmitted(double Now);
    /** Shared by FIRST and RETURN once the character is ready: connect and check who arrived. */
    bool TickEnter(double Now, int32 FirstStep, const TCHAR* ShotLabel);

    class UMarburgumAccountSubsystem* Accounts() const;
    class UMarburgumAccountScreen* Screen() const;
    /** The code in the newest outbox message to Email, or empty. */
    FString CodeFromOutbox() const;
    void Expect(bool bCondition, const FString& What);
    void Shot(const TCHAR* Label);
    void Advance(int32 Next, double Now);
    void Pass();
    void Fail(const FString& Reason);

    FTSTicker::FDelegateHandle TickerHandle;
    FString Role;
    FString ShotPrefix;
    FString Outbox;
    FString Email;
    FString User;
    FString Password;
    FString First;
    FString Last;
    FName Model;
    double TimeoutSeconds = 120.0;
    double HoldSeconds = 0.0;
    double StartSeconds = 0.0;

    int32 Step = 0;
    double StepSeconds = 0.0;
    int32 SeenRevision = 0;
    /** The roster body the creator's preview is being checked on. */
    int32 PreviewModel = 0;
    FString PendingShot;
    double ShotRequestedSeconds = 0.0;
    bool bDone = false;
    TArray<FString> Problems;
};
