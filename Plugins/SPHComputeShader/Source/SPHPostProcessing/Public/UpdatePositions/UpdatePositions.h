#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialRenderProxy.h"

#include "UpdatePositions.generated.h"

struct SPHPOSTPROCESSING_API FUpdatePositionsDispatchParams
{
	int X;
	int Y;
	int Z;

	
	TArray<FVector> Positions; //InputAndOutput
	TArray<FVector> Velocities; //InputAndOutput
	int NumParticles;

	FUpdatePositionsDispatchParams(int x, int y, int z)
		: X(x)
		, Y(y)
		, Z(z)
	{
	}
};

// This is a public interface that we define so outside code can invoke our compute shader.
class SPHPOSTPROCESSING_API FUpdatePositionsInterface {
public:
	// Executes this shader on the render thread
	static void DispatchRenderThread(
		FRHICommandListImmediate& RHICmdList,
		FUpdatePositionsDispatchParams Params,
		TFunction<void(const TArray<FVector>& Positions, const TArray<FVector>& Velocities)> AsyncCallback
	);

	// Executes this shader on the render thread from the game thread via EnqueueRenderThreadCommand
	static void DispatchGameThread(
		FUpdatePositionsDispatchParams Params,
		TFunction<void(const TArray<FVector>& Positions, const TArray<FVector>& Velocities)> AsyncCallback
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
		FUpdatePositionsDispatchParams Params,
		TFunction<void(const TArray<FVector>& Positions, const TArray<FVector>& Velocities)> AsyncCallback
	)
	{
		if (IsInRenderingThread()) {
			DispatchRenderThread(GetImmediateCommandList_ForRenderCommand(), Params, AsyncCallback);
		}else{
			DispatchGameThread(Params, AsyncCallback);
		}
	}
};



DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnUpdatePositionsLibrary_AsyncExecutionCompleted,
	const TArray<FVector>&, Positions,
	const TArray<FVector>&, Velocities);


UCLASS() // Change the _API to match your project
class SPHPOSTPROCESSING_API UUpdatePositionsLibrary_AsyncExecution : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	
	// Execute the actual load
	virtual void Activate() override {
		// Create a dispatch parameters struct and fill it the input array with our args
		FUpdatePositionsDispatchParams Params(NumParticles, 1, 1);
		Params.Positions = Positions;
		Params.Velocities = Velocities;

		TFunction<void(const TArray<FVector>&, const TArray<FVector>&)> Callback =
			[this](const TArray<FVector>& OutPositions, const TArray<FVector>& OutVelocities) {
			AsyncTask(ENamedThreads::GameThread, [this, OutPositions, OutVelocities]() {
				this->Completed.Broadcast(OutPositions, OutVelocities);
				});
			//this->Completed.Broadcast(OutVectors,OutVelocities);
			};

		FUpdatePositionsInterface::Dispatch(Params, Callback);
	}
	
	
	
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", Category = "ComputeShader", WorldContext = "WorldContextObject"))
	static UUpdatePositionsLibrary_AsyncExecution* ExecuteBaseComputeShader(UObject* WorldContextObject, const TArray<FVector>& Positions, const TArray<FVector>& Velocities, int NumParticles) {
		UUpdatePositionsLibrary_AsyncExecution* Action = NewObject<UUpdatePositionsLibrary_AsyncExecution>();
		Action->Positions = Positions;
		Action->Velocities = Velocities;
		Action->RegisterWithGameInstance(WorldContextObject);

		return Action;
	}

	UPROPERTY(BlueprintAssignable)
	FOnUpdatePositionsLibrary_AsyncExecutionCompleted Completed;

	TArray<FVector> Positions;
	TArray<FVector> Velocities;
	int NumParticles;
};