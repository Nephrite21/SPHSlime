#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialRenderProxy.h"

#include "ExternalForceKernel.generated.h"

struct SPHPREPROCESSING_API FExternalForceKernelDispatchParams
{
	int X;
	int Y;
	int Z;

	
	TArray<FVector> Positions; //Input
	TArray<FVector> Velocities; //Input
	int NumParticles;
	float gravity;

	TArray<FVector> PredictedPositions;

	
	

	FExternalForceKernelDispatchParams(int x, int y, int z)
		: X(x)
		, Y(y)
		, Z(z)
	{
	}
};

// This is a public interface that we define so outside code can invoke our compute shader.
class SPHPREPROCESSING_API FExternalForceKernelInterface {
public:
	// Executes this shader on the render thread
	static void DispatchRenderThread(
		FRHICommandListImmediate& RHICmdList,
		FExternalForceKernelDispatchParams Params,
		TFunction<void(const TArray<FVector>& PredictedPositions)> AsyncCallback
	);

	// Executes this shader on the render thread from the game thread via EnqueueRenderThreadCommand
	static void DispatchGameThread(
		FExternalForceKernelDispatchParams Params,
		TFunction<void(const TArray<FVector>& PredictedPositions)> AsyncCallback
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
		FExternalForceKernelDispatchParams Params,
		TFunction<void(const TArray<FVector>& PredictedPositions)> AsyncCallback
	)
	{
		if (IsInRenderingThread()) {
			DispatchRenderThread(GetImmediateCommandList_ForRenderCommand(), Params, AsyncCallback);
		}else{
			DispatchGameThread(Params, AsyncCallback);
		}
	}
};



DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnExternalForceKernelLibrary_AsyncExecutionCompleted, const TArray<FVector>&, PredictedPositions);


UCLASS() // Change the _API to match your project
class SPHPREPROCESSING_API UExternalForceKernelLibrary_AsyncExecution : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	
	// Execute the actual load
	virtual void Activate() override {
		// Create a dispatch parameters struct and fill it the input array with our args
		FExternalForceKernelDispatchParams Params(NumParticles, 1, 1);
		Params.Positions = Positions;
		Params.Velocities = Velocities;
		Params.gravity = gravity;
		Params.NumParticles = NumParticles;


		TFunction<void(const TArray<FVector>&)> Callback = [this](const TArray<FVector>& OutVectors) {
			this->Completed.Broadcast(OutVectors);
			};

		FExternalForceKernelInterface::Dispatch(Params, Callback);
	}
	
	
	
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", Category = "ComputeShader", WorldContext = "WorldContextObject"))
	static UExternalForceKernelLibrary_AsyncExecution* ExecuteBaseComputeShader(UObject* WorldContextObject, const TArray<FVector>& Positions, const TArray<FVector>& Velocities, int NumParticles, float gravity) {
		UExternalForceKernelLibrary_AsyncExecution* Action = NewObject<UExternalForceKernelLibrary_AsyncExecution>();
		Action->Positions = Positions;
		Action->Velocities = Velocities;
		Action->NumParticles = NumParticles;
		Action->gravity = gravity;
		Action->RegisterWithGameInstance(WorldContextObject);

		return Action;
	}

	UPROPERTY(BlueprintAssignable)
	FOnExternalForceKernelLibrary_AsyncExecutionCompleted Completed;

	
	TArray<FVector> Positions;
	TArray<FVector> Velocities;
	int NumParticles;
	float gravity;
};