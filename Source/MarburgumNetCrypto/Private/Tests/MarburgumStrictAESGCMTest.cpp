#include "MarburgumStrictAESGCM.h"

#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "Serialization/BitReader.h"
#include "Serialization/BitWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace MarburgumStrictAESGCMTest
{
    TArray<uint8> Key(int32 Seed)
    {
        FRandomStream Random(Seed);
        TArray<uint8> Bytes;
        for (int32 Index = 0; Index < FAESGCMHandlerComponent::KeySizeInBytes; ++Index)
        {
            Bytes.Add(static_cast<uint8>(Random.RandRange(0, 255)));
        }
        return Bytes;
    }

    void Keyed(FMarburgumStrictAESGCM& Component, const TArray<uint8>& InKey, bool bEnable)
    {
        FEncryptionData Data;
        Data.Key = InKey;
        Component.SetEncryptionData(Data);
        if (bEnable)
        {
            Component.EnableEncryption();
        }
    }

    /** What arrives after Incoming: the bytes left to read, or none with bError when the packet was refused. */
    struct FArrived
    {
        TArray<uint8> Bytes;
        bool bError = false;
    };

    FArrived Receive(FMarburgumStrictAESGCM& Component, const TArray<uint8>& Wire)
    {
        TArray<uint8> Copy = Wire;
        FBitReader Packet(Copy.GetData(), Copy.Num() * 8);
        TSharedPtr<const FInternetAddr> Address;
        FInPacketTraits Traits;
        FIncomingPacketRef Ref = { Packet, Address, Traits };
        Component.Incoming(Ref);
        FArrived Out;
        Out.bError = Packet.IsError();
        const int64 Left = Packet.GetBitsLeft() / 8;
        if (!Out.bError && Left > 0)
        {
            Out.Bytes.SetNumUninitialized(static_cast<int32>(Left));
            Packet.Serialize(Out.Bytes.GetData(), Left);
        }
        return Out;
    }

    TArray<uint8> Send(FMarburgumStrictAESGCM& Component, const TArray<uint8>& Payload)
    {
        FBitWriter Packet(8 * MAX_PACKET_SIZE, false);
        Packet.Serialize(const_cast<uint8*>(Payload.GetData()), Payload.Num());
        FOutPacketTraits Traits;
        Component.Outgoing(Packet, Traits);
        return TArray<uint8>(Packet.GetData(), static_cast<int32>(Packet.GetNumBytes()));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumStrictAESGCMTest, "Marburgum.Net.StrictEncryption",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMarburgumStrictAESGCMTest::RunTest(const FString& Parameters)
{
    using namespace MarburgumStrictAESGCMTest;
    const FString Text = TEXT("/Game/World/lent Bor Mlakar");
    TArray<uint8> Payload;
    for (const TCHAR Char : Text)
    {
        Payload.Add(static_cast<uint8>(Char));
    }

    FMarburgumStrictAESGCM Sender;
    FMarburgumStrictAESGCM Receiver;
    Keyed(Sender, Key(1), true);
    Keyed(Receiver, Key(1), true);

    // The wire carries no plaintext, marks itself encrypted, and decrypts to what was sent.
    const TArray<uint8> Wire = Send(Sender, Payload);
    TestTrue(TEXT("encrypted packets start with a non-zero byte"), Wire.Num() > 0 && Wire[0] != 0);
    bool bPlainOnWire = false;
    for (int32 At = 0; At + 4 <= Wire.Num(); ++At)
    {
        bPlainOnWire |= FMemory::Memcmp(Wire.GetData() + At, "lent", 4) == 0;
    }
    TestFalse(TEXT("no plaintext on the wire"), bPlainOnWire);
    const FArrived Arrived = Receive(Receiver, Wire);
    TestFalse(TEXT("a sound packet is accepted"), Arrived.bError);
    TestTrue(TEXT("and decrypts to what was sent"), Arrived.Bytes.Num() >= Payload.Num()
        && FMemory::Memcmp(Arrived.Bytes.GetData(), Payload.GetData(), Payload.Num()) == 0);

    // Plaintext injected into the keyed session is dropped (the engine's own component would pass it on).
    TArray<uint8> Injected = { 0 };
    Injected.Append(Payload);
    const FArrived Dropped = Receive(Receiver, Injected);
    TestTrue(TEXT("plaintext after the key is dropped"), Dropped.Bytes.IsEmpty());
    TestEqual(TEXT("and counted"), Receiver.GetPlaintextDropped(), int64(1));

    // Before the key, plaintext is still the handshake's normal traffic.
    FMarburgumStrictAESGCM Unkeyed;
    const FArrived Early = Receive(Unkeyed, Injected);
    TestTrue(TEXT("plaintext before the key passes"), Early.Bytes.Num() == Payload.Num()
        && FMemory::Memcmp(Early.Bytes.GetData(), Payload.GetData(), Payload.Num()) == 0);

    // A changed byte fails authentication; so does another key.
    AddExpectedMessage(TEXT("EVP_DecryptFinal_ex failed"), EAutomationExpectedMessageFlags::Contains, 2);
    TArray<uint8> Tampered = Wire;
    Tampered.Last() ^= 0x01;
    TestTrue(TEXT("a tampered packet is refused"), Receive(Receiver, Tampered).bError);
    FMarburgumStrictAESGCM Stranger;
    Keyed(Stranger, Key(2), true);
    TestTrue(TEXT("another key cannot read it"), Receive(Stranger, Wire).bError);
    return true;
}

#endif
