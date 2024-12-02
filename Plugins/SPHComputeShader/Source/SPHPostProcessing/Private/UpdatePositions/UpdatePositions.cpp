#include "UpdatePositions.h"
#include "SPHPostProcessing/Public/UpdatePositions/UpdatePositions.h"
#include "PixelShaderUtils.h"
#include "MeshPassProcessor.inl"
#include "StaticMeshResources.h"
#include "DynamicMeshBuilder.h"
#include "RenderGraphResources.h"
#include "GlobalShader.h"
#include "UnifiedBuffer.h"
#include "CanvasTypes.h"
#include "MeshDrawShaderBindings.h"
#include "RHIGPUReadback.h"
#include "MeshPassUtils.h"
#include "MaterialShader.h"

DECLARE_STATS_GROUP(TEXT("UpdatePositions"), STATGROUP_UpdatePositions, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("UpdatePositions Execute"), STAT_UpdatePositions_Execute, STATGROUP_UpdatePositions);

// This class carries our parameter declarations and acts as the bridge between cpp and HLSL.
class SPHPOSTPROCESSING_API FUpdatePositions: public FGlobalShader
{
public:
	
	DECLARE_GLOBAL_SHADER(FUpdatePositions);
	SHADER_USE_PARAMETER_STRUCT(FUpdatePositions, FGlobalShader);
	
	
	class FUpdatePositions_Perm_TEST : SHADER_PERMUTATION_INT("TEST", 1);
	using FPermutationDomain = TShaderPermutationDomain<
		FUpdatePositions_Perm_TEST
	>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		/*
		* Here's where you define one or more of the input parameters for your shader.
		* Some examples:
		*/
		// SHADER_PARAMETER(uint32, MyUint32) // On the shader side: uint32 MyUint32;
		// SHADER_PARAMETER(FVector3f, MyVector) // On the shader side: float3 MyVector;

		// SHADER_PARAMETER_TEXTURE(Texture2D, MyTexture) // On the shader side: Texture2D<float4> MyTexture; (float4 should be whatever you expect each pixel in the texture to be, in this case float4(R,G,B,A) for 4 channels)
		// SHADER_PARAMETER_SAMPLER(SamplerState, MyTextureSampler) // On the shader side: SamplerState MySampler; // CPP side: TStaticSamplerState<ESamplerFilter::SF_Bilinear>::GetRHI();

		// SHADER_PARAMETER_ARRAY(float, MyFloatArray, [3]) // On the shader side: float MyFloatArray[3];

		// SHADER_PARAMETER_UAV(RWTexture2D<FVector4f>, MyTextureUAV) // On the shader side: RWTexture2D<float4> MyTextureUAV;
		// SHADER_PARAMETER_UAV(RWStructuredBuffer<FMyCustomStruct>, MyCustomStructs) // On the shader side: RWStructuredBuffer<FMyCustomStruct> MyCustomStructs;
		// SHADER_PARAMETER_UAV(RWBuffer<FMyCustomStruct>, MyCustomStructs) // On the shader side: RWBuffer<FMyCustomStruct> MyCustomStructs;

		// SHADER_PARAMETER_SRV(StructuredBuffer<FMyCustomStruct>, MyCustomStructs) // On the shader side: StructuredBuffer<FMyCustomStruct> MyCustomStructs;
		// SHADER_PARAMETER_SRV(Buffer<FMyCustomStruct>, MyCustomStructs) // On the shader side: Buffer<FMyCustomStruct> MyCustomStructs;
		// SHADER_PARAMETER_SRV(Texture2D<FVector4f>, MyReadOnlyTexture) // On the shader side: Texture2D<float4> MyReadOnlyTexture;

		// SHADER_PARAMETER_STRUCT_REF(FMyCustomStruct, MyCustomStruct)

		SHADER_PARAMETER(int, NumParticles)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FVector>, Positions)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FVector>, Velocities)
		

	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		const FPermutationDomain PermutationVector(Parameters.PermutationId);
		
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		const FPermutationDomain PermutationVector(Parameters.PermutationId);

		/*
		* Here you define constants that can be used statically in the shader code.
		* Example:
		*/
		// OutEnvironment.SetDefine(TEXT("MY_CUSTOM_CONST"), TEXT("1"));

		/*
		* These defines are used in the thread count section of our shader
		*/
		OutEnvironment.SetDefine(TEXT("THREADS_X"), NUM_THREADS_UpdatePositions_X);
		OutEnvironment.SetDefine(TEXT("THREADS_Y"), NUM_THREADS_UpdatePositions_Y);
		OutEnvironment.SetDefine(TEXT("THREADS_Z"), NUM_THREADS_UpdatePositions_Z);

		// This shader must support typed UAV load and we are testing if it is supported at runtime using RHIIsTypedUAVLoadSupported
		//OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);

		// FForwardLightingParameters::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
	}
private:
};

// This will tell the engine to create the shader and where the shader entry point is.
//                            ShaderType                            ShaderPath                     Shader function name    Type
IMPLEMENT_GLOBAL_SHADER(FUpdatePositions, "/SPHPostProcessingShaders/UpdatePositions/UpdatePositions.usf", "UpdatePositions", SF_Compute);

void FUpdatePositionsInterface::DispatchRenderThread(FRHICommandListImmediate& RHICmdList, FUpdatePositionsDispatchParams Params,
	TFunction<void(const TArray<FVector>& Positions, const TArray<FVector>& Velocities)> AsyncCallback) {
	FRDGBuilder GraphBuilder(RHICmdList);

	{
		SCOPE_CYCLE_COUNTER(STAT_UpdatePositions_Execute);
		DECLARE_GPU_STAT(UpdatePositions)
		RDG_EVENT_SCOPE(GraphBuilder, "UpdatePositions");
		RDG_GPU_STAT_SCOPE(GraphBuilder, UpdatePositions);
		
		typename FUpdatePositions::FPermutationDomain PermutationVector;
		
		// Add any static permutation options here
		// PermutationVector.Set<FUpdatePositions::FMyPermutationName>(12345);

		TShaderMapRef<FUpdatePositions> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);
		

		bool bIsShaderValid = ComputeShader.IsValid();

		if (bIsShaderValid) {
			FUpdatePositions::FParameters* PassParameters = GraphBuilder.AllocParameters<FUpdatePositions::FParameters>();

			
			//Positions 버퍼 설정
			const void* RawData = Params.Positions.GetData();
			int NumVectors = Params.Positions.Num();
			int VectorSize = sizeof(FVector);
			FRDGBufferRef PositionsBuffer = CreateStructuredBuffer(
				GraphBuilder,
				TEXT("PositionsVectorBuffer"),
				sizeof(FVector),
				NumVectors,
				RawData,
				VectorSize * NumVectors
			);
			PassParameters->Positions = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(PositionsBuffer));


			//Velocities 버퍼 설정
			const void* RawData1 = Params.Velocities.GetData();
			FRDGBufferRef VelocitiesBuffer = CreateStructuredBuffer(
				GraphBuilder,
				TEXT("VelocitiesVectorBuffer"),
				sizeof(FVector),
				NumVectors,
				RawData1,
				VectorSize * NumVectors
			);
			PassParameters->Velocities = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(VelocitiesBuffer));

			PassParameters->NumParticles = Params.NumParticles;
			

			auto GroupCount = FComputeShaderUtils::GetGroupCount(
				FIntVector(Params.X, Params.Y, Params.Z),
				FComputeShaderUtils::kGolden2DGroupSize
			);

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("ExecuteUpdatePositions"),
				PassParameters,
				ERDGPassFlags::AsyncCompute,
				[&PassParameters, ComputeShader, GroupCount](FRHIComputeCommandList& RHICmdList)
			{
				FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *PassParameters, GroupCount);
			});

			
			//FRHIGPUBufferReadback* GPUBufferReadback = new FRHIGPUBufferReadback(TEXT("ExecuteUpdatePositionsOutput"));
			FRHIGPUBufferReadback* VelocitiesReadback = new FRHIGPUBufferReadback(TEXT("VelocitiesOutput"));
			FRHIGPUBufferReadback* PositionsReadback = new FRHIGPUBufferReadback(TEXT("PositionsOutput"));
			AddEnqueueCopyPass(GraphBuilder, PositionsReadback, PositionsBuffer, 0u);
			AddEnqueueCopyPass(GraphBuilder, VelocitiesReadback, VelocitiesBuffer, 0u);


			auto RunnerFunc = [PositionsReadback, VelocitiesReadback, AsyncCallback, NumVectors](auto&& RunnerFunc) -> void {
				if (PositionsReadback->IsReady() && VelocitiesReadback->IsReady()) {
					
					FVector* Buffer = (FVector*)PositionsReadback->Lock(NumVectors * sizeof(FVector));

					TArray<FVector> Positions;
					Positions.SetNum(NumVectors);
					FMemory::Memcpy(Positions.GetData(), Buffer, NumVectors * sizeof(FVector));
					
					PositionsReadback->Unlock();


					FVector* VelocitiesBuffer = (FVector*)VelocitiesReadback->Lock(NumVectors * sizeof(FVector));
					TArray<FVector> Velocities;
					Velocities.SetNum(NumVectors);
					FMemory::Memcpy(Velocities.GetData(), VelocitiesBuffer, NumVectors * sizeof(FVector));
					VelocitiesReadback->Unlock();


					AsyncTask(ENamedThreads::GameThread, [AsyncCallback, Positions, Velocities]() {
						AsyncCallback(Positions, Velocities);
					});

					delete VelocitiesReadback;
					delete PositionsReadback;
				} else {
					AsyncTask(ENamedThreads::ActualRenderingThread, [RunnerFunc]() {
						RunnerFunc(RunnerFunc);
					});
				}
			};

			AsyncTask(ENamedThreads::ActualRenderingThread, [RunnerFunc]() {
				RunnerFunc(RunnerFunc);
			});
			
		} else {
			#if WITH_EDITOR
				GEngine->AddOnScreenDebugMessage((uint64)42145125184, 6.f, FColor::Red, FString(TEXT("The compute shader has a problem.")));
			#endif

			// We exit here as we don't want to crash the game if the shader is not found or has an error.
			
		}
	}

	GraphBuilder.Execute();
}