#pragma once

#include "CoreMinimal.h"
#include "AESGCMHandlerComponent.h"
// The engine component holds its encryptor and decryptor in TUniquePtrs; deleting them needs the complete types.
#include "PlatformCryptoTypes.h"

/**
 * Which packet counters a receiver has already accepted (ADR-088). The sender numbers its packets with a 64-bit counter
 * in the GCM nonce; a counter seen before, or older than the window, is a replay. The engine's own 14-bit packet
 * sequence wraps after 16,384 packets, so a packet recorded earlier in a long session could otherwise come back as new.
 */
class MARBURGUMNETCRYPTO_API FMarburgumReplayWindow
{
public:
    /** Late packets this far behind the newest are still taken; UDP reorders, it does not delay by thousands. */
    static constexpr uint64 WindowPackets = 1024;

    /** True, and remembered, when Counter is new; false for a replay or a packet older than the window. */
    bool Accept(uint64 Counter);

private:
    bool bAny = false;
    uint64 Newest = 0;
    uint64 Seen[WindowPackets / 64] = {};

    bool Test(uint64 Counter) const { return (Seen[(Counter / 64) % (WindowPackets / 64)] >> (Counter % 64)) & 1; }
    void Set(uint64 Counter) { Seen[(Counter / 64) % (WindowPackets / 64)] |= uint64(1) << (Counter % 64); }
    void Clear(uint64 Counter) { Seen[(Counter / 64) % (WindowPackets / 64)] &= ~(uint64(1) << (Counter % 64)); }
};

/**
 * The engine's AES-256-GCM packet component (ADR-083), refusing plaintext once a connection has its key.
 *
 * FAESGCMHandlerComponent marks a plaintext packet with a leading zero byte and accepts one at any time, also after
 * encryption is on, so anyone on the path could slip unauthenticated packets into an encrypted session. Once the key
 * is set this component drops such packets instead (the same quiet drop the engine uses for a packet it cannot read
 * yet), so only packets that decrypt and authenticate under the connection's key reach the game. Packets sent in the
 * clear before the other side switched are dropped as well; their reliable contents are sent again, encrypted.
 * It also drops an authentic packet whose nonce counter it has already accepted (FMarburgumReplayWindow, ADR-088).
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
    /** Authentic packets dropped as replays. */
    int64 GetReplaysDropped() const { return ReplaysDropped; }

    /** The sender's packet counter: nonce bytes 1-8, little-endian (FAESGCMHandlerComponent::Outgoing). */
    static uint64 NonceCounter(const uint8* Packet) { uint64 Counter = 0; FMemory::Memcpy(&Counter, Packet + 1, sizeof(Counter)); return INTEL_ORDER64(Counter); }

    /** Whether a packet of these bytes is one this component refuses: plaintext, after the key. */
    static bool IsRefusedPlaintext(bool bKeyed, const uint8* Data, int64 Bytes) { return bKeyed && Bytes > 0 && Data[0] == 0; }

private:
    bool bKeyed = false;
    int64 PlaintextDropped = 0;
    int64 ReplaysDropped = 0;
    FMarburgumReplayWindow Window;
};

/** EncryptionComponent=MarburgumNetCrypto in [PacketHandlerComponents]. */
class FMarburgumNetCryptoModule : public FPacketHandlerComponentModuleInterface
{
public:
    virtual TSharedPtr<HandlerComponent> CreateComponentInstance(FString& Options) override;
};
