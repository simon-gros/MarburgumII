#include "MarburgumStrictAESGCM.h"

#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumNetCrypto, Log, All);

bool FMarburgumReplayWindow::Accept(uint64 Counter)
{
    if (!bAny)
    {
        bAny = true;
        Newest = Counter;
        Set(Counter);
        return true;
    }
    // The counter starts anywhere and wraps (FAESGCMHandlerComponent::Outgoing), so newer means a forward distance
    // under 2^63, not a larger number.
    const uint64 Ahead = Counter - Newest;
    if (Ahead != 0 && Ahead < (uint64(1) << 63))
    {
        // Forget the slots the window slides past, then take the new one.
        const uint64 Advance = Ahead;
        for (uint64 Step = 1; Step <= FMath::Min(Advance, WindowPackets); ++Step)
        {
            Clear(Newest + Step);
        }
        Newest = Counter;
        Set(Counter);
        return true;
    }
    if (Newest - Counter >= WindowPackets || Test(Counter))
    {
        return false;
    }
    Set(Counter);
    return true;
}

void FMarburgumStrictAESGCM::SetEncryptionData(const FEncryptionData& EncryptionData)
{
    FAESGCMHandlerComponent::SetEncryptionData(EncryptionData);
    // The engine ignores a key of the wrong size; then nothing decrypts and plaintext is not refused either way.
    bKeyed = EncryptionData.Key.Num() == KeySizeInBytes;
}

void FMarburgumStrictAESGCM::Incoming(FIncomingPacketRef PacketRef)
{
    FBitReader& Packet = PacketRef.Packet;
    if (IsValid() && IsRefusedPlaintext(bKeyed, Packet.GetData(), Packet.GetNumBytes()))
    {
        if (PlaintextDropped++ < 3)
        {
            UE_LOG(LogMarburgumNetCrypto, Log, TEXT("MB2CRYPTO event=plaintext_dropped count=%lld bytes=%lld"), PlaintextDropped,
                Packet.GetNumBytes());
        }
        Packet.SetData(nullptr, 0);
        return;
    }
    // An encrypted packet's counter, read before decryption replaces the bytes. It is only trusted, and only enters
    // the window, once the packet has authenticated: GCM covers the nonce, so a forged counter fails there.
    const bool bEncrypted = bKeyed && Packet.GetNumBytes() >= IVSizeInBytes && Packet.GetData()[0] != 0;
    const uint64 Counter = bEncrypted ? NonceCounter(Packet.GetData()) : 0;
    FAESGCMHandlerComponent::Incoming(PacketRef);
    if (bEncrypted && !Packet.IsError() && Packet.GetNumBytes() > 0 && !Window.Accept(Counter))
    {
        if (ReplaysDropped++ < 3)
        {
            UE_LOG(LogMarburgumNetCrypto, Log, TEXT("MB2CRYPTO event=replay_dropped count=%lld"), ReplaysDropped);
        }
        Packet.SetData(nullptr, 0);
    }
}

TSharedPtr<HandlerComponent> FMarburgumNetCryptoModule::CreateComponentInstance(FString& Options)
{
    return MakeShared<FMarburgumStrictAESGCM>();
}

IMPLEMENT_MODULE(FMarburgumNetCryptoModule, MarburgumNetCrypto)
