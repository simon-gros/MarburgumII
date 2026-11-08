#pragma once

#include "CoreMinimal.h"

/**
 * Pure shared chat text policy. The server enforces it for every request.
 * Code-unit maximum intentionally bounds Unreal RPC payload size.
 */
namespace MarburgumII::Chat
{
    inline constexpr int32 MaxMessageCharacters = 240;
    MARBURGUMII_API bool SanitizeMessage(const FString& RawText, FString& OutPlainText);
}
