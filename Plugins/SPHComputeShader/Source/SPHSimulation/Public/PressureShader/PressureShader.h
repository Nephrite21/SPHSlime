#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialRenderProxy.h"

#include "PressureShader.generated.h"

struct SPHSIMULATION_API FPressureShaderDispatchParams
{
	int X;
	int Y;
	int Z;

	int NumParticles;
	float SmoothingRadius;

	float TargetDensity;
	float PressureMultiplier;
	float NearPressureMultiplier;

	TArray<FVector2f> Densities;
	TArray<FVector3f> PredictedPositions;
	TArray<int> SpatialOffsets;
	TArray<FIntVector> SpatialIndices;
	TArray<FVector3f> Velocities; //Input and Output
	
	

	FPressureShaderDispatchParams(int x, int y, int z)
		: X(x)
		, Y(y)
		, Z(z)
	{
	}
};

// This is a public interface that we define so outside code can invoke our compute shader.
class SPHSIMULATION_API FPressureShaderInterface {
public:
	// Executes this shader on the render thread
	static void DispatchRenderThread(
		FRHICommandListImmediate& RHICmdList,
		FPressureShaderDispatchParams Params,
		TFunction<void(const TArray<FVector3f>& Velocities)> AsyncCallback
	);

	// Executes this shader on the render thread from the game thread via EnqueueRenderThreadCommand
	static void DispatchGameThread(
		FPressureShaderDispatchParams Params,
		TFunction<void(const TArray<FVector3f>& Velocities)> AsyncCallback
	)
	{
		ENQUEUE_RENDER_COMMAND(SceneDrawCompletion)(
		[Params, AsyncCallback](FRHICommandListImmediate& RHICmdList)
		{
			DispatchRenderThread(RHICmdList, Params, AsyncCallback);
		});
	}

	// Dispatches this shader. Can be called from any thread
	static void Dispatch(
		FPressureShaderDispatchParams Params,
		TFunction<void(const TArray<FVector3f>& Velocities)> AsyncCallback
	)
	{
		if (IsInRenderingThread()) {
			DispatchRenderThread(GetImmediateCommandList_ForRenderCommand(), Params, AsyncCallback);
		}else{
			DispatchGameThread(Params, AsyncCallback);
		}
	}
};



DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPressureShaderLibrary_AsyncExecutionCompleted, 
	const TArray<FVector3f>&, Velocities);


UCLASS() // Change the _API to match your project
class SPHSIMULATION_API UPressureShaderLibrary_AsyncExecution : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	
	// Execute the actual load
	virtual void Activate() override {
		// Create a dispatch parameters struct and fill it the input array with our args
		FPressureShaderDispatchParams Params(NumParticles, 1, 1);
		Params.Densities = Densities;
		Params.PredictedPositions = PredictedPositions;
		Params.SpatialOffsets = SpatialOffsets;
		Params.SpatialIndices = SpatialIndices;
		Params.Velocities = Velocities;
		Params.NumParticles = NumParticles;
		Params.SmoothingRadius = SmoothingRadius;
		Params.TargetDensity = TargetDensity;
		Params.PressureMultiplier = PressureMultiplier;
		Params.NearPressureMultiplier = NearPressureMultiplier;

		// Dispatch the compute shader and wait until it completes
		FPressureShaderInterface::Dispatch(Params, [this](const TArray<FVector3f>& Velocities) {
			this->Completed.Broadcast(Velocities);
		});
	}
	
	
	
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", Category = "ComputeShader", WorldContext = "WorldContextObject"))
	static UPressureShaderLibrary_AsyncExecution* ExecuteBaseComputeShader(UObject* WorldContextObject,
		const TArray<FVector2f>& Densities,
		const TArray<FVector3f>& PredictedPositions,
		const TArray<int>& SpatialOffsets,
		const TArray<FIntVector>& SpatialIndices,
		const TArray<FVector3f>& Velocities,
		int NumParticles,
		float SmoothingRadius,
		float TargetDensity,
		float PressureMultiplier,
		float NearPressureMultiplier

	) {
		UPressureShaderLibrary_AsyncExecution* Action = NewObject<UPressureShaderLibrary_AsyncExecution>();
		Action->Densities = Densities;
		Action->PredictedPositions = PredictedPositions;
		Action->SpatialOffsets = SpatialOffsets;
		Action->SpatialIndices = SpatialIndices;
		Action->Velocities = Velocities;
		Action->NumParticles = NumParticles;
		Action->SmoothingRadius = SmoothingRadius;
		Action->TargetDensity = TargetDensity;
		Action->PressureMultiplier = PressureMultiplier;
		Action->NearPressureMultiplier = NearPressureMultiplier;
		Action->RegisterWithGameInstance(WorldContextObject);

		return Action;
	}

	UPROPERTY(BlueprintAssignable)
	FOnPressureShaderLibrary_AsyncExecutionCompleted Completed;

	

	int NumParticles;
	float SmoothingRadius;
	float TargetDensity;
	float PressureMultiplier;
	float NearPressureMultiplier;

	TArray<FVector2f> Densities;
	TArray<FVector3f> PredictedPositions;
	TArray<int> SpatialOffsets;
	TArray<FIntVector> SpatialIndices;
	TArray<FVector3f> Velocities; //Input and Output
};