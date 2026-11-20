#pragma once

#include "CoreMinimal.h"
#include "Misc/Base64.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

/**
 * Accounts, the first character and world admission (ADR-055). The account service (services/accounts) owns
 * email verification, credentials, sessions and the character binding; the game server only spends the
 * one-minute admission ticket a signed-in client brings, and the client only shows the screens.
 */
namespace MarburgumII::Accounts
{
    /**
     * The single-use admission ticket travels as the engine's encryption token (ADR-083): the client adds it to its
     * pending connection only, the hello carries it, and the server spends it before it answers. Never the password,
     * the session or the key.
     */
    inline constexpr const TCHAR* EncryptionTokenOption = TEXT("EncryptionToken");
    /** Development and smoke clients joining from a raw URL: the ticket's connection key, in URL-safe base64. */
    inline constexpr const TCHAR* AdmissionKeySwitch = TEXT("MB2AdmissionKey=");
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
    /** The connection's AES-256-GCM key (the engine's AESGCMHandlerComponent takes exactly 32 bytes). */
    inline constexpr int32 ConnectionKeyBytes = 32;

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

    /** True when -MB2Accounts= is on the command line at all, usable or not: a server then admits signed-in players only. */
    inline bool ServiceRequested()
    {
        FString Url;
        return FParse::Value(FCommandLine::Get(), ServiceSwitch, Url);
    }

    /**
     * The service address from the command line, without a trailing slash; empty when accounts are off or the address
     * is not https://, so neither a password nor the server key ever travels in the clear.
     */
    inline FString ServiceUrl()
    {
        FString Url;
        if (!FParse::Value(FCommandLine::Get(), ServiceSwitch, Url) || !Url.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase))
        {
            return FString();
        }
        Url.RemoveFromEnd(TEXT("/"));
        return Url;
    }

    /** The account service names characters "c_" + 24 hex digits; the development key (ADR-014) must never take one. */
    inline bool IsAccountCharacterKey(const FString& Key)
    {
        return Key.StartsWith(TEXT("c_"), ESearchCase::CaseSensitive);
    }

    /** The connection key the service sent with a ticket, or empty when it is not 32 bytes of URL-safe base64. */
    inline TArray<uint8> DecodeConnectionKey(const FString& Text)
    {
        TArray<uint8> Key;
        if (Text.Len() != TicketLength || !FBase64::Decode(Text, Key, EBase64Mode::UrlSafe) || Key.Num() != ConnectionKeyBytes)
        {
            Key.Reset();
        }
        return Key;
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
