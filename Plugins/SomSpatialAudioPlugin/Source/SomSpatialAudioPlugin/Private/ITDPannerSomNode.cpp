#include "ITDPannerSomNode.h"

#include "MetasoundExecutableOperator.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundStandardNodesNames.h"
#include "MetasoundStandardNodesCategories.h"
#include "MetasoundFacade.h"
#include "MetasoundPrimitives.h"
#include "MetasoundParamHelper.h"
#include "MetasoundAudioBuffer.h"
#include "Math/UnrealMathUtility.h"

#define LOCTEXT_NAMESPACE "Metasound_ITDPanner"

namespace Metasound
{
	namespace ITDVertexNames
	{
		METASOUND_PARAM(InputAudio, "Audio In", "Mono input signal");
		METASOUND_PARAM(Azimuth, "Azimuth", "-1 = Left, 0 = Front, +1 = Right");
		METASOUND_PARAM(OutputLeft, "Left", "Left ear signal");
		METASOUND_PARAM(OutputRight, "Right", "Right ear signal");
	}
	
	class FITDPannerOperator : public TExecutableOperator<FITDPannerOperator>
	{
	public:
		FITDPannerOperator(
			const FBuildOperatorParams& InParams,
			const FAudioBufferReadRef& InAudio,
			const FFloatReadRef& InAzimuth)
			: AudioInput(InAudio)
			, Azimuth(InAzimuth)
			, AudioLeft(FAudioBufferWriteRef::CreateNew(InParams.OperatorSettings))
			, AudioRight(FAudioBufferWriteRef::CreateNew(InParams.OperatorSettings))
			, SampleRate(InParams.OperatorSettings.GetSampleRate())
		{
			Reset(InParams);
		}
		
		static const FNodeClassMetadata& GetNodeInfo()
		{
			static const FNodeClassMetadata Metadata =
			{
				FNodeClassName { StandardNodes::Namespace, "ITD Panner", StandardNodes::AudioVariant },
				1, 0,
				METASOUND_LOCTEXT("ITDDisplayName", "ITD Panner"),
				METASOUND_LOCTEXT("ITDDesc", "Mono to stereo interaural time difference panner"),
				PluginAuthor,
				PluginNodeMissingPrompt,
				DeclareVertexInterface(),
				{ NodeCategories::Spatialization },
				{},
				FNodeDisplayStyle()
			};
			return Metadata;
		}

		static const FVertexInterface& DeclareVertexInterface()
		{
			using namespace ITDVertexNames;

			static const FVertexInterface Interface(
				FInputVertexInterface(
					TInputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputAudio)),
					TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(Azimuth))
				),
				FOutputVertexInterface(
					TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputLeft)),
					TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputRight))
				)
			);
			return Interface;
		}

		void BindInputs(FInputVertexInterfaceData& InOutVertexData) override
		{
			using namespace ITDVertexNames;
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InputAudio), AudioInput);
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(Azimuth), Azimuth);
		}

		void BindOutputs(FOutputVertexInterfaceData& InOutVertexData) override
		{
			using namespace ITDVertexNames;
			InOutVertexData.BindWriteVertex(METASOUND_GET_PARAM_NAME(OutputLeft), AudioLeft);
			InOutVertexData.BindWriteVertex(METASOUND_GET_PARAM_NAME(OutputRight), AudioRight);
		}

		static TUniquePtr<IOperator> CreateOperator(
			const FBuildOperatorParams& InParams,
			FBuildResults&)
		{
			using namespace ITDVertexNames;

			const auto& Inputs = InParams.InputData;
			auto Audio = Inputs.GetOrCreateDefaultDataReadReference<FAudioBuffer>(
				METASOUND_GET_PARAM_NAME(InputAudio), InParams.OperatorSettings);

			auto Az = Inputs.GetOrCreateDefaultDataReadReference<float>(
				METASOUND_GET_PARAM_NAME(Azimuth), InParams.OperatorSettings);

			return MakeUnique<FITDPannerOperator>(InParams, Audio, Az);
		}

		void Reset(const IOperator::FResetParams&)
		{
			AudioLeft->Zero();
			AudioRight->Zero();

			WriteIndex = 0;
			DelayBuffer.SetNumZeroed(MaxDelaySamples);
		}
		
		void Execute()
		{
			const int32 NumFrames = AudioInput->Num();
			if (NumFrames == 0) return;

			const float* In = AudioInput->GetData();
			float* L = AudioLeft->GetData();
			float* R = AudioRight->GetData();
			
			const float Az = FMath::Clamp(*Azimuth, -1.f, 1.f);
			
			const float MaxITDSeconds = 0.0006f;
			
			const float ITDSeconds = Az * MaxITDSeconds;
			const float DelaySamples = FMath::Abs(ITDSeconds) * SampleRate;

			for (int32 i = 0; i < NumFrames; ++i)
			{
				DelayBuffer[WriteIndex] = In[i];

				const int32 ReadOffset = static_cast<int32>(DelaySamples);
				int32 ReadIndex = WriteIndex - ReadOffset;
				if (ReadIndex < 0)
					ReadIndex += MaxDelaySamples;

				const float DelayedSample = DelayBuffer[ReadIndex];

				if (Az > 0.f)
				{
					L[i] = DelayedSample;
					R[i] = In[i];
				}
				else
				{
					L[i] = In[i];
					R[i] = DelayedSample;
				}

				WriteIndex = (WriteIndex + 1) % MaxDelaySamples;
			}
		}

	private:
		FAudioBufferReadRef AudioInput;
		FFloatReadRef Azimuth;

		FAudioBufferWriteRef AudioLeft;
		FAudioBufferWriteRef AudioRight;

		float SampleRate;
		
		static constexpr int32 MaxDelaySamples = 64;
		TArray<float> DelayBuffer;
		int32 WriteIndex = 0;
	};
	
	class FITDPannerNode : public FNodeFacade
	{
	public:
		FITDPannerNode(const FNodeInitData& InitData)
			: FNodeFacade(InitData.InstanceName, InitData.InstanceID,
				TFacadeOperatorClass<FITDPannerOperator>())
		{}
	};

	METASOUND_REGISTER_NODE(FITDPannerNode)
}

#undef LOCTEXT_NAMESPACE