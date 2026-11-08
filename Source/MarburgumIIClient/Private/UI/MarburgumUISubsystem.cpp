#include "UI/MarburgumUISubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "MarburgumNetworkContract.h"
#include "UI/MarburgumConnectionScreen.h"
#include "UI/MarburgumNotificationFeed.h"
#include "UI/MarburgumUIGallery.h"
#include "UI/MarburgumUIRoot.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "MarburgumUISubsystem"

using MarburgumII::Session::EPhase;

bool UMarburgumUISubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UGameInstance* GameInstance = Cast<UGameInstance>(Outer);
    return GameInstance && !GameInstance->IsDedicatedServerInstance() && FApp::CanEverRender();
}

void UMarburgumUISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    const TCHAR* CommandLine = FCommandLine::Get();
    bGalleryMode = FParse::Param(CommandLine, TEXT("MB2UIGallery"));
    bDiagnostics = FParse::Param(CommandLine, TEXT("MB2UIDiagnostics"));
    float Scale = 1.f;
    if (FParse::Value(CommandLine, TEXT("MB2UIScale="), Scale))
    {
        SetInterfaceScale(Scale);
    }

    Session = Collection.InitializeDependency<UMarburgumSessionSubsystem>();
    if (UMarburgumSessionSubsystem* SessionSubsystem = Session.Get())
    {
        SessionChangedHandle = SessionSubsystem->OnSessionChanged().AddUObject(this, &UMarburgumUISubsystem::HandleSessionChanged);
    }
    PreLoadMapHandle = FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &UMarburgumUISubsystem::HandlePreLoadMap);
    PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UMarburgumUISubsystem::HandlePostLoadMap);
}

void UMarburgumUISubsystem::Deinitialize()
{
    FCoreUObjectDelegates::PreLoadMap.Remove(PreLoadMapHandle);
    FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
    if (UMarburgumSessionSubsystem* SessionSubsystem = Session.Get())
    {
        SessionSubsystem->OnSessionChanged().Remove(SessionChangedHandle);
    }
    TearDown();
    Super::Deinitialize();
}

void UMarburgumUISubsystem::SetInterfaceScale(float Scale)
{
    if (FSlateApplication::IsInitialized())
    {
        FSlateApplication::Get().SetApplicationScale(FMath::Clamp(Scale, 0.5f, 3.f));
    }
}

void UMarburgumUISubsystem::HandlePreLoadMap(const FString& MapName)
{
    // Widgets must not outlive the world whose actors they may reference.
    TearDown();
}

void UMarburgumUISubsystem::HandlePostLoadMap(UWorld* World)
{
    if (World && World->GetGameInstance() == GetGameInstance())
    {
        BuildForWorld(World);
    }
}

void UMarburgumUISubsystem::TearDown()
{
    if (Root)
    {
        Root->RemoveFromParent();
    }
    Root = nullptr;
    ConnectionScreen = nullptr;
    Feed = nullptr;
}

void UMarburgumUISubsystem::BuildForWorld(UWorld* World)
{
    TearDown();
    UGameInstance* GameInstance = GetGameInstance();
    Root = CreateWidget<UMarburgumUIRoot>(GameInstance, UMarburgumUIRoot::StaticClass());
    if (!Root)
    {
        return;
    }
    Root->AddToViewport(0);

    Feed = CreateWidget<UMarburgumNotificationFeed>(GameInstance, UMarburgumNotificationFeed::StaticClass());
    Root->AddToLayer(EMarburgumUILayer::Notifications, Feed);

    const bool bServerWorld = World->GetNetMode() == NM_Client;
    if (bDiagnostics && bServerWorld)
    {
        Root->AddToLayer(EMarburgumUILayer::Diagnostic,
            CreateWidget<UMarburgumDiagnosticsOverlay>(GameInstance, UMarburgumDiagnosticsOverlay::StaticClass()));
    }

    if (bGalleryMode && !bServerWorld)
    {
        Root->AddToLayer(EMarburgumUILayer::Screens, CreateWidget<UMarburgumUIGallery>(GameInstance, UMarburgumUIGallery::StaticClass()));
        return;
    }

    ConnectionScreen = CreateWidget<UMarburgumConnectionScreen>(GameInstance, UMarburgumConnectionScreen::StaticClass());
    Root->AddToLayer(EMarburgumUILayer::Screens, ConnectionScreen);

    if (UMarburgumSessionSubsystem* SessionSubsystem = Session.Get())
    {
        HandleSessionChanged(SessionSubsystem->GetView());
    }
}

void UMarburgumUISubsystem::HandleSessionChanged(const FMarburgumSessionView& View)
{
    const EPhase PreviousPhase = LastPhase;
    LastPhase = View.Phase;
    if (View.Phase == EPhase::Reconnecting)
    {
        bRecoveredFromLoss = true;
    }
    else if (View.Phase == EPhase::Idle || View.Phase == EPhase::Error)
    {
        bRecoveredFromLoss = false;
    }

    if (!ConnectionScreen)
    {
        return;
    }
    ConnectionScreen->ApplyView(View);
    const bool bShowScreen = View.Phase != EPhase::Ready;
    ConnectionScreen->SetVisibility(bShowScreen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    ApplyInputMode(bShowScreen);

    if (View.Phase == EPhase::Ready && PreviousPhase != EPhase::Ready && Feed)
    {
        // One toast per admission; the dedupe key collapses repeated reports of the same event.
        const FText Message = FText::Format(
            bRecoveredFromLoss ? LOCTEXT("Reconnected", "Reconnected to {0}  ·  network contract v{1}")
                               : LOCTEXT("Connected", "Connected to {0}  ·  network contract v{1}"),
            FText::FromString(View.Address), FText::AsNumber(MarburgumII::Network::ContractVersion));
        Feed->Push(TEXT("session.admitted"), LOCTEXT("ConnectionKind", "CONNECTION"), Message, MarburgumUI::ETone::Success);
        bRecoveredFromLoss = false;
    }
}

void UMarburgumUISubsystem::ApplyInputMode(bool bScreenOwnsInput)
{
    const UGameInstance* GameInstance = GetGameInstance();
    const UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
    APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
    if (!Controller || !Controller->IsLocalController())
    {
        return;
    }
    if (bScreenOwnsInput && ConnectionScreen)
    {
        FInputModeUIOnly Mode;
        if (UWidget* Focus = ConnectionScreen->GetPreferredFocus())
        {
            Mode.SetWidgetToFocus(Focus->TakeWidget());
        }
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        Controller->SetInputMode(Mode);
        Controller->SetShowMouseCursor(true);
    }
    else
    {
        Controller->SetInputMode(FInputModeGameOnly());
        Controller->SetShowMouseCursor(false);
    }
}

#undef LOCTEXT_NAMESPACE
