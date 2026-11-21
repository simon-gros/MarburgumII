#include "UI/MarburgumInputDevice.h"

#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumSessionContract.h"

#define LOCTEXT_NAMESPACE "MarburgumInputDevice"

namespace
{
    // A stick or trigger counts once it is pushed past this: resting drift on a worn pad must not flip the hints.
    constexpr float DeviceAnalogThreshold = 0.4f;

    EMarburgumInputDevice GActiveDevice = EMarburgumInputDevice::KeyboardMouse;

    /**
     * Watches every input Slate receives, before any widget or the game sees it, and notes the device it came from. It
     * never consumes an event.
     */
    class FMarburgumInputDeviceTracker final : public IInputProcessor
    {
    public:
        virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

        virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
        {
            MarburgumUI::NoteInputDevice(InKeyEvent.GetKey().IsGamepadKey() ? EMarburgumInputDevice::Gamepad : EMarburgumInputDevice::KeyboardMouse);
            return false;
        }

        virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent) override
        {
            if (InAnalogInputEvent.GetKey().IsGamepadKey() && FMath::Abs(InAnalogInputEvent.GetAnalogValue()) >= DeviceAnalogThreshold)
            {
                MarburgumUI::NoteInputDevice(EMarburgumInputDevice::Gamepad);
            }
            return false;
        }

        virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
        {
            // Synthetic moves (a widget moving under a still cursor) never reach preprocessors; a real move has a delta.
            if (!MouseEvent.GetCursorDelta().IsNearlyZero())
            {
                MarburgumUI::NoteInputDevice(EMarburgumInputDevice::KeyboardMouse);
            }
            return false;
        }

        virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
        {
            MarburgumUI::NoteInputDevice(EMarburgumInputDevice::KeyboardMouse);
            return false;
        }

        virtual bool HandleMouseWheelOrGestureEvent(FSlateApplication& SlateApp, const FPointerEvent& InWheelEvent, const FPointerEvent* InGestureEvent) override
        {
            MarburgumUI::NoteInputDevice(EMarburgumInputDevice::KeyboardMouse);
            return false;
        }

        virtual const TCHAR* GetDebugName() const override { return TEXT("MarburgumInputDeviceTracker"); }
    };

    TSharedPtr<FMarburgumInputDeviceTracker> GTracker;

    struct FKeyToken
    {
        EMarburgumInputAction Action;
        const TCHAR* Keyboard;
    };
    // The bracketed keyboard keys authored text may name, and the verb each one is.
    const FKeyToken KeyTokens[] = {
        { EMarburgumInputAction::Interact, TEXT("E") },
        { EMarburgumInputAction::Photograph, TEXT("P") },
        { EMarburgumInputAction::Journal, TEXT("J") },
        { EMarburgumInputAction::Inventory, TEXT("I") },
    };
}

namespace MarburgumUI
{
    EMarburgumInputDevice ActiveInputDevice()
    {
        return GActiveDevice;
    }

    FSimpleMulticastDelegate& OnInputDeviceChanged()
    {
        static FSimpleMulticastDelegate Changed;
        return Changed;
    }

    void NoteInputDevice(EMarburgumInputDevice Device)
    {
        if (Device == GActiveDevice)
        {
            return;
        }
        GActiveDevice = Device;
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=client event=input_device device=%s"), MarburgumII::Session::UiLogPrefix,
            Device == EMarburgumInputDevice::Gamepad ? TEXT("gamepad") : TEXT("keyboard_mouse"));
        OnInputDeviceChanged().Broadcast();
    }

    FText InputKeyLabel(EMarburgumInputAction Action, EMarburgumInputDevice Device)
    {
        // Button names as printed on the common pads (Xbox layout); SDL reports every pad in that layout on Linux.
        const bool bPad = Device == EMarburgumInputDevice::Gamepad;
        switch (Action)
        {
        case EMarburgumInputAction::Interact: return FText::AsCultureInvariant(bPad ? TEXT("A") : TEXT("E"));
        case EMarburgumInputAction::Photograph: return FText::AsCultureInvariant(bPad ? TEXT("RT") : TEXT("P"));
        case EMarburgumInputAction::Journal: return bPad ? LOCTEXT("PadView", "View") : FText::AsCultureInvariant(TEXT("J"));
        case EMarburgumInputAction::Inventory: return FText::AsCultureInvariant(bPad ? TEXT("Y") : TEXT("I"));
        case EMarburgumInputAction::Confirm: return bPad ? FText::AsCultureInvariant(TEXT("A")) : LOCTEXT("KeyEnter", "Enter");
        case EMarburgumInputAction::Back: return bPad ? FText::AsCultureInvariant(TEXT("B")) : LOCTEXT("KeyEsc", "Esc");
        case EMarburgumInputAction::Select: return bPad ? LOCTEXT("PadDpad", "D-pad") : FText::AsCultureInvariant(TEXT("↑ ↓"));
        }
        return FText::GetEmpty();
    }

    FString WithDeviceKeys(const FString& Text, EMarburgumInputDevice Device)
    {
        if (Device != EMarburgumInputDevice::Gamepad || !Text.Contains(TEXT("[")))
        {
            return Text;
        }
        FString Result = Text;
        for (const FKeyToken& Token : KeyTokens)
        {
            Result.ReplaceInline(*FString::Printf(TEXT("[%s]"), Token.Keyboard),
                *FString::Printf(TEXT("[%s]"), *InputKeyLabel(Token.Action, Device).ToString()), ESearchCase::CaseSensitive);
        }
        return Result;
    }

    void StartInputDeviceTracking()
    {
        if (!GTracker.IsValid() && FSlateApplication::IsInitialized())
        {
            GTracker = MakeShared<FMarburgumInputDeviceTracker>();
            FSlateApplication::Get().RegisterInputPreProcessor(GTracker);
        }
    }

    void StopInputDeviceTracking()
    {
        if (GTracker.IsValid() && FSlateApplication::IsInitialized())
        {
            FSlateApplication::Get().UnregisterInputPreProcessor(GTracker);
        }
        GTracker.Reset();
    }
}

#undef LOCTEXT_NAMESPACE
