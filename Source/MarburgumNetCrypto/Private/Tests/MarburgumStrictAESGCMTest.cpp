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

    // ADR-088: an authentic packet that comes back is dropped, however late; a reordered one is not.
    TestTrue(TEXT("a replayed packet is dropped"), Receive(Receiver, Wire).Bytes.IsEmpty());
    TestEqual(TEXT("and counted"), Receiver.GetReplaysDropped(), int64(1));
    const TArray<uint8> Second = Send(Sender, Payload);
    const TArray<uint8> Third = Send(Sender, Payload);
    TestFalse(TEXT("a later packet arrives first"), Receive(Receiver, Third).Bytes.IsEmpty());
    TestFalse(TEXT("an earlier one after it is still taken"), Receive(Receiver, Second).Bytes.IsEmpty());
    TestTrue(TEXT("but not twice"), Receive(Receiver, Second).Bytes.IsEmpty());
    for (int32 Index = 0; Index < 1100; ++Index)
    {
        Receive(Receiver, Send(Sender, Payload));
    }
    TestTrue(TEXT("a packet recorded 1,100 packets ago is refused"), Receive(Receiver, Third).Bytes.IsEmpty());
    TestEqual(TEXT("replays counted"), Receiver.GetReplaysDropped(), int64(3));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumReplayWindowTest, "Marburgum.Net.ReplayWindow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMarburgumReplayWindowTest::RunTest(const FString& Parameters)
{
    FMarburgumReplayWindow Window;
    const uint64 Start = 0xFFFFFFFFFFFFFF00ull; // a random start can sit right below the wrap
    TestTrue(TEXT("first"), Window.Accept(Start));
    TestFalse(TEXT("duplicate"), Window.Accept(Start));
    TestTrue(TEXT("next"), Window.Accept(Start + 2));
    TestTrue(TEXT("gap filled late"), Window.Accept(Start + 1));
    TestFalse(TEXT("gap filled twice"), Window.Accept(Start + 1));
    TestTrue(TEXT("jump ahead"), Window.Accept(Start + 200));
    TestTrue(TEXT("inside the window, unseen"), Window.Accept(Start + 3));
    TestFalse(TEXT("inside the window, seen"), Window.Accept(Start + 2));
    TestTrue(TEXT("far ahead"), Window.Accept(Start + 200 + 5000));
    TestFalse(TEXT("behind the window"), Window.Accept(Start + 200 + 5000 - FMarburgumReplayWindow::WindowPackets));
    TestTrue(TEXT("the last slot inside"), Window.Accept(Start + 200 + 5000 - FMarburgumReplayWindow::WindowPackets + 1));
    // A slot the window slid over is free for its new counter.
    TestTrue(TEXT("a reused slot"), Window.Accept(Start + 200 + 5000 + 1));
    return true;
}

#endif
