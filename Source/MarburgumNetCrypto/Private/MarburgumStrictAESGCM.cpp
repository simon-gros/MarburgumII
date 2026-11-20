#include "MarburgumStrictAESGCM.h"

#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumNetCrypto, Log, All);

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
    FAESGCMHandlerComponent::Incoming(PacketRef);
}

TSharedPtr<HandlerComponent> FMarburgumNetCryptoModule::CreateComponentInstance(FString& Options)
{
    return MakeShared<FMarburgumStrictAESGCM>();
}

IMPLEMENT_MODULE(FMarburgumNetCryptoModule, MarburgumNetCrypto)
