#pragma once

#include "CoreMinimal.h"

/**
 * The maps the content commandlets load and save. One definition for all of them: each commandlet used to define its own
 * LentMap, and two of those in one unity blob do not compile (an anonymous-namespace redefinition, or an ambiguity with a
 * `using namespace`), which depends only on how the module's files happen to be grouped.
 */
namespace MarburgumEditorMaps
{
    /** The launch map: Lent and its eight neighbouring windows (ADR-045). */
    inline constexpr const TCHAR* Lent = TEXT("/Game/World/lent");
}
