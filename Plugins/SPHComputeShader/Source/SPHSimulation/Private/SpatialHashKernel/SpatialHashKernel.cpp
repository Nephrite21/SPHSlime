#include "SpatialHashKernel.h"
#include "SPHSimulation/Public/SpatialHashKernel/SpatialHashKernel.h"
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

DECLARE_STATS_GROUP(TEXT("SpatialHashKernel"), STATGROUP_SpatialHashKernel, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("SpatialHashKernel Execute"), STAT_SpatialHashKernel_Execute, STATGROUP_SpatialHashKernel);

// This class carries our parameter declarations and acts as the bridge between cpp and HLSL.
class SPHSIMULATION_API FSpatialHashKernel: public FGlobalShader
{
public:
	
	DECLARE_GLOBAL_SHADER(FSpatialHashKernel);
	SHADER_USE_PARAMETER_STRUCT(FSpatialHashKernel, FGlobalShader);
	
	
	class FSpatialHashKernel_Perm_TEST : SHADER_PERMUTATION_INT("TEST", 1);
	using FPermutationDomain = TShaderPermutationDomain<
		FSpatialHashKernel_Perm_TEST
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
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<int>, SpatialOffsets)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FIntVector>, SpatialIndices)
		

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
		OutEnvironment.SetDefine(TEXT("THREADS_X"), NUM_THREADS_SpatialHashKernel_X);
		OutEnvironment.SetDefine(TEXT("THREADS_Y"), NUM_THREADS_SpatialHashKernel_Y);
		OutEnvironment.SetDefine(TEXT("THREADS_Z"), NUM_THREADS_SpatialHashKernel_Z);

		// This shader must support typed UAV load and we are testing if it is supported at runtime using RHIIsTypedUAVLoadSupported
		//OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);

		// FForwardLightingParameters::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
	}
private:
};

// This will tell the engine to create the shader and where the shader entry point is.
//                            ShaderType                            ShaderPath                     Shader function name    Type
IMPLEMENT_GLOBAL_SHADER(FSpatialHashKernel, "/SPHSimulationShaders/SpatialHashKernel/SpatialHashKernel.usf", "SpatialHashKernel", SF_Compute);

void FSpatialHashKernelInterface::DispatchRenderThread(FRHICommandListImmediate& RHICmdList, FSpatialHashKernelDispatchParams Params, 
	TFunction<void(const TArray<int>& SpatialOffsets, const TArray<FIntVector>& SpatialIndices)> AsyncCallback) {
	FRDGBuilder GraphBuilder(RHICmdList);

	{
		SCOPE_CYCLE_COUNTER(STAT_SpatialHashKernel_Execute);
		DECLARE_GPU_STAT(SpatialHashKernel)
		RDG_EVENT_SCOPE(GraphBuilder, "SpatialHashKernel");
		RDG_GPU_STAT_SCOPE(GraphBuilder, SpatialHashKernel);
		
		typename FSpatialHashKernel::FPermutationDomain PermutationVector;
		
		// Add any static permutation options here
		// PermutationVector.Set<FSpatialHashKernel::FMyPermutationName>(12345);

		TShaderMapRef<FSpatialHashKernel> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);
		

		bool bIsShaderValid = ComputeShader.IsValid();

		if (bIsShaderValid) {
			FSpatialHashKernel::FParameters* PassParameters = GraphBuilder.AllocParameters<FSpatialHashKernel::FParameters>();

			
			const void* RawData = (void*)Params.PredictedPositions.GetData();
			int NumVectors = Params.PredictedPositions.Num();
			int VectorSize = sizeof(FVector3f);
			FRDGBufferRef PredictedPositionsBuffer = CreateStructuredBuffer(
				GraphBuilder,
				TEXT("PredictedPositionsVectorBuffer"),
				sizeof(FVector3f),
				NumVectors,
				RawData,
				VectorSize * NumVectors
			);
			PassParameters->PredictedPositions = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(PredictedPositionsBuffer));

			const void* RawData1 = Params.SpatialOffsets.GetData();
			FRDGBufferRef SpatialOffsetsBuffer = CreateStructuredBuffer(
				GraphBuilder,
				TEXT("SpatialOffsetsBuffer"),
				sizeof(int),
				NumVectors,
				RawData1,
				VectorSize * NumVectors
			);
			PassParameters->SpatialOffsets = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(SpatialOffsetsBuffer));



			FRDGBufferRef SpatialIndices = GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateStructuredDesc(sizeof(FIntVector), NumVectors),
				TEXT("SpatialIndices"));

			PassParameters->SpatialIndices = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(SpatialIndices));
			
			PassParameters->NumParticles = Params.NumParticles;
			PassParameters->SmoothingRadius = Params.SmoothingRadius;

			auto GroupCount = FComputeShaderUtils::GetGroupCount(
				FIntVector(Params.X, Params.Y, Params.Z),
				FComputeShaderUtils::kGolden2DGroupSize
			); 

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("ExecuteSpatialHashKernel"),
				PassParameters,
				ERDGPassFlags::AsyncCompute,
				[&PassParameters, ComputeShader, GroupCount](FRHIComputeCommandList& RHICmdList)
			{
				FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *PassParameters, GroupCount);
			});

			
			FRHIGPUBufferReadback* SpatialOffsetsReadback = new FRHIGPUBufferReadback(TEXT("SpatialOffsetsOutput"));
			FRHIGPUBufferReadback* SpatialIndicesReadback = new FRHIGPUBufferReadback(TEXT("SpatialIndicesOutput"));
			AddEnqueueCopyPass(GraphBuilder, SpatialIndicesReadback, SpatialIndices, 0u);
			AddEnqueueCopyPass(GraphBuilder, SpatialOffsetsReadback, SpatialOffsetsBuffer, 0u);

			auto RunnerFunc = [SpatialOffsetsReadback, SpatialIndicesReadback, AsyncCallback, NumVectors](auto&& RunnerFunc) -> void {
				if (SpatialOffsetsReadback->IsReady() && SpatialIndicesReadback->IsReady()) {
					
					int* Buffer = (int*)SpatialOffsetsReadback->Lock(NumVectors * sizeof(int));
					TArray<int> SpatialOffsets;
					SpatialOffsets.SetNum(NumVectors);
					FMemory::Memcpy(SpatialOffsets.GetData(), Buffer, NumVectors * sizeof(int));
					SpatialOffsetsReadback->Unlock();


					FIntVector* SpatialIndicesBuffer = (FIntVector*)SpatialIndicesReadback->Lock(NumVectors * sizeof(FIntVector));
					TArray<FIntVector> SpatialIndices;
					SpatialIndices.SetNum(NumVectors);
					FMemory::Memcpy(SpatialIndices.GetData(), SpatialIndicesBuffer, NumVectors * sizeof(FIntVector));
					SpatialIndicesReadback->Unlock();


					AsyncTask(ENamedThreads::GameThread, [AsyncCallback, SpatialOffsets, SpatialIndices]() {
						AsyncCallback(SpatialOffsets, SpatialIndices);
					});

					delete SpatialOffsetsReadback;
					delete SpatialIndicesReadback;
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