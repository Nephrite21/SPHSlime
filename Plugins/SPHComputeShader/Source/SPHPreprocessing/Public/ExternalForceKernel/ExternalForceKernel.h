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

	
	TArray<FVector3f> Positions; //Input
	TArray<FVector3f> Velocities; //Input
	int NumParticles;
	float gravity;

	TArray<FVector3f> PredictedPositions;

	
	

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
		TFunction<void(const TArray<FVector3f>& PredictedPositions, const TArray<FVector3f>& Velocities)> AsyncCallback
	);

	// Executes this shader on the render thread from the game thread via EnqueueRenderThreadCommand
	static void DispatchGameThread(
		FExternalForceKernelDispatchParams Params,
		TFunction<void(const TArray<FVector3f>& PredictedPositions, const TArray<FVector3f>& Velocities)> AsyncCallback
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
		TFunction<void(const TArray<FVector3f>& PredictedPositions, const TArray<FVector3f>& Velocities)> AsyncCallback
	)
	{
		if (IsInRenderingThread()) {
			DispatchRenderThread(GetImmediateCommandList_ForRenderCommand(), Params, AsyncCallback);
		}else{
			DispatchGameThread(Params, AsyncCallback);
		}
	}
};



DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnExternalForceKernelLibrary_AsyncExecutionCompleted,
	const TArray<FVector3f>&, PredictedPositions,
	const TArray<FVector3f>&, Velocities);


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


		TFunction<void(const TArray<FVector3f>&, const TArray<FVector3f>&)> Callback = 
			[this](const TArray<FVector3f>& OutPredictedPositions, const TArray<FVector3f>& OutVelocities) {
			AsyncTask(ENamedThreads::GameThread, [this, OutPredictedPositions, OutVelocities]() {
				this->Completed.Broadcast(OutPredictedPositions, OutVelocities);
				});
			//this->Completed.Broadcast(OutVectors,OutVelocities);
			};

		FExternalForceKernelInterface::Dispatch(Params, Callback);
	}
	
	
	
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", Category = "ComputeShader", WorldContext = "WorldContextObject"))
	static UExternalForceKernelLibrary_AsyncExecution* ExecuteBaseComputeShader(UObject* WorldContextObject, 
		const TArray<FVector3f>& Positions, 
		const TArray<FVector3f>& Velocities, 
		int NumParticles, float gravity
	) {
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

	
	TArray<FVector3f> Positions;
	TArray<FVector3f> Velocities;
	int NumParticles;
	float gravity;
};