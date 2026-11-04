#include "Engine/SharedPtrCompatibility.h"
#include "Serializers/VolumeBrush.h"

#include "BSPOps.h"
#include "Builders/CubeBuilder.h"
#include "Components/BrushComponent.h"
#include "Engine/Brush.h"
#include "Engine/Polys.h"
#include "Model.h"
#include "PhysicsEngine/BodySetup.h"
#include "Containers/ExportContainer.h"
#include "Serializers/PropertySerializer.h"

DECLARE_LOG_CATEGORY_CLASS(LogReflectionVolumeBrush, All, All);

namespace {
	constexpr double FaceNormalTolerance = 0.9999;
	constexpr double FaceOffsetTolerance = 1.0;

	constexpr int32 MaximumHullCorners = 64;

	constexpr double ClipTolerance = 0.01;

	struct FBrushFace {
		FVector Normal = FVector::ZeroVector;
		double Offset = 0.0;

		TArray<FVector> Corners;
	};

	struct FBrushHull {
		TArray<FVector> Corners;
		TArray<FBrushFace> Faces;
	};

	void AddCorner(FBrushFace& Face, const FVector& Corner) {
		for (const FVector& Existing : Face.Corners) {
			if (Existing.Equals(Corner, 0.01)) return;
		}

		Face.Corners.Add(Corner);
	}

	FBrushFace& FindFace(TArray<FBrushFace>& Faces, const FVector& Normal, const double Offset) {
		for (FBrushFace& Face : Faces) {
			if (FVector::DotProduct(Face.Normal, Normal) > FaceNormalTolerance && FMath::Abs(Face.Offset - Offset) < FaceOffsetTolerance) {
				return Face;
			}
		}

		FBrushFace& Face = Faces.AddDefaulted_GetRef();

		Face.Normal = Normal;
		Face.Offset = Offset;

		return Face;
	}

	FVector CentreOf(const TArray<FVector>& Corners) {
		FVector Centre = FVector::ZeroVector;

		for (const FVector& Corner : Corners) {
			Centre += Corner;
		}

		return Corners.Num() > 0 ? Centre / Corners.Num() : Centre;
	}

	void SortCornersAroundNormal(const FVector& Normal, TArray<FVector>& Corners) {
		if (Corners.Num() < 3) return;

		const FVector Centre = CentreOf(Corners);

		FVector Right, Up;
		Normal.FindBestAxisVectors(Up, Right);

		Corners.Sort([&Centre, &Right, &Up](const FVector& A, const FVector& B) {
			const FVector OffsetA = A - Centre;
			const FVector OffsetB = B - Centre;

			return FMath::Atan2(FVector::DotProduct(OffsetA, Up), FVector::DotProduct(OffsetA, Right))
				 < FMath::Atan2(FVector::DotProduct(OffsetB, Up), FVector::DotProduct(OffsetB, Right));
		});
	}

	bool AddPoly(TArray<FPoly>& Polys, const FVector& Normal, TArray<FVector> Corners) {
		SortCornersAroundNormal(Normal, Corners);

		if (Corners.Num() < 3) return false;

		const FVector Ordered = FVector::CrossProduct(Corners[1] - Corners[0], Corners[2] - Corners[0]);

		if (FVector::DotProduct(Ordered, Normal) < 0.0) {
			Algo::Reverse(Corners);
		}

		FPoly Poly;

		Poly.Init();

		Poly.Base = FVector3f(Corners[0]);
		Poly.PolyFlags = 0;

		Poly.Material = nullptr;

		Poly.Vertices.Reserve(Corners.Num());

		for (const FVector& Corner : Corners) {
			Poly.Vertices.Add(FVector3f(Corner));
		}

		if (Poly.Finalize(nullptr, 1) != 0) return false;

		Polys.Add(MoveTemp(Poly));

		return true;
	}

	void GatherFacesFromPoints(const TArray<FVector>& Points, TArray<FBrushFace>& Faces) {
		const int32 Count = Points.Num();

		for (int32 First = 0; First < Count; ++First) {
			for (int32 Second = First + 1; Second < Count; ++Second) {
				for (int32 Third = Second + 1; Third < Count; ++Third) {
					const FVector Normal = FVector::CrossProduct(Points[Second] - Points[First], Points[Third] - Points[First]).GetSafeNormal();

					if (Normal.IsNearlyZero()) continue;

					const double Offset = FVector::DotProduct(Normal, Points[First]);

					bool bAnyInFront = false;
					bool bAnyBehind = false;

					for (int32 Point = 0; Point < Count; ++Point) {
						const double Distance = FVector::DotProduct(Normal, Points[Point]) - Offset;

						if (Distance > FaceOffsetTolerance) bAnyInFront = true;
						else if (Distance < -FaceOffsetTolerance) bAnyBehind = true;

						if (bAnyInFront && bAnyBehind) break;
					}

					if (bAnyInFront && bAnyBehind) continue;

					const FVector Outward = bAnyInFront ? -Normal : Normal;
					const double OutwardOffset = bAnyInFront ? -Offset : Offset;

					FBrushFace& Face = FindFace(Faces, Outward, OutwardOffset);

					for (const FVector& Point : Points) {
						if (FMath::Abs(FVector::DotProduct(Outward, Point) - OutwardOffset) < FaceOffsetTolerance) {
							AddCorner(Face, Point);
						}
					}
				}
			}
		}
	}

	void GatherHullFromBox(const FKBoxElem& Box, FBrushHull& Hull) {
		const FTransform Transform(Box.Rotation, Box.Center);
		const FVector Extent(Box.X * 0.5, Box.Y * 0.5, Box.Z * 0.5);

		static const FVector Normals[6] = {
			FVector(1, 0, 0), FVector(-1, 0, 0),
			FVector(0, 1, 0), FVector(0, -1, 0),
			FVector(0, 0, 1), FVector(0, 0, -1)
		};

		TArray<FVector> Local;

		for (int32 Corner = 0; Corner < 8; ++Corner) {
			Local.Add(FVector(
				(Corner & 1) ? Extent.X : -Extent.X,
				(Corner & 2) ? Extent.Y : -Extent.Y,
				(Corner & 4) ? Extent.Z : -Extent.Z
			));

			Hull.Corners.Add(Transform.TransformPosition(Local.Last()));
		}

		for (const FVector& Normal : Normals) {
			const FVector Outward = Transform.TransformVectorNoScale(Normal);

			FBrushFace& Face = Hull.Faces.AddDefaulted_GetRef();

			Face.Normal = Outward;

			for (int32 Corner = 0; Corner < Local.Num(); ++Corner) {
				if (FVector::DotProduct(Local[Corner], Normal) > 0.0) {
					Face.Corners.Add(Hull.Corners[Corner]);
				}
			}

			Face.Offset = Face.Corners.Num() > 0 ? FVector::DotProduct(Outward, Face.Corners[0]) : 0.0;
		}
	}

	bool GatherHullFromConvex(const FKConvexElem& Convex, FBrushHull& Hull) {
		const TArray<FVector>& Points = Convex.VertexData;

#if UE4_24_BELOW
		const TArray<int32> Indices;
#else
		const TArray<int32>& Indices = Convex.IndexData;
#endif

		if (Points.Num() < 4) return false;

		const FTransform Transform = Convex.GetTransform();

		Hull.Corners.Reserve(Points.Num());

		constexpr double SameCorner = 1.0;

		for (const FVector& Point : Points) {
			const FVector Placed = Transform.TransformPosition(Point);

			bool bAlready = false;

			for (const FVector& Kept : Hull.Corners) {
				if (FVector::DistSquared(Kept, Placed) <= SameCorner * SameCorner) {
					bAlready = true;

					break;
				}
			}

			if (!bAlready) Hull.Corners.Add(Placed);
		}

		if (Indices.Num() < 12 || Indices.Num() % 3 != 0) {
			if (Hull.Corners.Num() > MaximumHullCorners) return false;

			GatherFacesFromPoints(Hull.Corners, Hull.Faces);

			return Hull.Faces.Num() > 0;
		}

		for (int32 Index = 0; Index + 2 < Indices.Num(); Index += 3) {
			if (!Hull.Corners.IsValidIndex(Indices[Index]) || !Hull.Corners.IsValidIndex(Indices[Index + 1]) || !Hull.Corners.IsValidIndex(Indices[Index + 2])) {
				continue;
			}

			const FVector A = Hull.Corners[Indices[Index]];
			const FVector B = Hull.Corners[Indices[Index + 1]];
			const FVector C = Hull.Corners[Indices[Index + 2]];

			const FVector Normal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();

			if (Normal.IsNearlyZero()) continue;

			FBrushFace& Face = FindFace(Hull.Faces, Normal, FVector::DotProduct(Normal, A));

			AddCorner(Face, A);
			AddCorner(Face, B);
			AddCorner(Face, C);
		}

		return Hull.Faces.Num() > 0;
	}

	TArray<FVector> ClipToHalfspace(const TArray<FVector>& Polygon, const FVector& Normal, const double Offset, const bool bKeepBeyond) {
		TArray<FVector> Result;

		if (Polygon.Num() < 3) return Result;

		const double Side = bKeepBeyond ? -1.0 : 1.0;

		for (int32 Index = 0; Index < Polygon.Num(); ++Index) {
			const FVector& Current = Polygon[Index];
			const FVector& Next = Polygon[(Index + 1) % Polygon.Num()];

			const double Here = Side * (Offset - FVector::DotProduct(Normal, Current));
			const double There = Side * (Offset - FVector::DotProduct(Normal, Next));

			if (Here >= -ClipTolerance) Result.Add(Current);

			if ((Here > ClipTolerance && There < -ClipTolerance) || (Here < -ClipTolerance && There > ClipTolerance)) {
				Result.Add(FMath::Lerp(Current, Next, Here / (Here - There)));
			}
		}

		return Result;
	}

	bool OverlapsFace(const TArray<FVector>& Face, const FVector& FaceNormal, const FBrushHull& Hull) {
		TArray<FVector> Within = Face;

		for (const FBrushFace& Cut : Hull.Faces) {
			if (FMath::Abs(FVector::DotProduct(Cut.Normal, FaceNormal)) > FaceNormalTolerance) continue;

			Within = ClipToHalfspace(Within, Cut.Normal, Cut.Offset, false);

			if (Within.Num() < 3) return false;
		}

		return Within.Num() >= 3;
	}

	void SubtractHull(const TArray<FVector>& Face, const FVector& FaceNormal, const FBrushHull& Hull, TArray<TArray<FVector>>& Parts) {
		TArray<FVector> Remaining = Face;

		for (const FBrushFace& Cut : Hull.Faces) {
			if (FMath::Abs(FVector::DotProduct(Cut.Normal, FaceNormal)) > FaceNormalTolerance) continue;

			TArray<FVector> Beyond = ClipToHalfspace(Remaining, Cut.Normal, Cut.Offset, true);

			if (Beyond.Num() >= 3) Parts.Add(MoveTemp(Beyond));

			Remaining = ClipToHalfspace(Remaining, Cut.Normal, Cut.Offset, false);

			if (Remaining.Num() < 3) return;
		}
	}

	int32 BuildBoundaryPolys(TArray<FBrushHull>& Hulls, TArray<FPoly>& Polys) {
		int32 Covered = 0;

		for (int32 Index = 0; Index < Hulls.Num(); ++Index) {
			for (FBrushFace& Face : Hulls[Index].Faces) {
				if (Face.Corners.Num() < 3) continue;

				SortCornersAroundNormal(Face.Normal, Face.Corners);

				TArray<TArray<FVector>> Parts;
				Parts.Add(Face.Corners);

				for (int32 Other = 0; Other < Hulls.Num() && Parts.Num() > 0; ++Other) {
					if (Other == Index) continue;

					bool bBeyond = false;

					for (const FVector& Corner : Hulls[Other].Corners) {
						if (FVector::DotProduct(Face.Normal, Corner) - Face.Offset > FaceOffsetTolerance) {
							bBeyond = true;

							break;
						}
					}

					if (!bBeyond) continue;

					TArray<TArray<FVector>> Kept;

					for (const TArray<FVector>& Part : Parts) {
						if (OverlapsFace(Part, Face.Normal, Hulls[Other])) {
							SubtractHull(Part, Face.Normal, Hulls[Other], Kept);
						} else {
							Kept.Add(Part);
						}
					}

					Parts = MoveTemp(Kept);
				}

				if (Parts.Num() == 0) {
					Covered++;

					continue;
				}

				for (TArray<FVector>& Part : Parts) {
					AddPoly(Polys, Face.Normal, MoveTemp(Part));
				}
			}
		}

		return Covered;
	}

	bool TryReadBox(const TArray<FBrushHull>& Hulls, FVector& OutSize, FVector& OutCentre) {
		if (Hulls.Num() != 1 || Hulls[0].Faces.Num() != 6) return false;

		double Least[3] = { 0.0, 0.0, 0.0 };
		double Most[3] = { 0.0, 0.0, 0.0 };

		bool bSeenLeast[3] = { false, false, false };
		bool bSeenMost[3] = { false, false, false };

		for (const FBrushFace& Face : Hulls[0].Faces) {
			if (Face.Corners.Num() != 4) return false;

			int32 Axis = INDEX_NONE;
			double Along = 0.0;

			for (int32 Index = 0; Index < 3; ++Index) {
				if (FMath::Abs(Face.Normal[Index]) > FaceNormalTolerance) {
					Axis = Index;
					Along = Face.Normal[Index];
				}
			}

			if (Axis == INDEX_NONE) return false;

			if (Along > 0.0) {
				Most[Axis] = Face.Offset;
				bSeenMost[Axis] = true;
			} else {
				Least[Axis] = -Face.Offset;
				bSeenLeast[Axis] = true;
			}
		}

		for (int32 Index = 0; Index < 3; ++Index) {
			if (!bSeenLeast[Index] || !bSeenMost[Index]) return false;

			OutSize[Index] = Most[Index] - Least[Index];
			OutCentre[Index] = (Most[Index] + Least[Index]) * 0.5;

			if (OutSize[Index] <= FaceOffsetTolerance) return false;
		}

		return true;
	}

	bool FindShape(const UBrushComponent* Component, const TSharedPtr<FJsonObject>& Properties, const UPropertySerializer* Serializer, FKAggregateGeom& OutShape) {
		if (Component->BrushBodySetup != nullptr) {
			OutShape = Component->BrushBodySetup->AggGeom;

			return true;
		}

		if (!Properties.IsValid() || Serializer == nullptr || Serializer->ExportsContainer == nullptr) return false;

		const TSharedPtr<FJsonObject>* Reference;
		if (!Properties->TryGetObjectField(TEXT("BrushBodySetup"), Reference)) return false;

		FUObjectExport* Export = Serializer->ExportsContainer->GetExportByObjectPath(*Reference);
		if (Export == nullptr || !Export->IsJsonValid()) return false;

		const TSharedPtr<FJsonObject> Setup = Export->GetProperties();
		if (!Setup.IsValid()) return false;

		const TSharedPtr<FJsonObject>* Geometry;
		if (!Setup->TryGetObjectField(TEXT("AggGeom"), Geometry)) return false;

		Serializer->DeserializeStruct(FKAggregateGeom::StaticStruct(), Geometry->ToSharedRef(), &OutShape);

		return true;
	}
}

void FVolumeBrush::Rebuild(UBrushComponent* Component, const TSharedPtr<FJsonObject>& Properties, const UPropertySerializer* Serializer) {
	if (Component == nullptr) return;

	ABrush* Brush = Cast<ABrush>(Component->GetOwner());
	if (Brush == nullptr) return;

	if (Brush->GetBrushComponent() != Component) return;

	if (Brush->Brush != nullptr && Brush->Brush->Polys != nullptr && Brush->Brush->Polys->Element.Num() > 0) return;

	FKAggregateGeom Shape;

	if (!FindShape(Component, Properties, Serializer, Shape)) {
		UE_LOG(LogReflectionVolumeBrush, Warning, TEXT("\"%s\" came without collision, which is the one place a cooked volume keeps its shape, so it stays a point"), *Brush->GetName());

		return;
	}

	TArray<FBrushHull> Hulls;

	for (const FKBoxElem& Box : Shape.BoxElems) {
		GatherHullFromBox(Box, Hulls.AddDefaulted_GetRef());
	}

	for (const FKConvexElem& Convex : Shape.ConvexElems) {
		if (!GatherHullFromConvex(Convex, Hulls.AddDefaulted_GetRef())) {
			Hulls.Pop();
		}
	}

	const int32 Pieces = Hulls.Num();

	FVector BoxSize = FVector::ZeroVector;
	FVector BoxCentre = FVector::ZeroVector;

	const bool bIsBox = TryReadBox(Hulls, BoxSize, BoxCentre);

	if (bIsBox && !BoxCentre.IsNearlyZero()) {
		for (FBrushHull& Hull : Hulls) {
			for (FVector& Corner : Hull.Corners) {
				Corner -= BoxCentre;
			}

			for (FBrushFace& Face : Hull.Faces) {
				for (FVector& Corner : Face.Corners) {
					Corner -= BoxCentre;
				}

				Face.Offset -= FVector::DotProduct(Face.Normal, BoxCentre);
			}
		}
	}

	TArray<FPoly> Polys;

	const int32 Covered = BuildBoundaryPolys(Hulls, Polys);

	if (Polys.Num() == 0) {
		UE_LOG(LogReflectionVolumeBrush, Warning, TEXT("\"%s\" has collision of a kind a brush cannot be cut from (%d box(es), %d hull(s), %d sphere(s), %d capsule(s)), so it stays a point"), *Brush->GetName(), Shape.BoxElems.Num(), Shape.ConvexElems.Num(), Shape.SphereElems.Num(), Shape.SphylElems.Num());

		return;
	}

	Brush->PreEditChange(nullptr);

	if (bIsBox && !BoxCentre.IsNearlyZero()) {
		const FTransform Relative = Component->GetRelativeTransform();

		Component->SetRelativeLocation(Relative.GetLocation() + Relative.GetRotation().RotateVector(BoxCentre * Relative.GetScale3D()));
	}

	const EObjectFlags Flags = Brush->GetFlags() & (RF_Transient | RF_Transactional);

	Brush->PolyFlags = 0;

	if (Brush->Brush == nullptr) {
		Brush->Brush = NewObject<UModel>(Brush, NAME_None, Flags);
	}

	Brush->Brush->Initialize(nullptr, true);
	Brush->Brush->Polys = NewObject<UPolys>(Brush->Brush, NAME_None, Flags);
	Brush->Brush->Polys->Element = MoveTemp(Polys);

	Component->Brush = Brush->Brush;

	if (bIsBox) {
		UCubeBuilder* Builder = NewObject<UCubeBuilder>(Brush, NAME_None, Flags);

		Builder->X = BoxSize.X;
		Builder->Y = BoxSize.Y;
		Builder->Z = BoxSize.Z;

		Brush->BrushBuilder = Builder;
	}

	FBSPOps::csgPrepMovingBrush(Brush);

	Brush->PostEditChange();

	Component->MarkRenderStateDirty();

	if (bIsBox) {
		UE_LOG(LogReflectionVolumeBrush, Display, TEXT("\"%s\" was given a cube of %.0f by %.0f by %.0f, with its pivot moved %.0f to the middle of it"), *Brush->GetName(), BoxSize.X, BoxSize.Y, BoxSize.Z, BoxCentre.Size());
	} else {
		UE_LOG(LogReflectionVolumeBrush, Display, TEXT("\"%s\" was given a brush of %d face(s) from %d piece(s), %d dropped from where they met"), *Brush->GetName(), Brush->Brush->Polys->Element.Num(), Pieces, Covered);
	}
}

void FVolumeBrush::Rebuild(ABrush* Brush) {
	if (Brush != nullptr) {
		Rebuild(Brush->GetBrushComponent());
	}
}
