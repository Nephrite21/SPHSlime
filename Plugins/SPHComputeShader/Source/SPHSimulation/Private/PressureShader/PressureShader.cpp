#include "PressureShader.h"
#include "SPHSimulation/Public/PressureShader/PressureShader.h"
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

DECLARE_STATS_GROUP(TEXT("PressureShader"), STATGROUP_PressureShader, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("PressureShader Execute"), STAT_PressureShader_Execute, STATGROUP_PressureShader);

// This class carries our parameter declarations and acts as the bridge between cpp and HLSL.
class SPHSIMULATION_API FPressureShader: public FGlobalShader
{
public:
	
	DECLARE_GLOBAL_SHADER(FPressureShader);
	SHADER_USE_PARAMETER_STRUCT(FPressureShader, FGlobalShader);
	
	
	class FPressureShader_Perm_TEST : SHADER_PERMUTATION_INT("TEST", 1);
	using FPermutationDomain = TShaderPermutationDomain<
		FPressureShader_Perm_TEST
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
		SHADER_PARAMETER(float, SmoothingRadius)
		SHADER_PARAMETER(float,TargetDensity)
		SHADER_PARAMETER(float,PressureMultiplier)
		SHADER_PARAMETER(float,NearPressureMultiplier)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector2f>, Densities)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector3f>, PredictedPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<int>, SpatialOffsets)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FIntVector>, SpatialIndices)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FVector3f>, Velocities)

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
		OutEnvironment.SetDefine(TEXT("THREADS_X"), NUM_THREADS_PressureShader_X);
		OutEnvironment.SetDefine(TEXT("THREADS_Y"), NUM_THREADS_PressureShader_Y);
		OutEnvironment.SetDefine(TEXT("THREADS_Z"), NUM_THREADS_PressureShader_Z);

		// This shader must support typed UAV load and we are testing if it is supported at runtime using RHIIsTypedUAVLoadSupported
		//OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);

		// FForwardLightingParameters::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
	}
private:
};

// This will tell the engine to create the shader and where the shader entry point is.
//                            ShaderType                            ShaderPath                     Shader function name    Type
IMPLEMENT_GLOBAL_SHADER(FPressureShader, "/SPHSimulationShaders/PressureShader/PressureShader.usf", "PressureShader", SF_Compute);

void FPressureShaderInterface::DispatchRenderThread(FRHICommandListImmediate& RHICmdList, FPressureShaderDispatchParams Params, 
	TFunction<void(const TArray<FVector3f>& Velocities)> AsyncCallback) {
	FRDGBuilder GraphBuilder(RHICmdList);

	{
		SCOPE_CYCLE_COUNTER(STAT_PressureShader_Execute);
		DECLARE_GPU_STAT(PressureShader)
		RDG_EVENT_SCOPE(GraphBuilder, "PressureShader");
		RDG_GPU_STAT_SCOPE(GraphBuilder, PressureShader);
		
		typename FPressureShader::FPermutationDomain PermutationVector;
		
		// Add any static permutation options here
		// PermutationVector.Set<FPressureShader::FMyPermutationName>(12345);

		TShaderMapRef<FPressureShader> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);
		

		bool bIsShaderValid = ComputeShader.IsValid();

		if (bIsShaderValid) {
			FPressureShader::FParameters* PassParameters = GraphBuilder.AllocParameters<FPressureShader::FParameters>();

			
			/////Densities buffer desc
			const void* DensitiesRawData = (void*)Params.Densities.GetData();
			int NumVectors = Params.Densities.Num();
			int DensitiesVectorSize = sizeof(FVector2f);
			FRDGBufferRef DensitiesBuffer = CreateStructuredBuffer(
				GraphBuilder,
				TEXT("DensitiesBuffer"),
				sizeof(FVector2f),
				NumVectors,
				DensitiesRawData,
				DensitiesVectorSize * NumVectors
			);

			///// PredictedPositions buffer desc
			const void* PredictedPositionsRawData = (void*)Params.PredictedPositions.GetData();
			int PredictedPositionsVectorSize = sizeof(FVector3f);
			FRDGBufferRef PredictedPositionsBuffer = CreateStructuredBuffer(
				GraphBuilder,
				TEXT("PredictedPositionsBuffer"),
				sizeof(FVector3f),
				NumVectors,
				PredictedPositionsRawData,
				PredictedPositionsVectorSize * NumVectors
			);

			/////SpatialOffsets buffer desc
			const void* SpatialOffsetsRawData = (void*)Params.SpatialOffsets.GetData();
			int SpatialOffsetsVectorSize = sizeof(int);
			FRDGBufferRef SpatialOffsetsBuffer = CreateStructuredBuffer(
				GraphBuilder,
				TEXT("SpatialOffsetsBuffer"),
				sizeof(int),
				NumVectors,
				SpatialOffsetsRawData,
				SpatialOffsetsVectorSize * NumVectors
			);

			/////SpatialIndices buffer desc
			const void* SpatialIndicesRawData = (void*)Params.SpatialIndices.GetData();
			int SpatialIndicesVectorSize = sizeof(FIntVector);
			FRDGBufferRef SpatialIndicesBuffer = CreateStructuredBuffer(
				GraphBuilder,
				TEXT("SpatialIndicesBuffer"),
				sizeof(FIntVector),
				NumVectors,
				SpatialIndicesRawData,
				SpatialIndicesVectorSize * NumVectors
			);

			/////Velocities buffer desc
			const void* VelocitiesRawData = (void*)Params.Velocities.GetData();
			int VelocitiesVectorSize = sizeof(FVector3f);
			FRDGBufferRef VelocitiesBuffer = CreateStructuredBuffer(
				GraphBuilder,
				TEXT("VelocitiesBuffer"),
				sizeof(FVector3f),
				NumVectors,
				VelocitiesRawData,
				VelocitiesVectorSize * NumVectors
			);

			/////Create View and pass Parameters
			PassParameters->Densities = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(DensitiesBuffer));
			PassParameters->PredictedPositions = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(PredictedPositionsBuffer));
			PassParameters->SpatialOffsets = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(SpatialOffsetsBuffer));
			PassParameters->SpatialIndices = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(SpatialIndicesBuffer));
			PassParameters->Velocities = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(VelocitiesBuffer));
			PassParameters->TargetDensity = Params.TargetDensity;
			PassParameters->PressureMultiplier = Params.PressureMultiplier;
			PassParameters->NearPressureMultiplier = Params.NearPressureMultiplier;
			PassParameters->NumParticles = Params.NumParticles;
			PassParameters->SmoothingRadius = Params.SmoothingRadius;
			

			auto GroupCount = FComputeShaderUtils::GetGroupCount(FIntVector(Params.X, Params.Y, Params.Z), FComputeShaderUtils::kGolden2DGroupSize);
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("ExecutePressureShader"),
				PassParameters,
				ERDGPassFlags::AsyncCompute,
				[&PassParameters, ComputeShader, GroupCount](FRHIComputeCommandList& RHICmdList)
			{
				FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *PassParameters, GroupCount);
			});

			
			FRHIGPUBufferReadback* GPUBufferReadback = new FRHIGPUBufferReadback(TEXT("ExecutePressureShaderOutput"));
			AddEnqueueCopyPass(GraphBuilder, GPUBufferReadback, VelocitiesBuffer, 0u);

			auto RunnerFunc = [GPUBufferReadback, AsyncCallback, NumVectors](auto&& RunnerFunc) -> void {
				if (GPUBufferReadback->IsReady()) {
					
					FVector3f* Buffer = (FVector3f*)GPUBufferReadback->Lock(1);
					TArray<FVector3f> Velocities;
					Velocities.SetNum(NumVectors);
					FMemory::Memcpy(Velocities.GetData(), Buffer, NumVectors * sizeof(FVector3f));
					GPUBufferReadback->Unlock();

					AsyncTask(ENamedThreads::GameThread, [AsyncCallback, Velocities]() {
						AsyncCallback(Velocities);
					});

					delete GPUBufferReadback;
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