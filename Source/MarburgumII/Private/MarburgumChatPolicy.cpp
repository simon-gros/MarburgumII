#include "MarburgumChatPolicy.h"

bool MarburgumII::Chat::SanitizeMessage(const FString& RawText, FString& OutPlainText)
{
    OutPlainText = RawText.TrimStartAndEnd();
    if (OutPlainText.IsEmpty() || OutPlainText.Len() > MaxMessageCharacters)
    {
        return false;
    }

    // Disallow newlines, terminal controls, bidi spoofing and zero-width marks.
    // UMG must display this content as plain FText; never rich-text parse it.
    for (const TCHAR Character : OutPlainText)
    {
        const uint32 Point = static_cast<uint32>(Character);
        if (Point < 0x20 || Point == 0x7F || (Point >= 0x80 && Point <= 0x9F) ||
            (Point >= 0x200B && Point <= 0x200F) ||
            (Point >= 0x202A && Point <= 0x202E) ||
            (Point >= 0x2066 && Point <= 0x2069) || Point == 0xFEFF)
        {
            return false;
        }
    }

    return true;
}
