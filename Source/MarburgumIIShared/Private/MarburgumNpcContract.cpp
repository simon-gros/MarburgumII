#include "MarburgumNpcContract.h"

namespace MarburgumII::Npc
{
    FAnglerMoment AnglerMomentAt(double WorldSeconds, uint32 Seed)
    {
        double Period = 0.0;
        for (const double Seconds : AnglerPhaseSeconds)
        {
            Period += Seconds;
        }
        // Up to one whole period of offset, in tenths of a second, so the shift is the same on every machine.
        const double Offset = static_cast<double>(Seed % static_cast<uint32>(Period * 10.0)) / 10.0;
        const double T = FMath::Max(WorldSeconds, 0.0) + Offset;
        FAnglerMoment Moment;
        Moment.Cycle = static_cast<int64>(FMath::FloorToDouble(T / Period));
        double Into = T - static_cast<double>(Moment.Cycle) * Period;
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(AnglerPhaseSeconds); ++Index)
        {
            if (Into < AnglerPhaseSeconds[Index] || Index == UE_ARRAY_COUNT(AnglerPhaseSeconds) - 1)
            {
                Moment.Phase = static_cast<EAnglerPhase>(Index);
                Moment.Elapsed = FMath::Min(Into, AnglerPhaseSeconds[Index]);
                return Moment;
            }
            Into -= AnglerPhaseSeconds[Index];
        }
        return Moment;
    }

    const TCHAR* ToString(EAnglerPhase Phase)
    {
        switch (Phase)
        {
        case EAnglerPhase::Cast: return TEXT("cast");
        case EAnglerPhase::Wait: return TEXT("wait");
        case EAnglerPhase::Reel: return TEXT("reel");
        default: return TEXT("hold");
        }
    }
}
