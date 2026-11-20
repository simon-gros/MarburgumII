#pragma once

#include "CoreMinimal.h"
#include "AESGCMHandlerComponent.h"
// The engine component holds its encryptor and decryptor in TUniquePtrs; deleting them needs the complete types.
#include "PlatformCryptoTypes.h"

/**
 * The engine's AES-256-GCM packet component (ADR-083), refusing plaintext once a connection has its key.
 *
 * FAESGCMHandlerComponent marks a plaintext packet with a leading zero byte and accepts one at any time, also after
 * encryption is on, so anyone on the path could slip unauthenticated packets into an encrypted session. Once the key
 * is set this component drops such packets instead (the same quiet drop the engine uses for a packet it cannot read
 * yet), so only packets that decrypt and authenticate under the connection's key reach the game. Packets sent in the
 * clear before the other side switched are dropped as well; their reliable contents are sent again, encrypted.
 */
class MARBURGUMNETCRYPTO_API FMarburgumStrictAESGCM : public FAESGCMHandlerComponent
{
public:
    virtual void SetEncryptionData(const FEncryptionData& EncryptionData) override;
    virtual void Incoming(FIncomingPacketRef PacketRef) override;
    /** As the engine's component (each connection has its own state); defined here because the engine does not export it. */
    virtual bool SupportsParallelConnectionOutgoing() const override { return true; }

    /** Plaintext packets dropped on this connection since its key was set. */
    int64 GetPlaintextDropped() const { return PlaintextDropped; }

    /** Whether a packet of these bytes is one this component refuses: plaintext, after the key. */
    static bool IsRefusedPlaintext(bool bKeyed, const uint8* Data, int64 Bytes) { return bKeyed && Bytes > 0 && Data[0] == 0; }

private:
    bool bKeyed = false;
    int64 PlaintextDropped = 0;
};

/** EncryptionComponent=MarburgumNetCrypto in [PacketHandlerComponents]. */
class FMarburgumNetCryptoModule : public FPacketHandlerComponentModuleInterface
{
public:
    virtual TSharedPtr<HandlerComponent> CreateComponentInstance(FString& Options) override;
};
