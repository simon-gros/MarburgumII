#include "MarburgumAccountSmokeSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "MarburgumAccountContract.h"
#include "MarburgumCharacter.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumPlayerState.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Session/MarburgumAccountSubsystem.h"
#include "Session/MarburgumSessionSubsystem.h"
#include "UI/MarburgumAccountScreen.h"
#include "UI/MarburgumCharacterPreview.h"
#include "UI/MarburgumUISubsystem.h"
#include "UnrealClient.h"

#define MB2_ACCOUNT_LOG(Format, ...) \
    UE_LOG(LogMarburgumNet, Log, TEXT("MB2ACCOUNT role=client tag=%s " Format), *Role, ##__VA_ARGS__)

namespace MarburgumAccountSmoke
{
    constexpr double SettleSeconds = 1.0;
    constexpr double ShotTimeoutSeconds = 10.0;
    constexpr double OutboxWaitSeconds = 10.0;
}

using namespace MarburgumAccountSmoke;
using EStep = EMarburgumAccountStep;
using MarburgumII::Session::EPhase;

bool UMarburgumAccountSmokeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UGameInstance* GameInstance = Cast<UGameInstance>(Outer);
    return GameInstance && !GameInstance->IsDedicatedServerInstance() && FParse::Param(FCommandLine::Get(), TEXT("MB2AccountSmoke"));
}

void UMarburgumAccountSmokeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    const TCHAR* CommandLine = FCommandLine::Get();
    FParse::Value(CommandLine, TEXT("MB2AccountRole="), Role);
    FParse::Value(CommandLine, TEXT("MB2AccountShot="), ShotPrefix);
    FParse::Value(CommandLine, TEXT("MB2AccountTimeout="), TimeoutSeconds);
    FParse::Value(CommandLine, TEXT("MB2AccountHold="), HoldSeconds);
    FParse::Value(CommandLine, TEXT("MB2AccountOutbox="), Outbox);
    FParse::Value(CommandLine, TEXT("MB2AccountEmail="), Email);
    FParse::Value(CommandLine, TEXT("MB2AccountUser="), User);
    // From a file, so the password is not in the command line the engine logs.
    FString PasswordFile;
    if (FParse::Value(CommandLine, TEXT("MB2AccountPasswordFile="), PasswordFile))
    {
        FFileHelper::LoadFileToString(Password, *PasswordFile);
        Password.TrimStartAndEndInline();
    }
    FParse::Value(CommandLine, TEXT("MB2AccountFirst="), First);
    FParse::Value(CommandLine, TEXT("MB2AccountLast="), Last);
    FString Text;
    if (FParse::Value(CommandLine, TEXT("MB2AccountModel="), Text)) Model = FName(*Text);
    Role = Role.ToUpper();
    StartSeconds = FPlatformTime::Seconds();
    StepSeconds = StartSeconds;
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UMarburgumAccountSmokeSubsystem::Tick), 0.f);
    MB2_ACCOUNT_LOG("event=start model=%s shot=%d", *Model.ToString(), ShotPrefix.IsEmpty() ? 0 : 1);
}

void UMarburgumAccountSmokeSubsystem::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
    Super::Deinitialize();
}

UMarburgumAccountSubsystem* UMarburgumAccountSmokeSubsystem::Accounts() const
{
    return GetGameInstance() ? GetGameInstance()->GetSubsystem<UMarburgumAccountSubsystem>() : nullptr;
}

UMarburgumAccountScreen* UMarburgumAccountSmokeSubsystem::Screen() const
{
    const UMarburgumUISubsystem* UI = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMarburgumUISubsystem>() : nullptr;
    return UI ? UI->GetAccountScreen() : nullptr;
}

FString UMarburgumAccountSmokeSubsystem::CodeFromOutbox() const
{
    // What a player does with the email: read the newest message addressed to them and copy the code.
    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files, *FPaths::Combine(Outbox, TEXT("*.txt")), true, false);
    Files.Sort();
    for (int32 Index = Files.Num() - 1; Index >= 0; --Index)
    {
        FString Body;
        if (!FFileHelper::LoadFileToString(Body, *FPaths::Combine(Outbox, Files[Index])) || !Body.StartsWith(TEXT("To: ") + Email.ToLower()))
        {
            continue;
        }
        TArray<FString> Lines;
        Body.ParseIntoArrayLines(Lines);
        for (const FString& Line : Lines)
        {
            const FString Code = Line.TrimStartAndEnd();
            if (Code.Len() == MarburgumII::Accounts::CodeLength && Code.ToUpper() == Code && !Code.Contains(TEXT(" ")))
            {
                return Code;
            }
        }
        return FString();
    }
    return FString();
}

void UMarburgumAccountSmokeSubsystem::Expect(bool bCondition, const FString& What)
{
    if (!bCondition)
    {
        Problems.Add(What);
        MB2_ACCOUNT_LOG("event=expectation_failed what=%s", *What);
    }
}

void UMarburgumAccountSmokeSubsystem::Shot(const TCHAR* Label)
{
    if (ShotPrefix.IsEmpty())
    {
        return;
    }
    PendingShot = FString::Printf(TEXT("%s-%s.png"), *ShotPrefix, Label);
    IFileManager::Get().Delete(*PendingShot, false, true, true);
    FScreenshotRequest::RequestScreenshot(PendingShot, true, false);
    ShotRequestedSeconds = FPlatformTime::Seconds();
}

void UMarburgumAccountSmokeSubsystem::Advance(int32 Next, double Now)
{
    Step = Next;
    StepSeconds = Now;
    if (const UMarburgumAccountSubsystem* Subsystem = Accounts())
    {
        SeenRevision = Subsystem->GetView().Revision;
    }
}

bool UMarburgumAccountSmokeSubsystem::Tick(float DeltaTime)
{
    if (bDone)
    {
        return true;
    }
    const double Now = FPlatformTime::Seconds();
    if (Now - StartSeconds > TimeoutSeconds)
    {
        Fail(FString::Printf(TEXT("timeout step=%d"), Step));
        return true;
    }
    if (!PendingShot.IsEmpty())
    {
        const bool bSaved = IFileManager::Get().FileSize(*PendingShot) > 0;
        if (!bSaved && Now - ShotRequestedSeconds < ShotTimeoutSeconds)
        {
            return true;
        }
        MB2_ACCOUNT_LOG("event=shot file=%s saved=%d", *FPaths::GetCleanFilename(PendingShot), bSaved ? 1 : 0);
        Expect(bSaved, FString::Printf(TEXT("shot_not_saved=%s"), *FPaths::GetCleanFilename(PendingShot)));
        PendingShot.Reset();
    }
    if (Role == TEXT("REFUSED"))
    {
        TickRefused(Now);
        return true;
    }
    if (Role == TEXT("ADMITTED"))
    {
        TickAdmitted(Now);
        return true;
    }
    // The account steps need the front end's screens and an answer to the last request.
    const UMarburgumAccountSubsystem* Subsystem = Accounts();
    if (!Subsystem || !Subsystem->IsEnabled())
    {
        Fail(TEXT("accounts_disabled"));
        return true;
    }
    if (Step < 100 && (!Screen() || Subsystem->GetView().bBusy || Now - StepSeconds < SettleSeconds * 0.3))
    {
        return true;
    }
    if (Role == TEXT("FIRST")) TickFirst(Now);
    else if (Role == TEXT("RETURN")) TickReturn(Now);
    else Fail(TEXT("unknown_role"));
    return true;
}

void UMarburgumAccountSmokeSubsystem::TickFirst(double Now)
{
    UMarburgumAccountSubsystem* Subsystem = Accounts();
    UMarburgumAccountScreen* Form = Screen();
    const FMarburgumAccountView& View = Subsystem ? Subsystem->GetView() : FMarburgumAccountView();
    const bool bAnswered = View.Revision != SeenRevision;
    switch (Step)
    {
    case 0:
        if (Now - StepSeconds < SettleSeconds) return;
        Expect(View.Step == EStep::SignIn, TEXT("first_screen_not_sign_in"));
        Expect(!UMarburgumSessionSubsystem::IsAdmitted(GetGameInstance()->GetWorld()), TEXT("in_world_before_sign_in"));
        Shot(TEXT("signin"));
        Advance(1, Now);
        return;
    case 1:
        Subsystem->Show(EStep::Register);
        Form->SubmitForTest({ Email });
        Advance(2, Now);
        return;
    case 2:
        if (!bAnswered) return;
        Expect(View.Step == EStep::Confirm && !View.bError, FString::Printf(TEXT("register=%s"), *View.ErrorCode));
        // A wrong code first: refused, and the screen says so.
        Form->SubmitForTest({ TEXT("AAAA2222") });
        Advance(3, Now);
        return;
    case 3:
        if (!bAnswered) return;
        Expect(View.bError && View.ErrorCode == TEXT("code_invalid") && View.Step == EStep::Confirm, FString::Printf(TEXT("wrong_code=%s"), *View.ErrorCode));
        Shot(TEXT("code-refused"));
        Advance(4, Now);
        return;
    case 4:
    {
        const FString Code = CodeFromOutbox();
        if (Code.IsEmpty())
        {
            if (Now - StepSeconds > OutboxWaitSeconds) Fail(TEXT("no_code_in_outbox"));
            return;
        }
        MB2_ACCOUNT_LOG("event=code_read length=%d", Code.Len());
        Form->SubmitForTest({ Code.ToLower() });
        Advance(5, Now);
        return;
    }
    case 5:
        if (!bAnswered) return;
        Expect(View.Step == EStep::Credentials && !View.bError, FString::Printf(TEXT("confirm=%s"), *View.ErrorCode));
        // Two passwords that differ never leave the client.
        Form->SubmitForTest({ User, Password, Password + TEXT("x") });
        Advance(6, Now);
        return;
    case 6:
        if (Now - StepSeconds < SettleSeconds) return;
        Expect(View.Step == EStep::Credentials && View.Revision == SeenRevision, TEXT("mismatch_was_sent"));
        Shot(TEXT("credentials"));
        Form->SubmitForTest({ User, Password, Password });
        Advance(7, Now);
        return;
    case 7:
        if (View.Step != EStep::Character && !View.bError) return;
        Expect(View.Step == EStep::Character && View.Username == User.ToLower(), FString::Printf(TEXT("setup=%s step=%d"), *View.ErrorCode, (int32)View.Step));
        Expect(View.CharacterId.IsEmpty(), TEXT("character_before_creation"));
        PreviewModel = 0;
        Advance(71, Now);
        return;
    case 71:
        // The creator's 3D preview (ADR-057): every roster body in turn, as a player paging with ‹ ›.
        Form->SelectModelForTest(FName(MarburgumII::Accounts::CharacterModels[PreviewModel].Id));
        Advance(72, Now);
        return;
    case 72:
    {
        if (Now - StepSeconds < SettleSeconds * 1.5) return;
        const UMarburgumCharacterPreview* Preview = Form->GetPreview();
        const TCHAR* Id = MarburgumII::Accounts::CharacterModels[PreviewModel].Id;
        Expect(Preview && Preview->IsVisible() && Preview->GetShownModel() == PreviewModel,
            FString::Printf(TEXT("preview_not_shown=%s"), Id));
        MB2_ACCOUNT_LOG("event=preview model=%s shown=%d", Id, Preview && Preview->GetShownModel() == PreviewModel ? 1 : 0);
        Shot(*FString::Printf(TEXT("creator-%s"), Id));
        Advance(++PreviewModel < static_cast<int32>(UE_ARRAY_COUNT(MarburgumII::Accounts::CharacterModels)) ? 71 : 73, Now);
        return;
    }
    case 73:
    {
        // Dragging turns the body; the same path as the mouse, by a known amount.
        UMarburgumCharacterPreview* Preview = Form->GetPreview();
        const float Before = Preview ? Preview->GetYaw() : 0.f;
        if (Preview) Preview->TurnForTest(-140.f);
        Expect(Preview && !FMath::IsNearlyEqual(Preview->GetYaw(), Before), TEXT("preview_did_not_turn"));
        Advance(74, Now);
        return;
    }
    case 74:
        if (Now - StepSeconds < SettleSeconds) return;
        Shot(TEXT("creator-turned"));
        Advance(76, Now);
        return;
    case 76:
    {
        // The wheel brings the camera to head and shoulders, the same path as a player's scroll.
        UMarburgumCharacterPreview* Preview = Form->GetPreview();
        if (Preview) Preview->ZoomForTest(4.f);
        Expect(Preview && FMath::IsNearlyEqual(Preview->GetZoom(), 1.f), TEXT("preview_did_not_zoom"));
        Advance(77, Now);
        return;
    }
    case 77:
        if (Now - StepSeconds < SettleSeconds) return;
        Shot(TEXT("creator-close"));
        Advance(78, Now);
        return;
    case 78:
        // The Protagonist's face up close and from the front, undoing the turn: the textured body, so a blurred mip
        // shows here first.
        Form->SelectModelForTest(FName(MarburgumII::Accounts::CharacterModels[0].Id));
        if (UMarburgumCharacterPreview* Preview = Form->GetPreview()) Preview->TurnForTest(140.f);
        Advance(79, Now);
        return;
    case 79:
        if (Now - StepSeconds < SettleSeconds * 1.5) return;
        Shot(*FString::Printf(TEXT("creator-%s-close"), MarburgumII::Accounts::CharacterModels[0].Id));
        Advance(75, Now);
        return;
    case 75:
        // After the picture is saved: the screenshot is taken at the end of a later frame.
        if (UMarburgumCharacterPreview* Preview = Form->GetPreview()) Preview->ZoomForTest(-4.f);
        Form->SelectModelForTest(Model);
        Form->SubmitForTest({ TEXT("R2D2"), Last });
        Advance(8, Now);
        return;
    case 8:
        if (!bAnswered) return;
        Expect(View.bError && View.ErrorCode == TEXT("first_name_characters") && View.Step == EStep::Character,
            FString::Printf(TEXT("bad_name=%s"), *View.ErrorCode));
        Shot(TEXT("name-refused"));
        Advance(9, Now);
        return;
    case 9:
        Form->SubmitForTest({ First, Last });
        Advance(10, Now);
        return;
    case 10:
        if (!bAnswered) return;
        Expect(View.Step == EStep::Ready && View.FirstName == First && View.LastName == Last && View.Model == Model,
            FString::Printf(TEXT("create=%s first=%s model=%s"), *View.ErrorCode, *View.FirstName, *View.Model.ToString()));
        MB2_ACCOUNT_LOG("event=character_ready character=%s", *View.CharacterId);
        Advance(100, Now);
        return;
    default:
        if (TickEnter(Now, 100, TEXT("in-world")))
        {
            Pass();
        }
        return;
    }
}

void UMarburgumAccountSmokeSubsystem::TickReturn(double Now)
{
    UMarburgumAccountSubsystem* Subsystem = Accounts();
    UMarburgumAccountScreen* Form = Screen();
    const FMarburgumAccountView& View = Subsystem->GetView();
    const bool bAnswered = View.Revision != SeenRevision;
    switch (Step)
    {
    case 0:
        if (Now - StepSeconds < SettleSeconds) return;
        Form->SubmitForTest({ User, TEXT("not-the-password") });
        Advance(1, Now);
        return;
    case 1:
        if (!bAnswered) return;
        Expect(View.bError && View.ErrorCode == TEXT("credentials_invalid") && View.Step == EStep::SignIn,
            FString::Printf(TEXT("wrong_password=%s"), *View.ErrorCode));
        Shot(TEXT("password-refused"));
        Advance(2, Now);
        return;
    case 2:
        Form->SubmitForTest({ User.ToUpper(), Password });
        Advance(3, Now);
        return;
    case 3:
        if (!bAnswered) return;
        // A returning player skips the creator.
        Expect(View.Step == EStep::Ready && View.FirstName == First && View.LastName == Last && View.Model == Model,
            FString::Printf(TEXT("return=%s step=%d first=%s"), *View.ErrorCode, (int32)View.Step, *View.FirstName));
        MB2_ACCOUNT_LOG("event=character_ready character=%s", *View.CharacterId);
        Shot(TEXT("ready"));
        Advance(100, Now);
        return;
    default:
        if (TickEnter(Now, 100, TEXT("returned")))
        {
            Pass();
        }
        return;
    }
}

bool UMarburgumAccountSmokeSubsystem::TickEnter(double Now, int32 FirstStep, const TCHAR* ShotLabel)
{
    UGameInstance* GameInstance = GetGameInstance();
    UMarburgumSessionSubsystem* Session = GameInstance->GetSubsystem<UMarburgumSessionSubsystem>();
    UWorld* World = GameInstance->GetWorld();
    if (Step == FirstStep)
    {
        if (Now - StepSeconds < SettleSeconds) return false;
        Session->Connect(UMarburgumSessionSubsystem::DefaultAddress());
        Advance(FirstStep + 1, Now);
        return false;
    }
    if (Step == FirstStep + 1)
    {
        if (Session->GetView().Phase == EPhase::Error)
        {
            Fail(FString::Printf(TEXT("connect_failed detail=%s"), *Session->GetView().Detail));
            return false;
        }
        const APlayerController* Local = UMarburgumSessionSubsystem::IsAdmitted(World) ? World->GetFirstPlayerController() : nullptr;
        const AMarburgumPlayerState* State = Local ? Local->GetPlayerState<AMarburgumPlayerState>() : nullptr;
        const AMarburgumCharacter* Body = Local ? Cast<AMarburgumCharacter>(Local->GetPawn()) : nullptr;
        if (!State || !Body || !State->HasIdentity() || Body->GetAppliedModel().IsNone())
        {
            return false;
        }
        const FString Expected = First + TEXT(" ") + Last;
        Expect(State->GetPlayerName() == Expected, FString::Printf(TEXT("name=%s want %s"), *State->GetPlayerName(), *Expected));
        Expect(State->GetModel() == Model && Body->GetAppliedModel() == Model, FString::Printf(TEXT("model=%s body=%s"),
            *State->GetModel().ToString(), *Body->GetAppliedModel().ToString()));
        Expect(State->HasCharacter(), TEXT("no_custody"));
        MB2_ACCOUNT_LOG("event=in_world player=%d name=\"%s\" model=%s", State->GetPlayerId(), *State->GetPlayerName(), *State->GetModel().ToString());
        Advance(FirstStep + 2, Now);
        return false;
    }
    if (Step == FirstStep + 2)
    {
        if (Now - StepSeconds < SettleSeconds * 2.0) return false;
        Shot(ShotLabel);
        Advance(FirstStep + 3, Now);
        return false;
    }
    return PendingShot.IsEmpty();
}

void UMarburgumAccountSmokeSubsystem::TickRefused(double Now)
{
    const UMarburgumSessionSubsystem* Session = GetGameInstance()->GetSubsystem<UMarburgumSessionSubsystem>();
    if (!Session)
    {
        return;
    }
    if (UMarburgumSessionSubsystem::IsAdmitted(GetGameInstance()->GetWorld()))
    {
        Fail(TEXT("admitted"));
        return;
    }
    const FMarburgumSessionView& View = Session->GetView();
    if (View.Phase == EPhase::Error)
    {
        MB2_ACCOUNT_LOG("event=refused fault=%s detail=\"%s\"", MarburgumII::Session::ToString(View.Fault), *View.Detail.Left(160));
        Pass();
    }
}

void UMarburgumAccountSmokeSubsystem::TickAdmitted(double Now)
{
    const UMarburgumSessionSubsystem* Session = GetGameInstance()->GetSubsystem<UMarburgumSessionSubsystem>();
    if (Session && Session->GetView().Phase == EPhase::Error)
    {
        Fail(FString::Printf(TEXT("refused detail=%s"), *Session->GetView().Detail.Left(160)));
        return;
    }
    UWorld* World = GetGameInstance()->GetWorld();
    const APlayerController* Local = UMarburgumSessionSubsystem::IsAdmitted(World) ? World->GetFirstPlayerController() : nullptr;
    const AMarburgumPlayerState* State = Local ? Local->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    if (!State || !State->HasIdentity())
    {
        return;
    }
    if (Step == 0)
    {
        Expect(State->GetPlayerName() == First + TEXT(" ") + Last && State->GetModel() == Model,
            FString::Printf(TEXT("name=%s model=%s"), *State->GetPlayerName(), *State->GetModel().ToString()));
        MB2_ACCOUNT_LOG("event=in_world player=%d name=\"%s\" model=%s", State->GetPlayerId(), *State->GetPlayerName(), *State->GetModel().ToString());
        Advance(1, Now);
        return;
    }
    if (Now - StepSeconds >= HoldSeconds)
    {
        Pass();
    }
}

void UMarburgumAccountSmokeSubsystem::Pass()
{
    if (Problems.IsEmpty())
    {
        MB2_ACCOUNT_LOG("event=pass");
        bDone = true;
        FPlatformMisc::RequestExitWithStatus(false, 0);
    }
    else
    {
        Fail(TEXT("expectations"));
    }
}

void UMarburgumAccountSmokeSubsystem::Fail(const FString& Reason)
{
    if (bDone)
    {
        return;
    }
    Problems.Add(Reason);
    MB2_ACCOUNT_LOG("event=fail reason=%s", *FString::Join(Problems, TEXT(",")));
    bDone = true;
    FPlatformMisc::RequestExitWithStatus(false, 1);
}

#undef MB2_ACCOUNT_LOG
