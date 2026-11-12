#pragma once

#include "CoreMinimal.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

/**
 * Accounts, the first character and world admission (ADR-055). The account service (services/accounts) owns
 * email verification, credentials, sessions and the character binding; the game server only spends the
 * one-minute admission ticket a signed-in client brings, and the client only shows the screens.
 */
namespace MarburgumII::Accounts
{
    /** Join-URL option carrying the single-use admission ticket. Never the password or the session. */
    inline constexpr const TCHAR* TicketOption = TEXT("MB2Ticket");
    /** -MB2Accounts=https://host:port on the client and the server turns the account path on. */
    inline constexpr const TCHAR* ServiceSwitch = TEXT("MB2Accounts=");
    /** Server only: a file holding the key the service accepts for spending tickets. */
    inline constexpr const TCHAR* ServerKeyFileSwitch = TEXT("MB2AccountsKeyFile=");
    inline constexpr const TCHAR* AdmissionLogPrefix = TEXT("MB2ACCOUNT");

    /** Mirrors services/accounts/src/policy.rs, so a form can say what is wrong before it asks. */
    inline constexpr int32 NameMin = 2;
    inline constexpr int32 NameMax = 24;
    inline constexpr int32 UsernameMin = 3;
    inline constexpr int32 UsernameMax = 20;
    inline constexpr int32 PasswordMin = 10;
    inline constexpr int32 PasswordMax = 128;
    inline constexpr int32 CodeLength = 8;
    /** An admission ticket is 32 random bytes in URL-safe base64. */
    inline constexpr int32 TicketLength = 43;

    /** One body the first-character creator offers. Mirrors Data/Accounts/character_models.v0.1.json. */
    struct FCharacterModel
    {
        const TCHAR* Id;
        const TCHAR* NameEn;
        const TCHAR* NameSl;
        const TCHAR* Mesh;
    };

    inline constexpr FCharacterModel CharacterModels[] = {
        { TEXT("protagonist"), TEXT("Protagonist"), TEXT("Protagonist"), TEXT("/Game/Characters/Protagonist/SKM_Protagonist.SKM_Protagonist") },
        { TEXT("manny"), TEXT("Manny"), TEXT("Manny"), TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple") },
        { TEXT("quinn"), TEXT("Quinn"), TEXT("Quinn"), TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple") },
    };

    inline const FCharacterModel* FindModel(const FName Id)
    {
        for (const FCharacterModel& Model : CharacterModels)
        {
            if (Id == FName(Model.Id))
            {
                return &Model;
            }
        }
        return nullptr;
    }

    /** The service address from the command line, without a trailing slash; empty when accounts are off. */
    inline FString ServiceUrl()
    {
        FString Url;
        if (FParse::Value(FCommandLine::Get(), ServiceSwitch, Url))
        {
            Url.RemoveFromEnd(TEXT("/"));
        }
        return Url;
    }

    /** A ticket is opaque; this only keeps junk out of the request to the service. */
    inline bool IsWellFormedTicket(const FString& Ticket)
    {
        if (Ticket.Len() != TicketLength)
        {
            return false;
        }
        for (const TCHAR Char : Ticket)
        {
            if (!FChar::IsAlnum(Char) && Char != TEXT('-') && Char != TEXT('_'))
            {
                return false;
            }
        }
        return true;
    }
}
