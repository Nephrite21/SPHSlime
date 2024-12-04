#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialRenderProxy.h"

#include "SpatialHashKernel.generated.h"

struct SPHSIMULATION_API FSpatialHashKernelDispatchParams
{
	int X;
	int Y;
	int Z;

	TArray<FVector> PredictedPositions; //Input
	TArray<int> SpatialOffsets; //InputAndOutput
	TArray<FIntVector> SpatialIndices; //Output

	int NumParticles;
	float SmoothingRadius;


	

	FSpatialHashKernelDispatchParams(int x, int y, int z)
		: X(x)
		, Y(y)
		, Z(z)
	{
	}
};

// This is a public interface that we define so outside code can invoke our compute shader.
class SPHSIMULATION_API FSpatialHashKernelInterface {
public:
	// Executes this shader on the render thread
	static void DispatchRenderThread(
		FRHICommandListImmediate& RHICmdList,
		FSpatialHashKernelDispatchParams Params,
		TFunction<void(const TArray<int>& SpatialOffsets, const TArray<FIntVector>& SpatialIndices)> AsyncCallback
	);

	// Executes this shader on the render thread from the game thread via EnqueueRenderThreadCommand
	static void DispatchGameThread(
		FSpatialHashKernelDispatchParams Params,
		TFunction<void(const TArray<int>& SpatialOffsets, const TArray<FIntVector>& SpatialIndices)> AsyncCallback
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
		FSpatialHashKernelDispatchParams Params,
		TFunction<void(const TArray<int>& SpatialOffsets, const TArray<FIntVector>& SpatialIndices)> AsyncCallback
	)
	{
		if (IsInRenderingThread()) {
			DispatchRenderThread(GetImmediateCommandList_ForRenderCommand(), Params, AsyncCallback);
		}else{
			DispatchGameThread(Params, AsyncCallback);
		}
	}
};



DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSpatialHashKernelLibrary_AsyncExecutionCompleted,
	const TArray<int>&, SpatialOffests,
	const TArray<FIntVector>&, SpatialIndices
	);


UCLASS() // Change the _API to match your project
class SPHSIMULATION_API USpatialHashKernelLibrary_AsyncExecution : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	
	// Execute the actual load
	virtual void Activate() override {
		// Create a dispatch parameters struct and fill it the input array with our args
		FSpatialHashKernelDispatchParams Params(NumParticles, 1, 1);
		Params.NumParticles = NumParticles;
		Params.SmoothingRadius = SmoothingRadius;
		Params.PredictedPositions = PredictedPositions;
		Params.SpatialOffsets = SpatialOffsets;

		TFunction<void(const TArray<int>&, const TArray<FIntVector>&)> Callback =
			[this](const TArray<int>& SpatialOffsets, const TArray<FIntVector>& SpatialIndices) {
			AsyncTask(ENamedThreads::GameThread, [this, SpatialOffsets, SpatialIndices]() {
				this->Completed.Broadcast(SpatialOffsets, SpatialIndices);
				});
			//this->Completed.Broadcast(OutVectors,OutVelocities);
			};

		FSpatialHashKernelInterface::Dispatch(Params, Callback);
	}
	
	
	
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", Category = "ComputeShader", WorldContext = "WorldContextObject"))
	static USpatialHashKernelLibrary_AsyncExecution* ExecuteBaseComputeShader(UObject* WorldContextObject, const TArray<FVector>& PredictedPositions, const TArray<int>& SpatialOffsets, int NumParticles, float SmoothingRadius) {
		USpatialHashKernelLibrary_AsyncExecution* Action = NewObject<USpatialHashKernelLibrary_AsyncExecution>();
		Action->PredictedPositions = PredictedPositions;
		Action->SpatialOffsets = SpatialOffsets;
		Action->NumParticles = NumParticles;
		Action->SmoothingRadius = SmoothingRadius;
		Action->RegisterWithGameInstance(WorldContextObject);

		return Action;
	}

	UPROPERTY(BlueprintAssignable)
	FOnSpatialHashKernelLibrary_AsyncExecutionCompleted Completed;

	TArray<FVector> PredictedPositions;
	TArray<int> SpatialOffsets;

	int NumParticles;
	float SmoothingRadius;

};