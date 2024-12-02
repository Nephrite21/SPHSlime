#include "ExternalForceKernel.h"
#include "SPHPreprocessing/Public/ExternalForceKernel/ExternalForceKernel.h"
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

DECLARE_STATS_GROUP(TEXT("ExternalForceKernel"), STATGROUP_ExternalForceKernel, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("ExternalForceKernel Execute"), STAT_ExternalForceKernel_Execute, STATGROUP_ExternalForceKernel);

// This class carries our parameter declarations and acts as the bridge between cpp and HLSL.
class SPHPREPROCESSING_API FExternalForceKernel: public FGlobalShader
{
public:
	
	DECLARE_GLOBAL_SHADER(FExternalForceKernel);
	SHADER_USE_PARAMETER_STRUCT(FExternalForceKernel, FGlobalShader);
	
	
	class FExternalForceKernel_Perm_TEST : SHADER_PERMUTATION_INT("TEST", 1);
	using FPermutationDomain = TShaderPermutationDomain<
		FExternalForceKernel_Perm_TEST
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
		SHADER_PARAMETER(float, gravity)
		
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector>, Positions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector>, Velocities)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FVector>, PredictedPositions)
		

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
		OutEnvironment.SetDefine(TEXT("THREADS_X"), NUM_THREADS_ExternalForceKernel_X);
		OutEnvironment.SetDefine(TEXT("THREADS_Y"), NUM_THREADS_ExternalForceKernel_Y);
		OutEnvironment.SetDefine(TEXT("THREADS_Z"), NUM_THREADS_ExternalForceKernel_Z);

		// This shader must support typed UAV load and we are testing if it is supported at runtime using RHIIsTypedUAVLoadSupported
		//OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);

		// FForwardLightingParameters::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
	}
private:
};

// This will tell the engine to create the shader and where the shader entry point is.
//                            ShaderType                            ShaderPath                     Shader function name    Type
IMPLEMENT_GLOBAL_SHADER(FExternalForceKernel, "/SPHPreprocessingShaders/ExternalForceKernel/ExternalForceKernel.usf", "ExternalForceKernel", SF_Compute);

void FExternalForceKernelInterface::DispatchRenderThread(FRHICommandListImmediate& RHICmdList, FExternalForceKernelDispatchParams Params, TFunction<void(const TArray<FVector>& PredictedPositions)> AsyncCallback) {
	FRDGBuilder GraphBuilder(RHICmdList);

	{
		SCOPE_CYCLE_COUNTER(STAT_ExternalForceKernel_Execute);
		DECLARE_GPU_STAT(ExternalForceKernel)
		RDG_EVENT_SCOPE(GraphBuilder, "ExternalForceKernel");
		RDG_GPU_STAT_SCOPE(GraphBuilder, ExternalForceKernel);
		
		typename FExternalForceKernel::FPermutationDomain PermutationVector;
		
		// Add any static permutation options here
		// PermutationVector.Set<FExternalForceKernel::FMyPermutationName>(12345);

		TShaderMapRef<FExternalForceKernel> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);
		

		bool bIsShaderValid = ComputeShader.IsValid();

		if (bIsShaderValid) {
			FExternalForceKernel::FParameters* PassParameters = GraphBuilder.AllocParameters<FExternalForceKernel::FParameters>();

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
			PassParameters->Positions = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(PositionsBuffer));


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
			PassParameters->Velocities = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(VelocitiesBuffer));


			//PredictedPositions 버퍼 설정
			FRDGBufferRef PredictedPositionsBuffer = GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector), NumVectors),
				TEXT("PredictedPositionsBuffer"));

			PassParameters->PredictedPositions = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(PredictedPositionsBuffer));
			


			auto GroupCount = FComputeShaderUtils::GetGroupCount(
				FIntVector(Params.X, Params.Y, Params.Z),
				FComputeShaderUtils::kGolden2DGroupSize
			);

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("ExecuteExternalForceKernel"),
				PassParameters,
				ERDGPassFlags::AsyncCompute,
				[&PassParameters, ComputeShader, GroupCount](FRHIComputeCommandList& RHICmdList)
			{
				FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *PassParameters, GroupCount);
			});

			
			FRHIGPUBufferReadback* GPUBufferReadback = new FRHIGPUBufferReadback(TEXT("ExecuteExternalForceKernelOutput"));
			AddEnqueueCopyPass(GraphBuilder, GPUBufferReadback, PredictedPositionsBuffer, 0u);

			auto RunnerFunc = [GPUBufferReadback, AsyncCallback, NumVectors](auto&& RunnerFunc) -> void {
				if (GPUBufferReadback->IsReady()) {
					
					FVector* Buffer = (FVector*)GPUBufferReadback->Lock(NumVectors * sizeof(FVector));

					TArray<FVector> PredictedPositions;
					PredictedPositions.SetNum(NumVectors);
					FMemory::Memcpy(PredictedPositions.GetData(), Buffer, NumVectors * sizeof(FVector));

					GPUBufferReadback->Unlock();

					AsyncTask(ENamedThreads::GameThread, [AsyncCallback, PredictedPositions]() {
						AsyncCallback(PredictedPositions);
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