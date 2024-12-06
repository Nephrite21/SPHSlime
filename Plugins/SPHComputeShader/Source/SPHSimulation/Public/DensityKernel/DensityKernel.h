#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialRenderProxy.h"

#include "DensityKernel.generated.h"

struct SPHSIMULATION_API FDensityKernelDispatchParams
{
	int X;
	int Y;
	int Z;

	int NumParticles;
	float SmoothingRadius;

	TArray<FVector3f> PredictedPositions; //Input
	TArray<FIntVector> SpatialIndices; //Input
	TArray<int> SpatialOffsets; //Input

	TArray<FVector2f> Densities; //Output
	
	

	FDensityKernelDispatchParams(int x, int y, int z)
		: X(x)
		, Y(y)
		, Z(z)
	{
	}
};

// This is a public interface that we define so outside code can invoke our compute shader.
class SPHSIMULATION_API FDensityKernelInterface {
public:
	// Executes this shader on the render thread
	static void DispatchRenderThread(
		FRHICommandListImmediate& RHICmdList,
		FDensityKernelDispatchParams Params,
		TFunction<void(const TArray<FVector2f>& Densities)> AsyncCallback
	);

	// Executes this shader on the render thread from the game thread via EnqueueRenderThreadCommand
	static void DispatchGameThread(
		FDensityKernelDispatchParams Params,
		TFunction<void(const TArray<FVector2f>& Densities)> AsyncCallback
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
		FDensityKernelDispatchParams Params,
		TFunction<void(const TArray<FVector2f>& Densities)> AsyncCallback
	)
	{
		if (IsInRenderingThread()) {
			DispatchRenderThread(GetImmediateCommandList_ForRenderCommand(), Params, AsyncCallback);
		}else{
			DispatchGameThread(Params, AsyncCallback);
		}
	}
};



DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDensityKernelLibrary_AsyncExecutionCompleted, 
	const TArray<FVector2f>&, Densities);


UCLASS() // Change the _API to match your project
class SPHSIMULATION_API UDensityKernelLibrary_AsyncExecution : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	
	// Execute the actual load
	virtual void Activate() override {
		// Create a dispatch parameters struct and fill it the input array with our args
		FDensityKernelDispatchParams Params(NumParticles, 1, 1);
		Params.PredictedPositions = PredictedPositions;
		Params.SpatialIndices = SpatialIndices;
		Params.SpatialOffsets = SpatialOffsets;
		Params.NumParticles = NumParticles;
		Params.SmoothingRadius = SmoothingRadius;

		TFunction<void(const TArray<FVector2f>&)> Callback =
			[this](const TArray<FVector2f>& Densities) {
			AsyncTask(ENamedThreads::GameThread, [this, Densities]() {
				this->Completed.Broadcast(Densities);
				});
			//this->Completed.Broadcast(OutVectors,OutVelocities);
			};

		FDensityKernelInterface::Dispatch(Params, Callback);
	}
	
	
	
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", Category = "ComputeShader", WorldContext = "WorldContextObject"))
	static UDensityKernelLibrary_AsyncExecution* ExecuteBaseComputeShader(UObject* WorldContextObject,
		const TArray<FVector3f>& PredictedPositions,
		const TArray<FIntVector>& SpatialIndices,
		const TArray<int>& SpatialOffsets,
		int NumParticles,
		float SmoothingRadius
	) {
		UDensityKernelLibrary_AsyncExecution* Action = NewObject<UDensityKernelLibrary_AsyncExecution>();
		Action->PredictedPositions = PredictedPositions;
		Action->SpatialIndices = SpatialIndices;
		Action->SpatialOffsets = SpatialOffsets;
		Action->NumParticles = NumParticles;
		Action->SmoothingRadius = SmoothingRadius;

		Action->RegisterWithGameInstance(WorldContextObject);

		return Action;
	}

	UPROPERTY(BlueprintAssignable)
	FOnDensityKernelLibrary_AsyncExecutionCompleted Completed;

	
	TArray<FVector3f> PredictedPositions;
	TArray<FIntVector> SpatialIndices;
	TArray<int> SpatialOffsets;
	int NumParticles;
	float SmoothingRadius;
	
};