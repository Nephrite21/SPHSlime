#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialRenderProxy.h"

#include "InitializeParticle.generated.h"

struct SPHPREPROCESSING_API FInitializeParticleDispatchParams
{
	int X;
	int Y;
	int Z;

	
	TArray<FVector3f> InputVectors;
	int NumParticles;
	float SpawnLength;
	float boundsize;

	TArray<FVector3f> OutputVectors;
	
	

	FInitializeParticleDispatchParams(int x, int y, int z)
		: X(x)
		, Y(y)
		, Z(z)
	{
	}
};

// This is a public interface that we define so outside code can invoke our compute shader.
class SPHPREPROCESSING_API FInitializeParticleInterface {
public:
	// Executes this shader on the render thread
	static void DispatchRenderThread(
		FRHICommandListImmediate& RHICmdList,
		FInitializeParticleDispatchParams Params,
		TFunction<void(const TArray<FVector3f>& OutputVectors)> AsyncCallback
	);

	// Executes this shader on the render thread from the game thread via EnqueueRenderThreadCommand
	static void DispatchGameThread(
		FInitializeParticleDispatchParams Params,
		TFunction<void(const TArray<FVector3f>& OutputVectors)> AsyncCallback
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
		FInitializeParticleDispatchParams Params,
		TFunction<void(const TArray<FVector3f>& OutputVectors)> AsyncCallback
	)
	{
		if (IsInRenderingThread()) {
			DispatchRenderThread(GetImmediateCommandList_ForRenderCommand(), Params, AsyncCallback);
		}else{
			DispatchGameThread(Params, AsyncCallback);
		}
	}
};



DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInitializeParticleLibrary_AsyncExecutionCompleted, const TArray<FVector3f>&, Value);


UCLASS() // Change the _API to match your project
class SPHPREPROCESSING_API UInitializeParticleLibrary_AsyncExecution : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	
	// Execute the actual load
	virtual void Activate() override {
		// Create a dispatch parameters struct and fill it the input array with our args
		UE_LOG(LogTemp, Log, TEXT("InputVectors Size: %d"), InputVectors.Num());
		if (InputVectors.Num() == 0) {
			UE_LOG(LogTemp, Warning, TEXT("InputVectors is empty"));
			return;
		}

		FInitializeParticleDispatchParams Params(NumParticles, 1, 1);
		Params.InputVectors = InputVectors;
		Params.NumParticles = NumParticles;
		Params.boundsize = boundsize;
		UE_LOG(LogTemp, Log, TEXT("NumParticles : %d"), NumParticles);
		Params.SpawnLength = SpawnLength;

		TFunction<void(const TArray<FVector3f>&)> Callback = [this](const TArray<FVector3f>& OutVectors) {
			this->Completed.Broadcast(OutVectors);
			};

		// Dispatch the compute shader and wait until it completes
		FInitializeParticleInterface::Dispatch(Params, Callback);
	}
	
	
	
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", Category = "ComputeShader", WorldContext = "WorldContextObject"))
	static UInitializeParticleLibrary_AsyncExecution* ExecuteBaseComputeShader(UObject* WorldContextObject, const TArray<FVector3f>& InputVectors, int NumParticles, float spawnLength, float boundsize) {
		UInitializeParticleLibrary_AsyncExecution* Action = NewObject<UInitializeParticleLibrary_AsyncExecution>();
		Action->InputVectors = InputVectors;
		Action->NumParticles = NumParticles;
		Action->SpawnLength = spawnLength;
		Action->boundsize = boundsize;
		Action->RegisterWithGameInstance(WorldContextObject);

		return Action;
	}

	UPROPERTY(BlueprintAssignable)
	FOnInitializeParticleLibrary_AsyncExecutionCompleted Completed;

	TArray<FVector3f> InputVectors;
	int NumParticles;
	float SpawnLength;
	float boundsize;
	
};