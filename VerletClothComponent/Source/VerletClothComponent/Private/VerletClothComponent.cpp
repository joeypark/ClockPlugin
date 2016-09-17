// Copyright 2016 Moai Games, Inc. All Rights Reserved.

#include "VerletClothComponentPluginPrivatePCH.h"
#include "DynamicMeshBuilder.h"
#include "EngineGlobals.h"
#include "LocalVertexFactory.h"
#include "Engine/Engine.h"

DECLARE_CYCLE_STAT(TEXT("Update Verlet Cloth Time"), STAT_UpdateVerletClothTime, STATGROUP_Game)

/** Vertex Buffer */
class FVerletClothVertexBuffer : public FVertexBuffer
{
public:
	virtual void InitRHI() override
	{
		FRHIResourceCreateInfo CreateInfo;
		VertexBufferRHI = RHICreateVertexBuffer(NumVerts * sizeof(FDynamicMeshVertex), BUF_Dynamic, CreateInfo);
	}

	int32 NumVerts;
};

/** Index Buffer */
class FVerletClothIndexBuffer : public FIndexBuffer
{
public:
	virtual void InitRHI() override
	{
		FRHIResourceCreateInfo CreateInfo;
		IndexBufferRHI = RHICreateIndexBuffer(sizeof(int32), NumIndices * sizeof(int32), BUF_Dynamic, CreateInfo);
	}

	int32 NumIndices;
};

/** Vertex Factory */
class FVerletClothVertexFactory : public FLocalVertexFactory
{
public:

	FVerletClothVertexFactory()
	{}


	/** Initialization */
	void Init(const FVerletClothVertexBuffer* VertexBuffer)
	{
		if (IsInRenderingThread())
		{
			// Initialize the vertex factory's stream components.
			DataType NewData;
			NewData.PositionComponent = STRUCTMEMBER_VERTEXSTREAMCOMPONENT(VertexBuffer, FDynamicMeshVertex, Position, VET_Float3);
			NewData.TextureCoordinates.Add(
				FVertexStreamComponent(VertexBuffer, STRUCT_OFFSET(FDynamicMeshVertex, TextureCoordinate), sizeof(FDynamicMeshVertex), VET_Float2)
			);
			NewData.TangentBasisComponents[0] = STRUCTMEMBER_VERTEXSTREAMCOMPONENT(VertexBuffer, FDynamicMeshVertex, TangentX, VET_PackedNormal);
			NewData.TangentBasisComponents[1] = STRUCTMEMBER_VERTEXSTREAMCOMPONENT(VertexBuffer, FDynamicMeshVertex, TangentZ, VET_PackedNormal);
			SetData(NewData);
		}
		else
		{
			ENQUEUE_UNIQUE_RENDER_COMMAND_TWOPARAMETER(
				InitClothVertexFactory,
				FVerletClothVertexFactory*, VertexFactory, this,
				const FVerletClothVertexBuffer*, VertexBuffer, VertexBuffer,
				{
					// Initialize the vertex factory's stream components.
					DataType NewData;
			NewData.PositionComponent = STRUCTMEMBER_VERTEXSTREAMCOMPONENT(VertexBuffer,FDynamicMeshVertex,Position,VET_Float3);
			NewData.TextureCoordinates.Add(
				FVertexStreamComponent(VertexBuffer,STRUCT_OFFSET(FDynamicMeshVertex,TextureCoordinate),sizeof(FDynamicMeshVertex),VET_Float2)
			);
			NewData.TangentBasisComponents[0] = STRUCTMEMBER_VERTEXSTREAMCOMPONENT(VertexBuffer,FDynamicMeshVertex,TangentX,VET_PackedNormal);
			NewData.TangentBasisComponents[1] = STRUCTMEMBER_VERTEXSTREAMCOMPONENT(VertexBuffer,FDynamicMeshVertex,TangentZ,VET_PackedNormal);
			VertexFactory->SetData(NewData);
				});
		}
	}
};

/** Dynamic data sent to render thread */
struct FVerletClothDynamicHorizontalLine
{
	/** Array of points */
	TArray<FVector> Points;
};

struct FVerletClothDynamicData
{
	/** Array of lines */
	TArray<FVerletClothDynamicHorizontalLine> HorizontalLines;
};

//////////////////////////////////////////////////////////////////////////
// FVerletClothSceneProxy

class FVerletClothSceneProxy : public FPrimitiveSceneProxy
{
public:

	FVerletClothSceneProxy(UVerletClothComponent* Component)
		: FPrimitiveSceneProxy(Component)
		, Material(NULL)
		, DynamicData(NULL)
		, MaterialRelevance(Component->GetMaterialRelevance(GetScene().GetFeatureLevel()))
		, NumSegments(Component->NumSegments)
		, ClothWidth(Component->ClothWidth)
		, NumSides(Component->NumSides)
	{
		VertexBuffer.NumVerts = GetRequiredVertexCount();
		IndexBuffer.NumIndices = GetRequiredIndexCount();

		// Init vertex factory
		VertexFactory.Init(&VertexBuffer);

		// Enqueue initialization of render resource
		BeginInitResource(&VertexBuffer);
		BeginInitResource(&IndexBuffer);
		BeginInitResource(&VertexFactory);

		// Grab material
		Material = Component->GetMaterial(0);
		if (Material == NULL)
		{
			Material = UMaterial::GetDefaultMaterial(MD_Surface);
		}
	}

	virtual ~FVerletClothSceneProxy()
	{
		VertexBuffer.ReleaseResource();
		IndexBuffer.ReleaseResource();
		VertexFactory.ReleaseResource();

		if (DynamicData != NULL)
		{
			delete DynamicData;
		}
	}

	int32 GetRequiredVertexCount() const
	{
		return (NumSegments + 1) * (NumSides + 1);
	}

	int32 GetRequiredIndexCount() const
	{
		return (NumSegments * NumSides * 2) * 3;
	}

	int32 GetVertIndex(int32 LineIdx, int32 Point) const
	{
		return (LineIdx * (NumSides + 1)) + Point;
	}

	void BuildClothMesh(const TArray<FVerletClothDynamicHorizontalLine>& InLines, TArray<FDynamicMeshVertex>& OutVertices, TArray<int32>& OutIndices)
	{
		const int32 NumLines = InLines.Num();
		const int32 SegmentCount = NumLines - 1;

		// Build vertices				
		for (int32 LineIdx = 0; LineIdx < NumLines; LineIdx++)
		{
			const float AlongFrac = (float)LineIdx / (float)SegmentCount;
			const int32 PrevLineIdx = FMath::Max(LineIdx - 1, 0);
			const int32 NextLineIdx = FMath::Min(LineIdx + 1, NumLines - 1);

			int32 NumPoints = InLines[LineIdx].Points.Num();
			for (int32 PointIdx = 0; PointIdx < NumPoints; PointIdx++)
			{
				FVector VerticalDir, RightDir, UpDir;
				if(LineIdx==NextLineIdx)
					VerticalDir = (InLines[LineIdx].Points[PointIdx] - InLines[PrevLineIdx].Points[PointIdx]).GetSafeNormal();
				else
					VerticalDir = (InLines[NextLineIdx].Points[PointIdx] - InLines[LineIdx].Points[PointIdx]).GetSafeNormal();
								
				const int32 PrevPointIdx = FMath::Max(PointIdx - 1, 0);
				const int32 NextPointIdx = FMath::Min(PointIdx + 1, NumPoints - 1);
				if (PointIdx == NextPointIdx)
					RightDir = (InLines[LineIdx].Points[PointIdx] - InLines[LineIdx].Points[PrevPointIdx]).GetSafeNormal();
				else
					RightDir = (InLines[LineIdx].Points[NextPointIdx] - InLines[LineIdx].Points[PointIdx]).GetSafeNormal();
				UpDir = (RightDir ^ VerticalDir).GetSafeNormal();

				const float Frac = float(PointIdx) / float(NumPoints-1);
				FDynamicMeshVertex Vert;
				Vert.Position = InLines[LineIdx].Points[PointIdx];
				Vert.TextureCoordinate = FVector2D(AlongFrac, Frac);
				Vert.Color = FColor::White;
				Vert.SetTangents(RightDir, VerticalDir, UpDir);
				OutVertices.Add(Vert);
			}
		}

		// Build triangles
		for (int32 SegIdx = 0; SegIdx < SegmentCount; SegIdx++)
		{
			for (int32 SideIdx = 0; SideIdx < NumSides; SideIdx++)
			{
				int32 TL = GetVertIndex(SegIdx, SideIdx);
				int32 BL = GetVertIndex(SegIdx+1, SideIdx);
				int32 TR = GetVertIndex(SegIdx, SideIdx+1);
				int32 BR = GetVertIndex(SegIdx+1, SideIdx+1);

				OutIndices.Add(TL);
				OutIndices.Add(BL);
				OutIndices.Add(TR);

				OutIndices.Add(TR);
				OutIndices.Add(BL);
				OutIndices.Add(BR);
			}
		}
	}

	/** Called on render thread to assign new dynamic data */
	void SetDynamicData_RenderThread(FVerletClothDynamicData* NewDynamicData)
	{
		check(IsInRenderingThread());

		// Free existing data if present
		if (DynamicData)
		{
			delete DynamicData;
			DynamicData = NULL;
		}
		DynamicData = NewDynamicData;

		// Build mesh from cloth points
		TArray<FDynamicMeshVertex> Vertices;
		TArray<int32> Indices;

		BuildClothMesh(NewDynamicData->HorizontalLines, Vertices, Indices);

		check(Vertices.Num() == GetRequiredVertexCount());
		check(Indices.Num() == GetRequiredIndexCount());

		void* VertexBufferData = RHILockVertexBuffer(VertexBuffer.VertexBufferRHI, 0, Vertices.Num() * sizeof(FDynamicMeshVertex), RLM_WriteOnly);
		FMemory::Memcpy(VertexBufferData, &Vertices[0], Vertices.Num() * sizeof(FDynamicMeshVertex));
		RHIUnlockVertexBuffer(VertexBuffer.VertexBufferRHI);

		void* IndexBufferData = RHILockIndexBuffer(IndexBuffer.IndexBufferRHI, 0, Indices.Num() * sizeof(int32), RLM_WriteOnly);
		FMemory::Memcpy(IndexBufferData, &Indices[0], Indices.Num() * sizeof(int32));
		RHIUnlockIndexBuffer(IndexBuffer.IndexBufferRHI);
	}

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_ClothSceneProxy_GetDynamicMeshElements);

		bool bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;

		auto WireframeMaterialInstance = new FColoredMaterialRenderProxy(
			GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy(IsSelected()) : NULL,
			FLinearColor(0, 0.5f, 1.f)
		);

		Collector.RegisterOneFrameMaterialProxy(WireframeMaterialInstance);

		FMaterialRenderProxy* MaterialProxy = NULL;
		if (bWireframe)
		{
			MaterialProxy = WireframeMaterialInstance;
		}
		else
		{
			MaterialProxy = Material->GetRenderProxy(IsSelected());
		}

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				FMatrix LocalToWorld = GetLocalToWorld();
				const FSceneView* View = Views[ViewIndex];
				// Draw the mesh.
				FMeshBatch& Mesh = Collector.AllocateMesh();
				FMeshBatchElement& BatchElement = Mesh.Elements[0];
				BatchElement.IndexBuffer = &IndexBuffer;
				Mesh.bWireframe = bWireframe;
				Mesh.VertexFactory = &VertexFactory;
				Mesh.MaterialRenderProxy = MaterialProxy;
				BatchElement.PrimitiveUniformBuffer = CreatePrimitiveUniformBufferImmediate(LocalToWorld, GetBounds(), GetLocalBounds(), true, UseEditorDepthTest());
				BatchElement.FirstIndex = 0;
				BatchElement.NumPrimitives = GetRequiredIndexCount() / 3;
				BatchElement.MinVertexIndex = 0;
				BatchElement.MaxVertexIndex = GetRequiredVertexCount();
				Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
				Mesh.bDisableBackfaceCulling = true;
				Mesh.Type = PT_TriangleList;
				Mesh.DepthPriorityGroup = SDPG_World;
				Mesh.bCanApplyViewModeOverrides = false;
				Collector.AddMesh(ViewIndex, Mesh);

				if (bWireframe && DynamicData)
				{
					FLinearColor SimulationLineColor(1.0f, 0.f, 0.f);
					FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);
					int32 NumLines = DynamicData->HorizontalLines.Num() - 1;
					for (int32 LineIdx = 0; LineIdx < NumLines; LineIdx++)
					{
						int32 NumPoints = DynamicData->HorizontalLines[LineIdx].Points.Num();
						for (int32 PointIdx = 0; PointIdx < NumPoints; ++PointIdx)
						{
							FVector PointA = LocalToWorld.TransformPosition(DynamicData->HorizontalLines[LineIdx].Points[PointIdx]);
							FVector PointB = LocalToWorld.TransformPosition(DynamicData->HorizontalLines[LineIdx + 1].Points[PointIdx]);
							PDI->DrawLine(PointA, PointB, SimulationLineColor, SDPG_Foreground, 0.3f);
						}						
					}
				}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
				// Render bounds
				RenderBounds(Collector.GetPDI(ViewIndex), ViewFamily.EngineShowFlags, GetBounds(), IsSelected());
#endif
			}
		}
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		FPrimitiveViewRelevance Result;
		Result.bDrawRelevance = IsShown(View);
		Result.bShadowRelevance = IsShadowCast(View);
		Result.bDynamicRelevance = true;
		MaterialRelevance.SetPrimitiveViewRelevance(Result);
		return Result;
	}

	virtual uint32 GetMemoryFootprint(void) const override { return(sizeof(*this) + GetAllocatedSize()); }

	uint32 GetAllocatedSize(void) const { return(FPrimitiveSceneProxy::GetAllocatedSize()); }

private:

	UMaterialInterface* Material;
	FVerletClothVertexBuffer VertexBuffer;
	FVerletClothIndexBuffer IndexBuffer;
	FVerletClothVertexFactory VertexFactory;

	FVerletClothDynamicData* DynamicData;

	FMaterialRelevance MaterialRelevance;

	int32 NumSegments;

	float ClothWidth;

	int32 NumSides;
};


//////////////////////////////////////////////////////////////////////////

UVerletClothComponent::UVerletClothComponent( const FObjectInitializer& ObjectInitializer )
	: Super( ObjectInitializer )
{
	PrimaryComponentTick.bCanEverTick = true;
	bTickInEditor = true;
	bAutoActivate = true;

	ClothLength = 100.f;
	ClothWidth = 100.f;
	Damping = 0.0f;
	NumSegments = 10;
	SolverIterations = 10;
	NumSides = 1;
	FixedLineCount = 1;
	Acceleration = FVector(0.0f, 0.0f, -980.0f);
	SideAxis = ESideAxis::X;
	CollisionPlane = ECollisionPlane::NONE;
	ProcessWorldSpace = true;

	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
}

void UVerletClothComponent::OnRegister()
{
	Super::OnRegister();

	FVector SideAxisVector;
	switch (SideAxis)
	{
	case ESideAxis::X: SideAxisVector = ProcessWorldSpace ? ComponentToWorld.TransformVector(FVector::ForwardVector) : FVector::ForwardVector; break;
	case ESideAxis::Y: SideAxisVector = ProcessWorldSpace ? ComponentToWorld.TransformVector(FVector::RightVector) : FVector::RightVector; break;
	default: SideAxisVector = ProcessWorldSpace ? ComponentToWorld.TransformVector(FVector::UpVector) : FVector::UpVector; break;
	}

	const int32 NumLines = NumSegments + 1;

	HorizontalLines.Reset();
	HorizontalLines.AddZeroed(NumLines);

	FixedLineCount = FMath::Min(FixedLineCount, NumLines);
	for (int32 LineIdx = 0; LineIdx < NumLines; LineIdx++)
	{
		if (LineIdx < FixedLineCount)
			HorizontalLines[LineIdx].bFree = false;
		else
			HorizontalLines[LineIdx].bFree = true;
	}

	FVector CompLocation = GetComponentLocation();
	const FVector StartPosition = ProcessWorldSpace ? CompLocation : FVector::ZeroVector;
	const FVector Delta = ProcessWorldSpace ? Acceleration.GetSafeNormal() * ClothLength :
		ComponentToWorld.InverseTransformVector(Acceleration.GetSafeNormal()) * ClothLength;

	for (int32 LineIdx = 0; LineIdx < NumLines; LineIdx++)
	{
		FVerletClothHorizontalLine& Line = HorizontalLines[LineIdx];
		Line.SetHorizontal(NumSides, ClothWidth);
		Line.Damping = Damping;
		const float Alpha = (float)LineIdx / (float)NumSegments;
		const FVector RelativePosition = Alpha * Delta;
		const FVector InitialPosition = StartPosition + RelativePosition;
		if(Line.bFree)
			Line.SetInitPosition(InitialPosition, FVector::ZeroVector, SideAxisVector);
		else
			Line.SetInitPosition(InitialPosition, RelativePosition, SideAxisVector);
	}

	OldComponentLocation = CompLocation;

	SetTickGroup(TG_PostUpdateWork);
}

void UVerletClothComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	check( GetWorld()->GetWorldSettings() );
	float TimeDilation = GetWorld()->GetWorldSettings()->GetEffectiveTimeDilation();
	// Fixed step simulation at 60hz
	float FixedTimeStep = FMath::Min( DeltaTime, (1.f / 60.f) * TimeDilation);
	float RemainingTime = DeltaTime;
	
	{
		SCOPE_CYCLE_COUNTER( STAT_UpdateVerletClothTime );
		while (RemainingTime >= FixedTimeStep)
		{
			VerletIntegrate( FixedTimeStep );
			SolveConstraints();
			ProcessCollision();
			RemainingTime -= FixedTimeStep;
		}
	}

	// Need to send new data to render thread
	MarkRenderDynamicDataDirty();

	// Call this because bounds have changed
	UpdateComponentToWorld();
}

void UVerletClothComponent::SendRenderDynamicData_Concurrent()
{
	if (SceneProxy)
	{
		// Allocate cloth dynamic data
		FVerletClothDynamicData* DynamicData = new FVerletClothDynamicData;

		// Transform current positions from lines into component-space array
		int32 NumLines = HorizontalLines.Num();
		DynamicData->HorizontalLines.AddZeroed(NumLines);
		for (int32 LineIdx = 0; LineIdx < NumLines; LineIdx++)
		{
			int32 NumPoints = HorizontalLines[LineIdx].Positions.Num();
			DynamicData->HorizontalLines[LineIdx].Points.AddZeroed(NumPoints);
			for (int32 PointIdx = 0; PointIdx < NumPoints; ++PointIdx)
			{
				DynamicData->HorizontalLines[LineIdx].Points[PointIdx] = ProcessWorldSpace ? ComponentToWorld.InverseTransformPosition(HorizontalLines[LineIdx].Positions[PointIdx])
					: HorizontalLines[LineIdx].Positions[PointIdx];
			}			
		}

		// Enqueue command to send to render thread
		ENQUEUE_UNIQUE_RENDER_COMMAND_TWOPARAMETER(
			FSendClothDynamicData,
			FVerletClothSceneProxy*, ClothSceneProxy, (FVerletClothSceneProxy*)SceneProxy,
			FVerletClothDynamicData*, DynamicData, DynamicData,
			{
				ClothSceneProxy->SetDynamicData_RenderThread(DynamicData);
			});
	}
}

FPrimitiveSceneProxy* UVerletClothComponent::CreateSceneProxy()
{
	return new FVerletClothSceneProxy(this);
}

FBoxSphereBounds UVerletClothComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	// Calculate bounding box of cloth points
	FBox ClothBox(0);
	for (int32 LineIdx = 0; LineIdx < HorizontalLines.Num(); LineIdx++)
	{
		const FVerletClothHorizontalLine& Line = HorizontalLines[LineIdx];
		for (int32 PointIdx = 0; PointIdx < Line.Positions.Num(); ++PointIdx)
			ClothBox += ProcessWorldSpace ? Line.Positions[PointIdx] : ComponentToWorld.TransformPosition(Line.Positions[PointIdx]);
	}

	return FBoxSphereBounds(ClothBox);
}

void UVerletClothComponent::ProcessCollision()
{
	if (CollisionPlane == ECollisionPlane::NONE)
		return;

	FVector Origin = ProcessWorldSpace ? ComponentToWorld.GetLocation() : FVector::ZeroVector;
	FQuat Rotation = ProcessWorldSpace ? ComponentToWorld.GetRotation() : FQuat::Identity;

	FPlane Plane;
	switch (CollisionPlane)
	{
	case ECollisionPlane::XY: Plane = FPlane( Origin, Rotation.GetAxisZ() ); break;
	case ECollisionPlane::YZ: Plane = FPlane( Origin, Rotation.GetAxisX() ); break;
	case ECollisionPlane::ZX: Plane = FPlane( Origin, Rotation.GetAxisY() ); break;
	}

	const int32 NumLines = NumSegments + 1;
	for (int32 LineIdx = 0; LineIdx < NumLines; LineIdx++)
	{
		FVerletClothHorizontalLine& Line = HorizontalLines[LineIdx];
		if (Line.bFree)
		{
			for (int32 PointIdx = 0; PointIdx < Line.Positions.Num(); ++PointIdx)
			{
				float Distance = Plane.PlaneDot( Line.Positions[PointIdx] );
				if (Distance < 0.0f)
					Line.Positions[PointIdx] += (FVector( Plane.X, Plane.Y, Plane.Z ) * (-Distance));
			}
		}
	}
}

void UVerletClothComponent::SolveConstraints()
{
	const float SegmentLength = ClothLength / (float)NumSegments;
	const float HorizontalLength = ClothWidth / (float)NumSides;
	const float DiagonalLength = FMath::Sqrt(SegmentLength*SegmentLength + HorizontalLength*HorizontalLength);

	// For each iteration..
	for (int32 IterationIdx = 0; IterationIdx < SolverIterations; IterationIdx++)
	{
		// For each segment..
		for (int32 SegIdx = 0; SegIdx < NumSegments; SegIdx++)
		{
			FVerletClothHorizontalLine& LineA = HorizontalLines[SegIdx];
			FVerletClothHorizontalLine& LineB = HorizontalLines[SegIdx + 1];
			LineA.SolveConstraints(LineB, SegmentLength, DiagonalLength);
		}

		//마지막 라인은 가로만 제약
		HorizontalLines[NumSegments].SolveHorizontalConstraint();
	}
}

void UVerletClothComponent::VerletIntegrate(float InTime)
{
	FVector SideAxisVector;
	switch (SideAxis)
	{
	case ESideAxis::X: SideAxisVector = ProcessWorldSpace ? ComponentToWorld.TransformVector(FVector::ForwardVector) : FVector::ForwardVector; break;
	case ESideAxis::Y: SideAxisVector = ProcessWorldSpace ? ComponentToWorld.TransformVector(FVector::RightVector) : FVector::RightVector; break;
	default: SideAxisVector = ProcessWorldSpace ? ComponentToWorld.TransformVector(FVector::UpVector) : FVector::UpVector; break;
	}

	FVector CompLocation = GetComponentLocation();
	FVector CenterLocation = ProcessWorldSpace ? CompLocation : ComponentToWorld.InverseTransformVector(CompLocation - OldComponentLocation);
	OldComponentLocation = CompLocation;

	const int32 NumLines = NumSegments + 1;
	const float TimeSqr = InTime * InTime;
	const FVector Gravity = ProcessWorldSpace ? Acceleration : ComponentToWorld.InverseTransformVector(Acceleration);

	for (int32 LineIdx = 0; LineIdx < NumLines; LineIdx++)
	{
		FVerletClothHorizontalLine& Line = HorizontalLines[LineIdx];
		if (Line.bFree)
			Line.VerletProcess(Gravity, TimeSqr);
		else
			Line.FixedProcess(CenterLocation, SideAxisVector);
	}
}