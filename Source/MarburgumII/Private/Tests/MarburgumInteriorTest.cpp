#include "MarburgumInteriorCore.h"
#include "MarburgumInteriorSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumInteriorTest,
    "Marburgum.Gameplay.Interiors",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    namespace MI = MarburgumInteriors;

    MI::FPlan PlanRect(const char* Id, double W, double D, const char* Kind, int Levels)
    {
        MI::FPlan Plan;
        Plan.Id = Id;
        Plan.Kind = Kind;
        Plan.Outline = { { 0, 0 }, { W, 0 }, { W, D }, { 0, D } };
        Plan.Ground = 10;
        Plan.Eaves = 20;
        Plan.Area = W * D;
        Plan.Levels = Levels;
        return Plan;
    }

    MI::FLayout Prepared(std::vector<MI::FPlan> Plans, size_t Which = 0)
    {
        MI::PrepareCity(Plans);
        return MI::Generate(Plans[Which]);
    }

    // Room I in the frame of room 0 (all rooms of a layout share its axes): U0, U1, V0, V1.
    struct FSpan
    {
        double U0, U1, V0, V1;
    };

    FSpan SpanOf(const MI::FLayout& Layout, const MI::FRoom& Room)
    {
        const MI::FRoom& First = Layout.Rooms[0];
        const MI::FVec2 Along = MI::Normalize(First.Corner[1] - First.Corner[0]);
        const MI::FVec2 Across = MI::Normalize(First.Corner[3] - First.Corner[0]);
        FSpan Span{ 1.0e9, -1.0e9, 1.0e9, -1.0e9 };
        for (const MI::FVec2& C : Room.Corner)
        {
            const double U = MI::Dot(C - First.Corner[0], Along);
            const double V = MI::Dot(C - First.Corner[0], Across);
            Span.U0 = std::min(Span.U0, U);
            Span.U1 = std::max(Span.U1, U);
            Span.V0 = std::min(Span.V0, V);
            Span.V1 = std::max(Span.V1, V);
        }
        return Span;
    }

    bool InsideAnyRoom(const MI::FLayout& Layout, const MI::FVec2& P, double Slack)
    {
        const MI::FRoom& First = Layout.Rooms[0];
        const MI::FVec2 Along = MI::Normalize(First.Corner[1] - First.Corner[0]);
        const MI::FVec2 Across = MI::Normalize(First.Corner[3] - First.Corner[0]);
        const double U = MI::Dot(P - First.Corner[0], Along);
        const double V = MI::Dot(P - First.Corner[0], Across);
        for (const MI::FRoom& Room : Layout.Rooms)
        {
            const FSpan S = SpanOf(Layout, Room);
            if (U >= S.U0 - Slack && U <= S.U1 + Slack && V >= S.V0 - Slack && V <= S.V1 + Slack)
            {
                return true;
            }
        }
        return false;
    }

    // Every geometric rule a generated floor must keep. Returns the first broken one, or empty.
    FString Defect(const MI::FPlan& Plan, const MI::FLayout& Layout)
    {
        if (Layout.Status != MI::EStatus::Eligible)
        {
            return Layout.Rooms.empty() && !Layout.Reason.empty() ? FString() : TEXT("exception keeps rooms or has no reason");
        }
        if (!MI::RoomsConnected(Layout))
        {
            return TEXT("a room cannot be reached");
        }
        if (Layout.EntranceEdge < 0 || (Plan.Party.size() > static_cast<size_t>(Layout.EntranceEdge) && Plan.Party[Layout.EntranceEdge])
            || (Plan.Blocked.size() > static_cast<size_t>(Layout.EntranceEdge) && Plan.Blocked[Layout.EntranceEdge]))
        {
            return TEXT("street door on a shared or blocked wall");
        }
        const MI::FOpening& Street = Layout.Openings[0];
        if (MI::Length(Street.B - Street.A) < MI::DoorWidth - 1.0e-6 || Street.Z1 - Street.Z0 < MI::DoorHeight - 1.0e-6)
        {
            return TEXT("street door smaller than the cut");
        }
        if (Layout.Openings.size() > 1 && !Layout.Openings[1].bLeaf)
        {
            // The vestibule opens into the front room across the whole door: a person walks straight in.
            const MI::FOpening& Inner = Layout.Openings[1];
            const MI::FVec2 Along = MI::Normalize(Street.B - Street.A);
            const double I0 = std::min(MI::Dot(Inner.A - Street.A, Along), MI::Dot(Inner.B - Street.A, Along));
            const double I1 = std::max(MI::Dot(Inner.A - Street.A, Along), MI::Dot(Inner.B - Street.A, Along));
            if (I0 > 0.0 + 1.0e-6 || I1 < MI::DoorWidth - 1.0e-6)
            {
                return TEXT("the front room does not span the street door");
            }
        }
        for (size_t I = 0; I < Layout.Rooms.size(); ++I)
        {
            const MI::FRoom& Room = Layout.Rooms[I];
            for (const MI::FVec2& Corner : Room.Corner)
            {
                if (!MI::PointInside(Plan.Outline, Corner))
                {
                    return FString::Printf(TEXT("room %d outside the footprint"), static_cast<int32>(I));
                }
            }
            const FSpan S = SpanOf(Layout, Room);
            // The vestibule is the only room narrower than a room; it is still two capsules wide.
            const double Narrow = std::min(S.U1 - S.U0, S.V1 - S.V0);
            if (Narrow < (I == 0 && Layout.Openings.size() > 1 && !Layout.Openings[1].bLeaf ? 0.05 : MI::MinRoom - 1.0e-6))
            {
                return FString::Printf(TEXT("room %d too small (%.2f m)"), static_cast<int32>(I), Narrow);
            }
            if (Room.CeilingZ - Room.FloorZ < MI::MinHeadroom - 1.0e-6)
            {
                return TEXT("ceiling lower than a person");
            }
            for (size_t J = I + 1; J < Layout.Rooms.size(); ++J)
            {
                const FSpan T = SpanOf(Layout, Layout.Rooms[J]);
                const double OverU = std::min(S.U1, T.U1) - std::max(S.U0, T.U0);
                const double OverV = std::min(S.V1, T.V1) - std::max(S.V0, T.V0);
                if (OverU > 0.01 && OverV > 0.01)
                {
                    return FString::Printf(TEXT("rooms %d and %d overlap"), static_cast<int32>(I), static_cast<int32>(J));
                }
            }
        }
        for (const MI::FOpening& Opening : Layout.Openings)
        {
            // A player's capsule is 0.68 m across.
            if (MI::Length(Opening.B - Opening.A) < 0.90)
            {
                return TEXT("opening narrower than a person");
            }
            const MI::FLeaf Leaf = MI::LeafFor(Layout, Opening);
            if (!Leaf.bValid)
            {
                if (Opening.bLeaf)
                {
                    return TEXT("an opening that should hang a leaf has none");
                }
                continue;
            }
            // The open leaf lies inside the rooms, not through a wall, and leaves most of the doorway clear.
            const MI::FVec2 Tip = MI::OpenLeafTip(Leaf);
            for (int Step = 1; Step <= 10; ++Step)
            {
                const MI::FVec2 P = Leaf.Hinge + (Tip - Leaf.Hinge) * (Step / 10.0);
                if (!InsideAnyRoom(Layout, P, 0.02) || !MI::PointInside(Plan.Outline, P))
                {
                    return FString::Printf(TEXT("open leaf runs through a wall (%hs door)"), Opening.bEntrance ? "street" : "inside");
                }
            }
            const double Clear = MI::Length(Opening.B - Opening.A) - MI::LeafSideGap - MI::LeafThickness;
            if (Clear < 0.80)
            {
                return TEXT("open leaf leaves less than 0.80 m of doorway");
            }
        }
        return FString();
    }
}

bool FMarburgumInteriorTest::RunTest(const FString& Parameters)
{
    const MI::FDoorCut Door = MI::DoorCut(8.0, 3.0);
    TestTrue(TEXT("A wide enough wall gets one centred door"), Door.bFits && FMath::IsNearlyEqual(Door.U0 + Door.U1, 8.0, 1.0e-6));
    TestFalse(TEXT("A narrow wall gets no door"), MI::DoorCut(2.0, 3.0).bFits);
    TestFalse(TEXT("A low wall gets no door"), MI::DoorCut(8.0, 2.2).bFits);

    const MI::FLayout House = Prepared({ PlanRect("house", 18, 9, "house", 3) });
    const MI::FLayout Again = Prepared({ PlanRect("house", 18, 9, "house", 3) });
    TestTrue(TEXT("An 18 by 9 house is eligible"), House.Status == MI::EStatus::Eligible);
    TestTrue(TEXT("Every room can be reached from the street"), MI::RoomsConnected(House));
    TestTrue(TEXT("Only the ground floor is built"), House.FloorsGenerated == 1 && House.bUpperFloorsOmitted && House.FloorsNoted == 3);
    TestTrue(TEXT("The same plan is the same rooms"), House.Rooms.size() == Again.Rooms.size() && House.Openings.size() == Again.Openings.size());
    TestTrue(TEXT("The street door is first"), !House.Openings.empty() && House.Openings[0].bEntrance && House.Openings[0].RoomA == -1);

    TestEqual(TEXT("The house breaks no layout rule"), Defect(PlanRect("house", 18, 9, "house", 3), House), FString());

    MI::FPlan Clockwise = PlanRect("cw", 12, 8, "retail", 1);
    std::reverse(Clockwise.Outline.begin(), Clockwise.Outline.end());
    TestTrue(TEXT("A clockwise outline is cleaned and entered"), Prepared({ Clockwise }).Status == MI::EStatus::Eligible);

    const MI::FLayout Tiny = Prepared({ PlanRect("tiny", 3, 3, "house", 1) });
    TestTrue(TEXT("A 3 m shed is an exception with a reason"), Tiny.Status == MI::EStatus::Exception && !Tiny.Reason.empty() && Tiny.Rooms.empty());
    TestTrue(TEXT("A roof is not an interior"), Prepared({ PlanRect("roof", 20, 10, "roof", 1) }).Reason == "not an occupiable building");
    TestTrue(TEXT("A grandstand is not an interior"), Prepared({ PlanRect("stand", 40, 20, "grandstand", 1) }).Status == MI::EStatus::Exception);
    // The façade commandlet builds no shell under 30 m2, so there is nothing to walk into.
    TestTrue(TEXT("A 25 m2 hut has no shell and no floor"), Prepared({ PlanRect("hut", 5, 5, "house", 1) }).Reason.rfind("no shell", 0) == 0);

    // Scenario 1: a small building, one room behind the street door.
    {
        const MI::FPlan Small = PlanRect("small", 6, 5, "house", 1);
        const MI::FLayout Layout = Prepared({ Small });
        TestTrue(TEXT("A 6 by 5 house is eligible"), Layout.Status == MI::EStatus::Eligible);
        TestEqual(TEXT("The small house breaks no rule"), Defect(Small, Layout), FString());
        TestTrue(TEXT("The small house has a vestibule and one room"), Layout.Rooms.size() == 2);
        TestTrue(TEXT("Only the street door hangs a leaf in the small house"), Layout.Openings.size() == 2 && Layout.Openings[0].bLeaf && !Layout.Openings[1].bLeaf);
    }
    // Scenario 2: a medium building, several connected rooms.
    {
        const MI::FPlan Medium = PlanRect("medium", 14, 11, "apartments", 4);
        const MI::FLayout Layout = Prepared({ Medium });
        TestEqual(TEXT("The medium block breaks no rule"), Defect(Medium, Layout), FString());
        TestTrue(TEXT("The medium block has rooms behind the front room"), Layout.Rooms.size() >= 4);
    }
    // Scenario 3: large and irregular, an L and a U.
    {
        MI::FPlan Ell;
        Ell.Id = "ell";
        Ell.Kind = "office";
        Ell.Ground = 5;
        Ell.Eaves = 16;
        Ell.Area = 20 * 8 + 11 * 14;
        Ell.Outline = { { 0, 0 }, { 20, 0 }, { 20, 8 }, { 9, 8 }, { 9, 22 }, { 0, 22 } };
        const MI::FLayout EllLayout = Prepared({ Ell });
        TestTrue(TEXT("An L can be entered"), EllLayout.Status == MI::EStatus::Eligible && MI::RoomsConnected(EllLayout));
        MI::FPlan EllPrepared = Ell;
        std::vector<MI::FPlan> One{ EllPrepared };
        MI::PrepareCity(One);
        TestEqual(TEXT("The L breaks no rule"), Defect(One[0], EllLayout), FString());

        MI::FPlan You = Ell;
        You.Id = "you";
        You.Area = 30 * 20 - 12 * 12;
        You.Outline = { { 0, 0 }, { 30, 0 }, { 30, 20 }, { 21, 20 }, { 21, 8 }, { 9, 8 }, { 9, 20 }, { 0, 20 } };
        std::vector<MI::FPlan> Us{ You };
        MI::PrepareCity(Us);
        const MI::FLayout UsLayout = MI::Generate(Us[0]);
        TestTrue(TEXT("A U can be entered"), UsLayout.Status == MI::EStatus::Eligible);
        TestEqual(TEXT("The U breaks no rule"), Defect(Us[0], UsLayout), FString());
    }
    // Scenario 5: closed or problematic geometry. A terrace house shares both long walls and its short street
    // wall is too narrow, and a house whose only free long wall faces another building's back.
    {
        std::vector<MI::FPlan> Row{ PlanRect("west", 8, 12, "house", 2), PlanRect("mid", 2.1, 12, "house", 2), PlanRect("east", 8, 12, "house", 2) };
        Row[1].Area = 40;
        for (MI::FVec2& P : Row[1].Outline)
        {
            P.X += 8;
        }
        for (MI::FVec2& P : Row[2].Outline)
        {
            P.X += 10.1;
        }
        MI::PrepareCity(Row);
        const MI::FLayout Mid = MI::Generate(Row[1]);
        TestTrue(TEXT("A sliver between two party walls is an exception"), Mid.Status == MI::EStatus::Exception && !Mid.Reason.empty());

        std::vector<MI::FPlan> Facing{ PlanRect("front", 16, 8, "house", 2), PlanRect("behind", 16, 8, "house", 2) };
        for (MI::FVec2& P : Facing[1].Outline)
        {
            P.Y -= 8.6;
        }
        MI::PrepareCity(Facing);
        const MI::FLayout Front = MI::Generate(Facing[0]);
        TestTrue(TEXT("The wall 0.6 m from a neighbour is blocked"), Facing[0].Blocked[0] != 0);
        TestTrue(TEXT("The door goes on the other long wall"), Front.Status == MI::EStatus::Eligible && Front.EntranceEdge == 2);
        TestEqual(TEXT("The house with a neighbour behind breaks no rule"), Defect(Facing[0], Front), FString());
    }

    MI::FPlan Ell;
    Ell.Id = "ell";
    Ell.Kind = "office";
    Ell.Ground = 5;
    Ell.Eaves = 16;
    Ell.Area = 20 * 8 + 11 * 14;
    Ell.Outline = { { 0, 0 }, { 20, 0 }, { 20, 8 }, { 9, 8 }, { 9, 22 }, { 0, 22 } };
    const MI::FLayout EllLayout = MI::Generate(Ell);
    TestTrue(TEXT("An L can be entered"), EllLayout.Status == MI::EStatus::Eligible && MI::RoomsConnected(EllLayout));

    const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/World/lent_ground/buildings.json"));
    FString Text;
    TSharedPtr<FJsonObject> Root;
    if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
    {
        AddError(TEXT("buildings.json could not be read"));
        return false;
    }
    std::vector<MI::FPlan> Plans;
    if (!UMarburgumInteriorSubsystem::ReadPlans(Plans, nullptr))
    {
        AddError(TEXT("buildings.json could not be prepared"));
        return false;
    }
    std::vector<MI::FPlan> Twice;
    UMarburgumInteriorSubsystem::ReadPlans(Twice, nullptr);
    int32 Eligible = 0;
    int32 Exceptions = 0;
    int32 Broken = 0;
    int32 Blank = 0;
    int32 Unstable = 0;
    TMap<FString, int32> Reasons;
    for (size_t Index = 0; Index < Plans.size(); ++Index)
    {
        const MI::FPlan& Plan = Plans[Index];
        const MI::FLayout Layout = MI::Generate(Plan);
        const MI::FLayout Repeat = MI::Generate(Twice[Index]);
        bool bSame = Layout.Status == Repeat.Status && Layout.Rooms.size() == Repeat.Rooms.size() && Layout.Openings.size() == Repeat.Openings.size()
            && Layout.EntranceEdge == Repeat.EntranceEdge;
        for (size_t R = 0; bSame && R < Layout.Rooms.size(); ++R)
        {
            for (int C = 0; C < 4; ++C)
            {
                bSame &= Layout.Rooms[R].Corner[C].X == Repeat.Rooms[R].Corner[C].X && Layout.Rooms[R].Corner[C].Y == Repeat.Rooms[R].Corner[C].Y;
            }
        }
        Unstable += bSame ? 0 : 1;
        const FString Problem = Defect(Plan, Layout);
        if (!Problem.IsEmpty())
        {
            ++Broken;
            AddError(FString::Printf(TEXT("%hs: %s"), Plan.Id.c_str(), *Problem));
        }
        if (Layout.Status == MI::EStatus::Eligible)
        {
            ++Eligible;
            TestTrue(TEXT("An eligible building generates only the ground floor"), Layout.FloorsGenerated == 1);
        }
        else
        {
            ++Exceptions;
            Blank += Layout.Reason.empty() ? 1 : 0;
            Reasons.FindOrAdd(UTF8_TO_TCHAR(Layout.Reason.c_str()))++;
        }
    }
    TestEqual(TEXT("Every footprint is classified"), Eligible + Exceptions, static_cast<int32>(Plans.size()));
    // Measured floors (interior_floors.json) turn a slope, a low eave or a blocked street into a named exception.
    TestTrue(TEXT("Most of the city can be entered"), Eligible > 550);
    TestEqual(TEXT("No layout breaks a rule"), Broken, 0);
    TestEqual(TEXT("Every exception names its reason"), Blank, 0);
    TestEqual(TEXT("Reading the city twice gives the same floors"), Unstable, 0);
    FString ReasonText;
    for (const TPair<FString, int32>& Pair : Reasons)
    {
        ReasonText += FString::Printf(TEXT(" [%s]=%d"), *Pair.Key, Pair.Value);
    }
    AddInfo(FString::Printf(TEXT("MB2INTERIOR_TEST buildings=%d eligible=%d exceptions=%d%s"), static_cast<int32>(Plans.size()), Eligible, Exceptions, *ReasonText));
    return true;
}

#endif
