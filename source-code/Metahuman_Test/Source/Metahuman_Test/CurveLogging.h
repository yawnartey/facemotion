#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SocketSubsystem.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "CurveLogging.generated.h"

class USkeletalMeshComponent;
class UAnimMontage;
class USoundBase;
class UDataTable;

USTRUCT(BlueprintType)
struct METAHUMAN_TEST_API FCurveLogFrame
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Curve Logging")
	float TimeSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Curve Logging")
	TMap<FName, float> Curves;
};

USTRUCT(BlueprintType)
struct METAHUMAN_TEST_API FCurveAccuracySample
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Curve Logging")
	float TimeSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Curve Logging")
	float AccuracyPercent = 0.f;
};

USTRUCT(BlueprintType)
struct METAHUMAN_TEST_API FCurveExerciseProgressState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Curve Logging")
	bool bIsCompleted = false;

	UPROPERTY(BlueprintReadOnly, Category = "Curve Logging")
	FString StatusText;

	UPROPERTY(BlueprintReadOnly, Category = "Curve Logging")
	float AccuracyPercent = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Curve Logging")
	int32 RemainingTimeSeconds = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Curve Logging")
	float ProgressPercent = 0.f;
};

USTRUCT(BlueprintType)
struct METAHUMAN_TEST_API FCurveExerciseProgressUiState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Curve Logging")
	FString StatusText;

	UPROPERTY(BlueprintReadWrite, Category = "Curve Logging")
	FString PauseGuidanceText;

	UPROPERTY(BlueprintReadWrite, Category = "Curve Logging")
	int32 RemainingTimeSeconds = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Curve Logging")
	float ProgressPercent = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "Curve Logging")
	bool bIsCompleted = false;

	UPROPERTY(BlueprintReadWrite, Category = "Curve Logging")
	float AccuracyPercent = 0.f;
};

USTRUCT(BlueprintType)
struct METAHUMAN_TEST_API FCurveExerciseStageGuidance
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Curve Logging")
	TObjectPtr<UAnimMontage> FaceMontage = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Curve Logging")
	TObjectPtr<USoundBase> Audio = nullptr;
};

USTRUCT(BlueprintType)
struct METAHUMAN_TEST_API FCurveExerciseGuidanceConfig
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Curve Logging")
	FCurveExerciseStageGuidance Warmup;

	UPROPERTY(BlueprintReadWrite, Category = "Curve Logging")
	FCurveExerciseStageGuidance Introduction;

	UPROPERTY(BlueprintReadWrite, Category = "Curve Logging")
	FCurveExerciseStageGuidance Demonstration;

	UPROPERTY(BlueprintReadWrite, Category = "Curve Logging")
	FCurveExerciseStageGuidance RepetitionOne;

	UPROPERTY(BlueprintReadWrite, Category = "Curve Logging")
	FCurveExerciseStageGuidance RepetitionTwo;

	UPROPERTY(BlueprintReadWrite, Category = "Curve Logging")
	FCurveExerciseStageGuidance RepetitionThree;

	UPROPERTY(BlueprintReadWrite, Category = "Curve Logging")
	FCurveExerciseStageGuidance FeedbackGood;

	UPROPERTY(BlueprintReadWrite, Category = "Curve Logging")
	FCurveExerciseStageGuidance FeedbackMedium;

	UPROPERTY(BlueprintReadWrite, Category = "Curve Logging")
	FCurveExerciseStageGuidance FeedbackLow;

	UPROPERTY(BlueprintReadWrite, Category = "Curve Logging")
	FCurveExerciseStageGuidance Phase2Introduction;

	UPROPERTY(BlueprintReadWrite, Category = "Curve Logging")
	FCurveExerciseStageGuidance Phase2Demonstration;

	UPROPERTY(BlueprintReadWrite, Category = "Curve Logging")
	FCurveExerciseStageGuidance Phase3Introduction;

	UPROPERTY(BlueprintReadWrite, Category = "Curve Logging")
	FCurveExerciseStageGuidance Phase3Demonstration;

	UPROPERTY(BlueprintReadWrite, Category = "Curve Logging")
	FCurveExerciseStageGuidance Phase4Introduction;

	UPROPERTY(BlueprintReadWrite, Category = "Curve Logging")
	FCurveExerciseStageGuidance Phase4Demonstration;
};

UCLASS()
class METAHUMAN_TEST_API UCurveLogging : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Curve Logging")
	static void StartLogging(USkeletalMeshComponent* Mesh, UAnimMontage* Montage, const FString& Label);

	UFUNCTION(BlueprintCallable, Category = "Curve Logging")
	static void TickLogging(float DeltaSeconds);

	UFUNCTION(BlueprintCallable, Category = "Curve Logging")
	static void StopLogging();

	UFUNCTION(BlueprintCallable, Category = "Curve Logging")
	static void StopExerciseFlow();

	UFUNCTION(BlueprintCallable, Category = "Curve Logging")
	static bool PauseExerciseFlow();

	UFUNCTION(BlueprintCallable, Category = "Curve Logging")
	static bool ResumeExerciseFlow();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Curve Logging")
	static bool IsExerciseFlowPaused();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Curve Logging")
	static bool GetLatestFrame(FCurveLogFrame& OutFrame);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Curve Logging")
	static void GetAllFrames(TArray<FCurveLogFrame>& OutFrames);

	UFUNCTION(BlueprintCallable, Category = "Curve Logging")
	static void StartCurveMatching(
		USkeletalMeshComponent* TherapistMesh,
		USkeletalMeshComponent* PatientMesh,
		UAnimMontage* Montage,
		float Tolerance = 0.15f,
		float InSampleIntervalSeconds = 0.0333f,
		FString ExpressionName = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "Curve Logging")
	static void StartCurveMatchingWithGuidance(
		USkeletalMeshComponent* TherapistMesh,
		USkeletalMeshComponent* TherapistBodyMesh,
		USkeletalMeshComponent* PatientMesh,
		UAnimMontage* Montage,
		const FCurveExerciseGuidanceConfig& GuidanceConfig,
		float Tolerance = 0.15f,
		float InSampleIntervalSeconds = 0.0333f,
		FString ExpressionName = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "Curve Logging")
	static void SetupFacialExerciseMontagesFromDataTable(UDataTable* ExerciseTable);

	UFUNCTION(BlueprintCallable, Category = "Curve Logging")
	static void StartSelectedPhaseExercise(
		USkeletalMeshComponent* TherapistMesh,
		USkeletalMeshComponent* TherapistBodyMesh,
		USkeletalMeshComponent* PatientMesh,
		int32 PhaseNumber,
		int32 ItemId,
		const FCurveExerciseGuidanceConfig& GuidanceConfig,
		float Tolerance = 0.15f,
		float InSampleIntervalSeconds = 0.0333f);

	UFUNCTION(BlueprintCallable, Category = "Curve Logging")
	static void TickCurveMatching(float DeltaSeconds);

	UFUNCTION(BlueprintCallable, Category = "Curve Logging")
	static void EndCurveMatching(float& OutAverageAccuracyPercent, TArray<FCurveAccuracySample>& OutAccuracySamples);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Curve Logging")
	static float GetCurrentAccuracyPercent();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Curve Logging")
	static float GetLastExerciseAverageAccuracy();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Curve Logging")
	static FString GetExerciseFeedbackTitle(float AverageAccuracyPercent);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Curve Logging")
	static FString GetExerciseFeedbackMessage(float AverageAccuracyPercent);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Curve Logging")
	static void GetCurrentExerciseProgressLite(
		FString& OutStatusText,
		FString& OutPauseGuidanceText,
		int32& OutRemainingTimeSeconds,
		float& OutProgressPercent,
		bool& bOutIsCompleted,
		float& OutAccuracyPercent);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Curve Logging")
	static FCurveExerciseProgressUiState GetCurrentExerciseProgressUi();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Curve Logging")
	static FString GetCurrentExerciseDebugText();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Curve Logging")
	static void GetAccuracySamples(TArray<FCurveAccuracySample>& OutAccuracySamples);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Curve Logging")
	static bool GetTherapistPauseState(float& OutRemainingPauseSeconds);
	
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "IP Address")
    static FString GetIpAddress();

private:
	static bool bIsLogging;
	static TWeakObjectPtr<USkeletalMeshComponent> ActiveMesh;
	static TWeakObjectPtr<UAnimMontage> ActiveMontage;
	static FString ActiveLabel;
	static float StartTimeSeconds;
	static float NextSampleTimeSeconds;
	static float SampleIntervalSeconds;
	static TArray<FCurveLogFrame> Frames;
	static TArray<FCurveLogFrame> TherapistFramesForExport;
	static TArray<FCurveLogFrame> PatientFramesForExport;
	static TWeakObjectPtr<USkeletalMeshComponent> PatientMesh;
	static bool bIsMatching;
	static float MatchingTolerance;
	static float CurrentAccuracyPercent;
	static float AccumulatedAccuracyPercent;
	static int32 AccuracySampleCount;
	static TArray<FCurveAccuracySample> AccuracySamples;
	static FString ActiveExpressionName;
	static bool bIsTherapistAnimationPaused;
	static bool bPauseRequiresRecoveryAboveThreshold;
	static bool bExerciseCompleted;
	static float TherapistPauseResumeWorldTimeSeconds;
	static float PausedMontageTimeSeconds;
	static int32 ActiveBackendSessionId;
	static int32 PendingBackendCompletionAccuracyScore;
	static bool bBackendSessionCreateInFlight;
	static bool bBackendSessionCompletionPending;

	static float GetCurrentTimeSeconds();
	static void TickSelectedPhaseExercise(float DeltaSeconds);
	static void FinalizeSelectedPhaseExerciseScore();
	static bool CaptureCurves(FCurveLogFrame& OutFrame, float SampleTimeSeconds);
	static bool CaptureCurvesForMesh(USkeletalMeshComponent* Mesh, FCurveLogFrame& OutFrame, float SampleTimeSeconds);
	static float CalculateAccuracyPercent(const FCurveLogFrame& TherapistFrame, const FCurveLogFrame& PatientFrame, float Tolerance);
	static bool IsCustomTherapyExpression(const FString& ExpressionName);
	static float ApplyExpressionSpecificScoreAdjustment(float RawPercent);
	static float GetCurrentWorldTimeSeconds();
	static void UpdateTherapistPauseState(float CurrentAccuracy);
	static void PauseTherapistAnimationForFlow();
	static void ResumeTherapistAnimationForFlow();
	static void PauseTherapistAnimation();
	static void ResumeTherapistAnimation(bool bTimedOutWhileStillBelowThreshold);
	static void ExportCurveMatchingDataToJson();
	static bool ExportFramesToJsonFile(const FString& ExpressionName, const FString& SubjectLabel, int32 Index, const TArray<FCurveLogFrame>& InFrames);
	static FString MakeSafeExportExpressionName(const FString& InExpressionName);
	static void BeginBackendSession();
	static void CompleteBackendSession(int32 AccuracyScore);
};
