#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MarburgumAccountSubsystem.generated.h"

class FJsonObject;

/** Which first-launch screen the player is on (FIRST_LAUNCH_ONTOLOGY.md §4). */
enum class EMarburgumAccountStep : uint8
{
    /** No account service configured: the development key path (ADR-014). */
    Disabled,
    SignIn,
    Register,
    Confirm,
    Credentials,
    Forgot,
    Reset,
    Character,
    /** Signed in with a character: the connection screen's Connect enters the world. */
    Ready
};

/** Immutable snapshot for the account screens. */
struct FMarburgumAccountView
{
    EMarburgumAccountStep Step = EMarburgumAccountStep::Disabled;
    /** A request is in flight; the screen disables its actions. */
    bool bBusy = false;
    /** The last outcome to show: an error when bError, otherwise a neutral note. */
    FText Message;
    bool bError = false;
    /** Machine-readable reason of the last refusal, for tests ("credentials_invalid", ...). */
    FString ErrorCode;
    FString Email;
    FString Username;
    FString CharacterId;
    FString FirstName;
    FString LastName;
    FName Model;
    int32 Revision = 0;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FMarburgumAccountChanged, const FMarburgumAccountView&);
DECLARE_DELEGATE_TwoParams(FMarburgumTicketReady, const FString& /*Ticket*/, const FText& /*Error*/);

/**
 * The client side of accounts (ADR-055): email confirmation, username and password, sign-in, the first character
 * and admission tickets, all through the account service over HTTPS. The session token lives in memory only;
 * nothing here is written to disk or the log. Enabled by -MB2Accounts=<url>.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumAccountSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    bool IsEnabled() const { return !ServiceUrl.IsEmpty(); }
    const FMarburgumAccountView& GetView() const { return View; }
    FMarburgumAccountChanged& OnAccountChanged() { return Changed; }

    /** Between the sign-in, registration and forgotten-password screens; anything in flight is ignored. */
    void Show(EMarburgumAccountStep Step);

    void SignIn(const FString& Username, const FString& Password);
    void Register(const FString& Email);
    void ResendCode();
    void Confirm(const FString& Code);
    /** After a confirmed email: create the account, then sign in with it. */
    void SetCredentials(const FString& Username, const FString& Password);
    void RequestReset(const FString& Email);
    void Reset(const FString& Code, const FString& NewPassword);
    void CreateCharacter(const FString& FirstName, const FString& LastName, FName Model);
    void SignOut();

    /** A fresh single-use admission ticket for entering a world. */
    void RequestTicket(FMarburgumTicketReady Done);

    /** The text shown for a service refusal code. */
    static FText DescribeError(const FString& Code);

private:
    using FReply = TFunction<void(int32 /*Status*/, const TSharedPtr<FJsonObject>& /*Json*/)>;
    void Call(const FString& Verb, const FString& Path, const TSharedPtr<FJsonObject>& Body, bool bSession, FReply Reply);
    void Publish();
    void Fail(int32 Status, const TSharedPtr<FJsonObject>& Json);
    void Note(const FText& Message);
    void AdoptAccount(const TSharedPtr<FJsonObject>& Account);
    /** With accounts on, a local world that is not the front end is left for it: nobody plays before signing in. */
    void HandlePostLoadMap(UWorld* World);
    FDelegateHandle PostLoadMapHandle;

    FString ServiceUrl;
    FString Session;
    FString SetupTicket;
    /** Kept until the create succeeds, so a retry is recognised as the same request. */
    FString CharacterRequest;
    /** Remembered between setting credentials and the automatic sign-in that follows. */
    FString PendingPassword;
    FMarburgumAccountView View;
    FMarburgumAccountChanged Changed;
};
