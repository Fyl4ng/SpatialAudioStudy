// Copyright Epic Games, Inc.

#include "ITDPannerSomNode.h"
#include "Internationalization/Text.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundParamHelper.h"
#include "MetasoundPrimitives.h"
#include "MetasoundAudioBuffer.h"
#include "MetasoundFacade.h"
#include "MetasoundStandardNodesCategories.h"
#include "DSP/Delay.h"


#define LOCTEXT_NAMESPACE "MetasoundStandardNodes_ITDPanner"

namespace Metasound
{
    namespace ITDPannerVertexNames
    {
        METASOUND_PARAM(InputAudio,          "In",              "Mono input audio.")
        METASOUND_PARAM(InputAngle,          "Angle",           "Angle in degrees (0 = right, 90 = front, 180 = left, 270 = back).")
        METASOUND_PARAM(InputDistanceFactor, "Distance Factor", "0 = near (full ITD stereo), 1 = far (crossfades to mono). I need to do the graph volume in Metasound graph ?")
        METASOUND_PARAM(InputHeadWidth,      "Head Width",      "Head width in centimeters. Controls the ITD magnitude. Default 34cm.")
        METASOUND_PARAM(OutputLeft,          "Out Left",        "Left output.")
        METASOUND_PARAM(OutputRight,         "Out Right",       "Right output.")
    }
	
    class FITDPannerOperator final : public TExecutableOperator<FITDPannerOperator>
    {
    public:
    	
        static const FNodeClassMetadata& GetNodeInfo()
        {
            auto InitNodeInfo = []() -> FNodeClassMetadata
            {
                FNodeClassMetadata Info;
                Info.ClassName        = { TEXT("UE"), TEXT("Personal ITD Panner"), TEXT("Audio") };
                Info.MajorVersion     = 1;
                Info.MinorVersion     = 2;
                Info.DisplayName      = LOCTEXT("DisplayName", "Personal ITD Panner");
                Info.Description      = LOCTEXT("Description",
                    "Physically-based ITD stereo panner. Delays each ear based on the "
                    "distance from the source to each ear position. ");
                Info.Author           = PluginAuthor;
                Info.PromptIfMissing  = PluginNodeMissingPrompt;
                Info.DefaultInterface = GetVertexInterface();
                Info.CategoryHierarchy.Emplace(NodeCategories::Spatialization);
                return Info;
            };

            static const FNodeClassMetadata Info = InitNodeInfo();
            return Info;
        }
    	
        static const FVertexInterface& GetVertexInterface()
        {
            using namespace ITDPannerVertexNames;

            static const FVertexInterface Interface(
                FInputVertexInterface(
                    TInputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputAudio)),
                    TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputAngle),          90.f),
                    TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputDistanceFactor), 0.f),
                    TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputHeadWidth),      34.f)
                ),
                FOutputVertexInterface(
                    TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputLeft)),
                    TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputRight))
                )
            );

            return Interface;
        }
    	
        static TUniquePtr<IOperator> CreateOperator(const FBuildOperatorParams& InParams, FBuildResults& /*OutResults*/)
        {
            using namespace ITDPannerVertexNames;
            const FInputVertexInterfaceData& InputData = InParams.InputData;

            return MakeUnique<FITDPannerOperator>(
                InParams,
                InputData.GetOrCreateDefaultDataReadReference<FAudioBuffer>(METASOUND_GET_PARAM_NAME(InputAudio),          InParams.OperatorSettings),
                InputData.GetOrCreateDefaultDataReadReference<float>       (METASOUND_GET_PARAM_NAME(InputAngle),          InParams.OperatorSettings),
                InputData.GetOrCreateDefaultDataReadReference<float>       (METASOUND_GET_PARAM_NAME(InputDistanceFactor), InParams.OperatorSettings),
                InputData.GetOrCreateDefaultDataReadReference<float>       (METASOUND_GET_PARAM_NAME(InputHeadWidth),      InParams.OperatorSettings)
            );
        }
    	
        FITDPannerOperator(
            const FBuildOperatorParams& InParams,
            const FAudioBufferReadRef&  InAudio,
            const FFloatReadRef&        InAngle,
            const FFloatReadRef&        InDistance,
            const FFloatReadRef&        InHeadWidth)
            : AudioInput(InAudio)
            , Angle(InAngle)
            , DistanceFactor(InDistance)
            , HeadWidth(InHeadWidth)
            , AudioLeft (FAudioBufferWriteRef::CreateNew(InParams.OperatorSettings))
            , AudioRight(FAudioBufferWriteRef::CreateNew(InParams.OperatorSettings))
        {
            Reset(InParams);
        }
    	
        void Reset(const FResetParams& InParams)
        {
            SampleRate = InParams.OperatorSettings.GetSampleRate();

            // Max delay covers the absolute travel time to the far ear, not just the
            // delta between ears. With a 34cm head the far-ear absolute delay is
            // roughly (1m / 343 m/s) ≈ 3ms. 20ms gives plenty of headroom.
            LeftDelay.Init(SampleRate, 0.02f);
            RightDelay.Init(SampleRate, 0.02f);

        	//Try impleting it on Execute Function.
        	//Tau is used in Phyhsics to calculate how fast a curve can reach the source,
        	//So can apply Tau in DSP to smooth the change instead of jumping since this stuff is clicking when I move input
            constexpr float EaseTau = 0.008f;
            const float Ease = Audio::FExponentialEase::GetFactorForTau(EaseTau, SampleRate);
            LeftDelay.SetEaseFactor(Ease);
            RightDelay.SetEaseFactor(Ease); //I misunderstood how Reset works, have a look at Reset() function and do logs to see what is happening.
        	
        	//Compare to unreal 

            // Seed cached values and force a full recalc on the first Execute().
            CurrAngle     = *Angle;
            CurrDistance  = *DistanceFactor;
            CurrHeadWidth = *HeadWidth;
            UpdateParams(/*bIsInit=*/true); //I remember why I have this paramater here more inside the function
        }
    	
        virtual void BindInputs(FInputVertexInterfaceData& InOutVertexData) override
        {
            using namespace ITDPannerVertexNames;
            InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InputAudio),          AudioInput);
            InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InputAngle),          Angle);
            InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InputDistanceFactor), DistanceFactor);
            InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InputHeadWidth),      HeadWidth);
        }

        virtual void BindOutputs(FOutputVertexInterfaceData& InOutVertexData) override
        {
            using namespace ITDPannerVertexNames;
            InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(OutputLeft),  AudioLeft);
            InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(OutputRight), AudioRight);
        }
    	
        void Execute()
        {
            const int32  NumSamples = AudioInput->Num();
            const float* InBuffer   = AudioInput->GetData();
            float*       OutL       = AudioLeft->GetData();
            float*       OutR       = AudioRight->GetData();

            // Only recompute ITD geometry when a parameter has actually changed.
            if (!FMath::IsNearlyEqual(*Angle,          CurrAngle)    ||
                !FMath::IsNearlyEqual(*DistanceFactor, CurrDistance) ||
                !FMath::IsNearlyEqual(*HeadWidth,      CurrHeadWidth))
            {
                UpdateParams(/*bIsInit=*/false);
            }
        	
            // Run the mono signal through each ear's delay line.
            // Audio::FDelay handles the circular buffer and fractional-sample
            // interpolation internally.
            for (int32 i = 0; i < NumSamples; ++i)
            {
                OutL[i] = LeftDelay.ProcessAudioSample(InBuffer[i]);
                OutR[i] = RightDelay.ProcessAudioSample(InBuffer[i]);
            }
        	
            // At Distance = 0 the ITD stereo output passes through unchanged.
            // At Distance = 1 both channels fade to the raw mono signal 
            // Volume rolloff is handled separately in the MetaSound graph.
            // Skip the loop entirely when Distance= 0
            /*if (const float DistBlend = FMath::Clamp(CurrDistance, 0.f, 1.f); DistBlend > KINDA_SMALL_NUMBER)
            {*/
        		const float DistBlend = FMath::Clamp(CurrDistance, 0.f, 1.f);
                const float WetGain = 1.f - DistBlend;
                const float DryGain = DistBlend;

                for (int32 i = 0; i < NumSamples; ++i)
                {
                    // Raw mono at -6dB per channel keeps summed loudness consistent.
                    const float Mono = InBuffer[i] * 0.5f;
                    OutL[i] = OutL[i] * WetGain + Mono * DryGain;
                    OutR[i] = OutR[i] * WetGain + Mono * DryGain;
                }
            /*}*/
        }

    private:
    	
        void UpdateParams(bool bIsInit)
        {
            CurrAngle     = FMath::Clamp(*Angle,          0.f, 360.f);
            CurrDistance  = FMath::Clamp(*DistanceFactor, 0.f, 1.f);
            CurrHeadWidth = FMath::Max  (*HeadWidth,      0.f);
        	
            // 0°=right, 90°=front, 180°=left, 270°=back
            const float Radians = FMath::DegreesToRadians(CurrAngle);
            float X, Y;
            FMath::SinCos(&Y, &X, Radians);

            // HeadWidth is a diameter in cm → convert to radius in metres.
            const float HeadRadius = (CurrHeadWidth * 0.01f) * 0.5f;

            // Straight-line distance from source to each ear (Pythagoras).
            const float DistToLeftEar  = FMath::Sqrt(Y * Y + FMath::Square(HeadRadius + X));
            const float DistToRightEar = FMath::Sqrt(Y * Y + FMath::Square(HeadRadius - X));

            // Absolute travel time from source to each ear (speed of sound = 343 m/s).
            constexpr float SpeedOfSound   = 343.f;
            const float LeftDelayMs  = (DistToLeftEar  / SpeedOfSound) * 1000.f;
            const float RightDelayMs = (DistToRightEar / SpeedOfSound) * 1000.f;

        	//This is the reason why I pass bIsInit in Update params, first time it gets called it has no concept of a 
        	//delay, so if I let it run the first block of audio was glitching at the start, so if it is true it will 
        	//easy the value causing a smooth transition.
            LeftDelay.SetEasedDelayMsec (LeftDelayMs,  bIsInit);
            RightDelay.SetEasedDelayMsec(RightDelayMs, bIsInit);
        }
    	
        FAudioBufferReadRef  AudioInput;
        FFloatReadRef        Angle;
        FFloatReadRef        DistanceFactor;
        FFloatReadRef        HeadWidth;

        FAudioBufferWriteRef AudioLeft;
        FAudioBufferWriteRef AudioRight;

        // Audio::FDelay handles the circular buffer and fractional interpolation.
        Audio::FDelay LeftDelay;
        Audio::FDelay RightDelay;

        float SampleRate = 48000.f;

        // Cached for the check in Execute
        float CurrAngle     = 0.f;
        float CurrDistance  = 0.f;
        float CurrHeadWidth = 0.f;
    };
	
    using FITDPannerNode = TNodeFacade<FITDPannerOperator>;
    METASOUND_REGISTER_NODE(FITDPannerNode)

} // namespace Metasound

#undef LOCTEXT_NAMESPACE


/* #include "Internationalization/Text.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundDataTypeRegistrationMacro.h"
#include "MetasoundParamHelper.h"
#include "MetasoundPrimitives.h"
#include "MetasoundStandardNodesNames.h"
#include "MetasoundStandardNodesCategories.h"
#include "MetasoundFacade.h"
#include "MetasoundVertex.h"

#define LOCTEXT_NAMESPACE "MetasoundPlusNodes_Template"

namespace Metasound
{
	namespace TemplateVertexNames
	{
		METASOUND_PARAM(InputX, "X", "Input value X.")

		METASOUND_PARAM(OutputValue, "Output", "Output value X.")
	}

	class FTemplateOperator final : public TExecutableOperator<FTemplateOperator>
	{
	public:
		static const FNodeClassMetadata& GetNodeInfo();
		static const FVertexInterface& GetVertexInterface();
		static TUniquePtr<IOperator> CreateOperator(const FBuildOperatorParams& InParams, FBuildResults& OutResults);
		
		FTemplateOperator(const FBuildOperatorParams& InParams,
			const FFloatReadRef& InX
		);
		
		virtual void BindInputs(FInputVertexInterfaceData& InVertexData) override;
		virtual void BindOutputs(FOutputVertexInterfaceData& InVertexData) override;

		void Execute();
		void Reset(const FResetParams& InParams);

	private:
		FFloatReadRef X;

		FFloatWriteRef Output;
	};

	const FNodeClassMetadata& FTemplateOperator::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeClassMetadata
		{
			FNodeClassMetadata Info;
			Info.ClassName = { StandardNodes::Namespace, TEXT("Template"), TEXT("") };
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = METASOUND_LOCTEXT("Metasound_TemplateDisplayName", "Metasound Template");
			Info.Description = METASOUND_LOCTEXT("Metasound_TemplateNodeDescription", "Takes input, gives the same output.");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = GetVertexInterface();
			Info.CategoryHierarchy.Emplace(NodeCategories::Debug);
			return Info;
		};

		static const FNodeClassMetadata Info = InitNodeInfo();

		return Info;
	}

	const FVertexInterface& FTemplateOperator::GetVertexInterface()
	{
		using namespace TemplateVertexNames;

		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputX), 0.f)
			),
			FOutputVertexInterface(
				TOutputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputValue))
			)
		);

		return Interface;
	}

	TUniquePtr<IOperator> FTemplateOperator::CreateOperator(const FBuildOperatorParams& InParams, [[maybe_unused]] FBuildResults& OutResults)
	{
		using namespace TemplateVertexNames;

		const FInputVertexInterfaceData& InputData = InParams.InputData;
        FFloatReadRef InputX = InputData.GetOrCreateDefaultDataReadReference<float>(METASOUND_GET_PARAM_NAME(InputX), InParams.OperatorSettings);

		return MakeUnique<FTemplateOperator>(InParams, InputX);
	}

	FTemplateOperator::FTemplateOperator(const FBuildOperatorParams& InParams, const FFloatReadRef& InX)
		: X(InX)
		, Output(FFloatWriteRef::CreateNew(0.f)
		)
	{
		Reset(InParams);
	}

	void FTemplateOperator::BindInputs(FInputVertexInterfaceData& InVertexData)
	{
		using namespace TemplateVertexNames;

		InVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InputX), X);
	}

	void FTemplateOperator::BindOutputs(FOutputVertexInterfaceData& InVertexData)
	{
		using namespace TemplateVertexNames;

		InVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(OutputValue), Output);
	}

	void FTemplateOperator::Execute()
	{
		*Output = *X;
	}

	void FTemplateOperator::Reset(const FResetParams& InParams)
	{
	}

	using FTemplateNode = TNodeFacade<FTemplateOperator>;

	METASOUND_REGISTER_NODE(FTemplateNode)
}

#undef LOCTEXT_NAMESPACE */