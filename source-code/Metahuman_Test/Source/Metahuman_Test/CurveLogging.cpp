#include "CurveLogging.h"
#include "FacialTherapyApi.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimCurveTypes.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "HAL/PlatformFileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundBase.h"

namespace
{
	constexpr float AccuracyPauseThresholdPercent = 70.f;
	constexpr float AccuracyPauseDelaySeconds = 2.f;
	constexpr float AccuracyPauseDurationSeconds = 15.f;
	constexpr float WarmupStageSeconds = 30.f;
	constexpr float IntroductionStageSeconds = 5.f;
	constexpr float RepetitionStageSeconds = 15.f;
	constexpr float AccuracyScoringStartDelaySeconds = 2.f;
	constexpr float AccuracyScoringFreezeLeadSeconds = 5.f;

	enum class EExerciseUiStage : uint8
	{
		Idle,
		Warmup,
		Introduction,
		Demonstration,
		Phase2Introduction,
		Phase2Demonstration,
		Phase3Introduction,
		Phase3Demonstration,
		Phase4Introduction,
		Phase4Demonstration,
		RepetitionOne,
		RepetitionTwo,
		RepetitionThree,
		Feedback,
		Completed
	};

	FString GetStageLabel(EExerciseUiStage Stage);

	bool bPauseTriggeredForCurrentExercise = false;
	bool bPhase1ProgressSentForCurrentExercise = false;
	bool bExercisePlaybackStarted = false;
	bool bScoringWindowInitialized = false;
	bool bHasGuidanceConfig = false;
	bool bGuidanceConfigPendingForStart = false;
	bool bLastStageActionWindowActive = false;
	bool bHoldingExercisePoseForStage = false;
	float AccumulatedAccuracyPauseSeconds = 0.f;
	float CurrentAccuracyPauseStartWorldTimeSeconds = 0.f;
	bool bFeedbackStageActive = false;
	float FeedbackStageStartWorldTimeSeconds = 0.f;
	float FeedbackStageDurationSeconds = 0.f;
	TWeakObjectPtr<USkeletalMeshComponent> ActiveGuidanceBodyMesh = nullptr;
	TWeakObjectPtr<UAudioComponent> ActiveStageAudioComponent = nullptr;
	FString ActiveExerciseMode = TEXT("single");
	TArray<FString> RecentMontageDiagnostics;
	bool bExerciseManuallyPaused = false;
	float ManualExercisePauseStartWorldTimeSeconds = 0.f;
	float ExerciseFlowStartWorldTimeSeconds = 0.f;
	float ExercisePlaybackStartMontageTimeSeconds = 0.f;
	float LastCompletedAverageAccuracyPercent = 0.f;
	EExerciseUiStage LastAnnouncedStage = EExerciseUiStage::Idle;
	FCurveExerciseGuidanceConfig ActiveGuidanceConfig;

	enum class ESelectedPhaseStepType : uint8
	{
		Warmup,
		Introduction,
		Demonstration,
		Hold,
		Rest,
		Feedback
	};

	struct FSelectedPhaseStep
	{
		ESelectedPhaseStepType Type = ESelectedPhaseStepType::Rest;
		EExerciseUiStage GuidanceStage = EExerciseUiStage::Idle;
		FString Label;
		FString Note;
		FString ExpressionName;
		TWeakObjectPtr<UAnimMontage> Montage;
		float ActionDurationSeconds = 0.f;
		bool bScored = false;
		bool bPlayGuidance = false;
	};

	TMap<FString, TWeakObjectPtr<UAnimMontage>> ConfiguredExpressionMontages;
	TArray<FSelectedPhaseStep> SelectedPhaseSteps;
	bool bSelectedPhaseExerciseActive = false;
	bool bSelectedPhaseExerciseHasState = false;
	bool bSelectedPhaseStepStarted = false;
	bool bSelectedActionStarted = false;
	bool bSelectedHoldScoringInitialized = false;
	bool bSelectedProgressSentForCurrentExercise = false;
	bool bSelectedCompletionRecorded = false;
	int32 SelectedPhaseStepIndex = INDEX_NONE;
	int32 SelectedItemId = 0;
	int32 SelectedPairNumber = 0;
	FString SelectedSequenceCode;
	FString SelectedItemTitle;
	FString SelectedItemType;
	FString SelectedStatusText;
	float SelectedStepStartWorldTimeSeconds = 0.f;
	float SelectedScoredTimelineBaseSeconds = 0.f;

	void ResetSelectedPhaseRuntimeState()
	{
		SelectedPhaseSteps.Reset();
		bSelectedPhaseExerciseActive = false;
		bSelectedPhaseExerciseHasState = false;
		bSelectedPhaseStepStarted = false;
		bSelectedActionStarted = false;
		bSelectedHoldScoringInitialized = false;
		bSelectedProgressSentForCurrentExercise = false;
		bSelectedCompletionRecorded = false;
		SelectedPhaseStepIndex = INDEX_NONE;
		SelectedItemId = 0;
		SelectedPairNumber = 0;
		SelectedSequenceCode.Empty();
		SelectedItemTitle.Empty();
		SelectedItemType.Empty();
		SelectedStatusText.Empty();
		SelectedStepStartWorldTimeSeconds = 0.f;
		SelectedScoredTimelineBaseSeconds = 0.f;
	}

	FString NormalizeExpressionKey(const FString& InExpressionName)
	{
		FString Key = InExpressionName.ToLower();
		Key.ReplaceInline(TEXT(" "), TEXT(""));
		Key.ReplaceInline(TEXT("+"), TEXT(""));
		Key.ReplaceInline(TEXT("_"), TEXT(""));
		Key.ReplaceInline(TEXT("-"), TEXT(""));
		return Key;
	}

	void StoreExpressionMontage(const FString& ExpressionName, UAnimMontage* Montage)
	{
		if (Montage)
		{
			ConfiguredExpressionMontages.Add(NormalizeExpressionKey(ExpressionName), Montage);
		}
	}

	void StoreExpressionMontageWithAliases(const FString& ExpressionName, UAnimMontage* Montage)
	{
		StoreExpressionMontage(ExpressionName, Montage);
		const FString Key = NormalizeExpressionKey(ExpressionName);
		if (Key == TEXT("surprise") || Key == TEXT("surprised"))
		{
			StoreExpressionMontage(TEXT("Surprise"), Montage);
			StoreExpressionMontage(TEXT("Surprised"), Montage);
		}
		else if (Key == TEXT("sad") || Key == TEXT("sadness"))
		{
			StoreExpressionMontage(TEXT("Sad"), Montage);
			StoreExpressionMontage(TEXT("Sadness"), Montage);
		}
		else if (Key == TEXT("puffcheeks"))
		{
			StoreExpressionMontage(TEXT("Puffcheeks"), Montage);
			StoreExpressionMontage(TEXT("Puff Cheeks"), Montage);
		}
		else if (Key == TEXT("smicheeks") || Key == TEXT("smilecheeks"))
		{
			StoreExpressionMontage(TEXT("Smicheeks"), Montage);
			StoreExpressionMontage(TEXT("Smile Cheeks"), Montage);
			StoreExpressionMontage(TEXT("Smile + Cheeks"), Montage);
		}
	}

	UAnimMontage* GetConfiguredExpressionMontage(const FString& ExpressionName)
	{
		if (ExpressionName.Equals(TEXT("Neutral"), ESearchCase::IgnoreCase))
		{
			return nullptr;
		}
		if (const TWeakObjectPtr<UAnimMontage>* Montage = ConfiguredExpressionMontages.Find(NormalizeExpressionKey(ExpressionName)))
		{
			return Montage->Get();
		}
		return nullptr;
	}

	UAnimMontage* FindFirstMontageInTableRow(const UScriptStruct* RowStruct, const uint8* RowData)
	{
		if (!RowStruct || !RowData)
		{
			return nullptr;
		}
		for (TFieldIterator<FObjectPropertyBase> PropertyIt(RowStruct); PropertyIt; ++PropertyIt)
		{
			const FObjectPropertyBase* ObjectProperty = *PropertyIt;
			if (ObjectProperty && ObjectProperty->PropertyClass && ObjectProperty->PropertyClass->IsChildOf(UAnimMontage::StaticClass()))
			{
				if (UAnimMontage* Montage = Cast<UAnimMontage>(ObjectProperty->GetObjectPropertyValue_InContainer(RowData)))
				{
					return Montage;
				}
			}
		}

		for (TFieldIterator<FSoftObjectProperty> PropertyIt(RowStruct); PropertyIt; ++PropertyIt)
		{
			const FSoftObjectProperty* SoftObjectProperty = *PropertyIt;
			if (!SoftObjectProperty || !SoftObjectProperty->PropertyClass || !SoftObjectProperty->PropertyClass->IsChildOf(UAnimMontage::StaticClass()))
			{
				continue;
			}

			const FSoftObjectPtr SoftObjectValue = SoftObjectProperty->GetPropertyValue_InContainer(RowData);
			if (UObject* LoadedObject = SoftObjectValue.LoadSynchronous())
			{
				if (UAnimMontage* Montage = Cast<UAnimMontage>(LoadedObject))
				{
					return Montage;
				}
			}
		}

		return nullptr;
	}

	FString NormalizePhase1ExerciseKey(const FString& InExpressionName)
	{
		FString Key = InExpressionName.ToLower();
		Key.ReplaceInline(TEXT(" "), TEXT(""));
		Key.ReplaceInline(TEXT("+"), TEXT(""));
		Key.ReplaceInline(TEXT("_"), TEXT(""));
		return Key;
	}

	int32 GetPhase1ExerciseId(const FString& InExpressionName)
	{
		const FString Key = NormalizePhase1ExerciseKey(InExpressionName);
		if (Key == TEXT("angry")) return 1;
		if (Key == TEXT("surprise") || Key == TEXT("surprised")) return 2;
		if (Key == TEXT("disgust")) return 3;
		if (Key == TEXT("fear")) return 4;
		if (Key == TEXT("smile")) return 5;
		if (Key == TEXT("sad") || Key == TEXT("sadness")) return 6;
		if (Key == TEXT("pout")) return 7;
		if (Key == TEXT("puffcheeks")) return 8;
		if (Key == TEXT("smilecheeks") || Key == TEXT("smicheeks")) return 9;
		return 0;
	}

	bool IsGuidedExerciseMode()
	{
		return bHasGuidanceConfig || UFacialTherapyApi::GetStoredModeType() == EFacialTherapySessionMode::GuidedMode;
	}

	const FCurveExerciseStageGuidance* GetGuidanceForStage(EExerciseUiStage Stage)
	{
		if (!bHasGuidanceConfig)
		{
			return nullptr;
		}

		switch (Stage)
		{
		case EExerciseUiStage::Warmup:
			return &ActiveGuidanceConfig.Warmup;
		case EExerciseUiStage::Introduction:
			return &ActiveGuidanceConfig.Introduction;
		case EExerciseUiStage::Demonstration:
			return &ActiveGuidanceConfig.Demonstration;
		case EExerciseUiStage::Phase2Introduction:
			return &ActiveGuidanceConfig.Phase2Introduction;
		case EExerciseUiStage::Phase2Demonstration:
			return &ActiveGuidanceConfig.Phase2Demonstration;
		case EExerciseUiStage::Phase3Introduction:
			return &ActiveGuidanceConfig.Phase3Introduction;
		case EExerciseUiStage::Phase3Demonstration:
			return &ActiveGuidanceConfig.Phase3Demonstration;
		case EExerciseUiStage::Phase4Introduction:
			return &ActiveGuidanceConfig.Phase4Introduction;
		case EExerciseUiStage::Phase4Demonstration:
			return &ActiveGuidanceConfig.Phase4Demonstration;
		case EExerciseUiStage::RepetitionOne:
			return &ActiveGuidanceConfig.RepetitionOne;
		case EExerciseUiStage::RepetitionTwo:
			return &ActiveGuidanceConfig.RepetitionTwo;
		case EExerciseUiStage::RepetitionThree:
			return &ActiveGuidanceConfig.RepetitionThree;
		default:
			return nullptr;
		}
	}

	const FCurveExerciseStageGuidance* GetFeedbackGuidanceForAccuracy(float AverageAccuracyPercent)
	{
		if (!bHasGuidanceConfig)
		{
			return nullptr;
		}

		if (AverageAccuracyPercent >= 85.f)
		{
			return &ActiveGuidanceConfig.FeedbackGood;
		}
		if (AverageAccuracyPercent >= 70.f)
		{
			return &ActiveGuidanceConfig.FeedbackMedium;
		}
		return &ActiveGuidanceConfig.FeedbackLow;
	}

	FString DescribeMontageSlots(UAnimMontage* Montage)
	{
		if (!Montage)
		{
			return TEXT("none");
		}

		TArray<FString> SlotNames;
		for (const FSlotAnimationTrack& SlotTrack : Montage->SlotAnimTracks)
		{
			SlotNames.Add(SlotTrack.SlotName.ToString());
		}

		return SlotNames.Num() > 0 ? FString::Join(SlotNames, TEXT(",")) : TEXT("none");
	}

	FString DescribeMeshIdentity(USkeletalMeshComponent* Mesh)
	{
		if (!Mesh)
		{
			return TEXT("mesh=none");
		}

		AActor* Owner = Mesh->GetOwner();
		const FVector Location = Mesh->GetComponentLocation();
		const FString AnimMode = StaticEnum<EAnimationMode::Type>()
			? StaticEnum<EAnimationMode::Type>()->GetNameStringByValue(static_cast<int64>(Mesh->GetAnimationMode()))
			: FString::Printf(TEXT("%d"), static_cast<int32>(Mesh->GetAnimationMode()));

		return FString::Printf(
			TEXT("mesh=%s owner=%s path=%s skeletal=%s anim_mode=%s hidden=%s visible=%s loc=(%.1f,%.1f,%.1f)"),
			*Mesh->GetName(),
			Owner ? *Owner->GetName() : TEXT("none"),
			*Mesh->GetPathName(),
			Mesh->GetSkeletalMeshAsset() ? *Mesh->GetSkeletalMeshAsset()->GetName() : TEXT("none"),
			*AnimMode,
			Mesh->bHiddenInGame ? TEXT("true") : TEXT("false"),
			Mesh->IsVisible() ? TEXT("true") : TEXT("false"),
			Location.X,
			Location.Y,
			Location.Z);
	}

	void RecordMontageDiagnostic(const TCHAR* Context, USkeletalMeshComponent* Mesh, UAnimMontage* Montage, float PlayResultSeconds, bool bPlayingAfterCall)
	{
		UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
		const float MontagePosition = (AnimInstance && Montage) ? AnimInstance->Montage_GetPosition(Montage) : 0.f;
		const FString Line = FString::Printf(
			TEXT("%s | %s registered=%s anim=%s montage=%s slots=%s length=%.2f play_result=%.3f playing=%s pos=%.3f"),
			Context ? Context : TEXT("montage"),
			*DescribeMeshIdentity(Mesh),
			(Mesh && Mesh->IsRegistered()) ? TEXT("true") : TEXT("false"),
			AnimInstance ? *AnimInstance->GetClass()->GetName() : TEXT("none"),
			Montage ? *Montage->GetName() : TEXT("none"),
			*DescribeMontageSlots(Montage),
			Montage ? Montage->GetPlayLength() : 0.f,
			PlayResultSeconds,
			bPlayingAfterCall ? TEXT("true") : TEXT("false"),
			MontagePosition);

		RecentMontageDiagnostics.Add(Line);
		constexpr int32 MaxMontageDiagnosticLines = 10;
		while (RecentMontageDiagnostics.Num() > MaxMontageDiagnosticLines)
		{
			RecentMontageDiagnostics.RemoveAt(0);
		}

		UE_LOG(LogTemp, Warning, TEXT("CurveLogging montage diagnostic: %s"), *Line);
	}

	float ClampAccuracyPercent(float AverageAccuracyPercent)
	{
		return FMath::Clamp(AverageAccuracyPercent, 0.f, 100.f);
	}

	FString BuildExerciseFeedbackTitle(float AverageAccuracyPercent)
	{
		const float ClampedAccuracyPercent = ClampAccuracyPercent(AverageAccuracyPercent);
		if (ClampedAccuracyPercent >= 85.f)
		{
			return TEXT("Great job!");
		}
		if (ClampedAccuracyPercent >= 70.f)
		{
			return TEXT("Good work!");
		}
		return TEXT("Almost there");
	}

	FString BuildExerciseFeedbackMessage(float AverageAccuracyPercent)
	{
		const int32 RoundedAccuracyPercent = FMath::RoundToInt(ClampAccuracyPercent(AverageAccuracyPercent));
		if (RoundedAccuracyPercent >= 85)
		{
			return FString::Printf(
				TEXT("You did a good job matching the expression. Your average accuracy was %d%%. Keep the same smooth, steady movement as you continue to the next exercise."),
				RoundedAccuracyPercent);
		}
		if (RoundedAccuracyPercent >= 70)
		{
			return FString::Printf(
				TEXT("You are close. Your average accuracy was %d%%. Keep the movement steady, match the expression more precisely, and hold it a little longer on the next try."),
				RoundedAccuracyPercent);
		}
		return FString::Printf(
			TEXT("Give it another try. Your average accuracy was %d%%. Focus on matching the therapist more closely and holding the movement steady until the expression is fully formed."),
			RoundedAccuracyPercent);
	}

	void PlayGuidanceAudioIfNeeded(USkeletalMeshComponent* Mesh, EExerciseUiStage Stage)
	{
		if (!IsGuidedExerciseMode())
		{
			return;
		}

		const FCurveExerciseStageGuidance* Guidance = GetGuidanceForStage(Stage);
		if (!Guidance || !Guidance->Audio || !Mesh)
		{
			return;
		}

		if (UWorld* World = Mesh->GetWorld())
		{
			if (UAudioComponent* AudioComponent = ActiveStageAudioComponent.Get())
			{
				AudioComponent->Stop();
			}

			ActiveStageAudioComponent = UGameplayStatics::SpawnSound2D(World, Guidance->Audio);
		}
	}

	void PlayMontageOnMeshIfNeeded(USkeletalMeshComponent* Mesh, UAnimMontage* Montage, const TCHAR* Context = TEXT("montage_play"))
	{
		if (!Mesh || !Montage)
		{
			RecordMontageDiagnostic(Context, Mesh, Montage, 0.f, false);
			return;
		}

		if (UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
		{
			const float PlayResultSeconds = AnimInstance->Montage_Play(Montage);
			RecordMontageDiagnostic(Context, Mesh, Montage, PlayResultSeconds, AnimInstance->Montage_IsPlaying(Montage));
			return;
		}

		RecordMontageDiagnostic(Context, Mesh, Montage, 0.f, false);
	}

	void ShowFrozenMontagePose(USkeletalMeshComponent* Mesh, UAnimMontage* Montage)
	{
		if (!Mesh || !Montage)
		{
			RecordMontageDiagnostic(TEXT("frozen_pose_missing"), Mesh, Montage, 0.f, false);
			return;
		}

		if (UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
		{
			const float PlayResultSeconds = AnimInstance->Montage_Play(Montage, 1.f);
			RecordMontageDiagnostic(TEXT("frozen_pose_play"), Mesh, Montage, PlayResultSeconds, AnimInstance->Montage_IsPlaying(Montage));
			const float HoldPoseSeconds = FMath::Clamp(Montage->GetPlayLength() * 0.5f, 0.f, Montage->GetPlayLength());
			AnimInstance->Montage_SetPosition(Montage, HoldPoseSeconds);
			AnimInstance->Montage_Pause(Montage);
			RecordMontageDiagnostic(TEXT("frozen_pose_paused"), Mesh, Montage, PlayResultSeconds, AnimInstance->Montage_IsPlaying(Montage));
			return;
		}

		RecordMontageDiagnostic(TEXT("frozen_pose_no_anim_instance"), Mesh, Montage, 0.f, false);
	}

	void StopMontageOnMeshIfNeeded(USkeletalMeshComponent* Mesh, UAnimMontage* Montage)
	{
		if (!Mesh || !Montage)
		{
			return;
		}

		if (UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
		{
			AnimInstance->Montage_Stop(0.1f, Montage);
		}
	}

	void PauseAllMontagesOnMesh(USkeletalMeshComponent* Mesh)
	{
		if (!Mesh)
		{
			return;
		}

		if (UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
		{
			AnimInstance->Montage_Pause(nullptr);
		}
	}

	void ResumeAllMontagesOnMesh(USkeletalMeshComponent* Mesh)
	{
		if (!Mesh)
		{
			return;
		}

		if (UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
		{
			AnimInstance->Montage_Resume(nullptr);
		}
	}

	void PlayGuidanceMontageIfNeeded(USkeletalMeshComponent* Mesh, EExerciseUiStage Stage)
	{
		if (!IsGuidedExerciseMode())
		{
			return;
		}

		const FCurveExerciseStageGuidance* Guidance = GetGuidanceForStage(Stage);
		if (!Guidance || !Guidance->FaceMontage || !Mesh)
		{
			return;
		}

		if (Stage != EExerciseUiStage::Completed)
		{
			const FString Context = FString::Printf(TEXT("guidance_%s"), *GetStageLabel(Stage));
			PlayMontageOnMeshIfNeeded(Mesh, Guidance->FaceMontage, *Context);
			PlayMontageOnMeshIfNeeded(ActiveGuidanceBodyMesh.Get(), Guidance->FaceMontage, *Context);
		}
	}

	void StopGuidanceMontageIfNeeded(USkeletalMeshComponent* Mesh, EExerciseUiStage Stage)
	{
		if (!IsGuidedExerciseMode())
		{
			return;
		}

		const FCurveExerciseStageGuidance* Guidance = GetGuidanceForStage(Stage);
		if (!Guidance || !Guidance->FaceMontage)
		{
			return;
		}

		StopMontageOnMeshIfNeeded(Mesh, Guidance->FaceMontage);
		StopMontageOnMeshIfNeeded(ActiveGuidanceBodyMesh.Get(), Guidance->FaceMontage);
	}

	bool IsScoredStage(EExerciseUiStage Stage)
	{
		return Stage == EExerciseUiStage::RepetitionOne
			|| Stage == EExerciseUiStage::RepetitionTwo
			|| Stage == EExerciseUiStage::RepetitionThree;
	}

	bool UsesExerciseMontage(EExerciseUiStage Stage)
	{
		return Stage == EExerciseUiStage::Demonstration || IsScoredStage(Stage);
	}

	float GetStageDialogueDuration(EExerciseUiStage Stage)
	{
		if (!IsGuidedExerciseMode())
		{
			return 0.f;
		}

		const FCurveExerciseStageGuidance* Guidance = GetGuidanceForStage(Stage);
		if (!Guidance)
		{
			return 0.f;
		}

		const float MontageSeconds = Guidance->FaceMontage ? Guidance->FaceMontage->GetPlayLength() : 0.f;
		const float AudioSeconds = Guidance->Audio ? Guidance->Audio->GetDuration() : 0.f;
		return FMath::Max(MontageSeconds, AudioSeconds);
	}

	float GetRepetitionDuration(float MontageLengthSeconds)
	{
		return RepetitionStageSeconds;
	}

	float GetStageActionDuration(EExerciseUiStage Stage, float MontageLengthSeconds)
	{
		switch (Stage)
		{
		case EExerciseUiStage::Warmup:
			return WarmupStageSeconds;
		case EExerciseUiStage::Introduction:
			return IntroductionStageSeconds;
		case EExerciseUiStage::Demonstration:
			return FMath::Max(MontageLengthSeconds, 0.f);
		case EExerciseUiStage::RepetitionOne:
		case EExerciseUiStage::RepetitionTwo:
		case EExerciseUiStage::RepetitionThree:
			return GetRepetitionDuration(MontageLengthSeconds);
		default:
			return 0.f;
		}
	}

	float GetStageStartTime(EExerciseUiStage Stage, float MontageLengthSeconds)
	{
		float StartSeconds = 0.f;
		auto AddStage = [&](EExerciseUiStage PriorStage)
		{
			StartSeconds += GetStageDialogueDuration(PriorStage) + GetStageActionDuration(PriorStage, MontageLengthSeconds);
		};

		if (!IsGuidedExerciseMode())
		{
			switch (Stage)
			{
			case EExerciseUiStage::RepetitionOne:
				return 0.f;
			default:
				return 0.f;
			}
		}

		switch (Stage)
		{
		case EExerciseUiStage::Warmup:
			return 0.f;
		case EExerciseUiStage::Introduction:
			AddStage(EExerciseUiStage::Warmup);
			return StartSeconds;
		case EExerciseUiStage::Demonstration:
			AddStage(EExerciseUiStage::Warmup);
			AddStage(EExerciseUiStage::Introduction);
			return StartSeconds;
		case EExerciseUiStage::RepetitionOne:
			AddStage(EExerciseUiStage::Warmup);
			AddStage(EExerciseUiStage::Introduction);
			AddStage(EExerciseUiStage::Demonstration);
			return StartSeconds;
		case EExerciseUiStage::RepetitionTwo:
			AddStage(EExerciseUiStage::Warmup);
			AddStage(EExerciseUiStage::Introduction);
			AddStage(EExerciseUiStage::Demonstration);
			AddStage(EExerciseUiStage::RepetitionOne);
			return StartSeconds;
		case EExerciseUiStage::RepetitionThree:
			AddStage(EExerciseUiStage::Warmup);
			AddStage(EExerciseUiStage::Introduction);
			AddStage(EExerciseUiStage::Demonstration);
			AddStage(EExerciseUiStage::RepetitionOne);
			AddStage(EExerciseUiStage::RepetitionTwo);
			return StartSeconds;
		default:
			return 0.f;
		}
	}

	bool IsStageActionWindowActive(EExerciseUiStage Stage, float FlowElapsedSeconds, float MontageLengthSeconds)
	{
		const float ActionDuration = GetStageActionDuration(Stage, MontageLengthSeconds);
		if (ActionDuration <= KINDA_SMALL_NUMBER)
		{
			return false;
		}

		const float StageStartSeconds = GetStageStartTime(Stage, MontageLengthSeconds);
		const float DialogueDuration = GetStageDialogueDuration(Stage);
		const float StageElapsedSeconds = FMath::Max(FlowElapsedSeconds - StageStartSeconds, 0.f);
		return StageElapsedSeconds >= DialogueDuration;
	}

	float GetStageActionElapsedSeconds(EExerciseUiStage Stage, float FlowElapsedSeconds, float MontageLengthSeconds)
	{
		const float ActionDuration = GetStageActionDuration(Stage, MontageLengthSeconds);
		if (ActionDuration <= KINDA_SMALL_NUMBER)
		{
			return 0.f;
		}

		const float StageStartSeconds = GetStageStartTime(Stage, MontageLengthSeconds);
		const float DialogueDuration = GetStageDialogueDuration(Stage);
		const float StageElapsedSeconds = FMath::Max(FlowElapsedSeconds - StageStartSeconds, 0.f);
		if (StageElapsedSeconds <= DialogueDuration)
		{
			return 0.f;
		}

		return FMath::Clamp(StageElapsedSeconds - DialogueDuration, 0.f, ActionDuration);
	}

	float GetScoredTimelineSeconds(EExerciseUiStage Stage, float FlowElapsedSeconds, float MontageLengthSeconds)
	{
		const float ActionElapsedSeconds = GetStageActionElapsedSeconds(Stage, FlowElapsedSeconds, MontageLengthSeconds);
		if (!IsGuidedExerciseMode())
		{
			return ActionElapsedSeconds;
		}

		switch (Stage)
		{
		case EExerciseUiStage::RepetitionOne:
			return ActionElapsedSeconds;
		case EExerciseUiStage::RepetitionTwo:
			return RepetitionStageSeconds + ActionElapsedSeconds;
		case EExerciseUiStage::RepetitionThree:
			return (RepetitionStageSeconds * 2.f) + ActionElapsedSeconds;
		default:
			return ActionElapsedSeconds;
		}
	}

	float GetEffectiveFlowElapsedSeconds(float WorldTimeSeconds)
	{
		float EffectivePauseSeconds = AccumulatedAccuracyPauseSeconds;
		float RemainingPauseSeconds = 0.f;
		const bool bIsAccuracyPauseActive = UCurveLogging::GetTherapistPauseState(RemainingPauseSeconds) && bPauseTriggeredForCurrentExercise;
		if (bIsAccuracyPauseActive && CurrentAccuracyPauseStartWorldTimeSeconds > 0.f)
		{
			EffectivePauseSeconds += FMath::Max(WorldTimeSeconds - CurrentAccuracyPauseStartWorldTimeSeconds, 0.f);
		}
		if (bExerciseManuallyPaused && ManualExercisePauseStartWorldTimeSeconds > 0.f)
		{
			EffectivePauseSeconds += FMath::Max(WorldTimeSeconds - ManualExercisePauseStartWorldTimeSeconds, 0.f);
		}

		return FMath::Max(WorldTimeSeconds - ExerciseFlowStartWorldTimeSeconds - EffectivePauseSeconds, 0.f);
	}

	EExerciseUiStage GetBaseExerciseStage(bool bIsActive, bool bIsCompleted, float FlowElapsed, float MontageLengthSeconds)
	{
		if (bIsCompleted)
		{
			return EExerciseUiStage::Completed;
		}

		if (bFeedbackStageActive)
		{
			return EExerciseUiStage::Feedback;
		}

		if (!bIsActive)
		{
			return EExerciseUiStage::Idle;
		}

		if (!IsGuidedExerciseMode())
		{
			const EExerciseUiStage GameModeStages[] =
			{
				EExerciseUiStage::RepetitionOne
			};

			float AccumulatedSeconds = 0.f;
			for (const EExerciseUiStage Stage : GameModeStages)
			{
				AccumulatedSeconds += GetStageDialogueDuration(Stage) + GetStageActionDuration(Stage, MontageLengthSeconds);
				if (FlowElapsed < AccumulatedSeconds)
				{
					return Stage;
				}
			}

			return EExerciseUiStage::RepetitionOne;
		}

		const EExerciseUiStage OrderedStages[] =
		{
			EExerciseUiStage::Warmup,
			EExerciseUiStage::Introduction,
			EExerciseUiStage::Demonstration,
			EExerciseUiStage::RepetitionOne,
			EExerciseUiStage::RepetitionTwo,
			EExerciseUiStage::RepetitionThree
		};

		float AccumulatedSeconds = 0.f;
		for (const EExerciseUiStage Stage : OrderedStages)
		{
			AccumulatedSeconds += GetStageDialogueDuration(Stage) + GetStageActionDuration(Stage, MontageLengthSeconds);
			if (FlowElapsed < AccumulatedSeconds)
			{
				return Stage;
			}
		}

		return EExerciseUiStage::RepetitionThree;
	}

	FString GetStageLabel(EExerciseUiStage Stage)
	{
		if (!IsGuidedExerciseMode() && Stage == EExerciseUiStage::RepetitionOne)
		{
			return TEXT("Exercise");
		}

		switch (Stage)
		{
		case EExerciseUiStage::Warmup:
			return TEXT("Warm-up");
		case EExerciseUiStage::Introduction:
			return TEXT("Introduction");
		case EExerciseUiStage::Demonstration:
			return TEXT("Demonstration");
		case EExerciseUiStage::RepetitionOne:
			return TEXT("Repetition 1");
		case EExerciseUiStage::RepetitionTwo:
			return TEXT("Repetition 2");
		case EExerciseUiStage::RepetitionThree:
			return TEXT("Repetition 3");
		case EExerciseUiStage::Feedback:
			return TEXT("Feedback");
		case EExerciseUiStage::Completed:
			return TEXT("Completed");
		default:
			return TEXT("Idle");
		}
	}

	FString GetNextStageLabel(EExerciseUiStage Stage)
	{
		if (!IsGuidedExerciseMode())
		{
			switch (Stage)
			{
			case EExerciseUiStage::RepetitionOne:
				return TEXT("");
			case EExerciseUiStage::Feedback:
			case EExerciseUiStage::Completed:
				return TEXT("Next exercise");
			default:
				return TEXT("Start exercise");
			}
		}

		switch (Stage)
		{
		case EExerciseUiStage::Warmup:
			return TEXT("Introduction");
		case EExerciseUiStage::Introduction:
			return TEXT("Demonstration");
		case EExerciseUiStage::Demonstration:
			return TEXT("Repetition 1");
		case EExerciseUiStage::RepetitionOne:
			return TEXT("Repetition 2");
		case EExerciseUiStage::RepetitionTwo:
			return TEXT("Repetition 3");
		case EExerciseUiStage::RepetitionThree:
			return TEXT("Feedback");
		case EExerciseUiStage::Feedback:
			return TEXT("Next exercise");
		case EExerciseUiStage::Completed:
			return TEXT("Next exercise");
		default:
			return TEXT("Start exercise");
		}
	}

	void GetStageTiming(float MontageLengthSeconds, float FlowElapsedSeconds, EExerciseUiStage Stage, int32& OutRemainingSeconds, float& OutProgressPercent)
	{
		OutRemainingSeconds = 0;
		OutProgressPercent = 0.f;
		const float ActionDuration = GetStageActionDuration(Stage, MontageLengthSeconds);
		if (ActionDuration <= KINDA_SMALL_NUMBER)
		{
			return;
		}

		const float StageStartSeconds = GetStageStartTime(Stage, MontageLengthSeconds);
		const float DialogueDuration = GetStageDialogueDuration(Stage);
		const float StageElapsedSeconds = FMath::Max(FlowElapsedSeconds - StageStartSeconds, 0.f);

		if (StageElapsedSeconds <= DialogueDuration)
		{
			OutRemainingSeconds = FMath::Max(FMath::CeilToInt(ActionDuration), 0);
			OutProgressPercent = 1.f;
			return;
		}

		const float ActionElapsedSeconds = FMath::Clamp(StageElapsedSeconds - DialogueDuration, 0.f, ActionDuration);
		const float RemainingSecondsFloat = FMath::Max(ActionDuration - ActionElapsedSeconds, 0.f);
		OutRemainingSeconds = FMath::Max(FMath::CeilToInt(RemainingSecondsFloat), 0);
		OutProgressPercent = FMath::Clamp(RemainingSecondsFloat / ActionDuration, 0.f, 1.f);
	}

	enum class EFaceRegion : uint8
	{
		None,
		Brow,
		Cheek,
		Nose,
		MouthLip,
		JawChin,
		OtherExpression
	};

	bool IsRelevantFaceCurveName(const FName CurveName)
	{
		const FString Name = CurveName.ToString().ToLower();

		static const TCHAR* ExcludedTokens[] =
		{
			TEXT("card"), TEXT("cards"), TEXT("groom"), TEXT("hair"), TEXT("beard"), TEXT("mustache"),
			TEXT("cloth"), TEXT("body"), TEXT("spine"), TEXT("arm"), TEXT("leg"), TEXT("hand"),
			TEXT("finger"), TEXT("thumb"), TEXT("twist"), TEXT("rollbone"), TEXT("ik"), TEXT("fk"),
			TEXT("lod"), TEXT("material"), TEXT("shader"), TEXT("uv"), TEXT("normal"), TEXT("albedo"),
			TEXT("eye"), TEXT("eyelid"), TEXT("blink"), TEXT("squint"), TEXT("iris"), TEXT("pupil"),
			TEXT("lash"), TEXT("look"), TEXT("aim")
		};

		for (const TCHAR* Token : ExcludedTokens)
		{
			if (Name.Contains(Token))
			{
				return false;
			}
		}

		static const TCHAR* IncludedTokens[] =
		{
			TEXT("brow"), TEXT("cheek"), TEXT("nose"), TEXT("mouth"), TEXT("lip"),
			TEXT("jaw"), TEXT("chin"), TEXT("tongue"), TEXT("smile"), TEXT("frown"),
			TEXT("sneer"), TEXT("pucker"), TEXT("funnel"), TEXT("dimple"),
			TEXT("stretch"), TEXT("press"), TEXT("suck"), TEXT("shrug")
		};

		for (const TCHAR* Token : IncludedTokens)
		{
			if (Name.Contains(Token))
			{
				return true;
			}
		}

		return false;
	}

	EFaceRegion GetFaceRegion(const FName CurveName)
	{
		const FString Name = CurveName.ToString().ToLower();

		if (Name.Contains(TEXT("brow")))
		{
			return EFaceRegion::Brow;
		}
		if (Name.Contains(TEXT("cheek")))
		{
			return EFaceRegion::Cheek;
		}
		if (Name.Contains(TEXT("nose")) || Name.Contains(TEXT("sneer")))
		{
			return EFaceRegion::Nose;
		}
		if (Name.Contains(TEXT("jaw")) || Name.Contains(TEXT("chin")))
		{
			return EFaceRegion::JawChin;
		}
		if (Name.Contains(TEXT("mouth")) || Name.Contains(TEXT("lip")) || Name.Contains(TEXT("smile")) ||
			Name.Contains(TEXT("frown")) || Name.Contains(TEXT("pucker")) || Name.Contains(TEXT("funnel")) ||
			Name.Contains(TEXT("dimple")) || Name.Contains(TEXT("stretch")) || Name.Contains(TEXT("press")) ||
			Name.Contains(TEXT("suck")) || Name.Contains(TEXT("shrug")) || Name.Contains(TEXT("tongue")))
		{
			return EFaceRegion::MouthLip;
		}

		return EFaceRegion::OtherExpression;
	}

	bool TryGetMirroredCurveName(const FName InCurveName, FName& OutCurveName)
	{
		FString Name = InCurveName.ToString();
		FString Mirrored = Name;
		bool bChanged = false;

		auto ReplaceOne = [&](const TCHAR* A, const TCHAR* B) -> bool
		{
			if (Mirrored.Contains(A))
			{
				Mirrored = Mirrored.Replace(A, B, ESearchCase::CaseSensitive);
				return true;
			}
			return false;
		};

		bChanged = ReplaceOne(TEXT("Left"), TEXT("__TMP__")) || bChanged;
		bChanged = ReplaceOne(TEXT("Right"), TEXT("Left")) || bChanged;
		bChanged = ReplaceOne(TEXT("__TMP__"), TEXT("Right")) || bChanged;

		bChanged = ReplaceOne(TEXT("left"), TEXT("__tmp__")) || bChanged;
		bChanged = ReplaceOne(TEXT("right"), TEXT("left")) || bChanged;
		bChanged = ReplaceOne(TEXT("__tmp__"), TEXT("right")) || bChanged;

		bChanged = ReplaceOne(TEXT("_L"), TEXT("__TMP_LR__")) || bChanged;
		bChanged = ReplaceOne(TEXT("_R"), TEXT("_L")) || bChanged;
		bChanged = ReplaceOne(TEXT("__TMP_LR__"), TEXT("_R")) || bChanged;

		bChanged = ReplaceOne(TEXT(".L"), TEXT("__TMP_DOTLR__")) || bChanged;
		bChanged = ReplaceOne(TEXT(".R"), TEXT(".L")) || bChanged;
		bChanged = ReplaceOne(TEXT("__TMP_DOTLR__"), TEXT(".R")) || bChanged;

		bChanged = ReplaceOne(TEXT("L_"), TEXT("__TMP_PREFIXLR__")) || bChanged;
		bChanged = ReplaceOne(TEXT("R_"), TEXT("L_")) || bChanged;
		bChanged = ReplaceOne(TEXT("__TMP_PREFIXLR__"), TEXT("R_")) || bChanged;

		if (!bChanged || Mirrored == Name)
		{
			return false;
		}

		OutCurveName = FName(*Mirrored);
		return true;
	}

	float FindCurveValueForMatch(const TMap<FName, float>& Curves, const FName CurveName, bool bMirrorLookup)
	{
		if (bMirrorLookup)
		{
			FName MirroredName;
			if (TryGetMirroredCurveName(CurveName, MirroredName))
			{
				if (const float* MirroredValue = Curves.Find(MirroredName))
				{
					return *MirroredValue;
				}
			}
		}

		return Curves.FindRef(CurveName);
	}
}

bool UCurveLogging::bIsLogging = false;
TWeakObjectPtr<USkeletalMeshComponent> UCurveLogging::ActiveMesh = nullptr;
TWeakObjectPtr<UAnimMontage> UCurveLogging::ActiveMontage = nullptr;
FString UCurveLogging::ActiveLabel;
float UCurveLogging::StartTimeSeconds = 0.f;
float UCurveLogging::NextSampleTimeSeconds = 0.f;
float UCurveLogging::SampleIntervalSeconds = 0.5f;
TArray<FCurveLogFrame> UCurveLogging::Frames;
TArray<FCurveLogFrame> UCurveLogging::TherapistFramesForExport;
TArray<FCurveLogFrame> UCurveLogging::PatientFramesForExport;
TWeakObjectPtr<USkeletalMeshComponent> UCurveLogging::PatientMesh = nullptr;
bool UCurveLogging::bIsMatching = false;
float UCurveLogging::MatchingTolerance = 0.15f;
float UCurveLogging::CurrentAccuracyPercent = 0.f;
float UCurveLogging::AccumulatedAccuracyPercent = 0.f;
int32 UCurveLogging::AccuracySampleCount = 0;
TArray<FCurveAccuracySample> UCurveLogging::AccuracySamples;
FString UCurveLogging::ActiveExpressionName;
bool UCurveLogging::bIsTherapistAnimationPaused = false;
bool UCurveLogging::bPauseRequiresRecoveryAboveThreshold = false;
bool UCurveLogging::bExerciseCompleted = false;
float UCurveLogging::TherapistPauseResumeWorldTimeSeconds = 0.f;
float UCurveLogging::PausedMontageTimeSeconds = 0.f;
int32 UCurveLogging::ActiveBackendSessionId = 0;
int32 UCurveLogging::PendingBackendCompletionAccuracyScore = 0;
bool UCurveLogging::bBackendSessionCreateInFlight = false;
bool UCurveLogging::bBackendSessionCompletionPending = false;

void UCurveLogging::StartLogging(USkeletalMeshComponent* Mesh, UAnimMontage* Montage, const FString& Label)
{
	Frames.Reset();
	ActiveMesh = Mesh;
	ActiveMontage = Montage;
	ActiveLabel = Label;
	bIsLogging = Mesh != nullptr;

	if (!bIsLogging)
	{
		return;
	}

	UAnimInstance* AnimInstance = Mesh->GetAnimInstance();
	if (!AnimInstance || !Mesh->IsRegistered())
	{
		bIsLogging = false;
		ActiveMesh.Reset();
		ActiveMontage.Reset();
		return;
	}

	if (UWorld* World = Mesh->GetWorld())
	{
		StartTimeSeconds = World->GetTimeSeconds();
	}
	else
	{
		StartTimeSeconds = 0.f;
	}

	NextSampleTimeSeconds = 0.f;

	FCurveLogFrame InitialFrame;
	if (CaptureCurves(InitialFrame, 0.f))
	{
		Frames.Add(MoveTemp(InitialFrame));
		NextSampleTimeSeconds = SampleIntervalSeconds;
	}
}

void UCurveLogging::TickLogging(float DeltaSeconds)
{
	if (bIsMatching)
	{
		TickCurveMatching(DeltaSeconds);
		return;
	}

	if (!bIsLogging)
	{
		return;
	}

	if (!ActiveMesh.IsValid() || !ActiveMesh->IsRegistered())
	{
		bIsLogging = false;
		return;
	}

	const float CurrentTimeSeconds = GetCurrentTimeSeconds();
	if (CurrentTimeSeconds + KINDA_SMALL_NUMBER < NextSampleTimeSeconds)
	{
		return;
	}

	while (CurrentTimeSeconds + KINDA_SMALL_NUMBER >= NextSampleTimeSeconds)
	{
		FCurveLogFrame Frame;
		if (CaptureCurves(Frame, NextSampleTimeSeconds))
		{
			Frames.Add(MoveTemp(Frame));
		}

		NextSampleTimeSeconds += SampleIntervalSeconds;
	}
}

void UCurveLogging::StopLogging()
{
	ResumeTherapistAnimation(false);
	bIsLogging = false;
	NextSampleTimeSeconds = 0.f;
	ActiveMesh.Reset();
	ActiveMontage.Reset();
	ActiveExpressionName.Empty();
	TherapistFramesForExport.Reset();
	PatientFramesForExport.Reset();
	bPauseRequiresRecoveryAboveThreshold = false;
	TherapistPauseResumeWorldTimeSeconds = 0.f;
	PausedMontageTimeSeconds = 0.f;
}

void UCurveLogging::StopExerciseFlow()
{
	auto StopAllMontagesOnMesh = [](USkeletalMeshComponent* Mesh)
	{
		if (!Mesh)
		{
			return;
		}

		if (UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
		{
			AnimInstance->Montage_Stop(0.1f);
		}
	};

	StopAllMontagesOnMesh(ActiveMesh.Get());
	StopAllMontagesOnMesh(ActiveGuidanceBodyMesh.Get());
	StopAllMontagesOnMesh(PatientMesh.Get());
	if (UAudioComponent* AudioComponent = ActiveStageAudioComponent.Get())
	{
		AudioComponent->Stop();
	}
	ActiveStageAudioComponent.Reset();

	bIsMatching = false;
	bExerciseCompleted = true;
	bExerciseManuallyPaused = false;
	ManualExercisePauseStartWorldTimeSeconds = 0.f;
	bFeedbackStageActive = false;
	FeedbackStageStartWorldTimeSeconds = 0.f;
	FeedbackStageDurationSeconds = 0.f;
	bHoldingExercisePoseForStage = false;
	bLastStageActionWindowActive = false;
	bGuidanceConfigPendingForStart = false;
	bHasGuidanceConfig = false;
	LastAnnouncedStage = EExerciseUiStage::Idle;
	ActiveGuidanceBodyMesh.Reset();
	ResetSelectedPhaseRuntimeState();

	StopLogging();
	PatientMesh.Reset();
}

bool UCurveLogging::PauseExerciseFlow()
{
	const bool bHasRunningExercise = bIsMatching || bIsLogging || bSelectedPhaseExerciseActive || bFeedbackStageActive;
	if (!bHasRunningExercise || bExerciseCompleted)
	{
		return false;
	}

	if (bExerciseManuallyPaused)
	{
		return true;
	}

	bExerciseManuallyPaused = true;
	ManualExercisePauseStartWorldTimeSeconds = GetCurrentWorldTimeSeconds();

	if (UAudioComponent* AudioComponent = ActiveStageAudioComponent.Get())
	{
		AudioComponent->SetPaused(true);
	}

	PauseAllMontagesOnMesh(ActiveMesh.Get());
	PauseAllMontagesOnMesh(ActiveGuidanceBodyMesh.Get());
	PauseAllMontagesOnMesh(PatientMesh.Get());
	return true;
}

bool UCurveLogging::ResumeExerciseFlow()
{
	if (!bExerciseManuallyPaused)
	{
		return false;
	}

	const float CurrentWorldTimeSeconds = GetCurrentWorldTimeSeconds();
	const float PausedSeconds = FMath::Max(CurrentWorldTimeSeconds - ManualExercisePauseStartWorldTimeSeconds, 0.f);
	AccumulatedAccuracyPauseSeconds += PausedSeconds;
	if (bSelectedPhaseExerciseHasState && bSelectedPhaseStepStarted)
	{
		SelectedStepStartWorldTimeSeconds += PausedSeconds;
	}
	if (bFeedbackStageActive)
	{
		FeedbackStageStartWorldTimeSeconds += PausedSeconds;
	}

	bExerciseManuallyPaused = false;
	ManualExercisePauseStartWorldTimeSeconds = 0.f;

	if (UAudioComponent* AudioComponent = ActiveStageAudioComponent.Get())
	{
		AudioComponent->SetPaused(false);
	}

	const bool bKeepTherapistHoldPoseFrozen = bIsTherapistAnimationPaused && bHoldingExercisePoseForStage;
	if (!bKeepTherapistHoldPoseFrozen)
	{
		ResumeAllMontagesOnMesh(ActiveMesh.Get());
		ResumeAllMontagesOnMesh(ActiveGuidanceBodyMesh.Get());
	}
	ResumeAllMontagesOnMesh(PatientMesh.Get());
	return true;
}

bool UCurveLogging::IsExerciseFlowPaused()
{
	return bExerciseManuallyPaused;
}

bool UCurveLogging::GetLatestFrame(FCurveLogFrame& OutFrame)
{
	if (Frames.Num() == 0)
	{
		return false;
	}

	OutFrame = Frames.Last();
	return true;
}

void UCurveLogging::GetAllFrames(TArray<FCurveLogFrame>& OutFrames)
{
	OutFrames = Frames;
}

void UCurveLogging::StartCurveMatching(
	USkeletalMeshComponent* TherapistMesh,
	USkeletalMeshComponent* InPatientMesh,
	UAnimMontage* Montage,
	float Tolerance,
	float InSampleIntervalSeconds,
	FString ExpressionName)
{
	if (bGuidanceConfigPendingForStart)
	{
		bGuidanceConfigPendingForStart = false;
	}
	else
	{
		bHasGuidanceConfig = false;
		ActiveGuidanceBodyMesh.Reset();
	}

	if (UAudioComponent* AudioComponent = ActiveStageAudioComponent.Get())
	{
		AudioComponent->Stop();
	}
	ActiveStageAudioComponent.Reset();
	bFeedbackStageActive = false;
	FeedbackStageStartWorldTimeSeconds = 0.f;
	FeedbackStageDurationSeconds = 0.f;
	LastAnnouncedStage = EExerciseUiStage::Idle;
	bLastStageActionWindowActive = false;
	bExercisePlaybackStarted = false;
	bScoringWindowInitialized = false;
	bHoldingExercisePoseForStage = false;
	RecentMontageDiagnostics.Reset();
	AccumulatedAccuracyPauseSeconds = 0.f;
	CurrentAccuracyPauseStartWorldTimeSeconds = 0.f;
	bExerciseManuallyPaused = false;
	ManualExercisePauseStartWorldTimeSeconds = 0.f;

	AccuracySamples.Reset();
	CurrentAccuracyPercent = 0.f;
	AccumulatedAccuracyPercent = 0.f;
	AccuracySampleCount = 0;
	TherapistFramesForExport.Reset();
	PatientFramesForExport.Reset();
	bExerciseCompleted = false;
	bIsTherapistAnimationPaused = false;
	bPauseRequiresRecoveryAboveThreshold = false;
	bPauseTriggeredForCurrentExercise = false;
	TherapistPauseResumeWorldTimeSeconds = 0.f;
	PausedMontageTimeSeconds = 0.f;

	MatchingTolerance = FMath::Max(Tolerance, KINDA_SMALL_NUMBER);
	bIsMatching = false;
	PatientMesh = InPatientMesh;
	SampleIntervalSeconds = FMath::Max(InSampleIntervalSeconds, KINDA_SMALL_NUMBER);
	ActiveExpressionName = ExpressionName;

	StartLogging(TherapistMesh, Montage, FString());

	if (!bIsLogging || !PatientMesh.IsValid() || !PatientMesh->IsRegistered() || !PatientMesh->GetAnimInstance())
	{
		StopLogging();
		PatientMesh.Reset();
		return;
	}

	FCurveLogFrame TherapistFrame;
	FCurveLogFrame PatientFrame;
	Frames.Reset();
	if (!CaptureCurvesForMesh(ActiveMesh.Get(), TherapistFrame, 0.f))
	{
		StopLogging();
		PatientMesh.Reset();
		return;
	}

	if (CaptureCurvesForMesh(PatientMesh.Get(), PatientFrame, TherapistFrame.TimeSeconds))
	{
		Frames.Add(TherapistFrame);
		TherapistFramesForExport.Add(TherapistFrame);
		PatientFramesForExport.Add(PatientFrame);
		NextSampleTimeSeconds = SampleIntervalSeconds;

		const float RawAccuracyPercent = CalculateAccuracyPercent(TherapistFrame, PatientFrame, MatchingTolerance);
		const float AdjustedAccuracyPercent = ApplyExpressionSpecificScoreAdjustment(RawAccuracyPercent);
		CurrentAccuracyPercent = AdjustedAccuracyPercent;
		AccumulatedAccuracyPercent = CurrentAccuracyPercent;
		AccuracySampleCount = 1;

		FCurveAccuracySample AccuracySample;
		AccuracySample.TimeSeconds = TherapistFrame.TimeSeconds;
		AccuracySample.AccuracyPercent = CurrentAccuracyPercent;
		AccuracySamples.Add(AccuracySample);

		bIsMatching = true;
		BeginBackendSession();
		PauseTherapistAnimationForFlow();
	}
	else
	{
		StopLogging();
		PatientMesh.Reset();
	}
}

void UCurveLogging::StartCurveMatchingWithGuidance(
	USkeletalMeshComponent* TherapistMesh,
	USkeletalMeshComponent* TherapistBodyMesh,
	USkeletalMeshComponent* InPatientMesh,
	UAnimMontage* Montage,
	const FCurveExerciseGuidanceConfig& GuidanceConfig,
	float Tolerance,
	float InSampleIntervalSeconds,
	FString ExpressionName)
{
	ActiveGuidanceConfig = GuidanceConfig;
	bHasGuidanceConfig = true;
	bGuidanceConfigPendingForStart = true;
	ActiveGuidanceBodyMesh = TherapistBodyMesh;
	StartCurveMatching(TherapistMesh, InPatientMesh, Montage, Tolerance, InSampleIntervalSeconds, ExpressionName);
	if (bIsMatching && bIsLogging)
	{
		LastAnnouncedStage = EExerciseUiStage::Warmup;
		bLastStageActionWindowActive = false;
		PlayGuidanceAudioIfNeeded(TherapistMesh, EExerciseUiStage::Warmup);
		PlayGuidanceMontageIfNeeded(TherapistMesh, EExerciseUiStage::Warmup);
	}
}

void UCurveLogging::SetupFacialExerciseMontagesFromDataTable(UDataTable* ExerciseTable)
{
	ConfiguredExpressionMontages.Reset();
	if (!ExerciseTable || !ExerciseTable->GetRowStruct())
	{
		return;
	}

	for (const TPair<FName, uint8*>& RowPair : ExerciseTable->GetRowMap())
	{
		if (UAnimMontage* Montage = FindFirstMontageInTableRow(ExerciseTable->GetRowStruct(), RowPair.Value))
		{
			StoreExpressionMontageWithAliases(RowPair.Key.ToString(), Montage);
		}
	}
}

void UCurveLogging::StartSelectedPhaseExercise(
	USkeletalMeshComponent* TherapistMesh,
	USkeletalMeshComponent* TherapistBodyMesh,
	USkeletalMeshComponent* InPatientMesh,
	int32 PhaseNumber,
	int32 ItemId,
	const FCurveExerciseGuidanceConfig& GuidanceConfig,
	float Tolerance,
	float InSampleIntervalSeconds)
{
	ResetSelectedPhaseRuntimeState();
	const bool bUseGuidedFlow = UFacialTherapyApi::GetStoredModeType() == EFacialTherapySessionMode::GuidedMode;

	auto StartExistingSingleExerciseFlow = [&](const FString& ExpressionName) -> bool
	{
		UAnimMontage* Montage = GetConfiguredExpressionMontage(ExpressionName);
		if (!Montage)
		{
			SelectedStatusText = FString::Printf(TEXT("No montage is configured for %s."), *ExpressionName);
			bSelectedPhaseExerciseHasState = true;
			bExerciseCompleted = true;
			return true;
		}

		if (bUseGuidedFlow)
		{
			StartCurveMatchingWithGuidance(
				TherapistMesh,
				TherapistBodyMesh,
				InPatientMesh,
				Montage,
				GuidanceConfig,
				Tolerance,
				InSampleIntervalSeconds,
				ExpressionName);
		}
		else
		{
			StartCurveMatching(
				TherapistMesh,
				InPatientMesh,
				Montage,
				Tolerance,
				InSampleIntervalSeconds,
				ExpressionName);
		}
		return true;
	};

	if (PhaseNumber == 1 || (ItemId >= 1 && ItemId <= 9))
	{
		switch (ItemId)
		{
		case 1: StartExistingSingleExerciseFlow(TEXT("Angry")); return;
		case 2: StartExistingSingleExerciseFlow(TEXT("Surprise")); return;
		case 3: StartExistingSingleExerciseFlow(TEXT("Disgust")); return;
		case 4: StartExistingSingleExerciseFlow(TEXT("Fear")); return;
		case 5: StartExistingSingleExerciseFlow(TEXT("Smile")); return;
		case 6: StartExistingSingleExerciseFlow(TEXT("Sad")); return;
		case 7: StartExistingSingleExerciseFlow(TEXT("Pout")); return;
		case 8: StartExistingSingleExerciseFlow(TEXT("Puffcheeks")); return;
		case 9: StartExistingSingleExerciseFlow(TEXT("Smicheeks")); return;
		default: break;
		}
	}

	StopExerciseFlow();

	bSelectedPhaseExerciseHasState = true;
	bSelectedPhaseExerciseActive = false;
	bSelectedPhaseStepStarted = false;
	bSelectedActionStarted = false;
	bSelectedHoldScoringInitialized = false;
	bSelectedProgressSentForCurrentExercise = false;
	bSelectedCompletionRecorded = false;
	SelectedPhaseStepIndex = INDEX_NONE;
	SelectedItemId = ItemId;
	SelectedPairNumber = 0;
	SelectedSequenceCode.Empty();
	SelectedItemTitle.Empty();
	SelectedItemType.Empty();
	SelectedStatusText = TEXT("Preparing exercise...");
	SelectedStepStartWorldTimeSeconds = 0.f;
	SelectedScoredTimelineBaseSeconds = 0.f;

	AccuracySamples.Reset();
	Frames.Reset();
	TherapistFramesForExport.Reset();
	PatientFramesForExport.Reset();
	CurrentAccuracyPercent = 0.f;
	AccumulatedAccuracyPercent = 0.f;
	AccuracySampleCount = 0;
	LastCompletedAverageAccuracyPercent = 0.f;
	MatchingTolerance = FMath::Max(Tolerance, KINDA_SMALL_NUMBER);
	SampleIntervalSeconds = FMath::Max(InSampleIntervalSeconds, KINDA_SMALL_NUMBER);
	NextSampleTimeSeconds = 0.f;

	ActiveMesh = TherapistMesh;
	ActiveGuidanceBodyMesh = TherapistBodyMesh;
	PatientMesh = InPatientMesh;
	ActiveGuidanceConfig = GuidanceConfig;
	bHasGuidanceConfig = bUseGuidedFlow;
	bExerciseCompleted = false;
	bIsTherapistAnimationPaused = false;
	bPauseRequiresRecoveryAboveThreshold = false;
	bPauseTriggeredForCurrentExercise = false;
	ActiveExpressionName.Empty();

	if (!TherapistMesh || !TherapistMesh->IsRegistered() || !TherapistMesh->GetAnimInstance() ||
		!PatientMesh.IsValid() || !PatientMesh->IsRegistered() || !PatientMesh->GetAnimInstance())
	{
		SelectedStatusText = TEXT("Unable to start exercise. Therapist or patient mesh is not ready.");
		bExerciseCompleted = true;
		return;
	}

	auto AddStep = [](ESelectedPhaseStepType Type, EExerciseUiStage GuidanceStage, const FString& Label, const FString& Note,
		const FString& ExpressionName, float ActionDurationSeconds, bool bScored, bool bPlayGuidance)
	{
		FSelectedPhaseStep Step;
		Step.Type = Type;
		Step.GuidanceStage = GuidanceStage;
		Step.Label = Label;
		Step.Note = Note;
		Step.ExpressionName = ExpressionName;
		Step.Montage = GetConfiguredExpressionMontage(ExpressionName);
		Step.ActionDurationSeconds = FMath::Max(ActionDurationSeconds, 0.f);
		Step.bScored = bScored;
		Step.bPlayGuidance = bPlayGuidance;
		SelectedPhaseSteps.Add(Step);
	};

	auto AddWarmupAndIntro = [&AddStep](const FString& IntroNote, EExerciseUiStage IntroGuidanceStage = EExerciseUiStage::Introduction)
	{
		AddStep(ESelectedPhaseStepType::Warmup, EExerciseUiStage::Warmup, TEXT("Warm-up"),
			TEXT("Facial massage, gentle movement, and conscious breathing."), FString(), WarmupStageSeconds, false, true);
		AddStep(ESelectedPhaseStepType::Introduction, IntroGuidanceStage, TEXT("Introduction"),
			IntroNote, FString(), IntroductionStageSeconds, false, true);
	};

	auto AddDemonstration = [&AddStep](const TArray<FString>& Expressions, EExerciseUiStage DemonstrationGuidanceStage = EExerciseUiStage::Demonstration)
	{
		AddStep(ESelectedPhaseStepType::Demonstration, DemonstrationGuidanceStage, TEXT("Demonstration"),
			TEXT("Watch the therapist demonstrate the full set before you begin."), FString(), 0.f, false, true);

		for (int32 Index = 0; Index < Expressions.Num(); ++Index)
		{
			const FString& Expression = Expressions[Index];
			if (Expression.Equals(TEXT("Neutral"), ESearchCase::IgnoreCase))
			{
				AddStep(ESelectedPhaseStepType::Rest, EExerciseUiStage::Idle, TEXT("Neutral"),
					TEXT("Relax your face and return to neutral."), FString(), 3.f, false, false);
				continue;
			}

			AddStep(ESelectedPhaseStepType::Demonstration, EExerciseUiStage::Idle,
				FString::Printf(TEXT("Demonstration: %s"), *Expression),
				TEXT("Watch this expression."), Expression, 5.f, false, false);

			if (Index + 1 < Expressions.Num())
			{
				AddStep(ESelectedPhaseStepType::Rest, EExerciseUiStage::Idle, TEXT("Transition"),
					TEXT("Prepare for the next expression."), FString(), 2.f, false, false);
			}
		}
	};

	auto AddYourTurnGuidance = [&AddStep]()
	{
		AddStep(ESelectedPhaseStepType::Introduction, EExerciseUiStage::RepetitionOne, TEXT("Your turn"),
			TEXT("Now it is your turn. Follow the therapist and hold each expression steadily."), FString(), 0.f, false, true);
	};

	auto AddHold = [&AddStep](const FString& Expression, float HoldSeconds, int32 RoundIndex = 0, int32 TotalRounds = 0)
	{
		const FString Label = (RoundIndex > 0 && TotalRounds > 0)
			? FString::Printf(TEXT("Round %d of %d - Hold: %s"), RoundIndex, TotalRounds, *Expression)
			: FString::Printf(TEXT("Hold: %s"), *Expression);
		const FString Note = (RoundIndex > 0 && TotalRounds > 0)
			? FString::Printf(TEXT("Round %d of %d. Match the therapist and hold %s steadily."), RoundIndex, TotalRounds, *Expression)
			: TEXT("Match the therapist and hold the expression steadily.");
		AddStep(ESelectedPhaseStepType::Hold, EExerciseUiStage::Idle,
			Label, Note, Expression, HoldSeconds, true, false);
	};

	auto AddRest = [&AddStep](float DurationSeconds, const FString& NextLabel, int32 RoundIndex = 0, int32 TotalRounds = 0)
	{
		FString Note = NextLabel.IsEmpty()
			? TEXT("Rest now. Relax your face.")
			: FString::Printf(TEXT("Rest now. Relax your face. Next expression: %s."), *NextLabel);
		if (RoundIndex > 0 && TotalRounds > 0)
		{
			Note = FString::Printf(TEXT("Round %d of %d. %s"), RoundIndex, TotalRounds, *Note);
		}
		AddStep(ESelectedPhaseStepType::Rest, EExerciseUiStage::Idle, TEXT("Rest"), Note, FString(), DurationSeconds, false, false);
	};

	auto ConfigureSingle = [&](const FString& Title, const FString& Expression, int32 ExerciseId, int32 HoldSeconds, int32 Repetitions)
	{
		SelectedItemTitle = Title;
		SelectedItemType = TEXT("SingleExercise");
		SelectedItemId = ExerciseId;
		TArray<FString> Expressions;
		Expressions.Add(Expression);
		if (bUseGuidedFlow)
		{
			AddWarmupAndIntro(TEXT("Practice one facial expression. Focus on correct muscle activation."));
			AddDemonstration(Expressions);
			AddYourTurnGuidance();
		}
		const int32 RepeatCount = bUseGuidedFlow ? Repetitions : 1;
		for (int32 RepIndex = 0; RepIndex < RepeatCount; ++RepIndex)
		{
			AddHold(Expression, HoldSeconds);
			if (RepIndex + 1 < RepeatCount)
			{
				AddRest(10.f, Expression);
			}
		}
	};

	auto ConfigurePair = [&](const FString& Title, int32 PairNumber, const FString& A, const FString& B)
	{
		SelectedItemTitle = Title;
		SelectedItemType = TEXT("Pair");
		SelectedPairNumber = PairNumber;
		TArray<FString> Expressions;
		Expressions.Add(A);
		Expressions.Add(B);
		if (bUseGuidedFlow)
		{
			AddWarmupAndIntro(TEXT("Alternate between two expressions. Focus on smooth transitions."), EExerciseUiStage::Phase2Introduction);
			AddDemonstration(Expressions, EExerciseUiStage::Phase2Demonstration);
			AddYourTurnGuidance();
		}
		const int32 PairRounds = bUseGuidedFlow ? 4 : 1;
		for (int32 RepIndex = 0; RepIndex < PairRounds; ++RepIndex)
		{
			const int32 RoundNumber = RepIndex + 1;
			AddHold(A, 20.f, RoundNumber, PairRounds);
			AddRest(5.f, B, RoundNumber, PairRounds);
			if (B.Equals(TEXT("Neutral"), ESearchCase::IgnoreCase))
			{
				AddStep(ESelectedPhaseStepType::Rest, EExerciseUiStage::Idle, TEXT("Neutral Hold"),
					TEXT("Relax fully to neutral."), FString(), 20.f, false, false);
			}
			else
			{
				AddHold(B, 20.f, RoundNumber, PairRounds);
			}
			if (RepIndex + 1 < PairRounds)
			{
				AddRest(10.f, A, RoundNumber, PairRounds);
			}
		}
	};

	auto ConfigureSequence = [&](const FString& Title, const FString& Code, const TArray<FString>& Expressions, const FString& IntroText, EExerciseUiStage IntroGuidanceStage, EExerciseUiStage DemonstrationGuidanceStage)
	{
		SelectedItemTitle = Title;
		SelectedItemType = TEXT("Sequence");
		SelectedSequenceCode = Code;
		if (bUseGuidedFlow)
		{
			AddWarmupAndIntro(IntroText, IntroGuidanceStage);
			AddDemonstration(Expressions, DemonstrationGuidanceStage);
			AddYourTurnGuidance();
		}
		const int32 PassCount = bUseGuidedFlow ? 2 : 1;
		for (int32 PassIndex = 0; PassIndex < PassCount; ++PassIndex)
		{
			for (int32 ExpressionIndex = 0; ExpressionIndex < Expressions.Num(); ++ExpressionIndex)
			{
				AddHold(Expressions[ExpressionIndex], 25.f);
				if (ExpressionIndex + 1 < Expressions.Num())
				{
					AddRest(5.f, Expressions[ExpressionIndex + 1]);
				}
			}
			if (PassIndex + 1 < PassCount && Expressions.Num() > 0)
			{
				AddRest(20.f, Expressions[0]);
			}
		}
	};

	switch (ItemId)
	{
	case 1: ConfigureSingle(TEXT("Angry"), TEXT("Angry"), 1, 15, 3); break;
	case 2: ConfigureSingle(TEXT("Surprised"), TEXT("Surprise"), 2, 15, 3); break;
	case 3: ConfigureSingle(TEXT("Disgust"), TEXT("Disgust"), 3, 15, 3); break;
	case 4: ConfigureSingle(TEXT("Fear"), TEXT("Fear"), 4, 15, 3); break;
	case 5: ConfigureSingle(TEXT("Smile"), TEXT("Smile"), 5, 15, 3); break;
	case 6: ConfigureSingle(TEXT("Sadness"), TEXT("Sad"), 6, 15, 3); break;
	case 7: ConfigureSingle(TEXT("Pout"), TEXT("Pout"), 7, 15, 3); break;
	case 8: ConfigureSingle(TEXT("Puff Cheeks"), TEXT("Puffcheeks"), 8, 15, 3); break;
	case 9: ConfigureSingle(TEXT("Smile + Cheeks"), TEXT("Smicheeks"), 9, 15, 3); break;
	case 201: ConfigurePair(TEXT("Pair 1: Surprised - Angry"), 1, TEXT("Surprise"), TEXT("Angry")); break;
	case 202: ConfigurePair(TEXT("Pair 2: Smile - Sadness"), 2, TEXT("Smile"), TEXT("Sad")); break;
	case 203: ConfigurePair(TEXT("Pair 3: Disgust - Neutral"), 3, TEXT("Disgust"), TEXT("Neutral")); break;
	case 204: ConfigurePair(TEXT("Pair 4: Fear - Surprised"), 4, TEXT("Fear"), TEXT("Surprise")); break;
	case 301: ConfigureSequence(TEXT("Sequence A: Upper Face"), TEXT("A"), {TEXT("Surprise"), TEXT("Angry"), TEXT("Surprise")}, TEXT("Practice a complex upper-face sequence."), EExerciseUiStage::Phase3Introduction, EExerciseUiStage::Phase3Demonstration); break;
	case 302: ConfigureSequence(TEXT("Sequence B: Lower Face"), TEXT("B"), {TEXT("Smile"), TEXT("Sad"), TEXT("Pout"), TEXT("Smile")}, TEXT("Practice a complex lower-face sequence."), EExerciseUiStage::Phase3Introduction, EExerciseUiStage::Phase3Demonstration); break;
	case 303: ConfigureSequence(TEXT("Sequence C: Whole Face"), TEXT("C"), {TEXT("Fear"), TEXT("Disgust"), TEXT("Surprise")}, TEXT("Practice a whole-face emotion sequence."), EExerciseUiStage::Phase3Introduction, EExerciseUiStage::Phase3Demonstration); break;
	case 304: ConfigureSequence(TEXT("Sequence D: Special Exercises"), TEXT("D"), {TEXT("Puffcheeks"), TEXT("Smicheeks")}, TEXT("Practice the special exercise sequence."), EExerciseUiStage::Phase3Introduction, EExerciseUiStage::Phase3Demonstration); break;
	case 401: ConfigureSequence(TEXT("Mastery: Complex Sequences"), TEXT("MASTERY_SEQ"), {TEXT("Surprise"), TEXT("Angry"), TEXT("Surprise"), TEXT("Smile"), TEXT("Sad"), TEXT("Pout"), TEXT("Smile")}, TEXT("Practice a mastery sequence mix."), EExerciseUiStage::Phase4Introduction, EExerciseUiStage::Phase4Demonstration); SelectedItemType = TEXT("Mixed"); break;
	case 402: ConfigureSequence(TEXT("Mastery: Targeted Practice"), TEXT("MASTERY_TARGET"), {TEXT("Disgust"), TEXT("Sad"), TEXT("Fear")}, TEXT("Practice targeted mastery exercises."), EExerciseUiStage::Phase4Introduction, EExerciseUiStage::Phase4Demonstration); SelectedItemType = TEXT("Mixed"); break;
	case 501: ConfigureSequence(TEXT("Short Maintenance"), TEXT("MAINT_SHORT"), {TEXT("Surprise"), TEXT("Angry"), TEXT("Surprise"), TEXT("Smile"), TEXT("Pout")}, TEXT("Practice a shorter maintenance session."), EExerciseUiStage::Phase4Introduction, EExerciseUiStage::Phase4Demonstration); SelectedItemType = TEXT("Mixed"); break;
	case 502: ConfigureSequence(TEXT("Intensive Maintenance"), TEXT("MAINT_INTENSIVE"), {TEXT("Surprise"), TEXT("Angry"), TEXT("Surprise"), TEXT("Smile"), TEXT("Sad"), TEXT("Pout"), TEXT("Fear"), TEXT("Disgust"), TEXT("Puffcheeks"), TEXT("Smicheeks")}, TEXT("Practice an intensive maintenance session."), EExerciseUiStage::Phase4Introduction, EExerciseUiStage::Phase4Demonstration); SelectedItemType = TEXT("Mixed"); break;
	default:
		SelectedStatusText = TEXT("Unknown phase exercise selection.");
		bExerciseCompleted = true;
		return;
	}

	AddStep(ESelectedPhaseStepType::Feedback, EExerciseUiStage::Feedback, TEXT("Feedback"),
		TEXT("Review your result and prepare for the next exercise."), FString(), 5.f, false, bUseGuidedFlow);

	if (SelectedPhaseSteps.Num() == 0)
	{
		SelectedStatusText = TEXT("No exercise steps were created.");
		bExerciseCompleted = true;
		return;
	}

	SelectedPhaseStepIndex = 0;
	SelectedStepStartWorldTimeSeconds = GetCurrentWorldTimeSeconds();
	bSelectedPhaseExerciseActive = true;
	bIsMatching = true;
	bIsLogging = true;
	ActiveLabel = SelectedItemTitle;
	ActiveExpressionName = SelectedItemTitle;
	BeginBackendSession();
}

void UCurveLogging::TickCurveMatching(float DeltaSeconds)
{
	if (bExerciseManuallyPaused)
	{
		return;
	}

	if (bSelectedPhaseExerciseActive && SelectedPhaseSteps.IsValidIndex(SelectedPhaseStepIndex))
	{
		TickSelectedPhaseExercise(DeltaSeconds);
		return;
	}
	else if (bSelectedPhaseExerciseActive)
	{
		bSelectedPhaseExerciseActive = false;
	}

	if (bFeedbackStageActive)
	{
		const float WorldTime = GetCurrentWorldTimeSeconds();
		if ((WorldTime - FeedbackStageStartWorldTimeSeconds) >= FeedbackStageDurationSeconds)
		{
			bFeedbackStageActive = false;
			bExerciseCompleted = true;
			StopLogging();
		}
		return;
	}

	if (!bIsMatching || !bIsLogging)
	{
		return;
	}

	if (!PatientMesh.IsValid() || !PatientMesh->IsRegistered() || !PatientMesh->GetAnimInstance())
	{
		bIsMatching = false;
		return;
	}

	if (!ActiveMesh.IsValid() || !ActiveMesh->IsRegistered())
	{
		bIsMatching = false;
		return;
	}

	const float WorldTime = GetCurrentWorldTimeSeconds();
	const float MontageLength = ActiveMontage.IsValid() ? ActiveMontage->GetPlayLength() : 0.f;
	const float FlowElapsed = GetEffectiveFlowElapsedSeconds(WorldTime);
	const EExerciseUiStage CurrentStage = GetBaseExerciseStage(true, bExerciseCompleted, FlowElapsed, MontageLength);
	const bool bStageActionWindowActive = IsStageActionWindowActive(CurrentStage, FlowElapsed, MontageLength);
	const float CurrentStageActionDuration = GetStageActionDuration(CurrentStage, MontageLength);
	const float CurrentStageActionElapsed = GetStageActionElapsedSeconds(CurrentStage, FlowElapsed, MontageLength);

	const bool bIsFinalScoredStage =
		(IsGuidedExerciseMode() && CurrentStage == EExerciseUiStage::RepetitionThree)
		|| (!IsGuidedExerciseMode() && CurrentStage == EExerciseUiStage::RepetitionOne);

	if (!bFeedbackStageActive
		&& !bExerciseCompleted
		&& bIsFinalScoredStage
		&& CurrentStageActionDuration > KINDA_SMALL_NUMBER
		&& CurrentStageActionElapsed >= (CurrentStageActionDuration - KINDA_SMALL_NUMBER))
	{
		float IgnoredAverageAccuracy = 0.f;
		TArray<FCurveAccuracySample> IgnoredAccuracySamples;
		EndCurveMatching(IgnoredAverageAccuracy, IgnoredAccuracySamples);
		return;
	}

	if (CurrentStage != LastAnnouncedStage)
	{
		LastAnnouncedStage = CurrentStage;
		bLastStageActionWindowActive = false;
		PlayGuidanceAudioIfNeeded(ActiveMesh.Get(), CurrentStage);
		PlayGuidanceMontageIfNeeded(ActiveMesh.Get(), CurrentStage);
	}

	if (bStageActionWindowActive && !bLastStageActionWindowActive)
	{
		StopGuidanceMontageIfNeeded(ActiveMesh.Get(), CurrentStage);
		bHoldingExercisePoseForStage = false;
		if (UsesExerciseMontage(CurrentStage) && ActiveMontage.IsValid())
		{
			if (IsScoredStage(CurrentStage))
			{
				ShowFrozenMontagePose(ActiveMesh.Get(), ActiveMontage.Get());
				ShowFrozenMontagePose(ActiveGuidanceBodyMesh.Get(), ActiveMontage.Get());
				bIsTherapistAnimationPaused = true;
				PausedMontageTimeSeconds = ActiveMontage->GetPlayLength() * 0.5f;
				bHoldingExercisePoseForStage = true;
			}
			else
			{
				PlayMontageOnMeshIfNeeded(ActiveMesh.Get(), ActiveMontage.Get(), TEXT("exercise_action_play"));
			}
		}
	}
	bLastStageActionWindowActive = bStageActionWindowActive;

	if (!bStageActionWindowActive)
	{
		PauseTherapistAnimationForFlow();
		CurrentAccuracyPercent = 0.f;
		return;
	}

	if (CurrentStage == EExerciseUiStage::Warmup || CurrentStage == EExerciseUiStage::Introduction)
	{
		PauseTherapistAnimationForFlow();
		CurrentAccuracyPercent = 0.f;
		return;
	}

	if (!bExercisePlaybackStarted)
	{
		bExercisePlaybackStarted = true;
		ExercisePlaybackStartMontageTimeSeconds = 0.f;
	}

	if (CurrentStage == EExerciseUiStage::Demonstration)
	{
		if (bIsTherapistAnimationPaused && !bPauseTriggeredForCurrentExercise)
		{
			ResumeTherapistAnimationForFlow();
		}
		CurrentAccuracyPercent = 0.f;
		return;
	}

	if (!IsScoredStage(CurrentStage))
	{
		CurrentAccuracyPercent = 0.f;
		return;
	}

	const float CurrentTimelineSeconds = GetScoredTimelineSeconds(CurrentStage, FlowElapsed, MontageLength);

	FCurveLogFrame LiveTherapistFrame;
	FCurveLogFrame LivePatientFrame;
	if (CaptureCurvesForMesh(ActiveMesh.Get(), LiveTherapistFrame, CurrentTimelineSeconds) &&
		CaptureCurvesForMesh(PatientMesh.Get(), LivePatientFrame, CurrentTimelineSeconds))
	{
		if (!bScoringWindowInitialized)
		{
			Frames.Reset();
			TherapistFramesForExport.Reset();
			PatientFramesForExport.Reset();
			AccuracySamples.Reset();
			AccumulatedAccuracyPercent = 0.f;
			AccuracySampleCount = 0;

			LiveTherapistFrame.TimeSeconds = CurrentTimelineSeconds;
			LivePatientFrame.TimeSeconds = CurrentTimelineSeconds;
			Frames.Add(LiveTherapistFrame);
			TherapistFramesForExport.Add(LiveTherapistFrame);
			PatientFramesForExport.Add(LivePatientFrame);

			const float RawInitialAccuracyPercent = CalculateAccuracyPercent(LiveTherapistFrame, LivePatientFrame, MatchingTolerance);
			CurrentAccuracyPercent = ApplyExpressionSpecificScoreAdjustment(RawInitialAccuracyPercent);
			AccumulatedAccuracyPercent = CurrentAccuracyPercent;
			AccuracySampleCount = 1;

			FCurveAccuracySample AccuracySample;
			AccuracySample.TimeSeconds = CurrentTimelineSeconds;
			AccuracySample.AccuracyPercent = CurrentAccuracyPercent;
			AccuracySamples.Add(AccuracySample);

			NextSampleTimeSeconds = CurrentTimelineSeconds + SampleIntervalSeconds;
			bScoringWindowInitialized = true;
			return;
		}

		float RawAccuracyPercent = CalculateAccuracyPercent(LiveTherapistFrame, LivePatientFrame, MatchingTolerance);

		const float MaxReactionDelaySeconds = 0.15f;
		for (int32 FrameIndex = Frames.Num() - 1; FrameIndex >= 0; --FrameIndex)
		{
			const FCurveLogFrame& TherapistHistoryFrame = Frames[FrameIndex];
			if ((CurrentTimelineSeconds - TherapistHistoryFrame.TimeSeconds) > MaxReactionDelaySeconds)
			{
				break;
			}

			const float HistoryScore = CalculateAccuracyPercent(TherapistHistoryFrame, LivePatientFrame, MatchingTolerance);
			RawAccuracyPercent = FMath::Max(RawAccuracyPercent, HistoryScore);
		}

		RawAccuracyPercent = ApplyExpressionSpecificScoreAdjustment(RawAccuracyPercent);

		const float EffectiveDeltaSeconds = FMath::Max(DeltaSeconds, 1.f / 120.f);
		const float SmoothSpeed = 10.f;
		const float SmoothingAlpha = 1.f - FMath::Exp(-SmoothSpeed * EffectiveDeltaSeconds);
		CurrentAccuracyPercent = FMath::Lerp(CurrentAccuracyPercent, RawAccuracyPercent, SmoothingAlpha);
		UpdateTherapistPauseState(CurrentAccuracyPercent);

		if (CurrentTimelineSeconds + KINDA_SMALL_NUMBER >= NextSampleTimeSeconds)
		{
			while (CurrentTimelineSeconds + KINDA_SMALL_NUMBER >= NextSampleTimeSeconds)
			{
				FCurveLogFrame SampledTherapistFrame = LiveTherapistFrame;
				SampledTherapistFrame.TimeSeconds = NextSampleTimeSeconds;
				Frames.Add(MoveTemp(SampledTherapistFrame));

				FCurveLogFrame SampledPatientFrame = LivePatientFrame;
				SampledPatientFrame.TimeSeconds = NextSampleTimeSeconds;

				TherapistFramesForExport.Add(Frames.Last());
				PatientFramesForExport.Add(MoveTemp(SampledPatientFrame));

				if (!bIsTherapistAnimationPaused || bHoldingExercisePoseForStage)
				{
					AccumulatedAccuracyPercent += CurrentAccuracyPercent;
					++AccuracySampleCount;

					FCurveAccuracySample AccuracySample;
					AccuracySample.TimeSeconds = NextSampleTimeSeconds;
					AccuracySample.AccuracyPercent = CurrentAccuracyPercent;
					AccuracySamples.Add(AccuracySample);
				}

				NextSampleTimeSeconds += SampleIntervalSeconds;
			}
		}
	}
}

void UCurveLogging::EndCurveMatching(float& OutAverageAccuracyPercent, TArray<FCurveAccuracySample>& OutAccuracySamples)
{
	OutAverageAccuracyPercent = (AccuracySampleCount > 0)
		? (AccumulatedAccuracyPercent / static_cast<float>(AccuracySampleCount))
		: 0.f;

	OutAccuracySamples = AccuracySamples;
	LastCompletedAverageAccuracyPercent = OutAverageAccuracyPercent;

	ExportCurveMatchingDataToJson();
	CompleteBackendSession(FMath::RoundToInt(OutAverageAccuracyPercent));

	if (!bPhase1ProgressSentForCurrentExercise)
	{
		const int32 PatientId = UFacialTherapyApi::GetStoredPatientId();
		const int32 ExerciseId = GetPhase1ExerciseId(ActiveExpressionName);
		if (PatientId > 0 && ExerciseId > 0)
		{
			bPhase1ProgressSentForCurrentExercise = true;
			UFacialTherapyApi::UpdateProgressNative(PatientId, ExerciseId, 0, FString(), true, [](bool bSuccess, const FString&, const FFacialTherapyProgressUpdateResponse&)
			{
				if (bSuccess)
				{
					FFacialTherapyDashboardCallback EmptyCallback;
					UFacialTherapyApi::GenerateCurrentPhaseProgressData(EmptyCallback);
				}
			});
		}
	}

	const FCurveExerciseStageGuidance* FeedbackGuidance = GetFeedbackGuidanceForAccuracy(LastCompletedAverageAccuracyPercent);
	float FeedbackDurationSecondsLocal = 0.f;
	if (FeedbackGuidance)
	{
		if (FeedbackGuidance->Audio)
		{
			FeedbackDurationSecondsLocal = FMath::Max(FeedbackDurationSecondsLocal, FeedbackGuidance->Audio->GetDuration());
			if (USkeletalMeshComponent* Mesh = ActiveMesh.Get())
			{
				if (UWorld* World = Mesh->GetWorld())
				{
					if (UAudioComponent* AudioComponent = ActiveStageAudioComponent.Get())
					{
						AudioComponent->Stop();
					}

					ActiveStageAudioComponent = UGameplayStatics::SpawnSound2D(World, FeedbackGuidance->Audio);
				}
			}
		}

		if (FeedbackGuidance->FaceMontage)
		{
			FeedbackDurationSecondsLocal = FMath::Max(FeedbackDurationSecondsLocal, FeedbackGuidance->FaceMontage->GetPlayLength());
			PlayMontageOnMeshIfNeeded(ActiveMesh.Get(), FeedbackGuidance->FaceMontage);
			PlayMontageOnMeshIfNeeded(ActiveGuidanceBodyMesh.Get(), FeedbackGuidance->FaceMontage);
		}
	}

	if (FeedbackDurationSecondsLocal > KINDA_SMALL_NUMBER)
	{
		bFeedbackStageActive = true;
		FeedbackStageStartWorldTimeSeconds = GetCurrentWorldTimeSeconds();
		FeedbackStageDurationSeconds = FeedbackDurationSecondsLocal;
		bExerciseCompleted = false;
	}
	else
	{
		bExerciseCompleted = true;
		StopLogging();
	}

	bIsMatching = false;
	PatientMesh.Reset();
	ActiveExpressionName.Empty();
}

float UCurveLogging::GetCurrentAccuracyPercent()
{
	return CurrentAccuracyPercent;
}

float UCurveLogging::GetLastExerciseAverageAccuracy()
{
	return LastCompletedAverageAccuracyPercent;
}

FString UCurveLogging::GetExerciseFeedbackTitle(float AverageAccuracyPercent)
{
	return BuildExerciseFeedbackTitle(AverageAccuracyPercent);
}

FString UCurveLogging::GetExerciseFeedbackMessage(float AverageAccuracyPercent)
{
	return BuildExerciseFeedbackMessage(AverageAccuracyPercent);
}

void UCurveLogging::GetCurrentExerciseProgressLite(
	FString& OutStatusText,
	FString& OutPauseGuidanceText,
	int32& OutRemainingTimeSeconds,
	float& OutProgressPercent,
	bool& bOutIsCompleted,
	float& OutAccuracyPercent)
{
	OutPauseGuidanceText.Reset();
	OutRemainingTimeSeconds = 0;
	OutProgressPercent = 0.f;
	bOutIsCompleted = bExerciseCompleted;
	OutAccuracyPercent = 0.f;

	if (bSelectedPhaseExerciseHasState)
	{
		if (bSelectedPhaseExerciseActive && SelectedPhaseSteps.IsValidIndex(SelectedPhaseStepIndex))
		{
			const FSelectedPhaseStep& Step = SelectedPhaseSteps[SelectedPhaseStepIndex];
			float CurrentWorldTimeSeconds = GetCurrentWorldTimeSeconds();
			if (bExerciseManuallyPaused && ManualExercisePauseStartWorldTimeSeconds > 0.f)
			{
				CurrentWorldTimeSeconds = ManualExercisePauseStartWorldTimeSeconds;
			}
			const float StepElapsedSeconds = FMath::Max(CurrentWorldTimeSeconds - SelectedStepStartWorldTimeSeconds, 0.f);
			const float DialogueDurationSeconds = Step.bPlayGuidance ? GetStageDialogueDuration(Step.GuidanceStage) : 0.f;
			const float ActionElapsedSeconds = FMath::Clamp(StepElapsedSeconds - DialogueDurationSeconds, 0.f, Step.ActionDurationSeconds);
			const float RemainingActionSeconds = FMath::Max(Step.ActionDurationSeconds - ActionElapsedSeconds, 0.f);

			OutRemainingTimeSeconds = FMath::Max(FMath::CeilToInt(RemainingActionSeconds), 0);
			OutProgressPercent = Step.ActionDurationSeconds > KINDA_SMALL_NUMBER
				? FMath::Clamp(RemainingActionSeconds / Step.ActionDurationSeconds, 0.f, 1.f)
				: 0.f;
			bOutIsCompleted = false;

			FString NextStepLabel = TEXT("Complete");
			if (SelectedPhaseSteps.IsValidIndex(SelectedPhaseStepIndex + 1))
			{
				NextStepLabel = SelectedPhaseSteps[SelectedPhaseStepIndex + 1].Label;
			}

			const FString StatusValue = bExerciseManuallyPaused ? TEXT("Paused") : StepElapsedSeconds < DialogueDurationSeconds ? TEXT("Listening") : TEXT("In progress");
			const FString NoteText = Step.Note.IsEmpty() ? TEXT("Follow the therapist guidance.") : Step.Note;
			OutStatusText = FString::Printf(
				TEXT("Current stage: %s\nStatus: %s\nNext: %s\nNote: %s"),
				*Step.Label,
				*StatusValue,
				*NextStepLabel,
				*NoteText);

			if (Step.Type == ESelectedPhaseStepType::Hold)
			{
				OutAccuracyPercent = CurrentAccuracyPercent;
			}

			return;
		}

		bOutIsCompleted = bExerciseCompleted;
		OutStatusText = SelectedStatusText.IsEmpty()
			? TEXT("Current stage: Completed\nStatus: Complete\nNext: Next exercise\nNote: Exercise complete.")
			: SelectedStatusText;
		OutAccuracyPercent = LastCompletedAverageAccuracyPercent;
		return;
	}

	float MontageLength = 0.f;
	float WorldTimeSeconds = GetCurrentWorldTimeSeconds();
	if (ActiveMontage.IsValid())
	{
		MontageLength = ActiveMontage->GetPlayLength();
	}

	const float FlowElapsedSeconds = GetEffectiveFlowElapsedSeconds(WorldTimeSeconds);
	const EExerciseUiStage BaseStage = GetBaseExerciseStage(bIsMatching, bExerciseCompleted, FlowElapsedSeconds, MontageLength);
	const bool bStageActionWindowActive = IsStageActionWindowActive(BaseStage, FlowElapsedSeconds, MontageLength);
	const FString StageLabel = GetStageLabel(BaseStage);
	const FString NextStageLabel = GetNextStageLabel(BaseStage);

	if (BaseStage == EExerciseUiStage::Feedback || BaseStage == EExerciseUiStage::Completed)
	{
		const FString FeedbackTitle = GetExerciseFeedbackTitle(LastCompletedAverageAccuracyPercent);
		const FString FeedbackMessage = GetExerciseFeedbackMessage(LastCompletedAverageAccuracyPercent);
		OutStatusText = FString::Printf(TEXT("Feedback\n%s\n%s"), *FeedbackTitle, *FeedbackMessage);
	}
	else if (BaseStage == EExerciseUiStage::Idle)
	{
		OutStatusText = TEXT("Current stage: Idle\nStatus: Ready\nNext: Start the exercise\nNote: Begin when you are ready.");
	}
	else if (bExerciseManuallyPaused)
	{
		if (!IsGuidedExerciseMode() && BaseStage == EExerciseUiStage::RepetitionOne)
		{
			OutStatusText = FString::Printf(
				TEXT("Current stage: %s\nStatus: Paused\nNote: Exercise paused. Press play to continue."),
				*StageLabel);
		}
		else
		{
			OutStatusText = FString::Printf(
				TEXT("Current stage: %s\nStatus: Paused\nNext: %s\nNote: Exercise paused. Press play to continue."),
				*StageLabel,
				*NextStageLabel);
		}
	}
	else if (bIsTherapistAnimationPaused && bPauseTriggeredForCurrentExercise)
	{
		if (!IsGuidedExerciseMode() && BaseStage == EExerciseUiStage::RepetitionOne)
		{
			OutStatusText = FString::Printf(
				TEXT("Current stage: %s\nStatus: Paused\nNote: Match the expression more closely before continuing."),
				*StageLabel);
		}
		else
		{
			OutStatusText = FString::Printf(
				TEXT("Current stage: %s\nStatus: Paused\nNext: %s\nNote: Match the expression more closely before continuing."),
				*StageLabel,
				*NextStageLabel);
		}
	}
	else
	{
		FString NoteText;
		if (BaseStage == EExerciseUiStage::Warmup)
		{
			NoteText = TEXT("Facial massage, gentle gripping, and conscious breathing for 30 seconds.");
		}
		else if (BaseStage == EExerciseUiStage::Introduction)
		{
			NoteText = TEXT("Today we will practice nine facial expressions. Each expression will be held for 15 seconds. Focus on correct muscle activation.");
		}
		else if (BaseStage == EExerciseUiStage::Demonstration)
		{
			NoteText = TEXT("Watch the therapist demonstration and prepare to repeat the expression.");
		}
		else if (BaseStage == EExerciseUiStage::RepetitionOne)
		{
			NoteText = IsGuidedExerciseMode()
				? TEXT("Follow the therapist and hold the expression steadily.")
				: TEXT("Now it is your turn. Repeat the expression and hold it steadily.");
		}
		else if (BaseStage == EExerciseUiStage::RepetitionTwo)
		{
			NoteText = TEXT("Repeat the expression a second time and keep the movement steady.");
		}
		else if (BaseStage == EExerciseUiStage::RepetitionThree)
		{
			NoteText = TEXT("Repeat the expression again and focus on correct muscle activation.");
		}
		else
		{
			NoteText = TEXT("Hold the expression on your own until this stage finishes.");
		}

		if (!IsGuidedExerciseMode() && BaseStage == EExerciseUiStage::RepetitionOne)
		{
			OutStatusText = FString::Printf(
				TEXT("Current stage: %s\nStatus: In progress\nNote: %s"),
				*StageLabel,
				*NoteText);
		}
		else
		{
			OutStatusText = FString::Printf(
				TEXT("Current stage: %s\nStatus: In progress\nNext: %s\nNote: %s"),
				*StageLabel,
				*NextStageLabel,
				*NoteText);
		}
	}

	GetStageTiming(MontageLength, FlowElapsedSeconds, BaseStage, OutRemainingTimeSeconds, OutProgressPercent);

	if (IsScoredStage(BaseStage) && bStageActionWindowActive && (!bIsTherapistAnimationPaused || bHoldingExercisePoseForStage))
	{
		OutAccuracyPercent = CurrentAccuracyPercent;
	}

	if (bIsTherapistAnimationPaused && bPauseTriggeredForCurrentExercise)
	{
		const int32 ResumeSeconds = FMath::Max(FMath::CeilToInt(TherapistPauseResumeWorldTimeSeconds - GetCurrentWorldTimeSeconds()), 0);
		const TCHAR* SecondLabel = ResumeSeconds == 1 ? TEXT("second") : TEXT("seconds");
		OutPauseGuidanceText = FString::Printf(
			TEXT("Animation paused because the expression accuracy dropped. Match the expression more closely and hold steady. Resuming in %d %s."),
			ResumeSeconds,
			SecondLabel);
	}
	else if (bExerciseManuallyPaused)
	{
		OutPauseGuidanceText = TEXT("Exercise paused. Press play to continue.");
	}
}

FCurveExerciseProgressUiState UCurveLogging::GetCurrentExerciseProgressUi()
{
	FCurveExerciseProgressUiState OutState;
	FString StatusText;
	FString PauseGuidanceText;
	int32 RemainingTimeSeconds = 0;
	float ProgressPercent = 0.f;
	bool bIsCompleted = false;
	float AccuracyPercent = 0.f;

	GetCurrentExerciseProgressLite(StatusText, PauseGuidanceText, RemainingTimeSeconds, ProgressPercent, bIsCompleted, AccuracyPercent);

	OutState.StatusText = MoveTemp(StatusText);
	OutState.PauseGuidanceText = MoveTemp(PauseGuidanceText);
	OutState.RemainingTimeSeconds = RemainingTimeSeconds;
	OutState.ProgressPercent = ProgressPercent;
	OutState.bIsCompleted = bIsCompleted;
	OutState.AccuracyPercent = AccuracyPercent;

	return OutState;
}

FString UCurveLogging::GetCurrentExerciseDebugText()
{
	auto BoolText = [](bool bValue) -> const TCHAR*
	{
		return bValue ? TEXT("true") : TEXT("false");
	};

	auto DescribeMesh = [BoolText](const TCHAR* Label, TWeakObjectPtr<USkeletalMeshComponent> MeshPtr) -> FString
	{
		USkeletalMeshComponent* Mesh = MeshPtr.Get();
		UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
		FCurveLogFrame Frame;
		const bool bCaptured = UCurveLogging::CaptureCurvesForMesh(Mesh, Frame, 0.f);
		return FString::Printf(
			TEXT("%s: valid=%s registered=%s anim_instance=%s capture=%s curve_count=%d %s"),
			Label,
			BoolText(Mesh != nullptr),
			BoolText(Mesh && Mesh->IsRegistered()),
			AnimInstance ? *AnimInstance->GetName() : TEXT("none"),
			BoolText(bCaptured),
			Frame.Curves.Num(),
			*DescribeMeshIdentity(Mesh));
	};

	auto DescribeGuidance = [BoolText](const TCHAR* Label, const FCurveExerciseStageGuidance& Guidance) -> FString
	{
		return FString::Printf(
			TEXT("%s: face_montage=%s audio=%s"),
			Label,
			Guidance.FaceMontage ? *Guidance.FaceMontage->GetName() : TEXT("none"),
			Guidance.Audio ? *Guidance.Audio->GetName() : TEXT("none"));
	};

	USkeletalMeshComponent* ActiveMeshPtr = ActiveMesh.Get();
	UAnimInstance* ActiveAnimInstance = ActiveMeshPtr ? ActiveMeshPtr->GetAnimInstance() : nullptr;
	const bool bActiveMontagePlaying = ActiveAnimInstance && ActiveMontage.IsValid()
		? ActiveAnimInstance->Montage_IsPlaying(ActiveMontage.Get())
		: false;
	const FString MontageDiagnostics = RecentMontageDiagnostics.Num() > 0
		? FString::Join(RecentMontageDiagnostics, TEXT("\n"))
		: TEXT("none");

	return FString::Printf(
		TEXT("is_logging=%s\nis_matching=%s\nselected_phase_active=%s\nselected_phase_has_state=%s\nguided_mode=%s\nhas_guidance_config=%s\nactive_expression=%s\nselected_item_id=%d\nselected_step_index=%d\nconfigured_montages=%d\nactive_montage=%s\nactive_montage_playing=%s\ntherapist_mesh: %s\ntherapist_body_mesh: %s\npatient_mesh: %s\nguidance_warmup: %s\nguidance_introduction: %s\nguidance_demonstration: %s\nguidance_repetition_one: %s\nguidance_repetition_two: %s\nguidance_repetition_three: %s\nrecent_montage_debug:\n%s\nselected_status=%s"),
		BoolText(bIsLogging),
		BoolText(bIsMatching),
		BoolText(bSelectedPhaseExerciseActive),
		BoolText(bSelectedPhaseExerciseHasState),
		BoolText(IsGuidedExerciseMode()),
		BoolText(bHasGuidanceConfig),
		*ActiveExpressionName,
		SelectedItemId,
		SelectedPhaseStepIndex,
		ConfiguredExpressionMontages.Num(),
		ActiveMontage.IsValid() ? *ActiveMontage->GetName() : TEXT("none"),
		BoolText(bActiveMontagePlaying),
		*DescribeMesh(TEXT("therapist"), ActiveMesh),
		*DescribeMesh(TEXT("therapist_body"), ActiveGuidanceBodyMesh),
		*DescribeMesh(TEXT("patient"), PatientMesh),
		*DescribeGuidance(TEXT("warmup"), ActiveGuidanceConfig.Warmup),
		*DescribeGuidance(TEXT("introduction"), ActiveGuidanceConfig.Introduction),
		*DescribeGuidance(TEXT("demonstration"), ActiveGuidanceConfig.Demonstration),
		*DescribeGuidance(TEXT("repetition_one"), ActiveGuidanceConfig.RepetitionOne),
		*DescribeGuidance(TEXT("repetition_two"), ActiveGuidanceConfig.RepetitionTwo),
		*DescribeGuidance(TEXT("repetition_three"), ActiveGuidanceConfig.RepetitionThree),
		*MontageDiagnostics,
		*SelectedStatusText);
}

void UCurveLogging::GetAccuracySamples(TArray<FCurveAccuracySample>& OutAccuracySamples)
{
	OutAccuracySamples = AccuracySamples;
}

bool UCurveLogging::GetTherapistPauseState(float& OutRemainingPauseSeconds)
{
	if (!bIsTherapistAnimationPaused)
	{
		OutRemainingPauseSeconds = 0.f;
		return false;
	}

	OutRemainingPauseSeconds = FMath::Max(TherapistPauseResumeWorldTimeSeconds - GetCurrentWorldTimeSeconds(), 0.f);
	return true;
}

void UCurveLogging::FinalizeSelectedPhaseExerciseScore()
{
	if (bSelectedCompletionRecorded)
	{
		return;
	}

	bSelectedCompletionRecorded = true;
	LastCompletedAverageAccuracyPercent = (AccuracySampleCount > 0)
		? (AccumulatedAccuracyPercent / static_cast<float>(AccuracySampleCount))
		: CurrentAccuracyPercent;

	ActiveExpressionName = SelectedItemTitle.IsEmpty() ? TEXT("SelectedPhaseExercise") : SelectedItemTitle;
	ExportCurveMatchingDataToJson();
	CompleteBackendSession(FMath::RoundToInt(LastCompletedAverageAccuracyPercent));

	if (!bSelectedProgressSentForCurrentExercise)
	{
		const int32 PatientId = UFacialTherapyApi::GetStoredPatientId();
		if (PatientId > 0)
		{
			bSelectedProgressSentForCurrentExercise = true;
			auto RefreshPhaseProgressData = [](bool bSuccess, const FString&, const FFacialTherapyProgressUpdateResponse&)
			{
				if (bSuccess)
				{
					FFacialTherapyDashboardCallback EmptyCallback;
					UFacialTherapyApi::GenerateCurrentPhaseProgressData(EmptyCallback);
				}
			};

			if (SelectedItemType == TEXT("Pair") && SelectedPairNumber > 0)
			{
				UFacialTherapyApi::UpdateProgressNative(PatientId, 0, SelectedPairNumber, FString(), true, RefreshPhaseProgressData);
			}
			else if ((SelectedItemType == TEXT("Sequence") || SelectedItemType == TEXT("Mixed")) && !SelectedSequenceCode.IsEmpty())
			{
				UFacialTherapyApi::UpdateProgressNative(PatientId, 0, 0, SelectedSequenceCode, true, RefreshPhaseProgressData);
			}
			else if (SelectedItemId > 0 && SelectedItemId < 100)
			{
				UFacialTherapyApi::UpdateProgressNative(PatientId, SelectedItemId, 0, FString(), true, RefreshPhaseProgressData);
			}
		}
	}
}

void UCurveLogging::TickSelectedPhaseExercise(float DeltaSeconds)
{
	if (!bSelectedPhaseExerciseActive || !SelectedPhaseSteps.IsValidIndex(SelectedPhaseStepIndex))
	{
		return;
	}

	if (!ActiveMesh.IsValid() || !ActiveMesh->IsRegistered() || !PatientMesh.IsValid() || !PatientMesh->IsRegistered())
	{
		bSelectedPhaseExerciseActive = false;
		bIsMatching = false;
		bIsLogging = false;
		SelectedStatusText = TEXT("Exercise stopped because a mesh became unavailable.");
		bExerciseCompleted = true;
		return;
	}

	FSelectedPhaseStep& Step = SelectedPhaseSteps[SelectedPhaseStepIndex];
	const float WorldTimeSeconds = GetCurrentWorldTimeSeconds();

	if (!bSelectedPhaseStepStarted)
	{
		bSelectedPhaseStepStarted = true;
		bSelectedActionStarted = false;
		bSelectedHoldScoringInitialized = false;
		bHoldingExercisePoseForStage = false;
		SelectedStepStartWorldTimeSeconds = WorldTimeSeconds;
		SelectedStatusText = Step.Label;

		if (bIsTherapistAnimationPaused && !bPauseTriggeredForCurrentExercise)
		{
			ResumeTherapistAnimationForFlow();
		}

		if (Step.bPlayGuidance)
		{
			PlayGuidanceAudioIfNeeded(ActiveMesh.Get(), Step.GuidanceStage);
			PlayGuidanceMontageIfNeeded(ActiveMesh.Get(), Step.GuidanceStage);
		}

		if (Step.Type == ESelectedPhaseStepType::Feedback)
		{
			const FCurveExerciseStageGuidance* FeedbackGuidance = GetFeedbackGuidanceForAccuracy(LastCompletedAverageAccuracyPercent);
			if (FeedbackGuidance && FeedbackGuidance->Audio)
			{
				if (UWorld* World = ActiveMesh->GetWorld())
				{
					if (UAudioComponent* AudioComponent = ActiveStageAudioComponent.Get())
					{
						AudioComponent->Stop();
					}
					ActiveStageAudioComponent = UGameplayStatics::SpawnSound2D(World, FeedbackGuidance->Audio);
				}
			}
			if (FeedbackGuidance && FeedbackGuidance->FaceMontage)
			{
				PlayMontageOnMeshIfNeeded(ActiveMesh.Get(), FeedbackGuidance->FaceMontage);
				PlayMontageOnMeshIfNeeded(ActiveGuidanceBodyMesh.Get(), FeedbackGuidance->FaceMontage);
			}
		}
	}

	const float DialogueDurationSeconds = Step.bPlayGuidance ? GetStageDialogueDuration(Step.GuidanceStage) : 0.f;
	const float StepElapsedSeconds = FMath::Max(WorldTimeSeconds - SelectedStepStartWorldTimeSeconds, 0.f);
	if (StepElapsedSeconds < DialogueDurationSeconds)
	{
		return;
	}

	if (!bSelectedActionStarted)
	{
		bSelectedActionStarted = true;
		if (UAnimMontage* StepMontage = Step.Montage.Get())
		{
			if (bIsTherapistAnimationPaused && !bPauseTriggeredForCurrentExercise)
			{
				ResumeTherapistAnimationForFlow();
			}

			ActiveMontage = StepMontage;
			ActiveExpressionName = Step.ExpressionName;
			if (Step.Type == ESelectedPhaseStepType::Hold)
			{
				ShowFrozenMontagePose(ActiveMesh.Get(), StepMontage);
				ShowFrozenMontagePose(ActiveGuidanceBodyMesh.Get(), StepMontage);
				bIsTherapistAnimationPaused = true;
				PausedMontageTimeSeconds = StepMontage->GetPlayLength() * 0.5f;
				bHoldingExercisePoseForStage = true;
			}
			else
			{
				PlayMontageOnMeshIfNeeded(ActiveMesh.Get(), StepMontage);
				PlayMontageOnMeshIfNeeded(ActiveGuidanceBodyMesh.Get(), StepMontage);
			}
		}
		else
		{
			ActiveMontage.Reset();
			ActiveExpressionName = Step.Type == ESelectedPhaseStepType::Rest ? TEXT("Rest") : Step.Label;
		}

		if (Step.Type == ESelectedPhaseStepType::Hold)
		{
			Frames.Reset();
			NextSampleTimeSeconds = SelectedScoredTimelineBaseSeconds;
			ActiveExpressionName = Step.ExpressionName;
		}
	}

	const float ActionElapsedSeconds = FMath::Clamp(StepElapsedSeconds - DialogueDurationSeconds, 0.f, Step.ActionDurationSeconds);
	const bool bIsHoldStep = Step.Type == ESelectedPhaseStepType::Hold && Step.bScored;
	if (bIsHoldStep)
	{
		const float CurrentTimelineSeconds = SelectedScoredTimelineBaseSeconds + ActionElapsedSeconds;
		FCurveLogFrame LiveTherapistFrame;
		FCurveLogFrame LivePatientFrame;
		if (CaptureCurvesForMesh(ActiveMesh.Get(), LiveTherapistFrame, CurrentTimelineSeconds) &&
			CaptureCurvesForMesh(PatientMesh.Get(), LivePatientFrame, CurrentTimelineSeconds))
		{
			if (!bSelectedHoldScoringInitialized)
			{
				Frames.Reset();
				LiveTherapistFrame.TimeSeconds = CurrentTimelineSeconds;
				LivePatientFrame.TimeSeconds = CurrentTimelineSeconds;
				Frames.Add(LiveTherapistFrame);
				TherapistFramesForExport.Add(LiveTherapistFrame);
				PatientFramesForExport.Add(LivePatientFrame);

				const float RawInitialAccuracyPercent = CalculateAccuracyPercent(LiveTherapistFrame, LivePatientFrame, MatchingTolerance);
				CurrentAccuracyPercent = ApplyExpressionSpecificScoreAdjustment(RawInitialAccuracyPercent);
				AccumulatedAccuracyPercent += CurrentAccuracyPercent;
				++AccuracySampleCount;

				FCurveAccuracySample AccuracySample;
				AccuracySample.TimeSeconds = CurrentTimelineSeconds;
				AccuracySample.AccuracyPercent = CurrentAccuracyPercent;
				AccuracySamples.Add(AccuracySample);

				NextSampleTimeSeconds = CurrentTimelineSeconds + SampleIntervalSeconds;
				bSelectedHoldScoringInitialized = true;
			}
			else
			{
				float RawAccuracyPercent = CalculateAccuracyPercent(LiveTherapistFrame, LivePatientFrame, MatchingTolerance);
				const float MaxReactionDelaySeconds = 0.15f;
				for (int32 FrameIndex = Frames.Num() - 1; FrameIndex >= 0; --FrameIndex)
				{
					const FCurveLogFrame& TherapistHistoryFrame = Frames[FrameIndex];
					if ((CurrentTimelineSeconds - TherapistHistoryFrame.TimeSeconds) > MaxReactionDelaySeconds)
					{
						break;
					}
					RawAccuracyPercent = FMath::Max(RawAccuracyPercent, CalculateAccuracyPercent(TherapistHistoryFrame, LivePatientFrame, MatchingTolerance));
				}

				RawAccuracyPercent = ApplyExpressionSpecificScoreAdjustment(RawAccuracyPercent);
				const float EffectiveDeltaSeconds = FMath::Max(DeltaSeconds, 1.f / 120.f);
				const float SmoothingAlpha = 1.f - FMath::Exp(-10.f * EffectiveDeltaSeconds);
				CurrentAccuracyPercent = FMath::Lerp(CurrentAccuracyPercent, RawAccuracyPercent, SmoothingAlpha);

				while (CurrentTimelineSeconds + KINDA_SMALL_NUMBER >= NextSampleTimeSeconds)
				{
					FCurveLogFrame SampledTherapistFrame = LiveTherapistFrame;
					SampledTherapistFrame.TimeSeconds = NextSampleTimeSeconds;
					Frames.Add(MoveTemp(SampledTherapistFrame));

					FCurveLogFrame SampledPatientFrame = LivePatientFrame;
					SampledPatientFrame.TimeSeconds = NextSampleTimeSeconds;
					TherapistFramesForExport.Add(Frames.Last());
					PatientFramesForExport.Add(MoveTemp(SampledPatientFrame));

					AccumulatedAccuracyPercent += CurrentAccuracyPercent;
					++AccuracySampleCount;

					FCurveAccuracySample AccuracySample;
					AccuracySample.TimeSeconds = NextSampleTimeSeconds;
					AccuracySample.AccuracyPercent = CurrentAccuracyPercent;
					AccuracySamples.Add(AccuracySample);
					NextSampleTimeSeconds += SampleIntervalSeconds;
				}
			}
		}
	}

	if (ActionElapsedSeconds < Step.ActionDurationSeconds)
	{
		return;
	}

	if (UAnimMontage* StepMontage = Step.Montage.Get())
	{
		if (bIsTherapistAnimationPaused && !bPauseTriggeredForCurrentExercise)
		{
			ResumeTherapistAnimationForFlow();
		}

		StopMontageOnMeshIfNeeded(ActiveMesh.Get(), StepMontage);
		StopMontageOnMeshIfNeeded(ActiveGuidanceBodyMesh.Get(), StepMontage);
	}

	if (bIsHoldStep)
	{
		SelectedScoredTimelineBaseSeconds += Step.ActionDurationSeconds;
	}

	++SelectedPhaseStepIndex;
	bSelectedPhaseStepStarted = false;
	bSelectedActionStarted = false;
	bSelectedHoldScoringInitialized = false;
	bHoldingExercisePoseForStage = false;

	if (SelectedPhaseSteps.IsValidIndex(SelectedPhaseStepIndex) &&
		SelectedPhaseSteps[SelectedPhaseStepIndex].Type == ESelectedPhaseStepType::Feedback)
	{
		FinalizeSelectedPhaseExerciseScore();
	}

	if (!SelectedPhaseSteps.IsValidIndex(SelectedPhaseStepIndex))
	{
		FinalizeSelectedPhaseExerciseScore();
		bSelectedPhaseExerciseActive = false;
		bIsMatching = false;
		bIsLogging = false;
		bExerciseCompleted = true;
		SelectedStatusText = FString::Printf(TEXT("Feedback\n%s\n%s"),
			*GetExerciseFeedbackTitle(LastCompletedAverageAccuracyPercent),
			*GetExerciseFeedbackMessage(LastCompletedAverageAccuracyPercent));
		PatientMesh.Reset();
		ActiveExpressionName.Empty();
	}
}

float UCurveLogging::GetCurrentTimeSeconds()
{
	if (ActiveMontage.IsValid())
	{
		if (USkeletalMeshComponent* Mesh = ActiveMesh.Get())
		{
			if (UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
			{
				if (bIsTherapistAnimationPaused)
				{
					return PausedMontageTimeSeconds;
				}

				const float MontagePosition = AnimInstance->Montage_GetPosition(ActiveMontage.Get());
				if (MontagePosition > 0.f || AnimInstance->Montage_IsPlaying(ActiveMontage.Get()))
				{
					return MontagePosition;
				}
			}
		}
	}

	if (USkeletalMeshComponent* Mesh = ActiveMesh.Get())
	{
		if (UWorld* World = Mesh->GetWorld())
		{
			return World->GetTimeSeconds() - StartTimeSeconds;
		}
	}

	return 0.f;
}

float UCurveLogging::GetCurrentWorldTimeSeconds()
{
	if (USkeletalMeshComponent* Mesh = ActiveMesh.Get())
	{
		if (UWorld* World = Mesh->GetWorld())
		{
			return World->GetTimeSeconds();
		}
	}

	return 0.f;
}

bool UCurveLogging::CaptureCurves(FCurveLogFrame& OutFrame, float SampleTimeSeconds)
{
	USkeletalMeshComponent* Mesh = ActiveMesh.Get();
	return CaptureCurvesForMesh(Mesh, OutFrame, SampleTimeSeconds);
}

bool UCurveLogging::CaptureCurvesForMesh(USkeletalMeshComponent* Mesh, FCurveLogFrame& OutFrame, float SampleTimeSeconds)
{
	if (!Mesh || !Mesh->IsRegistered())
	{
		return false;
	}

	UAnimInstance* AnimInstance = Mesh->GetAnimInstance();
	if (!AnimInstance)
	{
		return false;
	}

	OutFrame.TimeSeconds = SampleTimeSeconds;
	OutFrame.Curves.Reset();

	const TMap<FName, float>& AttributeCurves = AnimInstance->GetAnimationCurveList(EAnimCurveType::AttributeCurve);
	const TMap<FName, float>& MaterialCurves = AnimInstance->GetAnimationCurveList(EAnimCurveType::MaterialCurve);
	const TMap<FName, float>& MorphCurves = AnimInstance->GetAnimationCurveList(EAnimCurveType::MorphTargetCurve);

	for (const TPair<FName, float>& Pair : AttributeCurves)
	{
		OutFrame.Curves.Add(Pair.Key, Pair.Value);
	}
	for (const TPair<FName, float>& Pair : MaterialCurves)
	{
		OutFrame.Curves.Add(Pair.Key, Pair.Value);
	}
	for (const TPair<FName, float>& Pair : MorphCurves)
	{
		OutFrame.Curves.Add(Pair.Key, Pair.Value);
	}

	return true;
}

float UCurveLogging::CalculateAccuracyPercent(const FCurveLogFrame& TherapistFrame, const FCurveLogFrame& PatientFrame, float Tolerance)
{
	const float SafeTolerance = FMath::Max(Tolerance, KINDA_SMALL_NUMBER);
	const auto ComputeScore = [&](bool bMirrorPatient) -> float
	{
		const float TherapistActiveThreshold = 0.04f;
		const float TherapistInactiveThreshold = 0.02f;
		const float PatientExtraThreshold = 0.05f;

		struct FRegionStats
		{
			float TherapistSum = 0.f;
			float PatientSum = 0.f;
			float TherapistMax = 0.f;
			float PatientMax = 0.f;
		};

		FRegionStats Regions[7];
		float CurveWeightedSimilaritySum = 0.f;
		float CurveWeightSum = 0.f;
		float TherapistActiveEnergy = 0.f;
		float PatientExtraEnergy = 0.f;

		for (const TPair<FName, float>& TherapistCurve : TherapistFrame.Curves)
		{
			if (!IsRelevantFaceCurveName(TherapistCurve.Key))
			{
				continue;
			}

			const float TherapistAbs = FMath::Abs(TherapistCurve.Value);
			if (TherapistAbs < TherapistActiveThreshold)
			{
				continue;
			}

			const float PatientValue = FindCurveValueForMatch(PatientFrame.Curves, TherapistCurve.Key, bMirrorPatient);
			const float PatientAbs = FMath::Abs(PatientValue);
			const float Difference = FMath::Abs(TherapistCurve.Value - PatientValue);

			const float CurveTolerance = FMath::Max(SafeTolerance, TherapistAbs * 0.35f);
			const float CurveSimilarity01 = 1.f - FMath::Clamp(Difference / CurveTolerance, 0.f, 1.f);
			const float CurveWeight = FMath::Sqrt(TherapistAbs);

			CurveWeightedSimilaritySum += CurveSimilarity01 * CurveWeight;
			CurveWeightSum += CurveWeight;
			TherapistActiveEnergy += TherapistAbs;

			const int32 RegionIndex = static_cast<int32>(GetFaceRegion(TherapistCurve.Key));
			FRegionStats& Region = Regions[RegionIndex];
			Region.TherapistSum += TherapistAbs;
			Region.PatientSum += PatientAbs;
			Region.TherapistMax = FMath::Max(Region.TherapistMax, TherapistAbs);
			Region.PatientMax = FMath::Max(Region.PatientMax, PatientAbs);
		}

		if (CurveWeightSum <= KINDA_SMALL_NUMBER)
		{
			return 0.f;
		}

		const float CurveSimilarityPercent = (CurveWeightedSimilaritySum / CurveWeightSum) * 100.f;

		float RegionSimilarityWeightedSum = 0.f;
		float RegionWeightSum = 0.f;
		for (int32 RegionIndex = 0; RegionIndex < UE_ARRAY_COUNT(Regions); ++RegionIndex)
		{
			const FRegionStats& Region = Regions[RegionIndex];
			if (Region.TherapistSum < TherapistActiveThreshold)
			{
				continue;
			}

			const float RegionWeight = Region.TherapistSum;
			const float RegionTolerance = FMath::Max(SafeTolerance * 2.f, Region.TherapistSum * 0.35f);
			const float SumSimilarity = 1.f - FMath::Clamp(FMath::Abs(Region.TherapistSum - Region.PatientSum) / RegionTolerance, 0.f, 1.f);
			const float PeakTolerance = FMath::Max(SafeTolerance, Region.TherapistMax * 0.35f);
			const float PeakSimilarity = 1.f - FMath::Clamp(FMath::Abs(Region.TherapistMax - Region.PatientMax) / PeakTolerance, 0.f, 1.f);
			const float RegionSimilarity = (SumSimilarity * 0.7f) + (PeakSimilarity * 0.3f);

			RegionSimilarityWeightedSum += RegionSimilarity * RegionWeight;
			RegionWeightSum += RegionWeight;
		}

		const float RegionSimilarityPercent = (RegionWeightSum > KINDA_SMALL_NUMBER)
			? ((RegionSimilarityWeightedSum / RegionWeightSum) * 100.f)
			: CurveSimilarityPercent;

		for (const TPair<FName, float>& PatientCurve : PatientFrame.Curves)
		{
			if (!IsRelevantFaceCurveName(PatientCurve.Key))
			{
				continue;
			}

			const float PatientAbs = FMath::Abs(PatientCurve.Value);
			if (PatientAbs < PatientExtraThreshold)
			{
				continue;
			}

			const float TherapistMatchedValue = FindCurveValueForMatch(TherapistFrame.Curves, PatientCurve.Key, bMirrorPatient);
			const float TherapistAbs = FMath::Abs(TherapistMatchedValue);
			if (TherapistAbs > TherapistInactiveThreshold)
			{
				continue;
			}

			PatientExtraEnergy += (PatientAbs - TherapistAbs);
		}

		const float ExtraPenaltyPercent = (TherapistActiveEnergy > KINDA_SMALL_NUMBER)
			? (FMath::Clamp(PatientExtraEnergy / (TherapistActiveEnergy + SafeTolerance), 0.f, 1.f) * 100.f)
			: 0.f;

		const float BlendedBasePercent = (RegionSimilarityPercent * 0.65f) + (CurveSimilarityPercent * 0.35f);
		return FMath::Clamp(BlendedBasePercent - (ExtraPenaltyPercent * 0.8f), 0.f, 100.f);
	};

	const float DirectScore = ComputeScore(false);
	const float MirroredScore = ComputeScore(true);
	return FMath::Max(DirectScore, MirroredScore);
}

bool UCurveLogging::IsCustomTherapyExpression(const FString& ExpressionName)
{
	const FString Normalized = ExpressionName.TrimStartAndEnd();
	return Normalized.Equals(TEXT("Puff Cheeks"), ESearchCase::IgnoreCase)
		|| Normalized.Equals(TEXT("Pout"), ESearchCase::IgnoreCase);
}

float UCurveLogging::ApplyExpressionSpecificScoreAdjustment(float RawPercent)
{
	const float ClampedRaw = FMath::Clamp(RawPercent, 0.f, 100.f);
	const FString Normalized = ActiveExpressionName.TrimStartAndEnd();

	if (Normalized.IsEmpty())
	{
		return ClampedRaw;
	}

	if (IsCustomTherapyExpression(Normalized))
	{
		return ClampedRaw;
	}

	if (ClampedRaw <= 10.f)
	{
		return ClampedRaw;
	}

	if (ClampedRaw <= 20.f)
	{
		return FMath::GetMappedRangeValueClamped(FVector2D(10.f, 20.f), FVector2D(10.f, 60.f), ClampedRaw);
	}

	return FMath::GetMappedRangeValueClamped(FVector2D(20.f, 100.f), FVector2D(80.f, 100.f), ClampedRaw);
}

void UCurveLogging::UpdateTherapistPauseState(float CurrentAccuracy)
{
	if (bHoldingExercisePoseForStage && !bPauseTriggeredForCurrentExercise)
	{
		return;
	}

	if (!bIsMatching || !ActiveMesh.IsValid() || !ActiveMontage.IsValid())
	{
		return;
	}

	const float WorldTimeSeconds = GetCurrentWorldTimeSeconds();
	const float FlowElapsedSeconds = GetEffectiveFlowElapsedSeconds(WorldTimeSeconds);
	const float MontageLengthSeconds = ActiveMontage->GetPlayLength();
	const EExerciseUiStage CurrentStage = GetBaseExerciseStage(true, bExerciseCompleted, FlowElapsedSeconds, MontageLengthSeconds);
	if (!IsScoredStage(CurrentStage) || !IsStageActionWindowActive(CurrentStage, FlowElapsedSeconds, MontageLengthSeconds))
	{
		if (bIsTherapistAnimationPaused)
		{
			ResumeTherapistAnimation(false);
		}
		bPauseRequiresRecoveryAboveThreshold = false;
		return;
	}

	const float StageActionElapsedSeconds = GetStageActionElapsedSeconds(CurrentStage, FlowElapsedSeconds, MontageLengthSeconds);

	if (bPauseRequiresRecoveryAboveThreshold && CurrentAccuracy >= AccuracyPauseThresholdPercent)
	{
		bPauseRequiresRecoveryAboveThreshold = false;
	}

	if (bIsTherapistAnimationPaused)
	{
		if (CurrentAccuracy >= AccuracyPauseThresholdPercent)
		{
			ResumeTherapistAnimation(false);
		}
		else if (GetCurrentWorldTimeSeconds() >= TherapistPauseResumeWorldTimeSeconds)
		{
			ResumeTherapistAnimation(true);
		}

		return;
	}

	if (bPauseRequiresRecoveryAboveThreshold)
	{
		return;
	}

	if (bPauseTriggeredForCurrentExercise)
	{
		return;
	}

	const float PauseCheckStartSeconds = AccuracyScoringStartDelaySeconds + AccuracyPauseDelaySeconds;
	if (StageActionElapsedSeconds < PauseCheckStartSeconds)
	{
		return;
	}

	if (CurrentAccuracy < AccuracyPauseThresholdPercent)
	{
		if (UAnimMontage* Montage = ActiveMontage.Get())
		{
			const float RemainingMontageTimeSeconds = Montage->GetPlayLength() - GetCurrentTimeSeconds();
			if (RemainingMontageTimeSeconds < 1.f)
			{
				return;
			}
		}

		PauseTherapistAnimation();
	}
}

void UCurveLogging::PauseTherapistAnimationForFlow()
{
	if (bIsTherapistAnimationPaused)
	{
		return;
	}

	USkeletalMeshComponent* Mesh = ActiveMesh.Get();
	if (!Mesh)
	{
		return;
	}

	UAnimInstance* AnimInstance = Mesh->GetAnimInstance();
	if (!AnimInstance || !ActiveMontage.IsValid())
	{
		return;
	}

	PausedMontageTimeSeconds = AnimInstance->Montage_GetPosition(ActiveMontage.Get());
	AnimInstance->Montage_Pause(ActiveMontage.Get());
	bIsTherapistAnimationPaused = true;
	TherapistPauseResumeWorldTimeSeconds = 0.f;
}

void UCurveLogging::ResumeTherapistAnimationForFlow()
{
	if (!bIsTherapistAnimationPaused)
	{
		return;
	}

	if (USkeletalMeshComponent* Mesh = ActiveMesh.Get())
	{
		if (UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
		{
			if (ActiveMontage.IsValid())
			{
				AnimInstance->Montage_Resume(ActiveMontage.Get());
			}
		}
	}

	bIsTherapistAnimationPaused = false;
	TherapistPauseResumeWorldTimeSeconds = 0.f;
	PausedMontageTimeSeconds = 0.f;
}

void UCurveLogging::PauseTherapistAnimation()
{
	if (bIsTherapistAnimationPaused)
	{
		return;
	}

	USkeletalMeshComponent* Mesh = ActiveMesh.Get();
	if (!Mesh)
	{
		return;
	}

	UAnimInstance* AnimInstance = Mesh->GetAnimInstance();
	if (!AnimInstance || !ActiveMontage.IsValid())
	{
		return;
	}

	PausedMontageTimeSeconds = AnimInstance->Montage_GetPosition(ActiveMontage.Get());
	AnimInstance->Montage_Pause(ActiveMontage.Get());
	bIsTherapistAnimationPaused = true;
	bPauseTriggeredForCurrentExercise = true;
	CurrentAccuracyPauseStartWorldTimeSeconds = GetCurrentWorldTimeSeconds();
	TherapistPauseResumeWorldTimeSeconds = GetCurrentWorldTimeSeconds() + AccuracyPauseDurationSeconds;
}

void UCurveLogging::ResumeTherapistAnimation(bool bTimedOutWhileStillBelowThreshold)
{
	if (!bIsTherapistAnimationPaused)
	{
		return;
	}

	if (USkeletalMeshComponent* Mesh = ActiveMesh.Get())
	{
		if (UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
		{
			if (ActiveMontage.IsValid())
			{
				AnimInstance->Montage_Resume(ActiveMontage.Get());
			}
		}
	}

	bIsTherapistAnimationPaused = false;
	bPauseRequiresRecoveryAboveThreshold = bTimedOutWhileStillBelowThreshold;
	if (CurrentAccuracyPauseStartWorldTimeSeconds > 0.f)
	{
		AccumulatedAccuracyPauseSeconds += FMath::Max(GetCurrentWorldTimeSeconds() - CurrentAccuracyPauseStartWorldTimeSeconds, 0.f);
		CurrentAccuracyPauseStartWorldTimeSeconds = 0.f;
	}
	TherapistPauseResumeWorldTimeSeconds = 0.f;
	PausedMontageTimeSeconds = 0.f;
}

void UCurveLogging::ExportCurveMatchingDataToJson()
{
	if (TherapistFramesForExport.Num() == 0 && PatientFramesForExport.Num() == 0)
	{
		return;
	}

	const FString SafeExpressionName = MakeSafeExportExpressionName(ActiveExpressionName);
	const FString ExportDirectory = FPaths::Combine(FPaths::ProjectDir(), TEXT("ExportedCurves"));
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*ExportDirectory);

	int32 FileIndex = 1;
	while (true)
	{
		const FString TherapistFilePath = FPaths::Combine(
			ExportDirectory,
			FString::Printf(TEXT("%s-Therapist%d.json"), *SafeExpressionName, FileIndex));
		const FString PatientFilePath = FPaths::Combine(
			ExportDirectory,
			FString::Printf(TEXT("%s-Patient%d.json"), *SafeExpressionName, FileIndex));

		if (!PlatformFile.FileExists(*TherapistFilePath) && !PlatformFile.FileExists(*PatientFilePath))
		{
			break;
		}

		++FileIndex;
	}

	ExportFramesToJsonFile(SafeExpressionName, TEXT("Therapist"), FileIndex, TherapistFramesForExport);
	ExportFramesToJsonFile(SafeExpressionName, TEXT("Patient"), FileIndex, PatientFramesForExport);
}

bool UCurveLogging::ExportFramesToJsonFile(const FString& ExpressionName, const FString& SubjectLabel, int32 Index, const TArray<FCurveLogFrame>& InFrames)
{
	const FString ExportDirectory = FPaths::Combine(FPaths::ProjectDir(), TEXT("ExportedCurves"));
	const FString FilePath = FPaths::Combine(
		ExportDirectory,
		FString::Printf(TEXT("%s-%s%d.json"), *ExpressionName, *SubjectLabel, Index));

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("ExpressionName"), ActiveExpressionName);
	RootObject->SetStringField(TEXT("Subject"), SubjectLabel);

	TArray<TSharedPtr<FJsonValue>> FrameArray;
	FrameArray.Reserve(InFrames.Num());

	for (const FCurveLogFrame& Frame : InFrames)
	{
		TSharedRef<FJsonObject> FrameObject = MakeShared<FJsonObject>();
		FrameObject->SetNumberField(TEXT("TimeSeconds"), Frame.TimeSeconds);

		TSharedRef<FJsonObject> CurvesObject = MakeShared<FJsonObject>();
		for (const TPair<FName, float>& CurvePair : Frame.Curves)
		{
			CurvesObject->SetNumberField(CurvePair.Key.ToString(), CurvePair.Value);
		}

		FrameObject->SetObjectField(TEXT("Curves"), CurvesObject);
		FrameArray.Add(MakeShared<FJsonValueObject>(FrameObject));
	}

	RootObject->SetArrayField(TEXT("Frames"), FrameArray);

	FString JsonOutput;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonOutput);
	if (!FJsonSerializer::Serialize(RootObject, Writer))
	{
		return false;
	}

	return FFileHelper::SaveStringToFile(JsonOutput, *FilePath);
}

FString UCurveLogging::MakeSafeExportExpressionName(const FString& InExpressionName)
{
	FString SafeName = InExpressionName.TrimStartAndEnd();
	if (SafeName.IsEmpty())
	{
		SafeName = TEXT("Expression");
	}

	const TCHAR* InvalidChars = TEXT("<>:\"/\\|?*");
	for (int32 CharIndex = 0; InvalidChars[CharIndex] != '\0'; ++CharIndex)
	{
		SafeName.ReplaceCharInline(InvalidChars[CharIndex], TEXT('_'));
	}

	return SafeName;
}

void UCurveLogging::BeginBackendSession()
{
	ActiveBackendSessionId = 0;
	PendingBackendCompletionAccuracyScore = 0;
	bBackendSessionCompletionPending = false;
	bBackendSessionCreateInFlight = false;
	bExerciseCompleted = false;
	bPauseTriggeredForCurrentExercise = false;
	bPhase1ProgressSentForCurrentExercise = false;
	bExercisePlaybackStarted = false;
	bScoringWindowInitialized = false;
	bLastStageActionWindowActive = false;
	bHoldingExercisePoseForStage = false;
	AccumulatedAccuracyPauseSeconds = 0.f;
	CurrentAccuracyPauseStartWorldTimeSeconds = 0.f;
	bFeedbackStageActive = false;
	FeedbackStageStartWorldTimeSeconds = 0.f;
	FeedbackStageDurationSeconds = 0.f;
	ExerciseFlowStartWorldTimeSeconds = GetCurrentWorldTimeSeconds();
	ExercisePlaybackStartMontageTimeSeconds = 0.f;
	LastCompletedAverageAccuracyPercent = 0.f;
	LastAnnouncedStage = EExerciseUiStage::Idle;

	ActiveExerciseMode = UFacialTherapyApi::GetStoredMode();
	if (ActiveExerciseMode.IsEmpty())
	{
		ActiveExerciseMode = TEXT("single");
	}

	const int32 PatientId = UFacialTherapyApi::GetStoredPatientId();
	if (PatientId <= 0)
	{
		return;
	}

	FString ExerciseName = ActiveExpressionName.TrimStartAndEnd();
	if (ExerciseName.IsEmpty())
	{
		ExerciseName = TEXT("Exercise");
	}

	FString Mode = ActiveExerciseMode;
	if (Mode.IsEmpty())
	{
		Mode = TEXT("single");
	}

	bBackendSessionCreateInFlight = true;
	UFacialTherapyApi::CreateSessionNative(PatientId, ExerciseName, Mode, [](bool bSuccess, const FString&, const FFacialTherapySessionResponse& Response)
	{
		UCurveLogging::bBackendSessionCreateInFlight = false;
		if (!bSuccess)
		{
			UCurveLogging::ActiveBackendSessionId = 0;
			return;
		}

		UCurveLogging::ActiveBackendSessionId = Response.id;
		if (UCurveLogging::bBackendSessionCompletionPending && UCurveLogging::ActiveBackendSessionId > 0)
		{
			const int32 PendingAccuracyScore = UCurveLogging::PendingBackendCompletionAccuracyScore;
			UCurveLogging::bBackendSessionCompletionPending = false;
			UCurveLogging::PendingBackendCompletionAccuracyScore = 0;
			UFacialTherapyApi::CompleteSessionNative(UCurveLogging::ActiveBackendSessionId, PendingAccuracyScore, [](bool, const FString&, const FFacialTherapySessionResponse&) {});
			UCurveLogging::ActiveBackendSessionId = 0;
		}
	});
}

void UCurveLogging::CompleteBackendSession(int32 AccuracyScore)
{
	const int32 ClampedAccuracyScore = FMath::Clamp(AccuracyScore, 0, 100);
	if (ActiveBackendSessionId > 0)
	{
		const int32 SessionId = ActiveBackendSessionId;
		ActiveBackendSessionId = 0;
		bBackendSessionCompletionPending = false;
		PendingBackendCompletionAccuracyScore = 0;
		UFacialTherapyApi::CompleteSessionNative(SessionId, ClampedAccuracyScore, [](bool, const FString&, const FFacialTherapySessionResponse&) {});
		return;
	}

	if (bBackendSessionCreateInFlight)
	{
		bBackendSessionCompletionPending = true;
		PendingBackendCompletionAccuracyScore = ClampedAccuracyScore;
	}
}


FString UCurveLogging::GetIpAddress()
{
	bool bCanBind;
	TSharedRef<FInternetAddr> Addr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLocalHostAddr(*GLog, bCanBind);
	FString LocalIP = Addr->ToString(false);
	return LocalIP;
}
