// Copyright 2016 Moai Games, Inc. All Rights Reserved.

#pragma once

#include "VerletClothComponent.generated.h"

UENUM(BlueprintType)
enum class ESideAxis : uint8
{
	X = 0,
	Y,
	Z,
};

UENUM( BlueprintType )
enum class ECollisionPlane : uint8
{
	NONE = 0,
	XY,
	YZ,
	ZX,
};

/** Struct containing information about a point along the cable */
struct FVerletClothHorizontalLine
{
	FVerletClothHorizontalLine()
	: bFree(true), Damping(0.0f)
	{}

	void SetHorizontal(const int32& NumSides, const float& Width)
	{
		HorizontalWidth = Width;
		int32 NewSides = FMath::Max(1, NumSides);
		Acceleration.AddZeroed(NewSides + 1);
		Positions.AddZeroed(NewSides + 1);
		SavedPositions.AddZeroed(NewSides + 1);
	}

	void SetInitPosition(const FVector& CenterLocation, const FVector& RelativeLocation, const FVector& SideVector)
	{
		if (Positions.Num() > 1)
		{
			int32 NumSides = Positions.Num() - 1;
			FVector HorizontalStart = SideVector * (-HorizontalWidth / 2.0f);
			FVector HorizontalDelta = SideVector * (HorizontalWidth / NumSides);
			for (int32 Idx = 0; Idx < Positions.Num(); ++Idx)
			{
				Positions[Idx] = CenterLocation + (HorizontalStart + (HorizontalDelta * Idx));
				if (bFree)
					SavedPositions[Idx] = Positions[Idx];
				else
					SavedPositions[Idx] = RelativeLocation;
			}
		}
	}

	void VerletProcess(float InSubstepTime)
	{
		for (int32 Idx = 0; Idx < Positions.Num(); ++Idx)
		{
			const FVector Vel = (Positions[Idx] - SavedPositions[Idx]);
			FVector NewPosition = Positions[Idx] + Vel * (1.0f - Damping) + (InSubstepTime * Acceleration[Idx]);
			SavedPositions[Idx] = Positions[Idx];
			Positions[Idx] = NewPosition;
		}
	}

	void FixedProcess(const FVector& CenterLocation, const FVector& SideVector)
	{
		if (Positions.Num() > 1)
		{
			int32 NumSides = Positions.Num() - 1;
			FVector HorizontalStart = SideVector * (-HorizontalWidth / 2.0f);
			FVector HorizontalDelta = SideVector * (HorizontalWidth / NumSides);
			for (int32 Idx = 0; Idx < Positions.Num(); ++Idx)
				Positions[Idx] = CenterLocation + (HorizontalStart + (HorizontalDelta * Idx)) + SavedPositions[Idx];
		}
	}

	void UpdateAcceleration(FVerletClothHorizontalLine& NextLine, const FVector& Gravity, const FVector& Wind, bool bLastSegment)
	{
		bool bNoWind = Wind.IsNearlyZero();
		for (int32 Idx = 0; Idx < Positions.Num(); ++Idx)
			Acceleration[Idx] = Gravity;

		for (int32 Idx = 0; Idx < Positions.Num() - 1; ++Idx)
		{
			if (bNoWind)
				break;

			FVector First = Positions[Idx + 1] - Positions[Idx];
			FVector Second = NextLine.Positions[Idx] - Positions[Idx];
			FVector Normal = FVector::CrossProduct(First, Second).GetSafeNormal();
			Acceleration[Idx] += (Normal * FVector::DotProduct(Normal, Wind));

			First = NextLine.Positions[Idx + 1] - Positions[Idx + 1];
			Second = Positions[Idx] - Positions[Idx + 1];
			Normal = FVector::CrossProduct(First, Second).GetSafeNormal();
			Acceleration[Idx+1] += (Normal * FVector::DotProduct(Normal, Wind));
		}

		if (bLastSegment)
		{
			for (int32 Idx = 0; Idx < NextLine.Positions.Num(); ++Idx)
				NextLine.Acceleration[Idx] = Gravity;

			for (int32 Idx = 0; Idx < NextLine.Positions.Num() - 1; ++Idx)
			{
				if (bNoWind)
					break;

				FVector First = Positions[Idx] - NextLine.Positions[Idx];
				FVector Second = NextLine.Positions[Idx + 1] - NextLine.Positions[Idx];
				FVector Normal = FVector::CrossProduct(First, Second).GetSafeNormal();
				NextLine.Acceleration[Idx] += (Normal * FVector::DotProduct(Normal, Wind));

				First = NextLine.Positions[Idx] - NextLine.Positions[Idx + 1];
				Second = Positions[Idx + 1] - NextLine.Positions[Idx + 1];
				Normal = FVector::CrossProduct(First, Second).GetSafeNormal();
				NextLine.Acceleration[Idx + 1] += (Normal * FVector::DotProduct(Normal, Wind));
			}
		}
	}

	void SolveConstraints(FVerletClothHorizontalLine& NextLine, float SegmentLength, float DiagonalLength)
	{
		SolveHorizontalConstraint();
		SolveVerticalConstraint(NextLine, SegmentLength);
		SolveDiagonalConstraint1(NextLine, DiagonalLength);
		SolveDiagonalConstraint2(NextLine, DiagonalLength);
	}

	void SolveHorizontalConstraint()
	{
		if (bFree)
		{
			int32 IterationCount = Positions.Num() - 1;
			float DesiredDistance = HorizontalWidth / IterationCount;
			for (int32 Idx = 0; Idx < IterationCount; ++Idx)
				SolvePositionConstraint(Positions[Idx], true, Positions[Idx+1], true, DesiredDistance);
		}
	}

	void SolveVerticalConstraint(FVerletClothHorizontalLine& NextLine, float DesiredDistance)
	{
		for (int32 Idx = 0; Idx < Positions.Num(); ++Idx)
			SolvePositionConstraint(Positions[Idx], bFree, NextLine.Positions[Idx], NextLine.bFree, DesiredDistance);
	}

	// First Diagonal direction
	void SolveDiagonalConstraint1(FVerletClothHorizontalLine& NextLine, float DesiredDistance)
	{
		for (int32 Idx = 0; Idx < Positions.Num() - 1; ++Idx)
			SolvePositionConstraint(Positions[Idx], bFree, NextLine.Positions[Idx + 1], NextLine.bFree, DesiredDistance);
	}

	// Second diagonal direction
	void SolveDiagonalConstraint2(FVerletClothHorizontalLine& NextLine, float DesiredDistance)
	{		
		for (int32 Idx = 0; Idx < Positions.Num() - 1; ++Idx)
			SolvePositionConstraint(Positions[Idx + 1], bFree, NextLine.Positions[Idx], NextLine.bFree, DesiredDistance);
	}

	void SolvePositionConstraint(FVector& PositionA, bool bFreeA, FVector& PositionB, bool bFreeB, float DesiredDistance)
	{
		// Find current vector between lines
		FVector Delta = PositionB - PositionA;
		// 
		float CurrentDistance = Delta.Size();
		float ErrorFactor = (CurrentDistance - DesiredDistance) / CurrentDistance;
		if (ErrorFactor <= 0.0f)
			return;

		// Only move free lines to satisfy constraints
		if (bFreeA && bFreeB)
		{
			PositionA += ErrorFactor * 0.5f * Delta;
			PositionB -= ErrorFactor * 0.5f * Delta;
		}
		else if (bFreeA)
		{
			PositionA += ErrorFactor * Delta;
		}
		else if (bFreeB)
		{
			PositionB -= ErrorFactor * Delta;
		}
	}

	/** If this point is free (simulating) or fixed to something */
	bool bFree;
	/** total width of horizontal side */
	int32 HorizontalWidth;
	/** */
	float Damping;
	/** Current acceleration of point */
	TArray<FVector> Acceleration;
	/** Current position of point */
	TArray<FVector> Positions;
	/** if bFree, Position of point on previous iteration. if not, relative Position of previous point */
	TArray<FVector> SavedPositions;	
};

/** Component that allows you to specify custom triangle mesh geometry */
UCLASS(hidecategories=(Object, Physics, Collision, Activation, "Components|Activation"), Blueprintable, meta=(BlueprintSpawnableComponent), ClassGroup=Rendering)
class VERLETCLOTHCOMPONENT_API UVerletClothComponent : public UMeshComponent
{
	GENERATED_UCLASS_BODY()
public:

	//~ Begin UActorComponent Interface.
	virtual void OnRegister() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	virtual void SendRenderDynamicData_Concurrent() override;

	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual int32 GetNumMaterials() const override { return 1; }

	/** How wide the cloth geometry is */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Verlet Cloth", meta = (ClampMin = "0.01", UIMin = "0.01", UIMax = "50.0"))
	float ClothWidth;

	/** How long the cloth geometry is */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Verlet Cloth", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1000.0"))
	float ClothLength;

	/** Damping of the cloth */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Verlet Cloth", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Damping;

	/** The number of solver iterations controls how 'stiff' the cloth is */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Verlet Cloth", meta = (ClampMin = "1", ClampMax = "100"))
	int32 SolverIterations;

	/** Number of sides the cloth geometry has. (Horizontal) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Verlet Cloth", meta = (ClampMin = "1", ClampMax = "16"))
	int32 NumSides;

	/** Number of segments the cloth geometry has. (Vertical) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Verlet Cloth", meta = (ClampMin = "1", UIMin = "1", UIMax = "50"))
	int32 NumSegments;

	/** Lines are not freed below this count */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Verlet Cloth", meta = (ClampMin = "1"))
	int32 FixedLineCount;

	/** Including actor's trasform with simulating. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Verlet Cloth")
	bool ProcessWorldSpace;

	/** Use Component space gravity instead of world setting gravity. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Verlet Cloth")
	bool bUseLocalGravity;

	/** Component space gravity */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Verlet Cloth", meta = (EditCondition = "bUseLocalGravity"))
	FVector Gravity;

	/** Component space wind */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Verlet Cloth")
	FVector Wind;

	/** Horizontal line axis in component space. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Verlet Cloth")
	ESideAxis SideAxis;

	/** Collision plane. if XY plane has selected, Z axis would be the normal which affects collision side. */
	UPROPERTY( EditAnywhere, BlueprintReadOnly, Category = "Verlet Cloth" )
	ECollisionPlane CollisionPlane;	

private:

	void ProcessCollision();
	void SolveConstraints();
	void UpdateAcceleration(const FVector& Gravity, const FVector& WindVec);
	void VerletIntegrate(float InTime);

	/** Array of cloth lines */
	TArray<FVerletClothHorizontalLine>	HorizontalLines;

	FVector OldComponentLocation;
};