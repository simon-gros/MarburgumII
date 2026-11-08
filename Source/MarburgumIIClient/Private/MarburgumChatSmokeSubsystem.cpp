#include "MarburgumChatSmokeSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumPlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Session/MarburgumSessionSubsystem.h"
#include "UnrealClient.h"

#define MB2_CHAT_LOG(Format, ...) \
    UE_LOG(LogMarburgumNet, Log, TEXT("MB2CHAT role=client tag=%s " Format), *Role, ##__VA_ARGS__)

namespace
{
    constexpr double StepGapSeconds = 1.2;   // > server chat spacing (0.8 s) and notice throttle (1 s).
    constexpr double ReplyDelaySeconds = 1.0;
    constexpr double LingerSeconds = 2.5;
    constexpr double ShotTimeoutSeconds = 10.0;
    constexpr int32 GhostRecipientId = 999999;

    const TCHAR* ChannelName(EMarburgumChatChannel Channel)
    {
        switch (Channel)
        {
        case EMarburgumChatChannel::General: return TEXT("general");
        case EMarburgumChatChannel::Private: return TEXT("private");
        case EMarburgumChatChannel::System: return TEXT("system");
        }
        return TEXT("unknown");
    }
}

bool UMarburgumChatSmokeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UGameInstance* GameInstance = Cast<UGameInstance>(Outer);
    return GameInstance && !GameInstance->IsDedicatedServerInstance()
        && FParse::Param(FCommandLine::Get(), TEXT("MB2ChatSmoke"));
}

void UMarburgumChatSmokeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    const TCHAR* CommandLine = FCommandLine::Get();
    FParse::Value(CommandLine, TEXT("MB2ChatRole="), Role);
    FParse::Value(CommandLine, TEXT("MB2ChatToken="), Token);
    FParse::Value(CommandLine, TEXT("MB2ChatTimeout="), TimeoutSeconds);
    FParse::Value(CommandLine, TEXT("MB2ChatShot="), ShotPath);
    Role = Role.ToUpper();
    StartSeconds = FPlatformTime::Seconds();
    OperationId = FGuid::NewGuid();
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UMarburgumChatSmokeSubsystem::Tick), 0.05f);
    MB2_CHAT_LOG("event=start timeout=%.0f shot=%d", TimeoutSeconds, ShotPath.IsEmpty() ? 0 : 1);
}

void UMarburgumChatSmokeSubsystem::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
    if (AMarburgumPlayerController* Bound = Controller.Get())
    {
        Bound->OnChatReceived.RemoveDynamic(this, &UMarburgumChatSmokeSubsystem::HandleChat);
        Bound->OnRequestResult.RemoveAll(this);
    }
    Super::Deinitialize();
}

FString UMarburgumChatSmokeSubsystem::Body(const TCHAR* Kind) const
{
    // Slovene diacritics in the general body prove ordinary Unicode survives server sanitising.
    const FString Kinds(Kind);
    if (Kinds == TEXT("G1")) return FString::Printf(TEXT("general %s čšž ČŠŽ Šentilj"), *Token);
    if (Kinds == TEXT("P1")) return FString::Printf(TEXT("private-%s for B only"), *Token);
    if (Kinds == TEXT("R1")) return FString::Printf(TEXT("reply-%s from B"), *Token);
    if (Kinds == TEXT("GHOST")) return FString::Printf(TEXT("ghost-%s"), *Token);
    if (Kinds == TEXT("BAD")) return FString::Printf(TEXT("bad\nline %s"), *Token);
    if (Kinds == TEXT("BURST1")) return FString::Printf(TEXT("burst1 %s"), *Token);
    if (Kinds == TEXT("BURST2")) return FString::Printf(TEXT("burst2 %s"), *Token);
    if (Kinds == TEXT("END")) return FString::Printf(TEXT("end %s"), *Token);
    return FString();
}

bool UMarburgumChatSmokeSubsystem::ResolveRoster()
{
    const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    if (!UMarburgumSessionSubsystem::IsAdmitted(World))
    {
        return false;
    }
    AMarburgumPlayerController* Local = Cast<AMarburgumPlayerController>(World->GetFirstPlayerController());
    if (bRosterLogged)
    {
        // Peers finish and leave at different times; once IDs are known only this session matters.
        return Local && Local == Controller.Get();
    }
    const AGameStateBase* GameState = World->GetGameState();
    if (!Local || !GameState || !Local->PlayerState)
    {
        return false;
    }
    // The engine ignores ?Name= on join, so ask the server to rename this player. The name is only
    // a test label for discovering session IDs; chat routing never trusts names.
    const FString OwnName = FString::Printf(TEXT("MB2Chat%s"), *Role);
    if (Local->PlayerState->GetPlayerName() != OwnName)
    {
        if (!bRenameRequested)
        {
            bRenameRequested = true;
            Local->ServerChangeName(OwnName);
        }
        return false;
    }
    IdA = IdB = IdC = INDEX_NONE;
    for (const APlayerState* State : GameState->PlayerArray)
    {
        if (!State) continue;
        const FString Name = State->GetPlayerName();
        if (Name == TEXT("MB2ChatA")) IdA = State->GetPlayerId();
        else if (Name == TEXT("MB2ChatB")) IdB = State->GetPlayerId();
        else if (Name == TEXT("MB2ChatC")) IdC = State->GetPlayerId();
    }
    if (IdA == INDEX_NONE || IdB == INDEX_NONE || IdC == INDEX_NONE)
    {
        return false;
    }
    if (Controller.Get() != Local)
    {
        Local->OnChatReceived.AddUniqueDynamic(this, &UMarburgumChatSmokeSubsystem::HandleChat);
        Local->OnRequestResult.AddUObject(this, &UMarburgumChatSmokeSubsystem::HandleRequestResult);
        Controller = Local;
        // Deliveries can arrive before this client sees the full roster; count them from history.
        Received.Reset();
        for (const FMarburgumChatMessage& Earlier : Local->RecentChat)
        {
            HandleChat(Earlier);
        }
    }
    if (!bRosterLogged)
    {
        bRosterLogged = true;
        MB2_CHAT_LOG("event=roster self=%d a=%d b=%d c=%d", Local->PlayerState->GetPlayerId(), IdA, IdB, IdC);
    }
    return true;
}

void UMarburgumChatSmokeSubsystem::HandleChat(const FMarburgumChatMessage& Message)
{
    Received.Add({ Message, FPlatformTime::Seconds() });
    // Label known bodies instead of logging them; private text must never reach a log.
    const TCHAR* Known[] = { TEXT("G1"), TEXT("P1"), TEXT("R1"), TEXT("GHOST"), TEXT("BAD"), TEXT("BURST1"), TEXT("BURST2"), TEXT("END") };
    FString Label = Message.Channel == EMarburgumChatChannel::System ? TEXT("notice") : TEXT("other");
    for (const TCHAR* Kind : Known)
    {
        if (Message.Text == Body(Kind))
        {
            Label = Kind;
        }
    }
    MB2_CHAT_LOG("event=received channel=%s sender=%d recipient=%d seq=%d body=%s",
        ChannelName(Message.Channel), Message.SenderPlayerId, Message.RecipientPlayerId, Message.SenderSequence, *Label);
}

void UMarburgumChatSmokeSubsystem::HandleRequestResult(const FGuid& ResultId, EMarburgumRequestOutcome Outcome)
{
    if (ResultId == OperationId)
    {
        Outcomes.Add(Outcome);
        MB2_CHAT_LOG("event=receipt outcome=%d", static_cast<int32>(Outcome));
    }
}

int32 UMarburgumChatSmokeSubsystem::CountGeneral(const FString& Text) const
{
    int32 Count = 0;
    for (const FReceived& Entry : Received)
    {
        Count += Entry.Message.Channel == EMarburgumChatChannel::General && Entry.Message.Text == Text ? 1 : 0;
    }
    return Count;
}

int32 UMarburgumChatSmokeSubsystem::CountPrivate(int32 SenderId, int32 RecipientId, const FString& Text) const
{
    int32 Count = 0;
    for (const FReceived& Entry : Received)
    {
        const FMarburgumChatMessage& M = Entry.Message;
        Count += M.Channel == EMarburgumChatChannel::Private && M.SenderPlayerId == SenderId
            && M.RecipientPlayerId == RecipientId && M.Text == Text ? 1 : 0;
    }
    return Count;
}

int32 UMarburgumChatSmokeSubsystem::CountChannel(EMarburgumChatChannel Channel) const
{
    int32 Count = 0;
    for (const FReceived& Entry : Received)
    {
        Count += Entry.Message.Channel == Channel ? 1 : 0;
    }
    return Count;
}

bool UMarburgumChatSmokeSubsystem::AnyBodyContains(const FString& Fragment) const
{
    for (const FReceived& Entry : Received)
    {
        if (Entry.Message.Text.Contains(Fragment, ESearchCase::CaseSensitive))
        {
            return true;
        }
    }
    return false;
}

bool UMarburgumChatSmokeSubsystem::Tick(float DeltaTime)
{
    if (bDone)
    {
        return true;
    }
    const double Now = FPlatformTime::Seconds();

    if (bEvaluated)
    {
        // Waiting for the evidence screenshot before exiting.
        if (IFileManager::Get().FileSize(*ShotPath) > 0 || Now - ShotRequestedSeconds > ShotTimeoutSeconds)
        {
            MB2_CHAT_LOG("event=shot saved=%d", IFileManager::Get().FileSize(*ShotPath) > 0 ? 1 : 0);
            Finish(PendingExit);
        }
        return true;
    }
    if (Now - StartSeconds > TimeoutSeconds)
    {
        Fail(FString::Printf(TEXT("timeout step=%d"), Step));
        return true;
    }
    if (Now - LastHeartbeatLogSeconds >= 2.0)
    {
        LastHeartbeatLogSeconds = Now;
        const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
        MB2_CHAT_LOG("event=tick step=%d admitted=%d frame=%llu dt=%.3f", Step,
            UMarburgumSessionSubsystem::IsAdmitted(World) ? 1 : 0, (unsigned long long)GFrameCounter, DeltaTime);
    }
    if (!ResolveRoster())
    {
        return true;
    }

    if (EndSeenSeconds == 0.0 && CountGeneral(Body(TEXT("END"))) > 0)
    {
        EndSeenSeconds = Now;
    }

    if (Role == TEXT("A"))
    {
        TickRoleA(Now);
    }
    else
    {
        if (Role == TEXT("B"))
        {
            TickRoleB(Now);
        }
        if (EndSeenSeconds > 0.0 && Now - EndSeenSeconds >= LingerSeconds)
        {
            Evaluate();
        }
    }
    return true;
}

void UMarburgumChatSmokeSubsystem::TickRoleA(double Now)
{
    AMarburgumPlayerController* Local = Controller.Get();
    const bool bGap = Now - StepSeconds >= StepGapSeconds;
    auto Advance = [this, Now](int32 Next) { Step = Next; StepSeconds = Now; };

    switch (Step)
    {
    case 0:
        Local->SendGeneralChat(Body(TEXT("G1")));
        MB2_CHAT_LOG("event=sent kind=G1");
        Advance(1);
        return;
    case 1:
        if (CountGeneral(Body(TEXT("G1"))) > 0 && bGap)
        {
            Local->SendPrivateChat(IdB, Body(TEXT("P1")));
            MB2_CHAT_LOG("event=sent kind=P1 to=%d", IdB);
            Advance(2);
        }
        return;
    case 2:
        if (CountPrivate(IdB, IdA, Body(TEXT("R1"))) > 0)
        {
            // Evidence view of the private exchange before later entries scroll it away.
            if (!ShotPath.IsEmpty())
            {
                FScreenshotRequest::RequestScreenshot(ShotPath.Replace(TEXT(".png"), TEXT("-private.png")), true, false);
            }
            Advance(3);
        }
        return;
    case 3:
        if (bGap)
        {
            Local->SendPrivateChat(GhostRecipientId, Body(TEXT("GHOST")));
            MB2_CHAT_LOG("event=sent kind=GHOST to=%d", GhostRecipientId);
            Advance(4);
        }
        return;
    case 4:
        if (CountChannel(EMarburgumChatChannel::System) >= 1 && bGap)
        {
            Local->SendGeneralChat(Body(TEXT("BAD")));
            MB2_CHAT_LOG("event=sent kind=BAD");
            Advance(5);
        }
        return;
    case 5:
        if (CountChannel(EMarburgumChatChannel::System) >= 2 && bGap)
        {
            Local->SendGeneralChat(Body(TEXT("BURST1")));
            Local->SendGeneralChat(Body(TEXT("BURST2")));
            MB2_CHAT_LOG("event=sent kind=BURST1+BURST2");
            Advance(6);
        }
        return;
    case 6:
        if (CountChannel(EMarburgumChatChannel::System) >= 3 && CountGeneral(Body(TEXT("BURST1"))) > 0 && bGap)
        {
            Local->SendGeneralChat(Body(TEXT("END")));
            MB2_CHAT_LOG("event=sent kind=END");
            Advance(7);
        }
        return;
    case 7:
        if (EndSeenSeconds > 0.0)
        {
            // One authoritative action submitted twice: the HUD must show a single deduped receipt.
            Local->ServerSubmitOperation(OperationId);
            Local->ServerSubmitOperation(OperationId);
            MB2_CHAT_LOG("event=sent kind=OPERATION_TWICE");
            Advance(8);
        }
        return;
    case 8:
        if (Outcomes.Num() >= 2)
        {
            Advance(9);
        }
        return;
    case 9:
        if (Now - StepSeconds >= LingerSeconds)
        {
            Evaluate();
        }
        return;
    default:
        return;
    }
}

void UMarburgumChatSmokeSubsystem::TickRoleB(double Now)
{
    switch (Step)
    {
    case 0:
        if (CountPrivate(IdA, IdB, Body(TEXT("P1"))) > 0)
        {
            Step = 1;
            StepSeconds = Now;
        }
        return;
    case 1:
        if (Now - StepSeconds >= ReplyDelaySeconds)
        {
            Controller->SendPrivateChat(IdA, Body(TEXT("R1")));
            MB2_CHAT_LOG("event=sent kind=R1 to=%d", IdA);
            Step = 2;
        }
        return;
    default:
        return;
    }
}

void UMarburgumChatSmokeSubsystem::Evaluate()
{
    if (bEvaluated)
    {
        return;
    }
    TArray<FString> Problems;
    auto Expect = [&Problems](bool bCondition, const FString& What)
    {
        if (!bCondition) Problems.Add(What);
    };

    // Every role: each accepted general message exactly once, from A, with its exact Unicode text.
    for (const TCHAR* Kind : { TEXT("G1"), TEXT("BURST1"), TEXT("END") })
    {
        const int32 Count = CountGeneral(Body(Kind));
        Expect(Count == 1, FString::Printf(TEXT("general_%s=%d"), Kind, Count));
    }
    for (const FReceived& Entry : Received)
    {
        if (Entry.Message.Channel == EMarburgumChatChannel::General)
        {
            Expect(Entry.Message.SenderPlayerId == IdA, TEXT("general_sender_not_A"));
        }
    }
    Expect(CountGeneral(Body(TEXT("BURST2"))) == 0, TEXT("throttled_message_delivered"));
    Expect(!AnyBodyContains(TEXT("bad")), TEXT("invalid_text_delivered"));
    Expect(!AnyBodyContains(TEXT("ghost-")), TEXT("ghost_recipient_delivered"));

    const int32 Privates = CountChannel(EMarburgumChatChannel::Private);
    const int32 Notices = CountChannel(EMarburgumChatChannel::System);
    const int32 P1 = CountPrivate(IdA, IdB, Body(TEXT("P1")));
    const int32 R1 = CountPrivate(IdB, IdA, Body(TEXT("R1")));

    if (Role == TEXT("A") || Role == TEXT("B"))
    {
        Expect(P1 == 1, FString::Printf(TEXT("private_P1=%d"), P1));
        Expect(R1 == 1, FString::Printf(TEXT("private_R1=%d"), R1));
        Expect(Privates == 2, FString::Printf(TEXT("private_total=%d"), Privates));
    }
    if (Role == TEXT("A"))
    {
        // Invalid recipient, invalid text and the throttled burst each earn one refusal.
        Expect(Notices == 3, FString::Printf(TEXT("system_total=%d"), Notices));
        Expect(Outcomes.Num() == 2 && Outcomes[0] == EMarburgumRequestOutcome::Applied
            && Outcomes[1] == EMarburgumRequestOutcome::Duplicate, TEXT("operation_receipts"));
    }
    else
    {
        Expect(Notices == 0, FString::Printf(TEXT("system_total=%d"), Notices));
    }
    if (Role == TEXT("C"))
    {
        // The bystander: nothing private, by channel or by content.
        Expect(Privates == 0, FString::Printf(TEXT("bystander_private_total=%d"), Privates));
        Expect(!AnyBodyContains(TEXT("private-")) && !AnyBodyContains(TEXT("reply-")), TEXT("bystander_saw_private_text"));
    }

    MB2_CHAT_LOG("event=counts general=%d private=%d system=%d receipts=%d",
        CountChannel(EMarburgumChatChannel::General), Privates, Notices, Outcomes.Num());
    if (Problems.IsEmpty())
    {
        MB2_CHAT_LOG("event=pass");
        PendingExit = 0;
    }
    else
    {
        MB2_CHAT_LOG("event=fail reason=%s", *FString::Join(Problems, TEXT(",")));
        PendingExit = 1;
    }

    bEvaluated = true;
    if (ShotPath.IsEmpty())
    {
        Finish(PendingExit);
        return;
    }
    IFileManager::Get().Delete(*ShotPath, false, true, true);
    FScreenshotRequest::RequestScreenshot(ShotPath, true, false);
    ShotRequestedSeconds = FPlatformTime::Seconds();
}

void UMarburgumChatSmokeSubsystem::Fail(const FString& Reason)
{
    MB2_CHAT_LOG("event=fail reason=%s", *Reason);
    Finish(1);
}

void UMarburgumChatSmokeSubsystem::Finish(int32 ExitCode)
{
    if (bDone)
    {
        return;
    }
    bDone = true;
    FPlatformMisc::RequestExitWithStatus(false, static_cast<uint8>(ExitCode));
}

#undef MB2_CHAT_LOG
