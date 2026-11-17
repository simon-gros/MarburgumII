#pragma once

#include "CoreMinimal.h"

/**
 * The lit windows' content (ADR-066; MarburgumLitWindows.h names it), generated rather than authored.
 *
 * - **MPC_LitWindows**: the four numbers each client works out from the clock and the sky (the shares of households lit
 *   by evening and by morning, the shops' share, how dark it is outside).
 * - **M_WindowGlass**: the façades' glass. By day the same dark, smooth glass as before, tinted by BaseColorTint with
 *   RoughnessDefault, the parameters MI_facade_glass already sets. At night it reads its pane's household from the
 *   mesh's texture coordinates 2 and 3 and lights the pane if the household is up: warm bulb, cool tube, blue
 *   television or a shop's tubes, through its curtain, never brighter on screen than the street lamps' glass.
 *
 * The façade rebuild (-run=MarburgumRebuildFacades) writes the households and makes MI_facade_glass a child of
 * M_WindowGlass, so run this first.
 */
namespace MarburgumLitWindowsContent
{
    /** Writes both and saves them. */
    bool Write(FString& OutError);
}
