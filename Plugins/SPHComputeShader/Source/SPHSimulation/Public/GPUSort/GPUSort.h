#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialRenderProxy.h"

#include "GPUSort.generated.h"

struct SPHSIMULATION_API FGPUSortDispatchParams
{
	int X;
	int Y;
	int Z;

	
	int NumParticles;
	TArray<FIntVector> Entries; //Input And Output //Output 굳이 해야하는지?
	TArray<int> Offsets; //Input And Output

	
	

	FGPUSortDispatchParams(int x, int y, int z)
		: X(x)
		, Y(y)
		, Z(z)
	{
	}
};

// This is a public interface that we define so outside code can invoke our compute shader.
class SPHSIMULATION_API FGPUSortInterface {
public:
	// Executes this shader on the render thread
	static void DispatchRenderThread(
		FRHICommandListImmediate& RHICmdList,
		FGPUSortDispatchParams Params,
		TFunction<void(const TArray<FIntVector>& Entries, const TArray<int>& Offsets)> AsyncCallback
	);

	// Executes this shader on the render thread from the game thread via EnqueueRenderThreadCommand
	static void DispatchGameThread(
		FGPUSortDispatchParams Params,
		TFunction<void(const TArray<FIntVector>& Entries, const TArray<int>& Offsets)> AsyncCallback
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
		FGPUSortDispatchParams Params,
		TFunction<void(const TArray<FIntVector>& Entries,const TArray<int>& Offsets)> AsyncCallback
	)
	{
		if (IsInRenderingThread()) {
			DispatchRenderThread(GetImmediateCommandList_ForRenderCommand(), Params, AsyncCallback);
		}else{
			DispatchGameThread(Params, AsyncCallback);
		}
	}
};



DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGPUSortLibrary_AsyncExecutionCompleted,
	const TArray<FIntVector>&, Entries,
	const TArray<int>&, Offsets
);


UCLASS() // Change the _API to match your project
class SPHSIMULATION_API UGPUSortLibrary_AsyncExecution : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	
	// Execute the actual load
	virtual void Activate() override {
		// Create a dispatch parameters struct and fill it the input array with our args
		FGPUSortDispatchParams Params(NumParticles, 1, 1);

		Params.Entries = Entries;
		Params.NumParticles = NumParticles;
		Params.Offsets = Offsets;

		TFunction<void(const TArray<FIntVector>& ,const TArray<int>&)> Callback =
			[this](const TArray<FIntVector>& Entries ,const TArray<int>& Offsets) {
			AsyncTask(ENamedThreads::GameThread, [this, Entries, Offsets]() {
				this->Completed.Broadcast(Entries, Offsets);
				});
			//this->Completed.Broadcast(OutVectors,OutVelocities);
			};

		FGPUSortInterface::Dispatch(Params, Callback);
	}
	
	
	
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", Category = "ComputeShader", WorldContext = "WorldContextObject"))
	static UGPUSortLibrary_AsyncExecution* ExecuteBaseComputeShader(UObject* WorldContextObject, const TArray<int>& SpatialOffsets, const TArray<FIntVector>& SpatialIndicies, int NumParticles) {
		UGPUSortLibrary_AsyncExecution* Action = NewObject<UGPUSortLibrary_AsyncExecution>();
		Action->Entries = SpatialIndicies;
		Action->Offsets = SpatialOffsets;
		Action->NumParticles = NumParticles;
		Action->RegisterWithGameInstance(WorldContextObject);

		return Action;
	}

	UPROPERTY(BlueprintAssignable)
	FOnGPUSortLibrary_AsyncExecutionCompleted Completed;

	TArray<FIntVector> Entries;
	TArray<int> Offsets;
	
	int NumParticles;
	
};