#include "GPUSort.h"
#include "SPHSimulation/Public/GPUSort/GPUSort.h"
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

DECLARE_STATS_GROUP(TEXT("GPUSort"), STATGROUP_GPUSort, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("GPUSort Execute"), STAT_GPUSort_Execute, STATGROUP_GPUSort);

// This class carries our parameter declarations and acts as the bridge between cpp and HLSL.
class SPHSIMULATION_API FSortKernel : public FGlobalShader
{
public:
	
	DECLARE_GLOBAL_SHADER(FSortKernel);
	SHADER_USE_PARAMETER_STRUCT(FSortKernel, FGlobalShader);
	
	
	class FSortKernel_Perm_TEST : SHADER_PERMUTATION_INT("TEST", 1);
	using FPermutationDomain = TShaderPermutationDomain<
		FSortKernel_Perm_TEST
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

		SHADER_PARAMETER(int, StepIndex)
		SHADER_PARAMETER(int, GroupWidth)
		SHADER_PARAMETER(int, GroupHeight)
		SHADER_PARAMETER(int, NumEntries)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FIntVector>, Entries)//Input and Output

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
		OutEnvironment.SetDefine(TEXT("THREADS_X"), NUM_THREADS_GPUSort_X);
		OutEnvironment.SetDefine(TEXT("THREADS_Y"), NUM_THREADS_GPUSort_Y);
		OutEnvironment.SetDefine(TEXT("THREADS_Z"), NUM_THREADS_GPUSort_Z);

		// This shader must support typed UAV load and we are testing if it is supported at runtime using RHIIsTypedUAVLoadSupported
		//OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);

		// FForwardLightingParameters::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
	}
private:
};

IMPLEMENT_GLOBAL_SHADER(FSortKernel, "/SPHSimulationShaders/GPUSort/GPUSort.usf", "SortCS", SF_Compute);

class SPHSIMULATION_API FCalculateOffsetsKernel : public FGlobalShader
{
public:

	DECLARE_GLOBAL_SHADER(FCalculateOffsetsKernel);
	SHADER_USE_PARAMETER_STRUCT(FCalculateOffsetsKernel, FGlobalShader);


	class FCalculateOffsetsKernel_Perm_TEST : SHADER_PERMUTATION_INT("TEST", 1);
	using FPermutationDomain = TShaderPermutationDomain<
		FCalculateOffsetsKernel_Perm_TEST
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
		SHADER_PARAMETER(int, NumEntries)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FIntVector>, Entries)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<int>, Offsets)




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
		OutEnvironment.SetDefine(TEXT("THREADS_X"), NUM_THREADS_GPUSort_X);
		OutEnvironment.SetDefine(TEXT("THREADS_Y"), NUM_THREADS_GPUSort_Y);
		OutEnvironment.SetDefine(TEXT("THREADS_Z"), NUM_THREADS_GPUSort_Z);

		// This shader must support typed UAV load and we are testing if it is supported at runtime using RHIIsTypedUAVLoadSupported
		//OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);

		// FForwardLightingParameters::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
	}
private:
};

// This will tell the engine to create the shader and where the shader entry point is.
//                            ShaderType                            ShaderPath                     Shader function name    Type
IMPLEMENT_GLOBAL_SHADER(FCalculateOffsetsKernel, "/SPHSimulationShaders/GPUSort/GPUSort.usf", "CalculateOffsetsCS", SF_Compute);

void FGPUSortInterface::DispatchRenderThread(FRHICommandListImmediate& RHICmdList, FGPUSortDispatchParams Params, 
	TFunction<void(const TArray<FIntVector>& Entries,const TArray<int>& Offsets)> AsyncCallback) {
	FRDGBuilder GraphBuilder(RHICmdList);

	{
		SCOPE_CYCLE_COUNTER(STAT_GPUSort_Execute);
		DECLARE_GPU_STAT(GPUSort)
		RDG_EVENT_SCOPE(GraphBuilder, "GPUSort");
		RDG_GPU_STAT_SCOPE(GraphBuilder, GPUSort);
		
		typename FSortKernel::FPermutationDomain SortKernelPermutationVector;
		typename FCalculateOffsetsKernel::FPermutationDomain CalculateOffsetsKernelPermutationVector;
		
		// Add any static permutation options here
		// PermutationVector.Set<FGPUSort::FMyPermutationName>(12345);

		TShaderMapRef<FSortKernel> SortComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), SortKernelPermutationVector);
		TShaderMapRef<FCalculateOffsetsKernel> CalcOffsetComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), CalculateOffsetsKernelPermutationVector);

		bool bIsSortShaderValid = SortComputeShader.IsValid();
		bool bIsCalcOffsetShaderValid = CalcOffsetComputeShader.IsValid();

		if (bIsSortShaderValid&& bIsCalcOffsetShaderValid) {
			//FSortKernel::FParameters* SortPassParameters = GraphBuilder.AllocParameters<FSortKernel::FParameters>();
			int numStages = FMath::Log2(double(FMath::RoundUpToPowerOfTwo(Params.NumParticles))); //오류 가능성 있음 
			
			//재사용하는 버퍼에 대해서 for문 밖에서 선언
			const void* RawData = (void*)Params.Entries.GetData();
			int NumVectors = Params.Entries.Num();
			int VectorSize = sizeof(FIntVector);
			FRDGBufferRef EntriesBuffer = CreateStructuredBuffer(
				GraphBuilder,
				TEXT("EntriesBuffer"),
				sizeof(FIntVector),
				NumVectors,
				RawData,
				VectorSize * NumVectors
			);

			auto GroupCount = FComputeShaderUtils::GetGroupCount(FIntVector(Params.X, Params.Y, Params.Z), FComputeShaderUtils::kGolden2DGroupSize);

			for (int stageIndex = 0; stageIndex < numStages; stageIndex++)
			{
				for (int stepIndex = 0; stepIndex < stageIndex + 1; stepIndex++)
				{
					FSortKernel::FParameters* SortPassParameters = GraphBuilder.AllocParameters<FSortKernel::FParameters>();

					// 정렬 패턴 계산
					int groupWidth = 1 << (stageIndex - stepIndex);
					int groupHeight = 2 * groupWidth - 1;

					SortPassParameters->StepIndex = stepIndex;
					SortPassParameters->GroupWidth = groupWidth;
					SortPassParameters->GroupHeight = groupHeight;
					SortPassParameters->NumEntries = NumVectors;
					SortPassParameters->Entries = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(EntriesBuffer));

					
					GraphBuilder.AddPass(
						RDG_EVENT_NAME("SortPass_Stage%d_Step%d", stageIndex, stepIndex),
						SortPassParameters,
						ERDGPassFlags::AsyncCompute,
						[SortPassParameters, SortComputeShader, GroupCount](FRHIComputeCommandList& RHICmdList)
						{
							FComputeShaderUtils::Dispatch(RHICmdList, SortComputeShader, *SortPassParameters, GroupCount);
						});
				}
			}
			FCalculateOffsetsKernel::FParameters* CalcOffsetPassParameters = GraphBuilder.AllocParameters<FCalculateOffsetsKernel::FParameters>();

			FRDGBufferRef OffsetsBuffer = GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateStructuredDesc(sizeof(int), NumVectors),
				TEXT("Offsets"));

			CalcOffsetPassParameters->Entries = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(EntriesBuffer));
			CalcOffsetPassParameters->Offsets = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(OffsetsBuffer));
			CalcOffsetPassParameters->NumEntries = NumVectors;

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("CalculateOffset"),
				CalcOffsetPassParameters,
				ERDGPassFlags::AsyncCompute,
				[&CalcOffsetPassParameters, CalcOffsetComputeShader, GroupCount](FRHIComputeCommandList& RHICmdList)
				{
					FComputeShaderUtils::Dispatch(RHICmdList, CalcOffsetComputeShader, *CalcOffsetPassParameters, GroupCount);
				});
			FRHIGPUBufferReadback* EntriesReadback = new FRHIGPUBufferReadback(TEXT("SortedEntriesOutput"));
			FRHIGPUBufferReadback* CalcedOffsetsReadback = new FRHIGPUBufferReadback(TEXT("SortedEntriesOutput"));
			AddEnqueueCopyPass(GraphBuilder, EntriesReadback, EntriesBuffer, 0u);
			AddEnqueueCopyPass(GraphBuilder, CalcedOffsetsReadback, EntriesBuffer, 0u);

			auto RunnerFunc = [EntriesReadback, CalcedOffsetsReadback, AsyncCallback, NumVectors](auto&& RunnerFunc) -> void {
				if (EntriesReadback->IsReady() && CalcedOffsetsReadback->IsReady()) {

					FIntVector* EntBuffer = (FIntVector*)EntriesReadback->Lock(NumVectors * sizeof(FIntVector));
					TArray<FIntVector> Entries;
					Entries.SetNum(NumVectors);
					FMemory::Memcpy(Entries.GetData(), EntBuffer, NumVectors * sizeof(FIntVector));
					EntriesReadback->Unlock();


					int* Buffer = (int*)CalcedOffsetsReadback->Lock(NumVectors * sizeof(int));
					TArray<int> CalcedOffsets;
					CalcedOffsets.SetNum(NumVectors);
					FMemory::Memcpy(CalcedOffsets.GetData(), Buffer, NumVectors * sizeof(int));
					CalcedOffsetsReadback->Unlock();


					AsyncTask(ENamedThreads::GameThread, [AsyncCallback, Entries, CalcedOffsets]() {
						AsyncCallback(Entries,CalcedOffsets);
						});

					delete EntriesReadback;
					delete CalcedOffsetsReadback;
				}
				else {
					AsyncTask(ENamedThreads::ActualRenderingThread, [RunnerFunc]() {
						RunnerFunc(RunnerFunc);
						});
				}
			};


			

			/*const void* RawData = (void*)Params.Input;
			int NumInputs = 2;
			int InputSize = sizeof(int);
			FRDGBufferRef InputBuffer = CreateUploadBuffer(GraphBuilder, TEXT("InputBuffer"), InputSize, NumInputs, RawData, InputSize * NumInputs);

			SortPassParameters->Input = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InputBuffer, PF_R32_SINT));

			FRDGBufferRef OutputBuffer = GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateBufferDesc(sizeof(int32), 1),
				TEXT("OutputBuffer"));

			SortPassParameters->Output = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(OutputBuffer, PF_R32_SINT));
			

			auto GroupCount = FComputeShaderUtils::GetGroupCount(FIntVector(Params.X, Params.Y, Params.Z), FComputeShaderUtils::kGolden2DGroupSize);
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("ExecuteGPUSort"),
				SortPassParameters,
				ERDGPassFlags::AsyncCompute,
				[&SortPassParameters, SortComputeShader, GroupCount](FRHIComputeCommandList& RHICmdList)
			{
				FComputeShaderUtils::Dispatch(RHICmdList, SortComputeShader, *SortPassParameters, GroupCount);
			});*/

			/*
			FRHIGPUBufferReadback* GPUBufferReadback = new FRHIGPUBufferReadback(TEXT("ExecuteGPUSortOutput"));
			AddEnqueueCopyPass(GraphBuilder, GPUBufferReadback, OutputBuffer, 0u);

			auto RunnerFunc = [GPUBufferReadback, AsyncCallback](auto&& RunnerFunc) -> void {
				if (GPUBufferReadback->IsReady()) {
					
					int32* Buffer = (int32*)GPUBufferReadback->Lock(1);
					int OutVal = Buffer[0];
					
					GPUBufferReadback->Unlock();

					AsyncTask(ENamedThreads::GameThread, [AsyncCallback, OutVal]() {
						AsyncCallback(OutVal);
					});

					delete GPUBufferReadback;
				} else {
					AsyncTask(ENamedThreads::ActualRenderingThread, [RunnerFunc]() {
						RunnerFunc(RunnerFunc);
					});
				}
			};
			*/
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