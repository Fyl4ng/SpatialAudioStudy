#include "StereoPannerSomNode.h"
#include "Internationalization/Text.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundEnumRegistrationMacro.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundStandardNodesNames.h"
#include "MetasoundAudioBuffer.h"
#include "DSP/Dsp.h"
#include "MetasoundStandardNodesCategories.h"
#include "MetasoundFacade.h"
#include "MetasoundParamHelper.h"
#include "Math/UnrealMathUtility.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes_PannerMetasoundPlugin"

namespace Metasound
{
	namespace PannerVertexNames
	{
		METASOUND_PARAM(InputAudio, "Audio In", "Mono input signal");
		METASOUND_PARAM(PanAmount, "Pan", "-1 = Left, 0 = Center, 1 = Right");
		METASOUND_PARAM(PanLaw, "Pan Law", "Curve used for panning");
		METASOUND_PARAM(OutputLeft, "Left", "Left channel");
		METASOUND_PARAM(OutputRight, "Right", "Right channel");
	}
	
	enum class EPanLaw
	{
		Law_EqualPower = 0,
		Law_Linear,
		Law_SmoothStep,
		Law_Exponential
	};
	
	DECLARE_METASOUND_ENUM(
	   EPanLaw,                    
	   EPanLaw::Law_Linear,        
	   SOMSPATIALAUDIOPLUGIN_API, 
	   FEnumPanLaw,                
	   FEnumPanLawInfo,            
	   FEnumPanLawReadRef,         
	   FEnumPanLawWriteRef         
   );
	
	DEFINE_METASOUND_ENUM_BEGIN(EPanLaw, FEnumPanLaw, "PanLaw")
		DEFINE_METASOUND_ENUM_ENTRY(
			EPanLaw::Law_EqualPower,
			"PanningLawEqualPowerName", 
			"Equal Power", 
			"PanningLawEqualPowerTT", 
			"Equal power panning (default)"),
		DEFINE_METASOUND_ENUM_ENTRY(
			EPanLaw::Law_Linear,
			"PanningLawLinearName", 
			"Linear", 
			"PanningLawLinearTT", 
			"Linear crossfade"),
		DEFINE_METASOUND_ENUM_ENTRY(
			EPanLaw::Law_SmoothStep,
			"PanningLawSmoothStepName", 
			"SmoothStep", 
			"PanningSmoothStepTT", 
			"S-Curve"),
		DEFINE_METASOUND_ENUM_ENTRY(
			EPanLaw::Law_Exponential,
			"PanningLawExponentialName", 
			"Exponential", 
			"PanningLawExponentialTT", 
			"Exponential/log-like curve for musical bias"),
	DEFINE_METASOUND_ENUM_END()
	
	static void ComputeGains(const float T, const EPanLaw Law, float& OutLeft, float& OutRight)
	{
		switch (Law)
		{
			case EPanLaw::Law_EqualPower:
				{
					OutLeft = FMath::Cos( T * HALF_PI);
					OutRight = FMath::Sin( T * HALF_PI);
					break;
				}
			case EPanLaw::Law_Linear:
				{
					OutLeft = 1.f - T;
					OutRight = T;
					break;
				}
			case EPanLaw::Law_SmoothStep:
				{
					//SmoothStep formula: smoothstep(t) = t*t*(3-2*t)
					float s = T * T * ( 3.f - 2.f * T);
					OutLeft = 1.f - s;
					OutRight = s;
					break;
				}
			case EPanLaw::Law_Exponential:
				{
					// Exponential curve: map t in [0,1] to a shaped value using pow
					// exponent > 1 biases toward right slowly near center; < 1 biases toward right quickly.
					// 1.7 for a mild musical curve.
					constexpr float Exponent = 1.7f;
					const float s = FMath::Pow(T, Exponent);
					OutLeft = 1.0f - s;
					OutRight = s;
					break;
					break;
				}
			default:
				{
					OutLeft = FMath::Cos(T * HALF_PI);
					OutRight = FMath::Sin(T * HALF_PI);
					break;
				}
		}
	}
	
	class FPannerPanOperator : public TExecutableOperator<FPannerPanOperator>
	{
	public:
		FPannerPanOperator(
			const FBuildOperatorParams& InParams,
			const FAudioBufferReadRef& InAudioBuffer,
			const FFloatReadRef& InPanningAmount,
			const FEnumPanLawReadRef& InPanningLaw)
			: AudioInput(InAudioBuffer)
			, PanningAmount(InPanningAmount)
			, PanningLaw(InPanningLaw)
			, AudioLeft(FAudioBufferWriteRef::CreateNew(InParams.OperatorSettings))
			, AudioRight(FAudioBufferWriteRef::CreateNew(InParams.OperatorSettings))
			, SampleRate(InParams.OperatorSettings.GetSampleRate())
			, LastPan(0.f)
		{
			Reset(InParams);
		}
		
		static const FNodeClassMetadata& GetNodeInfo()
		{
			auto CreateNodeClassMetadata = []() -> FNodeClassMetadata
			{
				FVertexInterface NodeInterface = DeclareVertexInterface();
				FNodeClassMetadata Metadata
				{
					FNodeClassName { StandardNodes::Namespace, "Stereo Panner", StandardNodes::AudioVariant},
					1,
					0,
					METASOUND_LOCTEXT("StereoPannerName", "Stereo  Panner"),
					METASOUND_LOCTEXT("StereoPannerDesc", "Mono to stereo panner with selectable pan law"),
					PluginAuthor,
					PluginNodeMissingPrompt,
					NodeInterface,
					{ NodeCategories::Spatialization },
					{},
					FNodeDisplayStyle()
				};
				return Metadata;
			};
			
			static const FNodeClassMetadata Metadata = CreateNodeClassMetadata();
			return Metadata;
		}
		
		virtual void BindInputs(FInputVertexInterfaceData& InOutVertexData) override
		{
			using namespace PannerVertexNames;
			
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InputAudio), AudioInput);
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(PanAmount), PanningAmount);
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(PanLaw), PanningLaw);
		}
		
		virtual void BindOutputs(FOutputVertexInterfaceData& InOutVertexData) override
		{
			using namespace PannerVertexNames;
			
			InOutVertexData.BindWriteVertex(METASOUND_GET_PARAM_NAME(OutputLeft), AudioLeft);
			InOutVertexData.BindWriteVertex(METASOUND_GET_PARAM_NAME(OutputRight), AudioRight);
		}
		
		static const FVertexInterface& DeclareVertexInterface()
		{
			using namespace PannerVertexNames;
			
			static const FVertexInterface Interface(
				FInputVertexInterface(
						TInputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputAudio)),
						TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(PanAmount)),
						TInputDataVertex<FEnumPanLaw>(METASOUND_GET_PARAM_NAME_AND_METADATA(PanLaw), (int32)EPanLaw::Law_EqualPower)
						),
					FOutputVertexInterface(
						TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputLeft)),
						TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputRight)))
				);
			return Interface;
		}
		
		
		static TUniquePtr<IOperator> CreateOperator(const FBuildOperatorParams& InParams, FBuildResults& OutResults)
		{
			
			using namespace PannerVertexNames;
			
			const FInputVertexInterfaceData& InputData = InParams.InputData;
			
			FAudioBufferReadRef AudioIn = InputData.GetOrCreateDefaultDataReadReference<FAudioBuffer>(
				METASOUND_GET_PARAM_NAME(InputAudio),
				InParams.OperatorSettings);
			
			FFloatReadRef PanRateIn = InputData.GetOrCreateDefaultDataReadReference<float>(
				METASOUND_GET_PARAM_NAME(PanAmount),
				InParams.OperatorSettings);
			
			FEnumPanLawReadRef PanningLawIn = InputData.GetOrCreateDefaultDataReadReference<FEnumPanLaw>(
				METASOUND_GET_PARAM_NAME(PanLaw),
				InParams.OperatorSettings);
			
			return MakeUnique<FPannerPanOperator>(InParams, AudioIn, PanRateIn, PanningLawIn);
		}
		
		void Reset(const IOperator::FResetParams& InParams)
		{
			AudioLeft->Zero();
			AudioRight->Zero();
			
			LastPan = 0.0f;
			
			bIsInitialized = false;
			SmoothedLeft = 0.0f;
			SmoothedRight = 0.0f;
			SampleRate = InParams.OperatorSettings.GetSampleRate();
			
			
			constexpr float StartFade = (5.0f / 1000.f);
			FadeTotalSamples = FMath::Max(1, static_cast<int32>(StartFade) * SampleRate);
			FadeRemaining = FadeTotalSamples;
		}
		
		//DSP Calculation
		void Execute()
		{
			const int32 NumFrames = AudioInput->Num();
			
			AudioLeft->Zero();
			AudioRight->Zero();
			
			if (AudioLeft->Num() != NumFrames || AudioRight->Num() != NumFrames)
			{
				return;
			}
			
			const float* InData   = AudioInput->GetData();
			float* LeftData       = AudioLeft->GetData();
			float* RightData      = AudioRight->GetData();
			
			const float CurrentPan = FMath::Clamp(*PanningAmount, -1.f, 1.f);
			
			const float StartPan = LastPan;
			const float EndPan = CurrentPan;
			
			//Map
			const float TStart = FMath::Clamp(0.5f * (StartPan + 1.f), 0.f, 1.f);
			const float TEnd = FMath::Clamp(0.5f * (EndPan   + 1.f), 0.f, 1.f);
			
			//LAw
			const EPanLaw Law = *PanningLaw;
			
			float LeftGainStart = 1.f, RightGainStart = 1.f;
			float LeftGainEnd   = 1.f, RightGainEnd   = 1.f;
			ComputeGains(TStart, Law, LeftGainStart, RightGainStart);
			ComputeGains(TEnd,   Law, LeftGainEnd,   RightGainEnd);
			
			if (!bIsInitialized)
			{
				SmoothedLeft = LeftGainEnd;
				SmoothedRight = RightGainEnd;
				bIsInitialized = true;
			}
			
			const bool bGainsEqual = FMath::IsNearlyEqual(LeftGainStart, LeftGainEnd, KINDA_SMALL_NUMBER)
						&& FMath::IsNearlyEqual(RightGainStart, RightGainEnd, KINDA_SMALL_NUMBER);
			
			const bool bUseFade = FadeRemaining > 0;
			
			int32 LocalFadeRemaining = FadeRemaining;
			
			if (bGainsEqual)
			{
				const float LeftGain = LeftGainStart;
				const float RightGain = RightGainStart;

				for (int32 i = 0; i < NumFrames; ++i)
				{
					const float InSample = InData[i];
					LeftData[i]  = InSample * LeftGain;
					RightData[i] = InSample * RightGain;
				}
			}
			else
			{
				// Per-sample linear interpolation of gains (this avoids derivative discontinuities)
				for (int32 i = 0; i < NumFrames; ++i)
				{
					const float Alpha = (NumFrames > 1) ? (static_cast<float>(i) / static_cast<float>(NumFrames - 1)) : 0.f;

					const float LeftGain = FMath::Lerp(LeftGainStart, LeftGainEnd, Alpha);
					const float RightGain = FMath::Lerp(RightGainStart, RightGainEnd, Alpha);

					const float InSample = InData[i];
					LeftData[i]  = InSample * LeftGain;
					RightData[i] = InSample * RightGain;
					
					if (LocalFadeRemaining > 0)
					{
						--LocalFadeRemaining;
					}
				}
			}

			// Save end value for next block smoothing
			LastPan = CurrentPan;
			
			
		}
		
	private:
		FAudioBufferReadRef								AudioInput;
		FFloatReadRef									PanningAmount;
		FEnumPanLawReadRef								PanningLaw;
		
		FAudioBufferWriteRef							AudioLeft;
		FAudioBufferWriteRef							AudioRight;
		
		float											SampleRate;
		float											LastPan;
		float											SmoothedLeft;
		float											SmoothedRight;
		
		bool											bIsInitialized;
		
		int32											FadeTotalSamples = { 0 };
		int32											FadeRemaining = { 0 };
		
	};
	
	class FPannerPanNode : public FNodeFacade
	{
	public:
		FPannerPanNode(const FNodeInitData& InitData)
			: FNodeFacade(InitData.InstanceName, InitData.InstanceID, TFacadeOperatorClass<FPannerPanOperator>())
		{}
	};
	
	METASOUND_REGISTER_NODE(FPannerPanNode);
	
}


