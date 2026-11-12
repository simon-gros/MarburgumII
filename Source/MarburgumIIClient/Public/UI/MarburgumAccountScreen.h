#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Session/MarburgumAccountSubsystem.h"
#include "Types/SlateEnums.h"
#include "MarburgumAccountScreen.generated.h"

class UBorder;
class UButton;
class UEditableTextBox;
class UTextBlock;
class UVerticalBox;
class UWidget;

/**
 * The first-launch screens (ADR-055, UI-02A): sign in, create an account with an emailed code, choose a username
 * and password, reset a forgotten password, and create the first character. Shows only what the account
 * subsystem reports; it never decides that a sign-in or a save succeeded. Once the character is ready the
 * connection screen takes over, and its Connect enters the world.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumAccountScreen : public UUserWidget
{
    GENERATED_BODY()

public:
    void ApplyView(const FMarburgumAccountView& View);
    UWidget* GetPreferredFocus() const;

    /** Test driver: type into the current step's fields in order and press its primary action. */
    void SubmitForTest(const TArray<FString>& Values);
    /** Test driver: pick a model by roster id. */
    void SelectModelForTest(FName Model);

protected:
    virtual void NativeOnInitialized() override;

private:
    UFUNCTION() void HandleSignIn();
    UFUNCTION() void HandleShowRegister();
    UFUNCTION() void HandleShowForgot();
    UFUNCTION() void HandleBack();
    UFUNCTION() void HandleSendCode();
    UFUNCTION() void HandleConfirm();
    UFUNCTION() void HandleResend();
    UFUNCTION() void HandleCredentials();
    UFUNCTION() void HandleSendReset();
    UFUNCTION() void HandleReset();
    UFUNCTION() void HandlePreviousModel();
    UFUNCTION() void HandleNextModel();
    UFUNCTION() void HandleCreateCharacter();
    UFUNCTION() void HandleSignOut();
    UFUNCTION() void HandleCommitted(const FText& Text, ETextCommit::Type CommitMethod);

    UMarburgumAccountSubsystem* GetAccounts() const;
    void ShowLocalError(const FText& Message);
    void ShowModel();
    void Primary();

    UWidget* BuildBrand();
    UWidget* BuildCard();
    UEditableTextBox* AddField(UVerticalBox* Panel, const FText& Label, const FText& Hint, bool bPassword = false);
    UButton* AddButton(class UHorizontalBox* Row, const FText& Label, int32 Kind, float GapAfter);

    UPROPERTY(Transient) TObjectPtr<UTextBlock> StepTitle;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StepIntro;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Message;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Busy;
    /** One panel per step, indexed by EMarburgumAccountStep. */
    UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> Panels;
    /** The text fields of each step, in order, indexed like Panels. */
    TArray<TArray<TObjectPtr<UEditableTextBox>>> StepFields;
    UPROPERTY(Transient) TArray<TObjectPtr<UButton>> Actions;

    UPROPERTY(Transient) TObjectPtr<UTextBlock> ModelName;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ModelCount;
    int32 ModelIndex = 0;

    EMarburgumAccountStep ShownStep = EMarburgumAccountStep::Disabled;
};
