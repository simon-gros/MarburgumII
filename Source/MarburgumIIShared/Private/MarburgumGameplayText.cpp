#include "MarburgumGameplayContract.h"

#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"

namespace MarburgumII::Gameplay
{
    const TCHAR* Localized(const TCHAR* English)
    {
        if (!English || !*English || !FInternationalization::Get().GetCurrentLanguage()->GetTwoLetterISOLanguageName().Equals(TEXT("sl")))
        {
            return English;
        }
        // Built once: English -> Slovene from the generated catalogue.
        static const TMap<FString, const TCHAR*> Table = []
        {
            TMap<FString, const TCHAR*> Out;
            for (const FTextTranslation& Entry : SloveneText)
            {
                Out.Add(Entry.English, Entry.Slovene);
            }
            return Out;
        }();
        const TCHAR* const* Found = Table.Find(English);
        return Found ? *Found : English;
    }
}
