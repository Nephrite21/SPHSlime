#include "InitializeParticle.h"
#include "SPHPreprocessing/Public/InitializeParticle/InitializeParticle.h"
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

DECLARE_STATS_GROUP(TEXT("InitializeParticle"), STATGROUP_InitializeParticle, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("InitializeParticle Execute"), STAT_InitializeParticle_Execute, STATGROUP_InitializeParticle);

// This class carries our parameter declarations and acts as the bridge between cpp and HLSL.
class SPHPREPROCESSING_API FInitializeParticle: public FGlobalShader
{
public:
	
	DECLARE_GLOBAL_SHADER(FInitializeParticle);
	SHADER_USE_PARAMETER_STRUCT(FInitializeParticle, FGlobalShader);
	
	
	class FInitializeParticle_Perm_TEST : SHADER_PERMUTATION_INT("TEST", 1);
	using FPermutationDomain = TShaderPermutationDomain<
		FInitializeParticle_Perm_TEST
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
		SHADER_PARAMETER(float, SpawnLength)
		
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector>, InputVectors)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FVector>, OutputVectors)
		

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
		OutEnvironment.SetDefine(TEXT("THREADS_X"), NUM_THREADS_InitializeParticle_X);
		OutEnvironment.SetDefine(TEXT("THREADS_Y"), NUM_THREADS_InitializeParticle_Y);
		OutEnvironment.SetDefine(TEXT("THREADS_Z"), NUM_THREADS_InitializeParticle_Z);

		// This shader must support typed UAV load and we are testing if it is supported at runtime using RHIIsTypedUAVLoadSupported
		//OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);

		// FForwardLightingParameters::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
	}
private:
};

// This will tell the engine to create the shader and where the shader entry point is.
//                            ShaderType                            ShaderPath                     Shader function name    Type
IMPLEMENT_GLOBAL_SHADER(FInitializeParticle, "/SPHPreprocessingShaders/InitializeParticle/InitializeParticle.usf", "InitializeParticle", SF_Compute);

void FInitializeParticleInterface::DispatchRenderThread(FRHICommandListImmediate& RHICmdList, FInitializeParticleDispatchParams Params, TFunction<void(const TArray<FVector>& OutputVector)> AsyncCallback) {
	FRDGBuilder GraphBuilder(RHICmdList);
	{
		SCOPE_CYCLE_COUNTER(STAT_InitializeParticle_Execute);
		DECLARE_GPU_STAT(InitializeParticle)
		RDG_EVENT_SCOPE(GraphBuilder, "InitializeParticle");
		RDG_GPU_STAT_SCOPE(GraphBuilder, InitializeParticle);
		
		typename FInitializeParticle::FPermutationDomain PermutationVector;
		
		// Add any static permutation options here
		// PermutationVector.Set<FInitializeParticle::FMyPermutationName>(12345);

		TShaderMapRef<FInitializeParticle> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);
		

		bool bIsShaderValid = ComputeShader.IsValid();

		if (bIsShaderValid) {
			FInitializeParticle::FParameters* PassParameters = GraphBuilder.AllocParameters<FInitializeParticle::FParameters>();

			
			const void* RawData = Params.InputVectors.GetData();
			int NumVectors = Params.InputVectors.Num();
			int VectorSize = sizeof(FVector);
			//int InputSize = sizeof(int);
			FRDGBufferRef InputBuffer = CreateStructuredBuffer(
				GraphBuilder,
				TEXT("InputVectorBuffer"),
				sizeof(FVector),
				NumVectors,
				RawData,
				VectorSize * NumVectors
			);

			//ÀÎÇ² ¼³Á¤
			PassParameters->InputVectors = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InputBuffer));

			PassParameters->NumParticles = Params.NumParticles;
			PassParameters->SpawnLength = Params.SpawnLength;


			FRDGBufferRef OutputBuffer = GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector), NumVectors),
				TEXT("OutputBuffer"));

			PassParameters->OutputVectors = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(OutputBuffer));
			

			auto GroupCount = FComputeShaderUtils::GetGroupCount(
				FIntVector(Params.X, Params.Y, Params.Z),
				FComputeShaderUtils::kGolden2DGroupSize
			);
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("ExecuteInitializeParticle"),
				PassParameters,
				ERDGPassFlags::AsyncCompute,
				[&PassParameters, ComputeShader, GroupCount](FRHIComputeCommandList& RHICmdList)
			{
				FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *PassParameters, GroupCount);
			});

			
			FRHIGPUBufferReadback* GPUBufferReadback = new FRHIGPUBufferReadback(TEXT("ExecuteInitializeParticleOutput"));
			AddEnqueueCopyPass(GraphBuilder, GPUBufferReadback, OutputBuffer, 0u);

			auto RunnerFunc = [GPUBufferReadback, AsyncCallback, NumVectors](auto&& RunnerFunc) -> void {
				if (GPUBufferReadback->IsReady()) {

					FVector* Buffer = (FVector*)GPUBufferReadback->Lock(NumVectors * sizeof(FVector));

					TArray<FVector> OutputVector;
					OutputVector.SetNum(NumVectors);
					FMemory::Memcpy(OutputVector.GetData(), Buffer, NumVectors * sizeof(FVector));

					GPUBufferReadback->Unlock();

					AsyncTask(ENamedThreads::GameThread, [AsyncCallback, OutputVector]() {
						AsyncCallback(OutputVector);
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