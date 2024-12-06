#include "DensityKernel.h"
#include "SPHSimulation/Public/DensityKernel/DensityKernel.h"
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

DECLARE_STATS_GROUP(TEXT("DensityKernel"), STATGROUP_DensityKernel, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("DensityKernel Execute"), STAT_DensityKernel_Execute, STATGROUP_DensityKernel);

// This class carries our parameter declarations and acts as the bridge between cpp and HLSL.
class SPHSIMULATION_API FDensityKernel: public FGlobalShader
{
public:
	
	DECLARE_GLOBAL_SHADER(FDensityKernel);
	SHADER_USE_PARAMETER_STRUCT(FDensityKernel, FGlobalShader);
	
	
	class FDensityKernel_Perm_TEST : SHADER_PERMUTATION_INT("TEST", 1);
	using FPermutationDomain = TShaderPermutationDomain<
		FDensityKernel_Perm_TEST
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

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector3f>, PredictedPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FIntVector>, SpatialIndices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<int>, SpatialOffsets)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FVector2f>, Densities)
		

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
		OutEnvironment.SetDefine(TEXT("THREADS_X"), NUM_THREADS_DensityKernel_X);
		OutEnvironment.SetDefine(TEXT("THREADS_Y"), NUM_THREADS_DensityKernel_Y);
		OutEnvironment.SetDefine(TEXT("THREADS_Z"), NUM_THREADS_DensityKernel_Z);

		// This shader must support typed UAV load and we are testing if it is supported at runtime using RHIIsTypedUAVLoadSupported
		//OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);

		// FForwardLightingParameters::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
	}
private:
};

// This will tell the engine to create the shader and where the shader entry point is.
//                            ShaderType                            ShaderPath                     Shader function name    Type
IMPLEMENT_GLOBAL_SHADER(FDensityKernel, "/SPHSimulationShaders/DensityKernel/DensityKernel.usf", "DensityKernel", SF_Compute);

void FDensityKernelInterface::DispatchRenderThread(FRHICommandListImmediate& RHICmdList, FDensityKernelDispatchParams Params, 
	TFunction<void(const TArray<FVector2f>&)> AsyncCallback) {
	FRDGBuilder GraphBuilder(RHICmdList);

	{
		SCOPE_CYCLE_COUNTER(STAT_DensityKernel_Execute);
		DECLARE_GPU_STAT(DensityKernel)
		RDG_EVENT_SCOPE(GraphBuilder, "DensityKernel");
		RDG_GPU_STAT_SCOPE(GraphBuilder, DensityKernel);
		
		typename FDensityKernel::FPermutationDomain PermutationVector;
		
		// Add any static permutation options here
		// PermutationVector.Set<FDensityKernel::FMyPermutationName>(12345);

		TShaderMapRef<FDensityKernel> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);
		

		bool bIsShaderValid = ComputeShader.IsValid();

		if (bIsShaderValid) {
			FDensityKernel::FParameters* PassParameters = GraphBuilder.AllocParameters<FDensityKernel::FParameters>();

			/////Predicted Position Initialize
			const void* PredictedPositionRawData = Params.PredictedPositions.GetData();
			int NumVectors = Params.PredictedPositions.Num();
			int PredictedPositionVectorSize = sizeof(FVector3f);

			FRDGBufferRef PredictedPositionsBuffer = CreateStructuredBuffer(
				GraphBuilder,
				TEXT("PredictedPositionsVectorBuffer"),
				sizeof(FVector3f),
				NumVectors,
				PredictedPositionRawData,
				PredictedPositionVectorSize * NumVectors
			);
			PassParameters->PredictedPositions = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(PredictedPositionsBuffer));

			/////SpatialIndices Initialize
			const void* SpatialIndicesRawData = Params.SpatialIndices.GetData();
			int SpatialIndicesVectorSize = sizeof(FIntVector);
			FRDGBufferRef SpatialIndicesBuffer = CreateStructuredBuffer(
				GraphBuilder,
				TEXT("SpatialIndicesVectorBuffer"),
				sizeof(FVector3f),
				NumVectors,
				SpatialIndicesRawData,
				SpatialIndicesVectorSize * NumVectors
			);
			PassParameters->SpatialIndices = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(SpatialIndicesBuffer));

			/////SpatialOffsets Initialize
			const void* SpatialOffsetsRawData = Params.SpatialOffsets.GetData();
			int SpatialOffsetsVectorSize = sizeof(int);
			FRDGBufferRef SpatialOffsetsBuffer = CreateStructuredBuffer(
				GraphBuilder,
				TEXT("SpatialOffsetsVectorBuffer"),
				sizeof(int),
				NumVectors,
				SpatialOffsetsRawData,
				SpatialOffsetsVectorSize * NumVectors
			);
			PassParameters->SpatialOffsets = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(SpatialOffsetsBuffer));


			/////Output Densities Buffer Initialization
			FRDGBufferRef DensitiesBuffer = GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector2f), NumVectors),
				TEXT("DensitiesBuffer"));

			PassParameters->Densities = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(DensitiesBuffer));
			

			auto GroupCount = FComputeShaderUtils::GetGroupCount(FIntVector(Params.X, Params.Y, Params.Z), FComputeShaderUtils::kGolden2DGroupSize);
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("ExecuteDensityKernel"),
				PassParameters,
				ERDGPassFlags::AsyncCompute,
				[&PassParameters, ComputeShader, GroupCount](FRHIComputeCommandList& RHICmdList)
			{
				FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *PassParameters, GroupCount);
			});

			
			FRHIGPUBufferReadback* GPUBufferReadback = new FRHIGPUBufferReadback(TEXT("ExecuteDensityKernelOutput"));
			AddEnqueueCopyPass(GraphBuilder, GPUBufferReadback, DensitiesBuffer, 0u);

			auto RunnerFunc = [GPUBufferReadback, AsyncCallback, NumVectors](auto&& RunnerFunc) -> void {
				if (GPUBufferReadback->IsReady()) {
					
					FVector2f* Buffer = (FVector2f*)GPUBufferReadback->Lock(1);
					TArray<FVector2f> Densities;
					Densities.SetNum(NumVectors);
					FMemory::Memcpy(Densities.GetData(), Buffer, NumVectors * sizeof(FVector2f));
					
					GPUBufferReadback->Unlock();

					AsyncTask(ENamedThreads::GameThread, [AsyncCallback, Densities]() {
						AsyncCallback(Densities);
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