#include "Session/MarburgumAccountSubsystem.h"

#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "MarburgumAccountContract.h"
#include "MarburgumNetworkContract.h"
#include "Misc/Guid.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#define LOCTEXT_NAMESPACE "MarburgumAccount"

using namespace MarburgumII::Accounts;

namespace
{
    TSharedPtr<FJsonObject> Fields(std::initializer_list<TPair<const TCHAR*, FString>> Pairs)
    {
        const TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
        for (const TPair<const TCHAR*, FString>& Pair : Pairs)
        {
            Json->SetStringField(Pair.Key, Pair.Value);
        }
        return Json;
    }
}

bool UMarburgumAccountSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

void UMarburgumAccountSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    ServiceUrl = MarburgumII::Accounts::ServiceUrl();
    View.Step = IsEnabled() ? EMarburgumAccountStep::SignIn : EMarburgumAccountStep::Disabled;
    if (IsEnabled())
    {
        PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UMarburgumAccountSubsystem::HandlePostLoadMap);
    }
    Publish();
}

void UMarburgumAccountSubsystem::Deinitialize()
{
    FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
    Super::Deinitialize();
}

void UMarburgumAccountSubsystem::HandlePostLoadMap(UWorld* World)
{
    static const FString FrontEnd = TEXT("/Game/Maps/FrontEnd");
    if (World && World->GetGameInstance() == GetGameInstance() && World->GetNetMode() == NM_Standalone
        && UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) != FrontEnd)
    {
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=client event=front_end_enforced from=%s"), AdmissionLogPrefix, *World->GetMapName());
        UGameplayStatics::OpenLevel(World, FName(*FrontEnd));
    }
}

FText UMarburgumAccountSubsystem::DescribeError(const FString& Code)
{
    static const TMap<FString, FText> Messages = {
        { TEXT("email_invalid"), LOCTEXT("EmailInvalid", "That does not look like an email address.") },
        { TEXT("resend_too_soon"), LOCTEXT("ResendTooSoon", "A code was sent a moment ago. Wait a minute before asking for another.") },
        { TEXT("code_invalid"), LOCTEXT("CodeInvalid", "That code is wrong, used or expired. Check the email, or ask for a new code.") },
        { TEXT("setup_expired"), LOCTEXT("SetupExpired", "This confirmation has expired. Start again with your email address.") },
        { TEXT("username_length"), LOCTEXT("UsernameLength", "A username is 3 to 20 characters.") },
        { TEXT("username_characters"), LOCTEXT("UsernameCharacters", "Use letters a–z, digits, dots, dashes or underscores, starting with a letter or digit.") },
        { TEXT("username_reserved"), LOCTEXT("UsernameReserved", "That username is reserved. Choose another.") },
        { TEXT("username_taken"), LOCTEXT("UsernameTaken", "That username is taken. Choose another.") },
        { TEXT("password_length"), LOCTEXT("PasswordLength", "A password is 10 to 128 characters.") },
        { TEXT("password_contains_username"), LOCTEXT("PasswordUsername", "The password must not contain your username or be your email address.") },
        { TEXT("credentials_invalid"), LOCTEXT("CredentialsInvalid", "That username and password do not match.") },
        { TEXT("too_many_attempts"), LOCTEXT("TooMany", "Too many wrong passwords. Wait fifteen minutes, or reset the password.") },
        { TEXT("session_invalid"), LOCTEXT("SessionInvalid", "You have been signed out. Sign in again.") },
        { TEXT("first_name_length"), LOCTEXT("FirstLength", "A first name is 2 to 24 characters.") },
        { TEXT("first_name_characters"), LOCTEXT("FirstCharacters", "A first name uses letters, with single spaces, hyphens or apostrophes between them.") },
        { TEXT("first_name_reserved"), LOCTEXT("FirstReserved", "That first name is reserved. Choose another.") },
        { TEXT("last_name_length"), LOCTEXT("LastLength", "A last name is 2 to 24 characters.") },
        { TEXT("last_name_characters"), LOCTEXT("LastCharacters", "A last name uses letters, with single spaces, hyphens or apostrophes between them.") },
        { TEXT("last_name_reserved"), LOCTEXT("LastReserved", "That last name is reserved. Choose another.") },
        { TEXT("model_unknown"), LOCTEXT("ModelUnknown", "Choose one of the bodies shown.") },
        { TEXT("character_exists"), LOCTEXT("CharacterExists", "This account already has its character.") },
        { TEXT("no_character"), LOCTEXT("NoCharacter", "Create your character first.") },
        { TEXT("store_unavailable"), LOCTEXT("StoreUnavailable", "The account service could not save. Nothing changed; try again.") },
        { TEXT("mail_unavailable"), LOCTEXT("MailUnavailable", "The email could not be sent. Try again in a minute.") },
        { TEXT("unreachable"), LOCTEXT("Unreachable", "The account service cannot be reached. Check your connection and try again.") },
    };
    const FText* Found = Messages.Find(Code);
    return Found ? *Found : FText::Format(LOCTEXT("Unknown", "The account service refused the request ({0})."), FText::FromString(Code));
}

void UMarburgumAccountSubsystem::Publish()
{
    ++View.Revision;
    Changed.Broadcast(View);
}

void UMarburgumAccountSubsystem::Fail(int32 Status, const TSharedPtr<FJsonObject>& Json)
{
    FString Code = Status == 0 ? TEXT("unreachable") : FString();
    if (Code.IsEmpty() && !(Json.IsValid() && Json->TryGetStringField(TEXT("error"), Code)))
    {
        Code = FString::Printf(TEXT("http_%d"), Status);
    }
    View.bBusy = false;
    View.bError = true;
    View.ErrorCode = Code;
    View.Message = DescribeError(Code);
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=client event=refused status=%d code=%s"), AdmissionLogPrefix, Status, *Code);
    if (Code == TEXT("session_invalid"))
    {
        Session.Reset();
        View.Step = EMarburgumAccountStep::SignIn;
    }
    Publish();
}

void UMarburgumAccountSubsystem::Note(const FText& Message)
{
    View.bBusy = false;
    View.bError = false;
    View.ErrorCode.Reset();
    View.Message = Message;
}

void UMarburgumAccountSubsystem::Call(const FString& Verb, const FString& Path, const TSharedPtr<FJsonObject>& Body, bool bSession, FReply Reply)
{
    View.bBusy = true;
    View.bError = false;
    View.ErrorCode.Reset();
    View.Message = FText::GetEmpty();
    Publish();
    const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(ServiceUrl + Path);
    Request->SetVerb(Verb);
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetTimeout(15.f);
    if (bSession)
    {
        Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Session);
    }
    FString Text = TEXT("{}");
    if (Body.IsValid())
    {
        FJsonSerializer::Serialize(Body.ToSharedRef(), TJsonWriterFactory<>::Create(&Text));
    }
    Request->SetContentAsString(Text);
    Request->OnProcessRequestComplete().BindWeakLambda(this,
        [Reply = MoveTemp(Reply)](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
        {
            TSharedPtr<FJsonObject> Json;
            const int32 Status = bConnected && Response.IsValid() ? Response->GetResponseCode() : 0;
            if (Status != 0)
            {
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Json);
            }
            Reply(Status, Json);
        });
    Request->ProcessRequest();
}

void UMarburgumAccountSubsystem::Show(EMarburgumAccountStep Step)
{
    if (!IsEnabled() || View.bBusy)
    {
        return;
    }
    View.Step = Step;
    Note(FText::GetEmpty());
    Publish();
}

void UMarburgumAccountSubsystem::AdoptAccount(const TSharedPtr<FJsonObject>& Account)
{
    View.Username = Account.IsValid() ? Account->GetStringField(TEXT("username")) : FString();
    const TSharedPtr<FJsonObject>* Character = nullptr;
    if (Account.IsValid() && Account->TryGetObjectField(TEXT("character"), Character))
    {
        View.CharacterId = (*Character)->GetStringField(TEXT("id"));
        View.FirstName = (*Character)->GetStringField(TEXT("first_name"));
        View.LastName = (*Character)->GetStringField(TEXT("last_name"));
        View.Model = FName(*(*Character)->GetStringField(TEXT("model")));
        View.Step = EMarburgumAccountStep::Ready;
    }
    else
    {
        View.CharacterId.Reset();
        View.FirstName.Reset();
        View.LastName.Reset();
        View.Model = NAME_None;
        View.Step = EMarburgumAccountStep::Character;
    }
}

void UMarburgumAccountSubsystem::SignIn(const FString& Username, const FString& Password)
{
    if (View.bBusy)
    {
        return;
    }
    Call(TEXT("POST"), TEXT("/v1/login"), Fields({ { TEXT("username"), Username.TrimStartAndEnd() }, { TEXT("password"), Password } }), false,
        [this](int32 Status, const TSharedPtr<FJsonObject>& Json)
        {
            if (Status != 200 || !Json.IsValid())
            {
                Fail(Status, Json);
                return;
            }
            Session = Json->GetStringField(TEXT("session"));
            AdoptAccount(Json->GetObjectField(TEXT("account")));
            Note(FText::GetEmpty());
            UE_LOG(LogMarburgumNet, Log, TEXT("%s role=client event=signed_in has_character=%d"), AdmissionLogPrefix,
                View.Step == EMarburgumAccountStep::Ready ? 1 : 0);
            Publish();
        });
}

void UMarburgumAccountSubsystem::Register(const FString& Email)
{
    if (View.bBusy)
    {
        return;
    }
    View.Email = Email.TrimStartAndEnd();
    Call(TEXT("POST"), TEXT("/v1/register"), Fields({ { TEXT("email"), View.Email } }), false,
        [this](int32 Status, const TSharedPtr<FJsonObject>& Json)
        {
            if (Status != 202)
            {
                Fail(Status, Json);
                return;
            }
            View.Step = EMarburgumAccountStep::Confirm;
            // Worded so it says nothing about whether the address already has an account.
            Note(FText::Format(LOCTEXT("CodeSent", "We sent a message to {0}. Type the eight-character code from it below."),
                FText::FromString(View.Email)));
            UE_LOG(LogMarburgumNet, Log, TEXT("%s role=client event=code_requested"), AdmissionLogPrefix);
            Publish();
        });
}

void UMarburgumAccountSubsystem::ResendCode()
{
    if (!View.Email.IsEmpty())
    {
        Register(View.Email);
    }
}

void UMarburgumAccountSubsystem::Confirm(const FString& Code)
{
    if (View.bBusy)
    {
        return;
    }
    Call(TEXT("POST"), TEXT("/v1/verify"), Fields({ { TEXT("email"), View.Email }, { TEXT("code"), Code } }), false,
        [this](int32 Status, const TSharedPtr<FJsonObject>& Json)
        {
            if (Status != 200 || !Json.IsValid())
            {
                Fail(Status, Json);
                return;
            }
            SetupTicket = Json->GetStringField(TEXT("ticket"));
            View.Step = EMarburgumAccountStep::Credentials;
            Note(LOCTEXT("Confirmed", "Email confirmed. Choose the username and password you will sign in with."));
            UE_LOG(LogMarburgumNet, Log, TEXT("%s role=client event=email_confirmed"), AdmissionLogPrefix);
            Publish();
        });
}

void UMarburgumAccountSubsystem::SetCredentials(const FString& Username, const FString& Password)
{
    if (View.bBusy)
    {
        return;
    }
    PendingPassword = Password;
    const FString Name = Username.TrimStartAndEnd();
    Call(TEXT("POST"), TEXT("/v1/setup"), Fields({ { TEXT("ticket"), SetupTicket }, { TEXT("username"), Name }, { TEXT("password"), Password } }),
        false,
        [this, Name](int32 Status, const TSharedPtr<FJsonObject>& Json)
        {
            if (Status != 201)
            {
                PendingPassword.Reset();
                if (Json.IsValid() && Json->GetStringField(TEXT("error")) == TEXT("setup_expired"))
                {
                    View.Step = EMarburgumAccountStep::Register;
                }
                Fail(Status, Json);
                return;
            }
            SetupTicket.Reset();
            UE_LOG(LogMarburgumNet, Log, TEXT("%s role=client event=account_created"), AdmissionLogPrefix);
            const FString Password = MoveTemp(PendingPassword);
            PendingPassword.Reset();
            View.bBusy = false;
            SignIn(Name, Password);
        });
}

void UMarburgumAccountSubsystem::RequestReset(const FString& Email)
{
    if (View.bBusy)
    {
        return;
    }
    View.Email = Email.TrimStartAndEnd();
    Call(TEXT("POST"), TEXT("/v1/forgot"), Fields({ { TEXT("email"), View.Email } }), false,
        [this](int32 Status, const TSharedPtr<FJsonObject>& Json)
        {
            if (Status != 202)
            {
                Fail(Status, Json);
                return;
            }
            View.Step = EMarburgumAccountStep::Reset;
            Note(FText::Format(LOCTEXT("ResetSent", "If {0} has an account, a reset code is on its way, with your username."),
                FText::FromString(View.Email)));
            Publish();
        });
}

void UMarburgumAccountSubsystem::Reset(const FString& Code, const FString& NewPassword)
{
    if (View.bBusy)
    {
        return;
    }
    Call(TEXT("POST"), TEXT("/v1/reset"), Fields({ { TEXT("email"), View.Email }, { TEXT("code"), Code }, { TEXT("password"), NewPassword } }),
        false,
        [this](int32 Status, const TSharedPtr<FJsonObject>& Json)
        {
            if (Status != 200)
            {
                Fail(Status, Json);
                return;
            }
            View.Step = EMarburgumAccountStep::SignIn;
            Note(LOCTEXT("ResetDone", "Password changed. Every other session was signed out. Sign in with the new password."));
            Publish();
        });
}

void UMarburgumAccountSubsystem::CreateCharacter(const FString& FirstName, const FString& LastName, FName Model)
{
    if (View.bBusy)
    {
        return;
    }
    if (CharacterRequest.IsEmpty())
    {
        CharacterRequest = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
    }
    Call(TEXT("POST"), TEXT("/v1/character"),
        Fields({ { TEXT("request"), CharacterRequest }, { TEXT("first_name"), FirstName }, { TEXT("last_name"), LastName },
            { TEXT("model"), Model.ToString() } }),
        true,
        [this](int32 Status, const TSharedPtr<FJsonObject>& Json)
        {
            if (Status != 200 || !Json.IsValid())
            {
                Fail(Status, Json);
                return;
            }
            CharacterRequest.Reset();
            AdoptAccount(Json);
            Note(FText::Format(LOCTEXT("Created", "{0} {1} is ready. Enter the world when you are."),
                FText::FromString(View.FirstName), FText::FromString(View.LastName)));
            UE_LOG(LogMarburgumNet, Log, TEXT("%s role=client event=character_created character=%s model=%s"), AdmissionLogPrefix,
                *View.CharacterId, *View.Model.ToString());
            Publish();
        });
}

void UMarburgumAccountSubsystem::SignOut()
{
    if (!Session.IsEmpty())
    {
        // Best effort: the session also expires on its own.
        const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
        Request->SetURL(ServiceUrl + TEXT("/v1/logout"));
        Request->SetVerb(TEXT("POST"));
        Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Session);
        Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
        Request->SetContentAsString(TEXT("{}"));
        Request->ProcessRequest();
    }
    Session.Reset();
    SetupTicket.Reset();
    CharacterRequest.Reset();
    const FString Email = View.Email;
    View = FMarburgumAccountView{ EMarburgumAccountStep::SignIn };
    View.Email = Email;
    Publish();
}

void UMarburgumAccountSubsystem::RequestTicket(FMarburgumTicketReady Done)
{
    if (Session.IsEmpty() || View.Step != EMarburgumAccountStep::Ready)
    {
        Done.ExecuteIfBound(FString(), LOCTEXT("SignInFirst", "Sign in and create your character first."));
        return;
    }
    const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(ServiceUrl + TEXT("/v1/admission"));
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Session);
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetTimeout(15.f);
    Request->SetContentAsString(TEXT("{}"));
    Request->OnProcessRequestComplete().BindWeakLambda(this,
        [this, Done](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
        {
            const int32 Status = bConnected && Response.IsValid() ? Response->GetResponseCode() : 0;
            TSharedPtr<FJsonObject> Json;
            if (Status != 0)
            {
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Json);
            }
            FString Ticket;
            if (Status == 200 && Json.IsValid() && Json->TryGetStringField(TEXT("ticket"), Ticket) && IsWellFormedTicket(Ticket))
            {
                Done.ExecuteIfBound(Ticket, FText::GetEmpty());
                return;
            }
            FString Code = Status == 0 ? TEXT("unreachable") : FString::Printf(TEXT("http_%d"), Status);
            if (Json.IsValid())
            {
                Json->TryGetStringField(TEXT("error"), Code);
            }
            if (Code == TEXT("session_invalid"))
            {
                Fail(Status, Json);
            }
            Done.ExecuteIfBound(FString(), DescribeError(Code));
        });
    Request->ProcessRequest();
}

#undef LOCTEXT_NAMESPACE
