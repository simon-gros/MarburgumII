#include "MarburgumAccountAdmission.h"

#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "MarburgumAccountContract.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumNetworkContract.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

using namespace MarburgumII::Accounts;

bool UMarburgumAccountAdmission::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* World = Cast<UWorld>(Outer);
    return World && World->IsGameWorld() && Super::ShouldCreateSubsystem(Outer);
}

void UMarburgumAccountAdmission::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    ServiceUrl = MarburgumII::Accounts::ServiceUrl();
    FString KeyFile;
    if (!ServiceUrl.IsEmpty() && FParse::Value(FCommandLine::Get(), ServerKeyFileSwitch, KeyFile))
    {
        FFileHelper::LoadFileToString(ServerKey, *KeyFile);
        ServerKey.TrimStartAndEndInline();
    }
    if (ServiceRequested() && GetWorld() && GetWorld()->GetNetMode() == NM_DedicatedServer)
    {
        if (ServiceUrl.IsEmpty())
        {
            // Still signed-in players only (RequiresAccounts): a wrong address refuses everyone, it opens nothing.
            UE_LOG(LogMarburgumNet, Error, TEXT("%s role=server event=accounts_misconfigured reason=\"-MB2Accounts must be an https:// address\""),
                AdmissionLogPrefix);
        }
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=accounts_required service=%s key=%s"), AdmissionLogPrefix,
            ServiceUrl.IsEmpty() ? TEXT("invalid") : *ServiceUrl, ServerKey.Len() >= 32 ? TEXT("loaded") : TEXT("missing"));
    }
}

bool UMarburgumAccountAdmission::RequiresAccounts() const
{
    // Shipping and Test builds never fall back to the development key (ADR-014).
#if UE_BUILD_SHIPPING || UE_BUILD_TEST
    return true;
#else
    return ServiceRequested();
#endif
}

void UMarburgumAccountAdmission::Redeem(const FString& Ticket, FMarburgumTicketRedeemed Done)
{
    if (ServiceUrl.IsEmpty() || ServerKey.Len() < 32)
    {
        Done.ExecuteIfBound({}, TEXT("This server cannot check sign-ins right now."));
        return;
    }
    const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(ServiceUrl + TEXT("/v1/server/redeem"));
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetHeader(TEXT("Authorization"), TEXT("MB2-Server ") + ServerKey);
    Request->SetTimeout(10.f);
    const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
    Body->SetStringField(TEXT("ticket"), Ticket);
    FString Text;
    FJsonSerializer::Serialize(Body, TJsonWriterFactory<>::Create(&Text));
    Request->SetContentAsString(Text);
    Request->OnProcessRequestComplete().BindLambda(
        [Done](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
        {
            if (!bConnected || !Response.IsValid())
            {
                Done.ExecuteIfBound({}, TEXT("The account service is unavailable. Try again shortly."));
                return;
            }
            const int32 Status = Response->GetResponseCode();
            TSharedPtr<FJsonObject> Json;
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Json);
            if (Status != 200 || !Json.IsValid())
            {
                FString Code;
                if (Json.IsValid())
                {
                    Json->TryGetStringField(TEXT("error"), Code);
                }
                if (Code == TEXT("ticket_invalid"))
                {
                    Done.ExecuteIfBound({}, TEXT("Your sign-in for this world has expired. Sign in again."));
                    return;
                }
                // The operator's problem (a wrong key, a proxy error): the details go to the log, not to the player.
                UE_LOG(LogMarburgumNet, Warning, TEXT("%s role=server event=redeem_failed status=%d code=%s"), AdmissionLogPrefix, Status,
                    Code.IsEmpty() ? TEXT("none") : *Code);
                Done.ExecuteIfBound({}, TEXT("This world cannot check sign-ins right now. Try again shortly."));
                return;
            }
            const TSharedPtr<FJsonObject>* Character = nullptr;
            FMarburgumAdmittedCharacter Who;
            Who.AccountId = Json->GetStringField(TEXT("account"));
            if (Json->TryGetObjectField(TEXT("character"), Character))
            {
                Who.CharacterId = (*Character)->GetStringField(TEXT("id"));
                Who.FirstName = (*Character)->GetStringField(TEXT("first_name"));
                Who.LastName = (*Character)->GetStringField(TEXT("last_name"));
                Who.Model = FName(*(*Character)->GetStringField(TEXT("model")));
            }
            Who.ConnectionKey = DecodeConnectionKey(Json->GetStringField(TEXT("key")));
            // The service is trusted, but nothing it says becomes a store key or an asset path unchecked.
            if (!MarburgumII::Gameplay::IsValidCharacterKey(Who.CharacterId) || !FindModel(Who.Model) || Who.FirstName.IsEmpty()
                || Who.LastName.IsEmpty() || Who.FirstName.Len() > NameMax || Who.LastName.Len() > NameMax
                || Who.ConnectionKey.Num() != ConnectionKeyBytes)
            {
                Done.ExecuteIfBound({}, TEXT("The account service sent a character this server cannot admit."));
                return;
            }
            Done.ExecuteIfBound(Who, FString());
        });
    Request->ProcessRequest();
}
