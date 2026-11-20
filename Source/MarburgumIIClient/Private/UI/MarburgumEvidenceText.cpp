#include "UI/MarburgumEvidenceText.h"

#include "MarburgumPlayerState.h"

#define LOCTEXT_NAMESPACE "MarburgumEvidence"

namespace MarburgumUI
{
    using MarburgumII::Gameplay::EEvidenceClass;

    FText EvidenceClassName(EEvidenceClass Class)
    {
        switch (Class)
        {
        case EEvidenceClass::Original: return LOCTEXT("Original", "Original");
        case EEvidenceClass::Print: return LOCTEXT("Print", "Print");
        case EEvidenceClass::Copy: return LOCTEXT("Copy", "Copy");
        case EEvidenceClass::Testimony: return LOCTEXT("Testimony", "Testimony");
        case EEvidenceClass::Note: return LOCTEXT("Note", "Information");
        case EEvidenceClass::Inference: return LOCTEXT("Inference", "Inference");
        case EEvidenceClass::None: break;
        }
        return FText::GetEmpty();
    }

    FText EvidenceClassMeaning(EEvidenceClass Class)
    {
        switch (Class)
        {
        case EEvidenceClass::Original:
            return LOCTEXT("OriginalMeaning", "The thing itself. What it proves depends on where it has been: keep its custody unbroken.");
        case EEvidenceClass::Print:
            return LOCTEXT("PrintMeaning", "A photographic print. It shows what was in front of the lens; when and where still has to be established.");
        case EEvidenceClass::Copy:
            return LOCTEXT("CopyMeaning", "A copy. It records what the original says; the original, kept elsewhere, is what can be authenticated.");
        case EEvidenceClass::Testimony:
            return LOCTEXT("TestimonyMeaning", "What a person said. It can be true and still needs paper or a place to stand on.");
        case EEvidenceClass::Note:
            return LOCTEXT("NoteMeaning", "Information. Useful for finding things; it proves nothing by itself.");
        case EEvidenceClass::Inference:
            return LOCTEXT("InferenceMeaning", "A conclusion drawn from other evidence. It is only as strong as what it rests on.");
        case EEvidenceClass::None:
            break;
        }
        return FText::GetEmpty();
    }

    TMap<FString, MarburgumII::Missions::FProgressView> ProgressViews(const AMarburgumPlayerState* State)
    {
        TMap<FString, MarburgumII::Missions::FProgressView> Views;
        if (State)
        {
            for (const FMarburgumMissionState& Each : State->GetMissions())
            {
                Views.Add(Each.MissionId.ToString(), { Each.StageId.ToString(), Each.bCompleted, Each.Outcome.IsNone() ? FString() : Each.Outcome.ToString() });
            }
        }
        return Views;
    }
}

#undef LOCTEXT_NAMESPACE
