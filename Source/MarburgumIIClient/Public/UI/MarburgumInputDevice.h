#pragma once

#include "CoreMinimal.h"

/** The device the player last used: key hints follow it, so a prompt never names a key on the other device. */
enum class EMarburgumInputDevice : uint8
{
    KeyboardMouse,
    Gamepad,
};

/** The verbs whose keys the interface names (UI-02 input work). */
enum class EMarburgumInputAction : uint8
{
    Interact,
    Photograph,
    Journal,
    Inventory,
    Confirm,
    Back,
    Select,
};

namespace MarburgumUI
{
    /** The device of the last deliberate key, button, stick or mouse movement; keyboard and mouse until a gamepad is used. */
    MARBURGUMIICLIENT_API EMarburgumInputDevice ActiveInputDevice();
    /** Raised on the game thread when the active device changes, e.g. a gamepad button in the middle of a decision. */
    MARBURGUMIICLIENT_API FSimpleMulticastDelegate& OnInputDeviceChanged();
    /** Sets the active device as an input event would (the tracker, and tests). */
    MARBURGUMIICLIENT_API void NoteInputDevice(EMarburgumInputDevice Device);

    /** The key cap text of Action on Device: "E" or "A", "P" or "RT". */
    MARBURGUMIICLIENT_API FText InputKeyLabel(EMarburgumInputAction Action, EMarburgumInputDevice Device);
    inline FText InputKeyLabel(EMarburgumInputAction Action) { return InputKeyLabel(Action, ActiveInputDevice()); }

    /**
     * Authored text names keyboard keys in brackets ("frame the book and press [P]"); on a gamepad the bracketed keys of
     * the verbs above read as their buttons ("[RT]"). Other text is returned unchanged.
     */
    MARBURGUMIICLIENT_API FString WithDeviceKeys(const FString& Text, EMarburgumInputDevice Device);
    inline FText WithDeviceKeys(const FText& Text) { return FText::FromString(WithDeviceKeys(Text.ToString(), ActiveInputDevice())); }

    /** Starts watching Slate input for the active device; safe to call more than once. */
    void StartInputDeviceTracking();
    void StopInputDeviceTracking();
}
