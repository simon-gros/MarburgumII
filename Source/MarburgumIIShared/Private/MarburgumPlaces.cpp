#include "MarburgumPlaces.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumPlaces, Log, All);

namespace MarburgumII::Places
{
    namespace
    {
        /** The street index's bucket side: a lookup within reach of a few dozen metres reads a 3 × 3 block. */
        constexpr double CellCentimetres = 5000.0;

        FIntPoint CellOf(const FVector2D& Point)
        {
            return FIntPoint(FMath::FloorToInt32(Point.X / CellCentimetres), FMath::FloorToInt32(Point.Y / CellCentimetres));
        }

        /** A list of [east, south] plan-metre pairs, at least `Least` of them, all finite. */
        bool ReadPoints(const TArray<TSharedPtr<FJsonValue>>& Values, int32 Least, TArray<FVector2D>& Out, FBox2D& Bounds)
        {
            if (Values.Num() < Least)
            {
                return false;
            }
            Out.Reset(Values.Num());
            for (const TSharedPtr<FJsonValue>& Value : Values)
            {
                const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
                double East = 0.0, South = 0.0;
                if (!Value.IsValid() || !Value->TryGetArray(Pair) || Pair == nullptr || Pair->Num() != 2
                    || (*Pair)[0]->Type != EJson::Number || (*Pair)[1]->Type != EJson::Number)
                {
                    return false;
                }
                East = (*Pair)[0]->AsNumber();
                South = (*Pair)[1]->AsNumber();
                if (!FMath::IsFinite(East) || !FMath::IsFinite(South))
                {
                    return false;
                }
                Out.Add(FromPlan(East, South));
                Bounds += Out.Last();
            }
            return true;
        }

        bool ReadRings(const TSharedPtr<FJsonObject>& Object, TArray<TArray<FVector2D>>& Rings, FBox2D& Bounds)
        {
            const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
            if (!Object->TryGetArrayField(TEXT("rings"), Values) || Values == nullptr || Values->IsEmpty())
            {
                return false;
            }
            for (const TSharedPtr<FJsonValue>& Value : *Values)
            {
                const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;
                if (!Value.IsValid() || !Value->TryGetArray(Points) || Points == nullptr
                    || !ReadPoints(*Points, 3, Rings.AddDefaulted_GetRef(), Bounds))
                {
                    return false;
                }
            }
            return true;
        }

        /** A JSON number and nothing else: a `true` read as 1 m² would win every containment it reaches. */
        bool ReadPositive(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, double& Out)
        {
            const TSharedPtr<FJsonValue> Value = Object->TryGetField(Field);
            if (!Value.IsValid() || Value->Type != EJson::Number || !FMath::IsFinite(Value->AsNumber()) || Value->AsNumber() <= 0.0)
            {
                return false;
            }
            Out = Value->AsNumber();
            return true;
        }

        bool ReadName(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, FString& Out)
        {
            return Object->TryGetStringField(Field, Out) && !Out.TrimStartAndEnd().IsEmpty();
        }

        /** Even-odd ray casting over every ring, as the original's tools/build_districts.py `inside`. */
        bool ContainsRings(const TArray<TArray<FVector2D>>& Rings, const FBox2D& Bounds, const FVector2D& Point)
        {
            if (!Bounds.IsInside(Point))
            {
                return false;
            }
            bool bInside = false;
            for (const TArray<FVector2D>& Ring : Rings)
            {
                for (int32 I = 0, J = Ring.Num() - 1; I < Ring.Num(); J = I++)
                {
                    const FVector2D& A = Ring[J];
                    const FVector2D& B = Ring[I];
                    if ((A.Y > Point.Y) != (B.Y > Point.Y) && A.X + (Point.Y - A.Y) / (B.Y - A.Y) * (B.X - A.X) > Point.X)
                    {
                        bInside = !bInside;
                    }
                }
            }
            return bInside;
        }

        double DistanceToSegment(const FVector2D& Point, const FVector2D& A, const FVector2D& B)
        {
            const FVector2D Along = B - A;
            const double Length = Along.SizeSquared();
            if (Length <= UE_DOUBLE_SMALL_NUMBER)
            {
                return FVector2D::Distance(Point, A);
            }
            const double T = FMath::Clamp(FVector2D::DotProduct(Point - A, Along) / Length, 0.0, 1.0);
            return FVector2D::Distance(Point, A + Along * T);
        }

        double DistanceToRings(const TArray<TArray<FVector2D>>& Rings, const FVector2D& Point)
        {
            double Best = TNumericLimits<double>::Max();
            for (const TArray<FVector2D>& Ring : Rings)
            {
                for (int32 I = 0, J = Ring.Num() - 1; I < Ring.Num(); J = I++)
                {
                    Best = FMath::Min(Best, DistanceToSegment(Point, Ring[J], Ring[I]));
                }
            }
            return Best;
        }

        TArray<TSharedPtr<FJsonObject>> Records(const TSharedPtr<FJsonObject>& Root, const TCHAR* Field)
        {
            TArray<TSharedPtr<FJsonObject>> Out;
            const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
            if (Root->TryGetArrayField(Field, Values) && Values != nullptr)
            {
                for (const TSharedPtr<FJsonValue>& Value : *Values)
                {
                    const TSharedPtr<FJsonObject>* Object = nullptr;
                    Out.Add(Value.IsValid() && Value->TryGetObject(Object) && Object != nullptr ? *Object : nullptr);
                }
            }
            return Out;
        }
    }

    bool FCatalogue::Parse(const FString& Json, FString& OutError)
    {
        *this = FCatalogue();
        TSharedPtr<FJsonObject> Root;
        const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Json);
        if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
        {
            OutError = TEXT("not a JSON object");
            return false;
        }
        FString Format;
        int32 Version = 0;
        if (!Root->TryGetStringField(TEXT("format"), Format) || Format != TEXT("marburgumii.places")
            || !Root->TryGetNumberField(TEXT("version"), Version) || Version != 1)
        {
            OutError = TEXT("not a marburgumii.places version 1 document");
            return false;
        }

        const auto Reject = [this](const TCHAR* What, int32 Index)
        {
            // The original's rule: a record nobody can stand in is left out with a complaint.
            UE_LOG(LogMarburgumPlaces, Warning, TEXT("%s %d in the places file is malformed and is left out"), What, Index);
            ++Rejected;
        };

        const TArray<TSharedPtr<FJsonObject>> DistrictRecords = Records(Root, TEXT("districts"));
        for (int32 Index = 0; Index < DistrictRecords.Num(); ++Index)
        {
            const TSharedPtr<FJsonObject>& Record = DistrictRecords[Index];
            FDistrict District;
            if (!Record.IsValid() || !ReadName(Record, TEXT("id"), District.Id) || !ReadName(Record, TEXT("kind"), District.Kind)
                || !ReadName(Record, TEXT("display_name"), District.DisplayName)
                || !ReadPositive(Record, TEXT("area_m2"), District.AreaSquareMetres)
                || !ReadRings(Record, District.Rings, District.Bounds))
            {
                Reject(TEXT("district"), Index);
                continue;
            }
            Record->TryGetStringField(TEXT("name"), District.Name);
            Districts.Add(MoveTemp(District));
        }

        const TArray<TSharedPtr<FJsonObject>> StreetRecords = Records(Root, TEXT("streets"));
        for (int32 Index = 0; Index < StreetRecords.Num(); ++Index)
        {
            const TSharedPtr<FJsonObject>& Record = StreetRecords[Index];
            FStreet Street;
            const TArray<TSharedPtr<FJsonValue>>* Line = nullptr;
            bool bShaped = false;
            if (Record.IsValid() && ReadName(Record, TEXT("name"), Street.Name))
            {
                if (Record->HasField(TEXT("rings")))
                {
                    Street.bSquare = true;
                    bShaped = ReadRings(Record, Street.Rings, Street.Bounds);
                }
                else
                {
                    bShaped = Record->TryGetArrayField(TEXT("line"), Line) && Line != nullptr && ReadPoints(*Line, 2, Street.Line, Street.Bounds);
                }
            }
            if (!bShaped)
            {
                Reject(TEXT("street"), Index);
                continue;
            }
            Record->TryGetStringField(TEXT("id"), Street.Id);
            Record->TryGetStringField(TEXT("highway"), Street.Highway);
            Record->TryGetBoolField(TEXT("car_road"), Street.bCarRoad);
            Streets.Add(MoveTemp(Street));
        }

        const TArray<TSharedPtr<FJsonObject>> LandmarkRecords = Records(Root, TEXT("landmarks"));
        for (int32 Index = 0; Index < LandmarkRecords.Num(); ++Index)
        {
            const TSharedPtr<FJsonObject>& Record = LandmarkRecords[Index];
            FLandmark Landmark;
            if (!Record.IsValid() || !ReadName(Record, TEXT("eid"), Landmark.Eid)
                || !ReadName(Record, TEXT("display_name"), Landmark.DisplayName) || !ReadName(Record, TEXT("tier"), Landmark.Tier)
                || !ReadPositive(Record, TEXT("area_m2"), Landmark.AreaSquareMetres)
                || !ReadRings(Record, Landmark.Rings, Landmark.Bounds))
            {
                Reject(TEXT("landmark"), Index);
                continue;
            }
            Record->TryGetStringField(TEXT("name"), Landmark.Name);
            Record->TryGetStringField(TEXT("dating"), Landmark.Dating);
            Landmarks.Add(MoveTemp(Landmark));
        }

        Index();
        bLoaded = true;
        return true;
    }

    void FCatalogue::Index()
    {
        const auto Add = [this](int32 Street, const FVector2D& A, const FVector2D& B)
        {
            const int32 Segment = Segments.Add({ Street, A, B });
            const FIntPoint Low = CellOf(FVector2D(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y)));
            const FIntPoint High = CellOf(FVector2D(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y)));
            for (int32 X = Low.X; X <= High.X; ++X)
            {
                for (int32 Y = Low.Y; Y <= High.Y; ++Y)
                {
                    Cells.FindOrAdd(FIntPoint(X, Y)).Add(Segment);
                }
            }
        };
        for (int32 Street = 0; Street < Streets.Num(); ++Street)
        {
            const FStreet& Way = Streets[Street];
            if (Way.bSquare)
            {
                Squares.Add(Street);
                for (const TArray<FVector2D>& Ring : Way.Rings)
                {
                    for (int32 I = 0, J = Ring.Num() - 1; I < Ring.Num(); J = I++)
                    {
                        Add(Street, Ring[J], Ring[I]);
                    }
                }
            }
            else
            {
                for (int32 I = 0; I + 1 < Way.Line.Num(); ++I)
                {
                    Add(Street, Way.Line[I], Way.Line[I + 1]);
                }
            }
        }
    }

    void FCatalogue::ForEachSegmentNear(const FVector2D& Point, double Within, TFunctionRef<void(const FSegment&)> Visit) const
    {
        const FIntPoint Low = CellOf(Point - FVector2D(Within));
        const FIntPoint High = CellOf(Point + FVector2D(Within));
        for (int32 X = Low.X; X <= High.X; ++X)
        {
            for (int32 Y = Low.Y; Y <= High.Y; ++Y)
            {
                if (const TArray<int32>* Bucket = Cells.Find(FIntPoint(X, Y)))
                {
                    for (const int32 Segment : *Bucket)
                    {
                        Visit(Segments[Segment]);
                    }
                }
            }
        }
    }

    const FDistrict* FCatalogue::FindDistrict(const FVector2D& Point) const
    {
        const FDistrict* Smallest = nullptr;
        for (const FDistrict& District : Districts)
        {
            // Strictly smaller, so a tie keeps file order: quarters, then communities, then settlements.
            if ((Smallest == nullptr || District.AreaSquareMetres < Smallest->AreaSquareMetres) && Contains(District, Point))
            {
                Smallest = &District;
            }
        }
        return Smallest;
    }

    const FDistrict* FCatalogue::FindDistrictById(const FString& Id) const
    {
        return Districts.FindByPredicate([&Id](const FDistrict& District) { return District.Id == Id; });
    }

    const FLandmark* FCatalogue::FindLandmark(const FString& Eid) const
    {
        return Landmarks.FindByPredicate([&Eid](const FLandmark& Landmark) { return Landmark.Eid == Eid; });
    }

    FStreetHit FCatalogue::FindStreet(const FVector2D& Point, double WithinCentimetres) const
    {
        // The nearest edge of each street in reach; a square is zero for anyone standing on it.
        TMap<int32, double> Nearest;
        for (const int32 Square : Squares)
        {
            if (ContainsRings(Streets[Square].Rings, Streets[Square].Bounds, Point))
            {
                Nearest.Add(Square, 0.0);
            }
        }
        ForEachSegmentNear(Point, WithinCentimetres, [&](const FSegment& Segment)
        {
            const double Distance = DistanceToSegment(Point, Segment.A, Segment.B);
            double& Best = Nearest.FindOrAdd(Segment.Street, TNumericLimits<double>::Max());
            Best = FMath::Min(Best, Distance);
        });
        FStreetHit Hit;
        int32 BestStreet = INDEX_NONE;
        double Best = WithinCentimetres;
        for (const TPair<int32, double>& Candidate : Nearest)
        {
            if (Candidate.Value < Best || (Candidate.Value == Best && BestStreet != INDEX_NONE && Candidate.Key < BestStreet))
            {
                Best = Candidate.Value;
                BestStreet = Candidate.Key;
            }
        }
        if (BestStreet != INDEX_NONE)
        {
            Hit.Street = &Streets[BestStreet];
            Hit.Distance = Best;
        }
        return Hit;
    }

    double FCatalogue::DistanceToStreetNamed(const FVector2D& Point, const FString& Name, double WithinCentimetres) const
    {
        double Best = -1.0;
        const auto Offer = [&](double Distance)
        {
            if (Distance <= WithinCentimetres && (Best < 0.0 || Distance < Best))
            {
                Best = Distance;
            }
        };
        for (const int32 Square : Squares)
        {
            if (Streets[Square].Name == Name && ContainsRings(Streets[Square].Rings, Streets[Square].Bounds, Point))
            {
                Offer(0.0);
            }
        }
        ForEachSegmentNear(Point, WithinCentimetres, [&](const FSegment& Segment)
        {
            if (Streets[Segment.Street].Name == Name)
            {
                Offer(DistanceToSegment(Point, Segment.A, Segment.B));
            }
        });
        return Best;
    }

    TArray<FLandmarkHit> FCatalogue::GetNearbyLandmarks(const FVector2D& Point, double WithinCentimetres, const FString& Tier) const
    {
        TArray<FLandmarkHit> Hits;
        for (const FLandmark& Landmark : Landmarks)
        {
            if ((!Tier.IsEmpty() && Landmark.Tier != Tier)
                || Landmark.Bounds.ComputeSquaredDistanceToPoint(Point) > WithinCentimetres * WithinCentimetres)
            {
                continue;
            }
            const bool bInside = ContainsRings(Landmark.Rings, Landmark.Bounds, Point);
            const double Edge = DistanceToRings(Landmark.Rings, Point);
            const double Distance = bInside ? 0.0 : Edge;
            if (Distance <= WithinCentimetres)
            {
                Hits.Add({ &Landmark, Distance, bInside ? Edge : 0.0 });
            }
        }
        Hits.Sort([](const FLandmarkHit& A, const FLandmarkHit& B)
        {
            if (A.Distance != B.Distance)
            {
                return A.Distance < B.Distance;
            }
            if (A.Landmark->AreaSquareMetres != B.Landmark->AreaSquareMetres)
            {
                return A.Landmark->AreaSquareMetres < B.Landmark->AreaSquareMetres;
            }
            return A.Landmark->Eid < B.Landmark->Eid;
        });
        return Hits;
    }

    bool FCatalogue::Contains(const FDistrict& District, const FVector2D& Point)
    {
        return ContainsRings(District.Rings, District.Bounds, Point);
    }

    double FCatalogue::DistanceToBoundary(const FDistrict& District, const FVector2D& Point)
    {
        return DistanceToRings(District.Rings, Point);
    }

    const FCatalogue& Get()
    {
        // A function-local static is initialised once even with the server and client asking from different threads.
        static const FCatalogue Loaded = []
        {
            FCatalogue Catalogue;
            const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), PlacesFile);
            FString Text, Error;
            if (!FFileHelper::LoadFileToString(Text, *Path))
            {
                UE_LOG(LogMarburgumPlaces, Error, TEXT("cannot read %s; no place will be named"), *Path);
            }
            else if (!Catalogue.Parse(Text, Error))
            {
                UE_LOG(LogMarburgumPlaces, Error, TEXT("%s: %s; no place will be named"), *Path, *Error);
            }
            else
            {
                UE_LOG(LogMarburgumPlaces, Log, TEXT("places: %d districts, %d streets, %d landmarks (%d left out) from %s"),
                    Catalogue.GetDistricts().Num(), Catalogue.GetStreets().Num(), Catalogue.GetLandmarks().Num(), Catalogue.GetRejected(), *Path);
            }
            return Catalogue;
        }();
        return Loaded;
    }

    FString FDistrictResolver::Resolve(const FCatalogue& Catalogue, const FVector2D& Point, bool bInNamedArea)
    {
        if (bInNamedArea)
        {
            // Leaving the area hands the name straight back to the district around it.
            HeldId.Reset();
            return FString();
        }
        const FDistrict* Found = Catalogue.FindDistrict(Point);
        if (!HeldId.IsEmpty() && (Found == nullptr || Found->Id != HeldId))
        {
            // Only on the way out: a point still inside the held district has walked into a smaller one within it,
            // and that name is the right one now.
            const FDistrict* Held = Catalogue.FindDistrictById(HeldId);
            if (Held != nullptr && !FCatalogue::Contains(*Held, Point) && FCatalogue::DistanceToBoundary(*Held, Point) <= BorderHoldCentimetres)
            {
                return HeldId;
            }
        }
        HeldId = Found != nullptr ? Found->Id : FString();
        return HeldId;
    }

    FString FStreetResolver::Resolve(const FCatalogue& Catalogue, const FVector2D& Point)
    {
        const FStreetHit Hit = Catalogue.FindStreet(Point, StreetReachCentimetres);
        if (Hit.Street != nullptr && Hit.Street->Name == HeldName)
        {
            return HeldName;
        }
        if (!HeldName.IsEmpty())
        {
            // Measured to the held name over every way that carries it: past a junction the nearest piece of the same
            // street is often another way.
            const double Held = Catalogue.DistanceToStreetNamed(Point, HeldName, StreetReachCentimetres + StreetSwitchMarginCentimetres);
            if (Held >= 0.0 && (Hit.Street == nullptr || Held - Hit.Distance < StreetSwitchMarginCentimetres))
            {
                return HeldName;
            }
        }
        HeldName = Hit.Street != nullptr ? Hit.Street->Name : FString();
        return HeldName;
    }

    FString FLandmarkResolver::Resolve(const FCatalogue& Catalogue, const FVector2D& Point)
    {
        const TArray<FLandmarkHit> Hits = Catalogue.GetNearbyLandmarks(Point, LandmarkReachCentimetres + LandmarkSwitchMarginCentimetres, BannerLandmarkTier);
        // Nearest first, the smaller first among those the player is inside.
        const FLandmarkHit* Nearest = Hits.Num() > 0 && Hits[0].Distance <= LandmarkReachCentimetres ? &Hits[0] : nullptr;
        const FLandmarkHit* Held = HeldEid.IsEmpty() ? nullptr
            : Hits.FindByPredicate([this](const FLandmarkHit& Hit) { return Hit.Landmark->Eid == HeldEid; });
        if (Held != nullptr)
        {
            // A metre inside another outline, and a metre out of the held one or inside a smaller one within it: the
            // player has walked into that building, so it is named at once.
            const bool bWalkedIn = Nearest != nullptr && Nearest != Held && Nearest->Depth >= LandmarkWalkInCentimetres
                && (Held->Distance >= LandmarkWalkInCentimetres || Nearest->Landmark->AreaSquareMetres < Held->Landmark->AreaSquareMetres);
            if (!bWalkedIn && (Nearest == nullptr || Held->Distance - Nearest->Distance < LandmarkSwitchMarginCentimetres))
            {
                return HeldEid;
            }
        }
        HeldEid = Nearest != nullptr ? Nearest->Landmark->Eid : FString();
        return HeldEid;
    }
}
