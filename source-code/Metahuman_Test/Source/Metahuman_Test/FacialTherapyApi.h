#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FacialTherapyApi.generated.h"

class UWebBrowser;

UENUM(BlueprintType)
enum class EFacialTherapySessionMode : uint8
{
	GameMode UMETA(DisplayName = "Game Mode"),
	GuidedMode UMETA(DisplayName = "Guided Mode")
};

USTRUCT(BlueprintType)
struct FFacialTherapyLoginResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) int32 user_id = 0;
	UPROPERTY(BlueprintReadOnly) FString role;
	UPROPERTY(BlueprintReadOnly) int32 patient_id = 0;
	UPROPERTY(BlueprintReadOnly) FString access_token;
	UPROPERTY(BlueprintReadOnly) FString refresh_token;
	UPROPERTY(BlueprintReadOnly) FString token_type;
	UPROPERTY(BlueprintReadOnly) int32 expires_in = 0;
};

USTRUCT(BlueprintType)
struct FFacialTherapyRegisterResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) int32 user_id = 0;
	UPROPERTY(BlueprintReadOnly) FString access_token;
	UPROPERTY(BlueprintReadOnly) FString refresh_token;
	UPROPERTY(BlueprintReadOnly) FString token_type;
	UPROPERTY(BlueprintReadOnly) int32 expires_in = 0;
};

USTRUCT(BlueprintType)
struct FFacialTherapyTokenResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString access_token;
	UPROPERTY(BlueprintReadOnly) FString refresh_token;
	UPROPERTY(BlueprintReadOnly) FString token_type;
	UPROPERTY(BlueprintReadOnly) int32 expires_in = 0;
};

USTRUCT(BlueprintType)
struct FFacialTherapyCurrentUserResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) int32 user_id = 0;
	UPROPERTY(BlueprintReadOnly) FString username;
	UPROPERTY(BlueprintReadOnly) FString role;
	UPROPERTY(BlueprintReadOnly) int32 patient_id = 0;
};

USTRUCT(BlueprintType)
struct FFacialTherapyPatientResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) int32 id = 0;
	UPROPERTY(BlueprintReadOnly) int32 user_id = 0;
	UPROPERTY(BlueprintReadOnly) FString name;
	UPROPERTY(BlueprintReadOnly) FString birthdate;
	UPROPERTY(BlueprintReadOnly) int32 age = 0;
	UPROPERTY(BlueprintReadOnly) FString illness;
	UPROPERTY(BlueprintReadOnly) FString limitations;
	UPROPERTY(BlueprintReadOnly) int32 avatar_id = 0;
	UPROPERTY(BlueprintReadOnly) FString email;
	UPROPERTY(BlueprintReadOnly) FString created_at;
};

USTRUCT(BlueprintType)
struct FFacialTherapyUEPatientResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) bool success = false;
	UPROPERTY(BlueprintReadOnly) int32 patient_id = 0;
	UPROPERTY(BlueprintReadOnly) FString name;
	UPROPERTY(BlueprintReadOnly) int32 avatar_id = 0;
	UPROPERTY(BlueprintReadOnly) FString illness;
	UPROPERTY(BlueprintReadOnly) FString limitations;
};

USTRUCT(BlueprintType)
struct FFacialTherapyModeResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) bool success = false;
	UPROPERTY(BlueprintReadOnly) FString mode;
	UPROPERTY(BlueprintReadOnly) int32 patient_id = 0;
	UPROPERTY(BlueprintReadOnly) FString message;
};

USTRUCT(BlueprintType)
struct FFacialTherapySessionResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) int32 id = 0;
	UPROPERTY(BlueprintReadOnly) int32 patient_id = 0;
	UPROPERTY(BlueprintReadOnly) FString exercise_name;
	UPROPERTY(BlueprintReadOnly) int32 accuracy_score = 0;
	UPROPERTY(BlueprintReadOnly) FString mode;
	UPROPERTY(BlueprintReadOnly) FString started_at;
	UPROPERTY(BlueprintReadOnly) FString completed_at;
};

USTRUCT(BlueprintType)
struct FFacialTherapySessionListResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) TArray<FFacialTherapySessionResponse> items;
};

USTRUCT(BlueprintType)
struct FFacialTherapyPhaseExerciseItem
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FName row_name;
	UPROPERTY(BlueprintReadOnly) int32 item_id = 0;
	UPROPERTY(BlueprintReadOnly) int32 phase_number = 0;
	UPROPERTY(BlueprintReadOnly) FString title;
	UPROPERTY(BlueprintReadOnly) FString description;
	UPROPERTY(BlueprintReadOnly) FString item_type;
	UPROPERTY(BlueprintReadOnly) int32 hold_duration_seconds = 0;
	UPROPERTY(BlueprintReadOnly) int32 repetitions = 0;
	UPROPERTY(BlueprintReadOnly) int32 rest_between_seconds = 0;
	UPROPERTY(BlueprintReadOnly) int32 rest_after_seconds = 0;
	UPROPERTY(BlueprintReadOnly) int32 pair_number = 0;
	UPROPERTY(BlueprintReadOnly) FString sequence_code;
	UPROPERTY(BlueprintReadOnly) TArray<FName> expressions;
	UPROPERTY(BlueprintReadOnly) bool b_completed_before = false;
};

USTRUCT(BlueprintType)
struct FFacialTherapyPhaseExerciseListResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) int32 phase_number = 0;
	UPROPERTY(BlueprintReadOnly) FString phase_name;
	UPROPERTY(BlueprintReadOnly) FString list_heading;
	UPROPERTY(BlueprintReadOnly) FString instructions;
	UPROPERTY(BlueprintReadOnly) TArray<FFacialTherapyPhaseExerciseItem> items;
};

USTRUCT(BlueprintType)
struct FFacialTherapyPatientPhaseProgressResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) int32 id = 0;
	UPROPERTY(BlueprintReadOnly) int32 phase_number = 0;
	UPROPERTY(BlueprintReadOnly) TArray<int32> exercises_mastered;
	UPROPERTY(BlueprintReadOnly) TArray<int32> pairs_mastered;
	UPROPERTY(BlueprintReadOnly) TArray<FString> sequences_mastered;
	UPROPERTY(BlueprintReadOnly) int32 sessions_this_week = 0;
	UPROPERTY(BlueprintReadOnly) int32 total_sessions = 0;
	UPROPERTY(BlueprintReadOnly) FString phase_started_at;
	UPROPERTY(BlueprintReadOnly) FString phase_completed_at;
	UPROPERTY(BlueprintReadOnly) bool mastery_achieved = false;
};

USTRUCT(BlueprintType)
struct FFacialTherapyProgressUpdateResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) bool success = false;
	UPROPERTY(BlueprintReadOnly) FString message;
	UPROPERTY(BlueprintReadOnly) int32 current_phase = 0;
	UPROPERTY(BlueprintReadOnly) bool mastery_achieved = false;
	UPROPERTY(BlueprintReadOnly) bool phase_advanced = false;
	UPROPERTY(BlueprintReadOnly) FFacialTherapyPatientPhaseProgressResponse progress;
};

USTRUCT(BlueprintType)
struct FFacialTherapyProgressOverview
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) int32 patient_id = 0;
	UPROPERTY(BlueprintReadOnly) FString patient_name;
	UPROPERTY(BlueprintReadOnly) int32 therapy_program_id = 0;
	UPROPERTY(BlueprintReadOnly) FString enrolled_since;
	UPROPERTY(BlueprintReadOnly) int32 current_phase = 0;
	UPROPERTY(BlueprintReadOnly) FString phase_name;
	UPROPERTY(BlueprintReadOnly) int32 current_week = 0;
	UPROPERTY(BlueprintReadOnly) bool is_active = false;
	UPROPERTY(BlueprintReadOnly) int32 total_sessions_completed = 0;
	UPROPERTY(BlueprintReadOnly) int32 sessions_this_week = 0;
	UPROPERTY(BlueprintReadOnly) float overall_success_rate = 0.f;
	UPROPERTY(BlueprintReadOnly) float overall_accuracy = 0.f;
	UPROPERTY(BlueprintReadOnly) FString last_session_date;
	UPROPERTY(BlueprintReadOnly) TArray<int32> phases_completed;
	UPROPERTY(BlueprintReadOnly) bool needs_attention = false;
	UPROPERTY(BlueprintReadOnly) FString attention_reason;
};

USTRUCT(BlueprintType)
struct FFacialTherapyWeeklyFrequencyData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString week_start;
	UPROPERTY(BlueprintReadOnly) int32 week_number = 0;
	UPROPERTY(BlueprintReadOnly) int32 session_count = 0;
	UPROPERTY(BlueprintReadOnly) float avg_accuracy = 0.f;
};

USTRUCT(BlueprintType)
struct FFacialTherapyFrequencyHistoryResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) int32 patient_id = 0;
	UPROPERTY(BlueprintReadOnly) TArray<FFacialTherapyWeeklyFrequencyData> weeks;
	UPROPERTY(BlueprintReadOnly) int32 total_sessions = 0;
	UPROPERTY(BlueprintReadOnly) float average_weekly_sessions = 0.f;
};

USTRUCT(BlueprintType)
struct FFacialTherapyStreakSummary
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) int32 current_streak = 0;
	UPROPERTY(BlueprintReadOnly) int32 longest_streak = 0;
	UPROPERTY(BlueprintReadOnly) bool is_active_today = false;
	UPROPERTY(BlueprintReadOnly) int32 days_since_last_session = 0;
};

USTRUCT(BlueprintType)
struct FFacialTherapyTherapySessionResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) int32 id = 0;
	UPROPERTY(BlueprintReadOnly) int32 therapy_program_id = 0;
	UPROPERTY(BlueprintReadOnly) int32 phase_number = 0;
	UPROPERTY(BlueprintReadOnly) FString session_type;
	UPROPERTY(BlueprintReadOnly) FString exercise_type;
	UPROPERTY(BlueprintReadOnly) int32 support_level = 0;
	UPROPERTY(BlueprintReadOnly) FString daily_form;
	UPROPERTY(BlueprintReadOnly) FString generated_at;
	UPROPERTY(BlueprintReadOnly) FString started_at;
	UPROPERTY(BlueprintReadOnly) FString completed_at;
	UPROPERTY(BlueprintReadOnly) int32 overall_accuracy = 0;
	UPROPERTY(BlueprintReadOnly) int32 exercises_completed = 0;
	UPROPERTY(BlueprintReadOnly) int32 exercises_total = 0;
};

USTRUCT(BlueprintType)
struct FFacialTherapyTherapySessionListResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) TArray<FFacialTherapyTherapySessionResponse> items;
};

USTRUCT(BlueprintType)
struct FFacialTherapySchedulePreferenceResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) int32 id = 0;
	UPROPERTY(BlueprintReadOnly) int32 patient_id = 0;
	UPROPERTY(BlueprintReadOnly) TArray<int32> rest_days;
	UPROPERTY(BlueprintReadOnly) TArray<FString> rest_days_names;
	UPROPERTY(BlueprintReadOnly) FString preferred_time;
	UPROPERTY(BlueprintReadOnly) FString preferred_time_formatted;
	UPROPERTY(BlueprintReadOnly) bool reminder_enabled = false;
	UPROPERTY(BlueprintReadOnly) int32 reminder_minutes_before = 0;
	UPROPERTY(BlueprintReadOnly) FString custom_reminder_message;
	UPROPERTY(BlueprintReadOnly) FString created_at;
	UPROPERTY(BlueprintReadOnly) FString updated_at;
};

USTRUCT(BlueprintType)
struct FFacialTherapySchedulePreferenceSummary
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) bool is_configured = false;
	UPROPERTY(BlueprintReadOnly) TArray<FString> rest_days_names;
	UPROPERTY(BlueprintReadOnly) FString preferred_time_formatted;
	UPROPERTY(BlueprintReadOnly) bool reminder_enabled = false;
};

DECLARE_DYNAMIC_DELEGATE_TwoParams(FFacialTherapyBasicCallback, bool, bSuccess, const FString&, ErrorMessage);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FFacialTherapyLoginCallback, bool, bSuccess, const FString&, ErrorMessage, const FFacialTherapyLoginResponse&, Response);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FFacialTherapyRegisterCallback, bool, bSuccess, const FString&, ErrorMessage, const FFacialTherapyRegisterResponse&, Response);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FFacialTherapyTokenCallback, bool, bSuccess, const FString&, ErrorMessage, const FFacialTherapyTokenResponse&, Response);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FFacialTherapyCurrentUserCallback, bool, bSuccess, const FString&, ErrorMessage, const FFacialTherapyCurrentUserResponse&, Response);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FFacialTherapyPatientCallback, bool, bSuccess, const FString&, ErrorMessage, const FFacialTherapyPatientResponse&, Response);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FFacialTherapyUEPatientCallback, bool, bSuccess, const FString&, ErrorMessage, const FFacialTherapyUEPatientResponse&, Response);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FFacialTherapyModeCallback, bool, bSuccess, const FString&, ErrorMessage, const FFacialTherapyModeResponse&, Response);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FFacialTherapySessionCallback, bool, bSuccess, const FString&, ErrorMessage, const FFacialTherapySessionResponse&, Response);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FFacialTherapySessionListCallback, bool, bSuccess, const FString&, ErrorMessage, const FFacialTherapySessionListResponse&, Response);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FFacialTherapyPhaseExerciseListCallback, bool, bSuccess, const FString&, ErrorMessage, const FFacialTherapyPhaseExerciseListResponse&, Response);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FFacialTherapyProgressCallback, bool, bSuccess, const FString&, ErrorMessage, const FFacialTherapyPatientPhaseProgressResponse&, Response);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FFacialTherapyProgressUpdateCallback, bool, bSuccess, const FString&, ErrorMessage, const FFacialTherapyProgressUpdateResponse&, Response);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FFacialTherapyOverviewCallback, bool, bSuccess, const FString&, ErrorMessage, const FFacialTherapyProgressOverview&, Response);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FFacialTherapyFrequencyCallback, bool, bSuccess, const FString&, ErrorMessage, const FFacialTherapyFrequencyHistoryResponse&, Response);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FFacialTherapyStreakCallback, bool, bSuccess, const FString&, ErrorMessage, const FFacialTherapyStreakSummary&, Response);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FFacialTherapyTherapySessionListCallback, bool, bSuccess, const FString&, ErrorMessage, const FFacialTherapyTherapySessionListResponse&, Response);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FFacialTherapySchedulePreferenceCallback, bool, bSuccess, const FString&, ErrorMessage, const FFacialTherapySchedulePreferenceResponse&, Response);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FFacialTherapyScheduleSummaryCallback, bool, bSuccess, const FString&, ErrorMessage, const FFacialTherapySchedulePreferenceSummary&, Response);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FFacialTherapyDashboardCallback, bool, bSuccess, const FString&, ErrorMessage, const FString&, JsonFilePath);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FFacialTherapyWebBridgeEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FFacialTherapyExerciseStartEvent, int32, PhaseNumber, int32, ItemId, EFacialTherapySessionMode, Mode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FFacialTherapyFrameRectEvent, float, PositionX, float, PositionY, float, SizeX, float, SizeY);

UCLASS(BlueprintType)
class METAHUMAN_TEST_API UFacialTherapyWebBridge : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Facial Therapy Web Bridge")
	FFacialTherapyWebBridgeEvent OnCameraPageOpened;

	UPROPERTY(BlueprintAssignable, Category = "Facial Therapy Web Bridge")
	FFacialTherapyWebBridgeEvent OnCameraCheckCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Facial Therapy Web Bridge")
	FFacialTherapyWebBridgeEvent OnCameraBackRequested;

	UPROPERTY(BlueprintAssignable, Category = "Facial Therapy Web Bridge")
	FFacialTherapyFrameRectEvent OnCameraFrameRectChanged;

	UPROPERTY(BlueprintAssignable, Category = "Facial Therapy Web Bridge")
	FFacialTherapyExerciseStartEvent OnExerciseStartRequested;

	UFUNCTION(BlueprintCallable, Category = "Facial Therapy Web Bridge")
	void camerapageopened();

	UFUNCTION(BlueprintCallable, Category = "Facial Therapy Web Bridge")
	void cameracheckcompleted();

	UFUNCTION(BlueprintCallable, Category = "Facial Therapy Web Bridge")
	void camerabackrequested();

	UFUNCTION(BlueprintCallable, Category = "Facial Therapy Web Bridge")
	void cameraframerectchanged(float X, float Y, float Width, float Height, float ViewportWidth, float ViewportHeight);

	UFUNCTION(BlueprintCallable, Category = "Facial Therapy Web Bridge")
	bool getcameraframecanvasrect(float& PositionX, float& PositionY, float& SizeX, float& SizeY) const;

	UFUNCTION(BlueprintCallable, Category = "Facial Therapy Web Bridge")
	void login(const FString& Username, const FString& Password);

	UFUNCTION(BlueprintCallable, Category = "Facial Therapy Web Bridge")
	void clearcacheddata();

	UFUNCTION(BlueprintCallable, Category = "Facial Therapy Web Bridge")
	void closeapp();

	UFUNCTION(BlueprintCallable, Category = "Facial Therapy Web Bridge")
	void prepareexerciseroadmap();

	UFUNCTION(BlueprintCallable, Category = "Facial Therapy Web Bridge")
	void startexercise(int32 PhaseNumber, int32 ItemId, const FString& Mode);

	UFUNCTION(BlueprintCallable, Category = "Facial Therapy Web Bridge")
	void getexerciseprogress();

	UFUNCTION(BlueprintCallable, Category = "Facial Therapy Web Bridge")
	void pauseexercise();

	UFUNCTION(BlueprintCallable, Category = "Facial Therapy Web Bridge")
	void resumeexercise();

	UFUNCTION(BlueprintCallable, Category = "Facial Therapy Web Bridge")
	void stopexercise();

	UFUNCTION(BlueprintCallable, Category = "Facial Therapy Web Bridge")
	void logout();

	UFUNCTION(BlueprintCallable, Category = "Facial Therapy Web Bridge")
	void sendjymminactivity(float Value);

	void SetBoundWebBrowser(UWebBrowser* WebBrowser);

private:
	bool CalculateCameraFrameCanvasRect(float& PositionX, float& PositionY, float& SizeX, float& SizeY) const;
	void SendLoginResult(bool bSuccess, const FString& ErrorMessage, const FString& TargetUrl, const FFacialTherapyLoginResponse& Response) const;
	void SendExerciseRoadmapResult(bool bSuccess, const FString& ErrorMessage, const FString& TargetUrl) const;
	void SendExerciseStartQueued(bool bSuccess, const FString& ErrorMessage, int32 PhaseNumber, int32 ItemId, const FString& Mode) const;
	void SendExerciseProgress();

	TWeakObjectPtr<UWebBrowser> BoundWebBrowser;
	double LastExerciseProgressTickSeconds = 0.0;
	float LastExerciseProgressTickDeltaSeconds = 0.f;
	float LastFrameX = 0.f;
	float LastFrameY = 0.f;
	float LastFrameWidth = 0.f;
	float LastFrameHeight = 0.f;
	float LastViewportWidth = 0.f;
	float LastViewportHeight = 0.f;
};

UCLASS()
class METAHUMAN_TEST_API UFacialTherapyApi : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void SetApiBaseUrl(const FString& InBaseUrl);
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static FString GetApiBaseUrl();
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static bool LoadStoredAuth();
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void RestoreSavedSession(const FFacialTherapyTokenCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void ClearStoredAuth();
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static bool HasStoredAuth();
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static FString GetStoredAccessToken();
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static FString GetStoredRefreshToken();
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static int32 GetStoredUserId();
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static int32 GetStoredPatientId();
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static FString GetStoredRole();
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static FString GetStoredMode();
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static EFacialTherapySessionMode GetStoredModeType();
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void Login(const FString& Username, const FString& Password, const FFacialTherapyLoginCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void RegisterPatientAuth(const FString& Username, const FString& Password, const FFacialTherapyRegisterCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void RefreshAccessToken(const FFacialTherapyTokenCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void Logout(const FFacialTherapyBasicCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void LogoutAll(const FFacialTherapyBasicCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void GetCurrentUser(const FFacialTherapyCurrentUserCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void CreatePatientProfile(int32 UserId, const FString& Name, const FString& Birthdate, const FString& Illness, const FString& Limitations, const FString& Email, int32 AvatarId, const FFacialTherapyPatientCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void UpdatePatientProfile(int32 PatientId, const FString& Name, const FString& Birthdate, const FString& Illness, const FString& Limitations, const FString& Email, int32 AvatarId, const FFacialTherapyPatientCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void GetPatientProfile(int32 PatientId, const FFacialTherapyPatientCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void GetPatientForUE(int32 PatientId, const FFacialTherapyUEPatientCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void GetCurrentPatientForUE(const FFacialTherapyUEPatientCallback& Callback);
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static int32 GetStoredAvatarId();
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void UpdateCurrentPatientAvatar(int32 AvatarId, const FFacialTherapyPatientCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void SetPatientMode(int32 PatientId, int32 Mode, const FFacialTherapyModeCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void SetCurrentPatientMode(int32 Mode, const FFacialTherapyModeCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void SetCurrentPatientModeForUE(int32 Mode, const FFacialTherapyModeCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void SetCurrentPatientModeByType(EFacialTherapySessionMode ModeType, const FFacialTherapyModeCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void SetCurrentPatientModeByTypeLocal(EFacialTherapySessionMode ModeType);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void CreateSession(int32 PatientId, const FString& ExerciseName, const FString& Mode, const FFacialTherapySessionCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void UpdateSession(int32 SessionId, int32 AccuracyScore, const FString& CompletedAtIso, const FFacialTherapySessionCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void CompleteSession(int32 SessionId, int32 AccuracyScore, const FFacialTherapySessionCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void GetPatientSessions(int32 PatientId, int32 Limit, const FFacialTherapySessionListCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void GetExercisesForSelectedPhase(int32 PhaseNumber, const FFacialTherapyPhaseExerciseListCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void GetTherapySessionHistory(int32 PatientId, int32 Limit, const FFacialTherapyTherapySessionListCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void GetProgressOverview(int32 PatientId, const FFacialTherapyOverviewCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void GetCurrentProgress(int32 PatientId, const FFacialTherapyProgressCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void UpdateProgress(int32 PatientId, int32 ExerciseId, int32 PairNumber, const FString& SequenceCode, bool bIncrementSession, const FFacialTherapyProgressUpdateCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void GetFrequencyHistory(int32 PatientId, int32 Weeks, const FFacialTherapyFrequencyCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void GetStreakSummary(int32 PatientId, const FFacialTherapyStreakCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void GetSchedulePreference(int32 PatientId, const FFacialTherapySchedulePreferenceCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void SetSchedulePreference(int32 PatientId, const TArray<int32>& RestDays, const FString& PreferredTime, bool bReminderEnabled, int32 ReminderMinutesBefore, const FString& CustomReminderMessage, const FFacialTherapySchedulePreferenceCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void DeleteSchedulePreference(int32 PatientId, const FFacialTherapyBasicCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void GetScheduleSummary(int32 PatientId, const FFacialTherapyScheduleSummaryCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void GenerateCurrentPhaseProgressData(const FFacialTherapyDashboardCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void GenerateCurrentPatientDashboardData(const FFacialTherapyDashboardCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void GenerateCurrentUserDashboardData(const FFacialTherapyDashboardCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void GenerateCurrentSessionCheckInData(const FString& Stage, const FFacialTherapyDashboardCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void GenerateCurrentCameraCheckData(const FFacialTherapyDashboardCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void GenerateCurrentExerciseSelectionData(const FFacialTherapyDashboardCallback& Callback);
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static void ClearGeneratedWebData();
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static UFacialTherapyWebBridge* GetWebBridge();
	UFUNCTION(BlueprintCallable, Category = "Facial Therapy API") static bool BindWebBridge(UWebBrowser* WebBrowser);
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static FString GetPhaseProgressHtmlFileName();
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static FString GetDashboardHtmlPath();
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static FString GetDashboardJsonPath();
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static FString GetDashboardScriptPath();
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static FString GetPhaseProgressHtmlPath();
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static FString GetPhaseProgressJsonPath();
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static FString GetPhaseProgressScriptPath();
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static FString GetUserDashboardHtmlPath();
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static FString GetUserDashboardJsonPath();
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static FString GetUserDashboardScriptPath();
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static FString GetLoginHtmlPath();
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static FString GetSessionCheckInHtmlPath();
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static FString GetSessionCheckInJsonPath();
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static FString GetSessionCheckInScriptPath();
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static FString GetCameraCheckHtmlPath();
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static FString GetCameraCheckJsonPath();
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static FString GetCameraCheckScriptPath();
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static FString GetExerciseSelectionHtmlPath();
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static FString GetExerciseSelectionJsonPath();
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static FString GetExerciseSelectionScriptPath();
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static FString GetGuidedExerciseHtmlPath();
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static FString GetExerciseResultHtmlPath();
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Facial Therapy API") static FString GetGameExerciseHtmlPath();
	static void SaveStoredAuth();
	static void LoginNative(const FString& Username, const FString& Password, TFunction<void(bool, const FString&, const FFacialTherapyLoginResponse&)> Callback);
	static void GenerateCurrentSessionCheckInDataNative(const FString& Stage, TFunction<void(bool, const FString&, const FString&)> Callback);
	static void GenerateCurrentExerciseSelectionDataNative(TFunction<void(bool, const FString&, const FString&)> Callback);
	static void CreateSessionNative(int32 PatientId, const FString& ExerciseName, const FString& Mode, TFunction<void(bool, const FString&, const FFacialTherapySessionResponse&)> Callback);
	static void CompleteSessionNative(int32 SessionId, int32 AccuracyScore, TFunction<void(bool, const FString&, const FFacialTherapySessionResponse&)> Callback);
	static void SendJymminActivityNative(float Value);
	static void UpdateProgressNative(int32 PatientId, int32 ExerciseId, int32 PairNumber, const FString& SequenceCode, bool bIncrementSession, TFunction<void(bool, const FString&, const FFacialTherapyProgressUpdateResponse&)> Callback);
	static void GetCurrentPatientForUENative(TFunction<void(bool, const FString&, const FFacialTherapyUEPatientResponse&)> Callback);
	static void GetProgressOverviewNative(int32 PatientId, TFunction<void(bool, const FString&, const FFacialTherapyProgressOverview&)> Callback);
	static void GetCurrentProgressNative(int32 PatientId, TFunction<void(bool, const FString&, const FFacialTherapyPatientPhaseProgressResponse&)> Callback);
	static void GetPatientSessionsNative(int32 PatientId, int32 Limit, TFunction<void(bool, const FString&, const FFacialTherapySessionListResponse&)> Callback);
	static void GetFrequencyHistoryNative(int32 PatientId, int32 Weeks, TFunction<void(bool, const FString&, const FFacialTherapyFrequencyHistoryResponse&)> Callback);
	static void GetStreakSummaryNative(int32 PatientId, TFunction<void(bool, const FString&, const FFacialTherapyStreakSummary&)> Callback);
	static void GetTherapySessionHistoryNative(int32 PatientId, int32 Limit, TFunction<void(bool, const FString&, const FFacialTherapyTherapySessionListResponse&)> Callback);
	static void GetCurrentUserNative(TFunction<void(bool, const FString&, const FFacialTherapyCurrentUserResponse&)> Callback);
	static void GetPatientProfileNative(int32 PatientId, TFunction<void(bool, const FString&, const FFacialTherapyPatientResponse&)> Callback);
	static void GetScheduleSummaryNative(int32 PatientId, TFunction<void(bool, const FString&, const FFacialTherapySchedulePreferenceSummary&)> Callback);

private:
	static FString ApiBaseUrl;
	static FString AccessToken;
	static FString RefreshToken;
	static FString StoredRole;
	static FString StoredMode;
	static int32 StoredUserId;
	static int32 StoredPatientId;
	static int32 StoredAvatarId;
};
