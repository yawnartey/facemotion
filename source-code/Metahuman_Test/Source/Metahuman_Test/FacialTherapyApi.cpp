#include "FacialTherapyApi.h"

#include "CurveLogging.h"
#include "Dom/JsonObject.h"
#include "Components/CanvasPanelSlot.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "JsonObjectConverter.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SWebBrowser.h"
#include "WebBrowser.h"

FString UFacialTherapyApi::ApiBaseUrl = TEXT("https://facemotion.up.railway.app");
FString UFacialTherapyApi::AccessToken;
FString UFacialTherapyApi::RefreshToken;
FString UFacialTherapyApi::StoredRole;
FString UFacialTherapyApi::StoredMode;
int32 UFacialTherapyApi::StoredUserId = 0;
int32 UFacialTherapyApi::StoredPatientId = 0;
int32 UFacialTherapyApi::StoredAvatarId = 0;

void UFacialTherapyWebBridge::camerapageopened()
{
	OnCameraPageOpened.Broadcast();
}

void UFacialTherapyWebBridge::cameracheckcompleted()
{
	OnCameraCheckCompleted.Broadcast();
}

void UFacialTherapyWebBridge::camerabackrequested()
{
	OnCameraBackRequested.Broadcast();
}

void UFacialTherapyWebBridge::cameraframerectchanged(float X, float Y, float Width, float Height, float ViewportWidth, float ViewportHeight)
{
	LastFrameX = X;
	LastFrameY = Y;
	LastFrameWidth = Width;
	LastFrameHeight = Height;
	LastViewportWidth = ViewportWidth;
	LastViewportHeight = ViewportHeight;

	float PositionX = 0.f;
	float PositionY = 0.f;
	float SizeX = 0.f;
	float SizeY = 0.f;
	if (CalculateCameraFrameCanvasRect(PositionX, PositionY, SizeX, SizeY))
	{
		OnCameraFrameRectChanged.Broadcast(PositionX, PositionY, SizeX, SizeY);
	}
}

bool UFacialTherapyWebBridge::getcameraframecanvasrect(float& PositionX, float& PositionY, float& SizeX, float& SizeY) const
{
	return CalculateCameraFrameCanvasRect(PositionX, PositionY, SizeX, SizeY);
}

void UFacialTherapyWebBridge::login(const FString& Username, const FString& Password)
{
	TWeakObjectPtr<UFacialTherapyWebBridge> WeakThis(this);
	UFacialTherapyApi::LoginNative(Username, Password, [WeakThis](bool bLoginSuccess, const FString& LoginError, const FFacialTherapyLoginResponse& LoginResponse)
	{
		if (!WeakThis.IsValid())
		{
			return;
		}

		if (!bLoginSuccess)
		{
			WeakThis->SendLoginResult(false, LoginError, FString(), LoginResponse);
			return;
		}

		UFacialTherapyApi::GenerateCurrentSessionCheckInDataNative(TEXT("before"), [WeakThis, LoginResponse](bool bDataSuccess, const FString& DataError, const FString&)
		{
			if (!WeakThis.IsValid())
			{
				return;
			}

			if (!bDataSuccess)
			{
				WeakThis->SendLoginResult(false, DataError, FString(), LoginResponse);
				return;
			}

			WeakThis->SendLoginResult(true, FString(), TEXT("../SessionCheckIn/session_checkin.html"), LoginResponse);
		});
	});
}

void UFacialTherapyWebBridge::clearcacheddata()
{
	UFacialTherapyApi::ClearGeneratedWebData();
}

void UFacialTherapyWebBridge::closeapp()
{
	FPlatformMisc::RequestExit(false);
}

void UFacialTherapyWebBridge::prepareexerciseroadmap()
{
	TWeakObjectPtr<UFacialTherapyWebBridge> WeakThis(this);
	UFacialTherapyApi::GenerateCurrentExerciseSelectionDataNative([WeakThis](bool bSuccess, const FString& ErrorMessage, const FString&)
	{
		if (!WeakThis.IsValid())
		{
			return;
		}

		WeakThis->SendExerciseRoadmapResult(bSuccess, ErrorMessage, TEXT("exercise_selection.html"));
	});
}

void UFacialTherapyWebBridge::startexercise(int32 PhaseNumber, int32 ItemId, const FString& Mode)
{
	if (PhaseNumber <= 0 || ItemId <= 0)
	{
		SendExerciseStartQueued(false, TEXT("Exercise selection is missing."), PhaseNumber, ItemId, Mode);
		return;
	}

	const FString NormalizedMode = Mode.TrimStartAndEnd().ToLower();
	const EFacialTherapySessionMode ModeType = NormalizedMode.Equals(TEXT("guided"), ESearchCase::IgnoreCase)
		|| NormalizedMode.Equals(TEXT("exercise"), ESearchCase::IgnoreCase)
		|| NormalizedMode.Equals(TEXT("dual"), ESearchCase::IgnoreCase)
		? EFacialTherapySessionMode::GuidedMode
		: EFacialTherapySessionMode::GameMode;
	const FString WebMode = ModeType == EFacialTherapySessionMode::GuidedMode ? TEXT("guided") : TEXT("game");
	UFacialTherapyApi::SetCurrentPatientModeByTypeLocal(ModeType);
	LastExerciseProgressTickSeconds = 0.0;
	LastExerciseProgressTickDeltaSeconds = 0.f;
	OnExerciseStartRequested.Broadcast(PhaseNumber, ItemId, ModeType);
	SendExerciseStartQueued(true, FString(), PhaseNumber, ItemId, WebMode);
}

void UFacialTherapyWebBridge::getexerciseprogress()
{
	const double NowSeconds = FPlatformTime::Seconds();
	const float DeltaSeconds = LastExerciseProgressTickSeconds > 0.0
		? FMath::Clamp(static_cast<float>(NowSeconds - LastExerciseProgressTickSeconds), 1.f / 120.f, 0.5f)
		: 1.f / 60.f;
	LastExerciseProgressTickSeconds = NowSeconds;
	LastExerciseProgressTickDeltaSeconds = DeltaSeconds;
	UCurveLogging::TickCurveMatching(DeltaSeconds);
	SendExerciseProgress();
}

void UFacialTherapyWebBridge::pauseexercise()
{
	UCurveLogging::PauseExerciseFlow();
	SendExerciseProgress();
}

void UFacialTherapyWebBridge::resumeexercise()
{
	LastExerciseProgressTickSeconds = FPlatformTime::Seconds();
	LastExerciseProgressTickDeltaSeconds = 0.f;
	UCurveLogging::ResumeExerciseFlow();
	SendExerciseProgress();
}

void UFacialTherapyWebBridge::stopexercise()
{
	LastExerciseProgressTickSeconds = 0.0;
	LastExerciseProgressTickDeltaSeconds = 0.f;
	UCurveLogging::StopExerciseFlow();
}

void UFacialTherapyWebBridge::logout()
{
	UFacialTherapyApi::Logout(FFacialTherapyBasicCallback());
}

void UFacialTherapyWebBridge::sendjymminactivity(float Value)
{
	UFacialTherapyApi::SendJymminActivityNative(Value);
}

void UFacialTherapyWebBridge::SetBoundWebBrowser(UWebBrowser* WebBrowser)
{
	BoundWebBrowser = WebBrowser;
}

bool UFacialTherapyWebBridge::CalculateCameraFrameCanvasRect(float& PositionX, float& PositionY, float& SizeX, float& SizeY) const
{
	if (LastViewportWidth <= 0.f || LastViewportHeight <= 0.f || LastFrameWidth <= 0.f || LastFrameHeight <= 0.f)
	{
		return false;
	}

	FVector2D BrowserPosition = FVector2D::ZeroVector;
	FVector2D BrowserSize(LastViewportWidth, LastViewportHeight);

	if (const UWebBrowser* WebBrowser = BoundWebBrowser.Get())
	{
		if (const UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(WebBrowser->Slot))
		{
			BrowserPosition = CanvasSlot->GetPosition();
			BrowserSize = CanvasSlot->GetSize();
		}

		const FVector2D GeometrySize = WebBrowser->GetCachedGeometry().GetLocalSize();
		if (BrowserSize.X <= 0.f || BrowserSize.Y <= 0.f)
		{
			BrowserSize = GeometrySize;
		}
	}

	if (BrowserSize.X <= 0.f || BrowserSize.Y <= 0.f)
	{
		return false;
	}

	const float ScaleX = BrowserSize.X / LastViewportWidth;
	const float ScaleY = BrowserSize.Y / LastViewportHeight;
	PositionX = BrowserPosition.X + (LastFrameX * ScaleX);
	PositionY = BrowserPosition.Y + (LastFrameY * ScaleY);
	SizeX = LastFrameWidth * ScaleX;
	SizeY = LastFrameHeight * ScaleY;
	return true;
}

namespace
{
	const TCHAR* AuthSection = TEXT("/Script/Metahuman_Test.FacialTherapyApi");
	TMap<int32, TSet<int32>> LocallyMasteredExercisesByPatient;
	TMap<int32, TSet<int32>> LocallyMasteredPairsByPatient;
	TMap<int32, TSet<FString>> LocallyMasteredSequencesByPatient;
	UFacialTherapyWebBridge* WebBridgeInstance = nullptr;

	template <typename ValueType>
	void AddUniqueValues(TArray<ValueType>& Target, const TSet<ValueType>& Values)
	{
		for (const ValueType& Value : Values)
		{
			Target.AddUnique(Value);
		}
	}

	void RememberLocalProgress(int32 PatientId, int32 ExerciseId, int32 PairNumber, const FString& SequenceCode)
	{
		if (PatientId <= 0)
		{
			return;
		}
		if (ExerciseId > 0)
		{
			LocallyMasteredExercisesByPatient.FindOrAdd(PatientId).Add(ExerciseId);
		}
		if (PairNumber > 0)
		{
			LocallyMasteredPairsByPatient.FindOrAdd(PatientId).Add(PairNumber);
		}
		if (!SequenceCode.IsEmpty())
		{
			LocallyMasteredSequencesByPatient.FindOrAdd(PatientId).Add(SequenceCode.ToUpper());
		}
	}

	void MergeLocalProgress(int32 PatientId, FFacialTherapyPatientPhaseProgressResponse& Progress)
	{
		if (const TSet<int32>* LocalExercises = LocallyMasteredExercisesByPatient.Find(PatientId))
		{
			AddUniqueValues(Progress.exercises_mastered, *LocalExercises);
		}
		if (const TSet<int32>* LocalPairs = LocallyMasteredPairsByPatient.Find(PatientId))
		{
			AddUniqueValues(Progress.pairs_mastered, *LocalPairs);
		}
		if (const TSet<FString>* LocalSequences = LocallyMasteredSequencesByPatient.Find(PatientId))
		{
			AddUniqueValues(Progress.sequences_mastered, *LocalSequences);
		}

		if (Progress.pairs_mastered.Num() > 0)
		{
			Progress.phase_number = FMath::Max(Progress.phase_number, 2);
		}
		for (const FString& SequenceCode : Progress.sequences_mastered)
		{
			if (SequenceCode.StartsWith(TEXT("MAINT"), ESearchCase::IgnoreCase))
			{
				Progress.phase_number = FMath::Max(Progress.phase_number, 5);
			}
			else if (SequenceCode.StartsWith(TEXT("MASTERY"), ESearchCase::IgnoreCase))
			{
				Progress.phase_number = FMath::Max(Progress.phase_number, 4);
			}
			else
			{
				Progress.phase_number = FMath::Max(Progress.phase_number, 3);
			}
		}
	}

	FString NormalizeBaseUrl(const FString& InBaseUrl)
	{
		FString Result = InBaseUrl.TrimStartAndEnd();
		while (Result.EndsWith(TEXT("/")))
		{
			Result.LeftChopInline(1);
		}
		return Result;
	}

	FString MakeUrl(const FString& BaseUrl, const FString& Path)
	{
		const FString NormalizedBaseUrl = NormalizeBaseUrl(BaseUrl);
		if (Path.StartsWith(TEXT("/")))
		{
			return NormalizedBaseUrl + Path;
		}
		return NormalizedBaseUrl + TEXT("/") + Path;
	}

	FString MakeWebUiPath(const FString& RelativePath)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("WebUI") / RelativePath);
	}

	bool ConvertModeValue(int32 ModeValue, FString& OutMode)
	{
		if (ModeValue == 1)
		{
			OutMode = TEXT("single");
			return true;
		}
		if (ModeValue == 2)
		{
			OutMode = TEXT("dual");
			return true;
		}
		return false;
	}

	bool ConvertModeType(EFacialTherapySessionMode ModeType, FString& OutMode)
	{
		switch (ModeType)
		{
		case EFacialTherapySessionMode::GuidedMode:
			OutMode = TEXT("dual");
			return true;
		case EFacialTherapySessionMode::GameMode:
		default:
			OutMode = TEXT("single");
			return true;
		}
	}

	int32 ConvertModeTypeToLegacyValue(EFacialTherapySessionMode ModeType)
	{
		return ModeType == EFacialTherapySessionMode::GuidedMode ? 2 : 1;
	}

	EFacialTherapySessionMode ConvertStoredModeToType(const FString& StoredModeValue)
	{
		return StoredModeValue.Equals(TEXT("dual"), ESearchCase::IgnoreCase)
			? EFacialTherapySessionMode::GuidedMode
			: EFacialTherapySessionMode::GameMode;
	}

	FString SerializeJsonObject(const TSharedRef<FJsonObject>& JsonObject)
	{
		FString Output;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
		FJsonSerializer::Serialize(JsonObject, Writer);
		return Output;
	}

	bool DeserializeJsonObject(const FString& JsonString, TSharedPtr<FJsonObject>& OutObject)
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
		return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
	}

	template <typename StructType>
	bool ParseJsonStruct(const FString& JsonString, StructType& OutStruct)
	{
		return FJsonObjectConverter::JsonObjectStringToUStruct<StructType>(JsonString, &OutStruct, 0, 0);
	}

	template <typename StructType>
	bool ParseJsonArray(const FString& JsonString, TArray<StructType>& OutItems)
	{
		TArray<TSharedPtr<FJsonValue>> JsonValues;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
		if (!FJsonSerializer::Deserialize(Reader, JsonValues))
		{
			return false;
		}

		OutItems.Reset();
		for (const TSharedPtr<FJsonValue>& JsonValue : JsonValues)
		{
			if (!JsonValue.IsValid() || JsonValue->Type != EJson::Object)
			{
				continue;
			}

			StructType Item;
			if (FJsonObjectConverter::JsonObjectToUStruct(JsonValue->AsObject().ToSharedRef(), &Item, 0, 0))
			{
				OutItems.Add(Item);
			}
		}

		return true;
	}

	FString ExtractErrorMessage(FHttpResponsePtr Response)
	{
		if (!Response.IsValid())
		{
			return TEXT("No response received from server.");
		}

		const FString Body = Response->GetContentAsString();
		if (Body.IsEmpty())
		{
			return FString::Printf(TEXT("Request failed with status %d."), Response->GetResponseCode());
		}

		TSharedPtr<FJsonObject> JsonObject;
		if (!DeserializeJsonObject(Body, JsonObject))
		{
			return Body;
		}

		FString DetailString;
		if (JsonObject->TryGetStringField(TEXT("detail"), DetailString))
		{
			return DetailString;
		}

		const auto NormalizeValidationMessage = [](const FString& InMessage) -> FString
		{
			if (InMessage.Contains(TEXT("String should have at least")) || InMessage.Contains(TEXT("Field required")))
			{
				return TEXT("Enter username and password.");
			}

			return InMessage;
		};

		const TArray<TSharedPtr<FJsonValue>>* DetailArray = nullptr;
		if (JsonObject->TryGetArrayField(TEXT("detail"), DetailArray) && DetailArray)
		{
			TArray<FString> Parts;
			for (const TSharedPtr<FJsonValue>& Entry : *DetailArray)
			{
				if (!Entry.IsValid())
				{
					continue;
				}

				if (Entry->Type == EJson::Object)
				{
					const TSharedPtr<FJsonObject> EntryObject = Entry->AsObject();
					if (EntryObject.IsValid())
					{
						FString Message;
						if (EntryObject->TryGetStringField(TEXT("msg"), Message))
						{
							const TArray<TSharedPtr<FJsonValue>>* LocationArray = nullptr;
							FString FieldName;
							if (EntryObject->TryGetArrayField(TEXT("loc"), LocationArray) && LocationArray && LocationArray->Num() > 0)
							{
								const TSharedPtr<FJsonValue>& LastEntry = (*LocationArray)[LocationArray->Num() - 1];
								if (LastEntry.IsValid() && LastEntry->Type == EJson::String)
								{
									FieldName = LastEntry->AsString();
								}
							}

							if ((Message.Contains(TEXT("String should have at least 3 characters")) && FieldName.Equals(TEXT("username"), ESearchCase::IgnoreCase)) ||
								(Message.Contains(TEXT("String should have at least 1 characters")) && FieldName.Equals(TEXT("password"), ESearchCase::IgnoreCase)) ||
								(Message.Contains(TEXT("String should have at least 8 characters")) && FieldName.Equals(TEXT("password"), ESearchCase::IgnoreCase)) ||
								Message.Contains(TEXT("Field required")))
							{
								Parts.Add(TEXT("Enter username and password."));
							}
							else
							{
								Parts.Add(NormalizeValidationMessage(Message));
							}
						}
					}
				}
				else if (Entry->Type == EJson::String)
				{
					Parts.Add(NormalizeValidationMessage(Entry->AsString()));
				}
			}

			if (Parts.Num() > 0)
			{
				TArray<FString> UniqueParts;
				for (const FString& Part : Parts)
				{
					if (!UniqueParts.Contains(Part))
					{
						UniqueParts.Add(Part);
					}
				}

				return FString::Join(UniqueParts, TEXT(" "));
			}
		}

		FString MessageString;
		if (JsonObject->TryGetStringField(TEXT("message"), MessageString))
		{
			return MessageString;
		}

		return NormalizeValidationMessage(Body);
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> CreateRequest(const FString& BaseUrl, const FString& Path, const FString& Verb, const FString& Body, bool bAuthorized, const FString& AccessToken)
	{
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
		Request->SetURL(MakeUrl(BaseUrl, Path));
		Request->SetVerb(Verb);
		Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		if (bAuthorized && !AccessToken.IsEmpty())
		{
			Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *AccessToken));
		}
		if (!Body.IsEmpty())
		{
			Request->SetContentAsString(Body);
		}
		return Request;
	}

	bool IsSuccessfulResponse(FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		if (!bConnectedSuccessfully || !Response.IsValid())
		{
			return false;
		}

		const int32 StatusCode = Response->GetResponseCode();
		return StatusCode >= 200 && StatusCode < 300;
	}

}

void UFacialTherapyWebBridge::SendLoginResult(bool bSuccess, const FString& ErrorMessage, const FString& TargetUrl, const FFacialTherapyLoginResponse& Response) const
{
	TSharedRef<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetBoolField(TEXT("success"), bSuccess);
	ResultObject->SetStringField(TEXT("error"), ErrorMessage);
	ResultObject->SetStringField(TEXT("target"), TargetUrl);
	ResultObject->SetNumberField(TEXT("user_id"), Response.user_id);
	ResultObject->SetStringField(TEXT("role"), Response.role);
	ResultObject->SetNumberField(TEXT("patient_id"), Response.patient_id);

	const FString JsonOutput = SerializeJsonObject(ResultObject);
	if (UWebBrowser* WebBrowser = BoundWebBrowser.Get())
	{
		const FString Script = FString::Printf(TEXT("window.facemotionLoginResult && window.facemotionLoginResult(%s);"), *JsonOutput);
		WebBrowser->ExecuteJavascript(Script);
	}
}

void UFacialTherapyWebBridge::SendExerciseRoadmapResult(bool bSuccess, const FString& ErrorMessage, const FString& TargetUrl) const
{
	TSharedRef<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetBoolField(TEXT("success"), bSuccess);
	ResultObject->SetStringField(TEXT("error"), ErrorMessage);
	ResultObject->SetStringField(TEXT("target"), TargetUrl);

	const FString JsonOutput = SerializeJsonObject(ResultObject);
	if (UWebBrowser* WebBrowser = BoundWebBrowser.Get())
	{
		const FString Script = FString::Printf(TEXT("window.facemotionExerciseRoadmapReady && window.facemotionExerciseRoadmapReady(%s);"), *JsonOutput);
		WebBrowser->ExecuteJavascript(Script);
	}
}

void UFacialTherapyWebBridge::SendExerciseStartQueued(bool bSuccess, const FString& ErrorMessage, int32 PhaseNumber, int32 ItemId, const FString& Mode) const
{
	TSharedRef<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetBoolField(TEXT("success"), bSuccess);
	ResultObject->SetStringField(TEXT("error"), ErrorMessage);
	ResultObject->SetNumberField(TEXT("phase_number"), PhaseNumber);
	ResultObject->SetNumberField(TEXT("item_id"), ItemId);
	ResultObject->SetStringField(TEXT("mode"), Mode);

	const FString JsonOutput = SerializeJsonObject(ResultObject);
	if (UWebBrowser* WebBrowser = BoundWebBrowser.Get())
	{
		const FString Script = FString::Printf(TEXT("window.facemotionExerciseStartQueued && window.facemotionExerciseStartQueued(%s);"), *JsonOutput);
		WebBrowser->ExecuteJavascript(Script);
	}
}

void UFacialTherapyWebBridge::SendExerciseProgress()
{
	const FCurveExerciseProgressUiState Progress = UCurveLogging::GetCurrentExerciseProgressUi();
	const float LastAverageAccuracy = UCurveLogging::GetLastExerciseAverageAccuracy();
	const float DisplayAccuracy = Progress.bIsCompleted ? LastAverageAccuracy : Progress.AccuracyPercent;
	const FString FeedbackTitle = UCurveLogging::GetExerciseFeedbackTitle(DisplayAccuracy);
	const FString FeedbackMessage = UCurveLogging::GetExerciseFeedbackMessage(DisplayAccuracy);

	TSharedRef<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetBoolField(TEXT("success"), true);
	ResultObject->SetStringField(TEXT("status_text"), Progress.StatusText);
	ResultObject->SetStringField(TEXT("pause_guidance_text"), Progress.PauseGuidanceText);
	ResultObject->SetNumberField(TEXT("remaining_time_seconds"), Progress.RemainingTimeSeconds);
	ResultObject->SetNumberField(TEXT("progress_percent"), Progress.ProgressPercent);
	ResultObject->SetBoolField(TEXT("completed"), Progress.bIsCompleted);
	ResultObject->SetNumberField(TEXT("accuracy_percent"), DisplayAccuracy);
	ResultObject->SetStringField(TEXT("feedback_title"), FeedbackTitle);
	ResultObject->SetStringField(TEXT("feedback_message"), FeedbackMessage);
	ResultObject->SetNumberField(TEXT("web_tick_delta_seconds"), LastExerciseProgressTickDeltaSeconds);

	const FString JsonOutput = SerializeJsonObject(ResultObject);
	if (UWebBrowser* WebBrowser = BoundWebBrowser.Get())
	{
		const FString Script = FString::Printf(TEXT("window.facemotionExerciseProgress && window.facemotionExerciseProgress(%s);"), *JsonOutput);
		WebBrowser->ExecuteJavascript(Script);
	}
}

void UFacialTherapyApi::SetApiBaseUrl(const FString& InBaseUrl)
{
	ApiBaseUrl = NormalizeBaseUrl(InBaseUrl);
}

FString UFacialTherapyApi::GetApiBaseUrl()
{
	return ApiBaseUrl;
}

FString UFacialTherapyApi::GetDashboardHtmlPath()
{
	return MakeWebUiPath(TEXT("PatientDashboard/facial_therapy_patient.html"));
}

FString UFacialTherapyApi::GetDashboardJsonPath()
{
	return MakeWebUiPath(TEXT("PatientDashboard/facial_therapy_patient_data.json"));
}

FString UFacialTherapyApi::GetDashboardScriptPath()
{
	return MakeWebUiPath(TEXT("PatientDashboard/facial_therapy_patient_data.js"));
}

FString UFacialTherapyApi::GetUserDashboardHtmlPath()
{
	return MakeWebUiPath(TEXT("UserDashboard/facial_therapy_user_dashboard_v10.html"));
}

FString UFacialTherapyApi::GetUserDashboardJsonPath()
{
	return MakeWebUiPath(TEXT("UserDashboard/facial_therapy_user_dashboard_data.json"));
}

FString UFacialTherapyApi::GetUserDashboardScriptPath()
{
	return MakeWebUiPath(TEXT("UserDashboard/facial_therapy_user_dashboard_data.js"));
}

FString UFacialTherapyApi::GetLoginHtmlPath()
{
	return MakeWebUiPath(TEXT("Auth/facial_therapy_login.html"));
}

FString UFacialTherapyApi::GetPhaseProgressHtmlFileName()
{
	return TEXT("WebUI/PhaseProgress/facial_therapy_phase_progress_v4");
}

FString UFacialTherapyApi::GetPhaseProgressHtmlPath()
{
	return MakeWebUiPath(TEXT("PhaseProgress/facial_therapy_phase_progress_v4.html"));
}

FString UFacialTherapyApi::GetPhaseProgressJsonPath()
{
	return MakeWebUiPath(TEXT("PhaseProgress/facial_therapy_phase_progress_data.json"));
}

FString UFacialTherapyApi::GetPhaseProgressScriptPath()
{
	return MakeWebUiPath(TEXT("PhaseProgress/facial_therapy_phase_progress_data.js"));
}

FString UFacialTherapyApi::GetSessionCheckInHtmlPath()
{
	return GetLoginHtmlPath();
}

FString UFacialTherapyApi::GetSessionCheckInJsonPath()
{
	return MakeWebUiPath(TEXT("SessionCheckIn/session_checkin_data.json"));
}

FString UFacialTherapyApi::GetSessionCheckInScriptPath()
{
	return MakeWebUiPath(TEXT("SessionCheckIn/session_checkin_data.js"));
}

FString UFacialTherapyApi::GetCameraCheckHtmlPath()
{
	return MakeWebUiPath(TEXT("CameraCheck/camera_check.html"));
}

FString UFacialTherapyApi::GetCameraCheckJsonPath()
{
	return MakeWebUiPath(TEXT("CameraCheck/camera_check_data.json"));
}

FString UFacialTherapyApi::GetCameraCheckScriptPath()
{
	return MakeWebUiPath(TEXT("CameraCheck/camera_check_data.js"));
}

FString UFacialTherapyApi::GetExerciseSelectionHtmlPath()
{
	return MakeWebUiPath(TEXT("ExerciseSelection/exercise_selection.html"));
}

FString UFacialTherapyApi::GetExerciseSelectionJsonPath()
{
	return MakeWebUiPath(TEXT("ExerciseSelection/exercise_selection_data.json"));
}

FString UFacialTherapyApi::GetExerciseSelectionScriptPath()
{
	return MakeWebUiPath(TEXT("ExerciseSelection/exercise_selection_data.js"));
}

FString UFacialTherapyApi::GetGuidedExerciseHtmlPath()
{
	return MakeWebUiPath(TEXT("GuidedExercise/guided_exercise.html"));
}

FString UFacialTherapyApi::GetExerciseResultHtmlPath()
{
	return MakeWebUiPath(TEXT("ExerciseResult/exercise_result.html"));
}

FString UFacialTherapyApi::GetGameExerciseHtmlPath()
{
	return MakeWebUiPath(TEXT("GameExercise/game_exercise.html"));
}

void UFacialTherapyApi::ClearGeneratedWebData()
{
	const TArray<FString> Paths =
	{
		GetDashboardJsonPath(),
		GetDashboardScriptPath(),
		GetUserDashboardJsonPath(),
		GetUserDashboardScriptPath(),
		GetPhaseProgressJsonPath(),
		GetPhaseProgressScriptPath(),
		GetSessionCheckInJsonPath(),
		GetSessionCheckInScriptPath(),
		GetExerciseSelectionJsonPath(),
		GetExerciseSelectionScriptPath()
	};

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	for (const FString& Path : Paths)
	{
		if (PlatformFile.FileExists(*Path))
		{
			PlatformFile.DeleteFile(*Path);
		}
	}
}

bool UFacialTherapyApi::LoadStoredAuth()
{
	if (!GConfig)
	{
		return false;
	}

	GConfig->GetString(AuthSection, TEXT("ApiBaseUrl"), ApiBaseUrl, GGameIni);
	GConfig->GetString(AuthSection, TEXT("AccessToken"), AccessToken, GGameIni);
	GConfig->GetString(AuthSection, TEXT("RefreshToken"), RefreshToken, GGameIni);
	GConfig->GetString(AuthSection, TEXT("StoredRole"), StoredRole, GGameIni);
	GConfig->GetString(AuthSection, TEXT("StoredMode"), StoredMode, GGameIni);
	GConfig->GetInt(AuthSection, TEXT("StoredUserId"), StoredUserId, GGameIni);
	GConfig->GetInt(AuthSection, TEXT("StoredPatientId"), StoredPatientId, GGameIni);
	GConfig->GetInt(AuthSection, TEXT("StoredAvatarId"), StoredAvatarId, GGameIni);

	ApiBaseUrl = NormalizeBaseUrl(ApiBaseUrl);
	return !AccessToken.IsEmpty() || !RefreshToken.IsEmpty();
}

void UFacialTherapyApi::RestoreSavedSession(const FFacialTherapyTokenCallback& Callback)
{
	if (!LoadStoredAuth())
	{
		FFacialTherapyTokenResponse EmptyResponse;
		Callback.ExecuteIfBound(false, TEXT("No saved session exists."), EmptyResponse);
		return;
	}

	RefreshAccessToken(Callback);
}

void UFacialTherapyApi::ClearStoredAuth()
{
	AccessToken.Empty();
	RefreshToken.Empty();
	StoredRole.Empty();
	StoredMode.Empty();
	StoredUserId = 0;
	StoredPatientId = 0;
	StoredAvatarId = 0;

	if (GConfig)
	{
		GConfig->EmptySection(AuthSection, GGameIni);
		GConfig->Flush(false, GGameIni);
	}
}

bool UFacialTherapyApi::HasStoredAuth()
{
	if (AccessToken.IsEmpty() && RefreshToken.IsEmpty())
	{
		LoadStoredAuth();
	}

	return !AccessToken.IsEmpty() || !RefreshToken.IsEmpty();
}

FString UFacialTherapyApi::GetStoredAccessToken()
{
	return AccessToken;
}

FString UFacialTherapyApi::GetStoredRefreshToken()
{
	return RefreshToken;
}

int32 UFacialTherapyApi::GetStoredUserId()
{
	return StoredUserId;
}

int32 UFacialTherapyApi::GetStoredPatientId()
{
	return StoredPatientId;
}

int32 UFacialTherapyApi::GetStoredAvatarId()
{
	return StoredAvatarId;
}

FString UFacialTherapyApi::GetStoredRole()
{
	return StoredRole;
}

FString UFacialTherapyApi::GetStoredMode()
{
	return StoredMode;
}

EFacialTherapySessionMode UFacialTherapyApi::GetStoredModeType()
{
	return ConvertStoredModeToType(StoredMode);
}

void UFacialTherapyApi::SaveStoredAuth()
{
	if (!GConfig)
	{
		return;
	}

	GConfig->SetString(AuthSection, TEXT("ApiBaseUrl"), *ApiBaseUrl, GGameIni);
	GConfig->SetString(AuthSection, TEXT("AccessToken"), *AccessToken, GGameIni);
	GConfig->SetString(AuthSection, TEXT("RefreshToken"), *RefreshToken, GGameIni);
	GConfig->SetString(AuthSection, TEXT("StoredRole"), *StoredRole, GGameIni);
	GConfig->SetString(AuthSection, TEXT("StoredMode"), *StoredMode, GGameIni);
	GConfig->SetInt(AuthSection, TEXT("StoredUserId"), StoredUserId, GGameIni);
	GConfig->SetInt(AuthSection, TEXT("StoredPatientId"), StoredPatientId, GGameIni);
	GConfig->SetInt(AuthSection, TEXT("StoredAvatarId"), StoredAvatarId, GGameIni);
	GConfig->Flush(false, GGameIni);
}

void UFacialTherapyApi::CreateSessionNative(int32 PatientId, const FString& ExerciseName, const FString& Mode, TFunction<void(bool, const FString&, const FFacialTherapySessionResponse&)> Callback)
{
	FFacialTherapySessionResponse EmptyResponse;
	if (AccessToken.IsEmpty())
	{
		Callback(false, TEXT("No access token is stored."), EmptyResponse);
		return;
	}

	TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetNumberField(TEXT("patient_id"), PatientId);
	JsonObject->SetStringField(TEXT("exercise_name"), ExerciseName);
	JsonObject->SetStringField(TEXT("mode"), Mode);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, TEXT("/sessions/"), TEXT("POST"), SerializeJsonObject(JsonObject), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback = MoveTemp(Callback)](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully) mutable
	{
		FFacialTherapySessionResponse ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonStruct(Response->GetContentAsString(), ParsedResponse))
		{
			Callback(false, TEXT("Failed to parse session response."), ParsedResponse);
			return;
		}

		Callback(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::CompleteSessionNative(int32 SessionId, int32 AccuracyScore, TFunction<void(bool, const FString&, const FFacialTherapySessionResponse&)> Callback)
{
	FFacialTherapySessionResponse EmptyResponse;
	if (AccessToken.IsEmpty())
	{
		Callback(false, TEXT("No access token is stored."), EmptyResponse);
		return;
	}

	const FString Path = FString::Printf(TEXT("/sessions/%d/complete?accuracy_score=%d"), SessionId, FMath::Clamp(AccuracyScore, 0, 100));
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, Path, TEXT("POST"), FString(), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback = MoveTemp(Callback)](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully) mutable
	{
		FFacialTherapySessionResponse ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonStruct(Response->GetContentAsString(), ParsedResponse))
		{
			Callback(false, TEXT("Failed to parse session completion response."), ParsedResponse);
			return;
		}

		Callback(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::UpdateProgressNative(int32 PatientId, int32 ExerciseId, int32 PairNumber, const FString& SequenceCode, bool bIncrementSession, TFunction<void(bool, const FString&, const FFacialTherapyProgressUpdateResponse&)> Callback)
{
	if (AccessToken.IsEmpty())
	{
		LoadStoredAuth();
	}

	if (AccessToken.IsEmpty())
	{
		FFacialTherapyProgressUpdateResponse EmptyResponse;
		Callback(false, TEXT("No access token is stored."), EmptyResponse);
		return;
	}

	TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	if (ExerciseId > 0)
	{
		JsonObject->SetNumberField(TEXT("exercise_id"), ExerciseId);
	}
	if (PairNumber > 0)
	{
		JsonObject->SetNumberField(TEXT("pair_number"), PairNumber);
	}
	if (!SequenceCode.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("sequence_code"), SequenceCode);
	}
	JsonObject->SetBoolField(TEXT("increment_session"), bIncrementSession);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, FString::Printf(TEXT("/therapy/progress/%d"), PatientId), TEXT("PUT"), SerializeJsonObject(JsonObject), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback, PatientId, ExerciseId, PairNumber, SequenceCode](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		FFacialTherapyProgressUpdateResponse ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonStruct(Response->GetContentAsString(), ParsedResponse))
		{
			Callback(false, TEXT("Failed to parse progress update response."), ParsedResponse);
			return;
		}

		RememberLocalProgress(PatientId, ExerciseId, PairNumber, SequenceCode);
		MergeLocalProgress(PatientId, ParsedResponse.progress);
		Callback(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::GetCurrentPatientForUENative(TFunction<void(bool, const FString&, const FFacialTherapyUEPatientResponse&)> Callback)
{
	FFacialTherapyUEPatientResponse EmptyResponse;
	if (StoredPatientId <= 0)
	{
		LoadStoredAuth();
	}

	if (StoredPatientId <= 0)
	{
		Callback(false, TEXT("No stored patient id is available. Login first."), EmptyResponse);
		return;
	}

	if (AccessToken.IsEmpty())
	{
		LoadStoredAuth();
	}

	if (AccessToken.IsEmpty())
	{
		Callback(false, TEXT("No access token is stored."), EmptyResponse);
		return;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, FString::Printf(TEXT("/ue/patient/%d"), StoredPatientId), TEXT("GET"), FString(), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback = MoveTemp(Callback)](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully) mutable
	{
		FFacialTherapyUEPatientResponse ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonStruct(Response->GetContentAsString(), ParsedResponse))
		{
			Callback(false, TEXT("Failed to parse Unreal patient response."), ParsedResponse);
			return;
		}

		if (ParsedResponse.patient_id > 0)
		{
			UFacialTherapyApi::StoredPatientId = ParsedResponse.patient_id;
			UFacialTherapyApi::SaveStoredAuth();
		}

		Callback(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::GetProgressOverviewNative(int32 PatientId, TFunction<void(bool, const FString&, const FFacialTherapyProgressOverview&)> Callback)
{
	FFacialTherapyProgressOverview EmptyResponse;
	if (AccessToken.IsEmpty())
	{
		LoadStoredAuth();
	}
	if (AccessToken.IsEmpty())
	{
		Callback(false, TEXT("No access token is stored."), EmptyResponse);
		return;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, FString::Printf(TEXT("/therapy/overview/%d"), PatientId), TEXT("GET"), FString(), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback = MoveTemp(Callback)](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully) mutable
	{
		FFacialTherapyProgressOverview ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonStruct(Response->GetContentAsString(), ParsedResponse))
		{
			Callback(false, TEXT("Failed to parse progress overview response."), ParsedResponse);
			return;
		}

		Callback(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::GetCurrentProgressNative(int32 PatientId, TFunction<void(bool, const FString&, const FFacialTherapyPatientPhaseProgressResponse&)> Callback)
{
	FFacialTherapyPatientPhaseProgressResponse EmptyResponse;
	if (AccessToken.IsEmpty())
	{
		LoadStoredAuth();
	}
	if (AccessToken.IsEmpty())
	{
		Callback(false, TEXT("No access token is stored."), EmptyResponse);
		return;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, FString::Printf(TEXT("/therapy/progress/%d"), PatientId), TEXT("GET"), FString(), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback = MoveTemp(Callback), PatientId](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully) mutable
	{
		FFacialTherapyPatientPhaseProgressResponse ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonStruct(Response->GetContentAsString(), ParsedResponse))
		{
			Callback(false, TEXT("Failed to parse progress response."), ParsedResponse);
			return;
		}

		MergeLocalProgress(PatientId, ParsedResponse);
		Callback(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::GetPatientSessionsNative(int32 PatientId, int32 Limit, TFunction<void(bool, const FString&, const FFacialTherapySessionListResponse&)> Callback)
{
	FFacialTherapySessionListResponse EmptyResponse;
	if (AccessToken.IsEmpty())
	{
		Callback(false, TEXT("No access token is stored."), EmptyResponse);
		return;
	}

	const FString Path = FString::Printf(TEXT("/sessions/patient/%d?limit=%d"), PatientId, FMath::Max(Limit, 1));
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, Path, TEXT("GET"), FString(), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback = MoveTemp(Callback)](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully) mutable
	{
		FFacialTherapySessionListResponse ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonArray(Response->GetContentAsString(), ParsedResponse.items))
		{
			Callback(false, TEXT("Failed to parse patient sessions response."), ParsedResponse);
			return;
		}

		Callback(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::GetFrequencyHistoryNative(int32 PatientId, int32 Weeks, TFunction<void(bool, const FString&, const FFacialTherapyFrequencyHistoryResponse&)> Callback)
{
	FFacialTherapyFrequencyHistoryResponse EmptyResponse;
	if (AccessToken.IsEmpty())
	{
		Callback(false, TEXT("No access token is stored."), EmptyResponse);
		return;
	}

	const FString Path = FString::Printf(TEXT("/therapy/frequency/%d?weeks=%d"), PatientId, FMath::Clamp(Weeks, 1, 12));
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, Path, TEXT("GET"), FString(), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback = MoveTemp(Callback)](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully) mutable
	{
		FFacialTherapyFrequencyHistoryResponse ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonStruct(Response->GetContentAsString(), ParsedResponse))
		{
			Callback(false, TEXT("Failed to parse frequency response."), ParsedResponse);
			return;
		}

		Callback(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::GetStreakSummaryNative(int32 PatientId, TFunction<void(bool, const FString&, const FFacialTherapyStreakSummary&)> Callback)
{
	FFacialTherapyStreakSummary EmptyResponse;
	if (AccessToken.IsEmpty())
	{
		Callback(false, TEXT("No access token is stored."), EmptyResponse);
		return;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, FString::Printf(TEXT("/therapy/streak/%d"), PatientId), TEXT("GET"), FString(), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback = MoveTemp(Callback)](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully) mutable
	{
		FFacialTherapyStreakSummary ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonStruct(Response->GetContentAsString(), ParsedResponse))
		{
			Callback(false, TEXT("Failed to parse streak response."), ParsedResponse);
			return;
		}

		Callback(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::GetTherapySessionHistoryNative(int32 PatientId, int32 Limit, TFunction<void(bool, const FString&, const FFacialTherapyTherapySessionListResponse&)> Callback)
{
	FFacialTherapyTherapySessionListResponse EmptyResponse;
	if (AccessToken.IsEmpty())
	{
		Callback(false, TEXT("No access token is stored."), EmptyResponse);
		return;
	}

	const FString Path = FString::Printf(TEXT("/therapy/session/history/%d?limit=%d"), PatientId, FMath::Max(Limit, 1));
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, Path, TEXT("GET"), FString(), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback = MoveTemp(Callback)](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully) mutable
	{
		FFacialTherapyTherapySessionListResponse ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonArray(Response->GetContentAsString(), ParsedResponse.items))
		{
			Callback(false, TEXT("Failed to parse therapy session history response."), ParsedResponse);
			return;
		}

		Callback(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::GetCurrentUserNative(TFunction<void(bool, const FString&, const FFacialTherapyCurrentUserResponse&)> Callback)
{
	FFacialTherapyCurrentUserResponse EmptyResponse;
	if (AccessToken.IsEmpty())
	{
		Callback(false, TEXT("No access token is stored."), EmptyResponse);
		return;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, TEXT("/auth/me"), TEXT("GET"), FString(), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback = MoveTemp(Callback)](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully) mutable
	{
		FFacialTherapyCurrentUserResponse ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonStruct(Response->GetContentAsString(), ParsedResponse))
		{
			Callback(false, TEXT("Failed to parse current user response."), ParsedResponse);
			return;
		}

		if (ParsedResponse.user_id > 0)
		{
			UFacialTherapyApi::StoredUserId = ParsedResponse.user_id;
		}
		if (!ParsedResponse.role.IsEmpty())
		{
			UFacialTherapyApi::StoredRole = ParsedResponse.role;
		}
		if (ParsedResponse.patient_id > 0)
		{
			UFacialTherapyApi::StoredPatientId = ParsedResponse.patient_id;
		}
		UFacialTherapyApi::SaveStoredAuth();

		Callback(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::GetPatientProfileNative(int32 PatientId, TFunction<void(bool, const FString&, const FFacialTherapyPatientResponse&)> Callback)
{
	FFacialTherapyPatientResponse EmptyResponse;
	if (AccessToken.IsEmpty())
	{
		Callback(false, TEXT("No access token is stored."), EmptyResponse);
		return;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, FString::Printf(TEXT("/patients/%d"), PatientId), TEXT("GET"), FString(), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback = MoveTemp(Callback)](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully) mutable
	{
		FFacialTherapyPatientResponse ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonStruct(Response->GetContentAsString(), ParsedResponse))
		{
			Callback(false, TEXT("Failed to parse patient profile response."), ParsedResponse);
			return;
		}

		if (ParsedResponse.id > 0)
		{
			UFacialTherapyApi::StoredPatientId = ParsedResponse.id;
			UFacialTherapyApi::StoredAvatarId = ParsedResponse.avatar_id;
			UFacialTherapyApi::SaveStoredAuth();
		}

		Callback(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::GetScheduleSummaryNative(int32 PatientId, TFunction<void(bool, const FString&, const FFacialTherapySchedulePreferenceSummary&)> Callback)
{
	FFacialTherapySchedulePreferenceSummary EmptyResponse;
	if (AccessToken.IsEmpty())
	{
		Callback(false, TEXT("No access token is stored."), EmptyResponse);
		return;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, FString::Printf(TEXT("/therapy/patient/%d/schedule/summary"), PatientId), TEXT("GET"), FString(), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback = MoveTemp(Callback)](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully) mutable
	{
		FFacialTherapySchedulePreferenceSummary ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonStruct(Response->GetContentAsString(), ParsedResponse))
		{
			Callback(false, TEXT("Failed to parse schedule summary response."), ParsedResponse);
			return;
		}

		Callback(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::LoginNative(const FString& Username, const FString& Password, TFunction<void(bool, const FString&, const FFacialTherapyLoginResponse&)> Callback)
{
	TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetStringField(TEXT("username"), Username);
	JsonObject->SetStringField(TEXT("password"), Password);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, TEXT("/auth/login"), TEXT("POST"), SerializeJsonObject(JsonObject), false, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback = MoveTemp(Callback)](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully) mutable
	{
		FFacialTherapyLoginResponse ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonStruct(Response->GetContentAsString(), ParsedResponse))
		{
			Callback(false, TEXT("Failed to parse login response."), ParsedResponse);
			return;
		}

		UFacialTherapyApi::AccessToken = ParsedResponse.access_token;
		UFacialTherapyApi::RefreshToken = ParsedResponse.refresh_token;
		UFacialTherapyApi::StoredUserId = ParsedResponse.user_id;
		UFacialTherapyApi::StoredPatientId = ParsedResponse.patient_id;
		UFacialTherapyApi::StoredRole = ParsedResponse.role;
		UFacialTherapyApi::SaveStoredAuth();
		Callback(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::Login(const FString& Username, const FString& Password, const FFacialTherapyLoginCallback& Callback)
{
	LoginNative(Username, Password, [Callback](bool bSuccess, const FString& ErrorMessage, const FFacialTherapyLoginResponse& Response)
	{
		Callback.ExecuteIfBound(bSuccess, ErrorMessage, Response);
	});
}

void UFacialTherapyApi::RegisterPatientAuth(const FString& Username, const FString& Password, const FFacialTherapyRegisterCallback& Callback)
{
	TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetStringField(TEXT("username"), Username);
	JsonObject->SetStringField(TEXT("password"), Password);
	JsonObject->SetStringField(TEXT("role"), TEXT("patient"));

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, TEXT("/auth/register"), TEXT("POST"), SerializeJsonObject(JsonObject), false, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		FFacialTherapyRegisterResponse ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback.ExecuteIfBound(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonStruct(Response->GetContentAsString(), ParsedResponse))
		{
			Callback.ExecuteIfBound(false, TEXT("Failed to parse registration response."), ParsedResponse);
			return;
		}

		UFacialTherapyApi::AccessToken = ParsedResponse.access_token;
		UFacialTherapyApi::RefreshToken = ParsedResponse.refresh_token;
		UFacialTherapyApi::StoredUserId = ParsedResponse.user_id;
		UFacialTherapyApi::StoredRole = TEXT("patient");
		UFacialTherapyApi::SaveStoredAuth();
		Callback.ExecuteIfBound(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::RefreshAccessToken(const FFacialTherapyTokenCallback& Callback)
{
	if (RefreshToken.IsEmpty())
	{
		FFacialTherapyTokenResponse EmptyResponse;
		Callback.ExecuteIfBound(false, TEXT("No refresh token is stored."), EmptyResponse);
		return;
	}

	TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetStringField(TEXT("refresh_token"), RefreshToken);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, TEXT("/auth/refresh"), TEXT("POST"), SerializeJsonObject(JsonObject), false, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		FFacialTherapyTokenResponse ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback.ExecuteIfBound(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonStruct(Response->GetContentAsString(), ParsedResponse))
		{
			Callback.ExecuteIfBound(false, TEXT("Failed to parse token refresh response."), ParsedResponse);
			return;
		}

		UFacialTherapyApi::AccessToken = ParsedResponse.access_token;
		UFacialTherapyApi::RefreshToken = ParsedResponse.refresh_token;
		UFacialTherapyApi::SaveStoredAuth();
		Callback.ExecuteIfBound(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::Logout(const FFacialTherapyBasicCallback& Callback)
{
	if (RefreshToken.IsEmpty())
	{
		Callback.ExecuteIfBound(false, TEXT("No refresh token is stored."));
		return;
	}

	TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetStringField(TEXT("refresh_token"), RefreshToken);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, TEXT("/auth/logout"), TEXT("POST"), SerializeJsonObject(JsonObject), false, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback.ExecuteIfBound(false, ExtractErrorMessage(Response));
			return;
		}

		UFacialTherapyApi::ClearStoredAuth();
		Callback.ExecuteIfBound(true, FString());
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::LogoutAll(const FFacialTherapyBasicCallback& Callback)
{
	if (AccessToken.IsEmpty())
	{
		Callback.ExecuteIfBound(false, TEXT("No access token is stored."));
		return;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, TEXT("/auth/logout-all"), TEXT("POST"), FString(), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback.ExecuteIfBound(false, ExtractErrorMessage(Response));
			return;
		}

		UFacialTherapyApi::ClearStoredAuth();
		Callback.ExecuteIfBound(true, FString());
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::GetCurrentUser(const FFacialTherapyCurrentUserCallback& Callback)
{
	if (AccessToken.IsEmpty())
	{
		FFacialTherapyCurrentUserResponse EmptyResponse;
		Callback.ExecuteIfBound(false, TEXT("No access token is stored."), EmptyResponse);
		return;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, TEXT("/auth/me"), TEXT("GET"), FString(), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		FFacialTherapyCurrentUserResponse ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback.ExecuteIfBound(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonStruct(Response->GetContentAsString(), ParsedResponse))
		{
			Callback.ExecuteIfBound(false, TEXT("Failed to parse current user response."), ParsedResponse);
			return;
		}

		UFacialTherapyApi::StoredUserId = ParsedResponse.user_id;
		UFacialTherapyApi::StoredPatientId = ParsedResponse.patient_id;
		UFacialTherapyApi::StoredRole = ParsedResponse.role;
		UFacialTherapyApi::SaveStoredAuth();
		Callback.ExecuteIfBound(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::CreatePatientProfile(int32 UserId, const FString& Name, const FString& Birthdate, const FString& Illness, const FString& Limitations, const FString& Email, int32 AvatarId, const FFacialTherapyPatientCallback& Callback)
{
	if (AccessToken.IsEmpty())
	{
		FFacialTherapyPatientResponse EmptyResponse;
		Callback.ExecuteIfBound(false, TEXT("No access token is stored."), EmptyResponse);
		return;
	}

	TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetNumberField(TEXT("user_id"), UserId);
	JsonObject->SetStringField(TEXT("name"), Name);
	JsonObject->SetStringField(TEXT("birthdate"), Birthdate);
	JsonObject->SetStringField(TEXT("illness"), Illness);
	if (!Limitations.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("limitations"), Limitations);
	}
	if (!Email.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("email"), Email);
	}
	if (AvatarId > 0)
	{
		JsonObject->SetNumberField(TEXT("avatar_id"), AvatarId);
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, TEXT("/patients/"), TEXT("POST"), SerializeJsonObject(JsonObject), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		FFacialTherapyPatientResponse ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback.ExecuteIfBound(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonStruct(Response->GetContentAsString(), ParsedResponse))
		{
			Callback.ExecuteIfBound(false, TEXT("Failed to parse patient profile response."), ParsedResponse);
			return;
		}

		UFacialTherapyApi::StoredPatientId = ParsedResponse.id;
		UFacialTherapyApi::StoredAvatarId = ParsedResponse.avatar_id;
		UFacialTherapyApi::SaveStoredAuth();
		Callback.ExecuteIfBound(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::UpdatePatientProfile(int32 PatientId, const FString& Name, const FString& Birthdate, const FString& Illness, const FString& Limitations, const FString& Email, int32 AvatarId, const FFacialTherapyPatientCallback& Callback)
{
	if (AccessToken.IsEmpty())
	{
		FFacialTherapyPatientResponse EmptyResponse;
		Callback.ExecuteIfBound(false, TEXT("No access token is stored."), EmptyResponse);
		return;
	}

	TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	if (!Name.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("name"), Name);
	}
	if (!Birthdate.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("birthdate"), Birthdate);
	}
	if (!Illness.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("illness"), Illness);
	}
	if (!Limitations.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("limitations"), Limitations);
	}
	if (!Email.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("email"), Email);
	}
	if (AvatarId > 0)
	{
		JsonObject->SetNumberField(TEXT("avatar_id"), AvatarId);
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, FString::Printf(TEXT("/patients/%d"), PatientId), TEXT("PUT"), SerializeJsonObject(JsonObject), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		FFacialTherapyPatientResponse ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback.ExecuteIfBound(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonStruct(Response->GetContentAsString(), ParsedResponse))
		{
			Callback.ExecuteIfBound(false, TEXT("Failed to parse patient profile response."), ParsedResponse);
			return;
		}

		UFacialTherapyApi::StoredPatientId = ParsedResponse.id;
		UFacialTherapyApi::StoredAvatarId = ParsedResponse.avatar_id;
		UFacialTherapyApi::SaveStoredAuth();
		Callback.ExecuteIfBound(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::GetPatientProfile(int32 PatientId, const FFacialTherapyPatientCallback& Callback)
{
	if (AccessToken.IsEmpty())
	{
		FFacialTherapyPatientResponse EmptyResponse;
		Callback.ExecuteIfBound(false, TEXT("No access token is stored."), EmptyResponse);
		return;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, FString::Printf(TEXT("/patients/%d"), PatientId), TEXT("GET"), FString(), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		FFacialTherapyPatientResponse ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback.ExecuteIfBound(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonStruct(Response->GetContentAsString(), ParsedResponse))
		{
			Callback.ExecuteIfBound(false, TEXT("Failed to parse patient response."), ParsedResponse);
			return;
		}

		UFacialTherapyApi::StoredPatientId = ParsedResponse.id;
		UFacialTherapyApi::StoredAvatarId = ParsedResponse.avatar_id;
		Callback.ExecuteIfBound(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::GetPatientForUE(int32 PatientId, const FFacialTherapyUEPatientCallback& Callback)
{
	if (AccessToken.IsEmpty())
	{
		FFacialTherapyUEPatientResponse EmptyResponse;
		Callback.ExecuteIfBound(false, TEXT("No access token is stored."), EmptyResponse);
		return;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, FString::Printf(TEXT("/ue/patient/%d"), PatientId), TEXT("GET"), FString(), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		FFacialTherapyUEPatientResponse ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback.ExecuteIfBound(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonStruct(Response->GetContentAsString(), ParsedResponse))
		{
			Callback.ExecuteIfBound(false, TEXT("Failed to parse Unreal patient response."), ParsedResponse);
			return;
		}

		if (ParsedResponse.patient_id > 0)
		{
			UFacialTherapyApi::StoredPatientId = ParsedResponse.patient_id;
			UFacialTherapyApi::StoredAvatarId = ParsedResponse.avatar_id;
			UFacialTherapyApi::SaveStoredAuth();
		}
		Callback.ExecuteIfBound(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::GetCurrentPatientForUE(const FFacialTherapyUEPatientCallback& Callback)
{
	if (StoredPatientId <= 0)
	{
		LoadStoredAuth();
	}

	if (StoredPatientId <= 0)
	{
		FFacialTherapyUEPatientResponse EmptyResponse;
		Callback.ExecuteIfBound(false, TEXT("No stored patient id is available. Login first."), EmptyResponse);
		return;
	}

	GetPatientForUE(StoredPatientId, Callback);
}

void UFacialTherapyApi::UpdateCurrentPatientAvatar(int32 AvatarId, const FFacialTherapyPatientCallback& Callback)
{
	if (StoredPatientId <= 0)
	{
		LoadStoredAuth();
	}

	if (StoredPatientId <= 0)
	{
		FFacialTherapyPatientResponse EmptyResponse;
		Callback.ExecuteIfBound(false, TEXT("No stored patient id is available. Login first."), EmptyResponse);
		return;
	}

	UpdatePatientProfile(StoredPatientId, FString(), FString(), FString(), FString(), FString(), AvatarId, Callback);
}

void UFacialTherapyApi::SetPatientMode(int32 PatientId, int32 Mode, const FFacialTherapyModeCallback& Callback)
{
	if (AccessToken.IsEmpty())
	{
		FFacialTherapyModeResponse EmptyResponse;
		Callback.ExecuteIfBound(false, TEXT("No access token is stored."), EmptyResponse);
		return;
	}

	FString ModeString;
	if (!ConvertModeValue(Mode, ModeString))
	{
		FFacialTherapyModeResponse EmptyResponse;
		Callback.ExecuteIfBound(false, TEXT("Invalid mode. Use 1 for single or 2 for dual."), EmptyResponse);
		return;
	}

	TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetNumberField(TEXT("patient_id"), PatientId);
	JsonObject->SetStringField(TEXT("mode"), ModeString);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, TEXT("/ue/mode"), TEXT("POST"), SerializeJsonObject(JsonObject), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		FFacialTherapyModeResponse ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback.ExecuteIfBound(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonStruct(Response->GetContentAsString(), ParsedResponse))
		{
			Callback.ExecuteIfBound(false, TEXT("Failed to parse mode response."), ParsedResponse);
			return;
		}

		UFacialTherapyApi::StoredMode = ParsedResponse.mode;
		UFacialTherapyApi::SaveStoredAuth();
		Callback.ExecuteIfBound(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::SetCurrentPatientMode(int32 Mode, const FFacialTherapyModeCallback& Callback)
{
	if (StoredPatientId <= 0)
	{
		LoadStoredAuth();
	}

	if (StoredPatientId <= 0)
	{
		FFacialTherapyModeResponse EmptyResponse;
		Callback.ExecuteIfBound(false, TEXT("No stored patient id is available. Login first."), EmptyResponse);
		return;
	}

	SetPatientMode(StoredPatientId, Mode, Callback);
}

void UFacialTherapyApi::SetCurrentPatientModeForUE(int32 Mode, const FFacialTherapyModeCallback& Callback)
{
	if (StoredPatientId <= 0)
	{
		LoadStoredAuth();
	}

	if (StoredPatientId <= 0)
	{
		FFacialTherapyModeResponse EmptyResponse;
		Callback.ExecuteIfBound(false, TEXT("No stored patient id is available. Login first."), EmptyResponse);
		return;
	}

	FString ModeString;
	if (!ConvertModeValue(Mode, ModeString))
	{
		FFacialTherapyModeResponse EmptyResponse;
		Callback.ExecuteIfBound(false, TEXT("Invalid mode. Use 1 for single or 2 for dual."), EmptyResponse);
		return;
	}

	StoredMode = ModeString;
	SaveStoredAuth();

	SetPatientMode(StoredPatientId, Mode, Callback);
}

void UFacialTherapyApi::SetCurrentPatientModeByType(EFacialTherapySessionMode ModeType, const FFacialTherapyModeCallback& Callback)
{
	if (StoredPatientId <= 0)
	{
		LoadStoredAuth();
	}

	if (StoredPatientId <= 0)
	{
		FFacialTherapyModeResponse EmptyResponse;
		Callback.ExecuteIfBound(false, TEXT("No stored patient id is available. Login first."), EmptyResponse);
		return;
	}

	FString ModeString;
	if (!ConvertModeType(ModeType, ModeString))
	{
		FFacialTherapyModeResponse EmptyResponse;
		Callback.ExecuteIfBound(false, TEXT("Invalid mode type."), EmptyResponse);
		return;
	}

	StoredMode = ModeString;
	SaveStoredAuth();

	SetPatientMode(StoredPatientId, ConvertModeTypeToLegacyValue(ModeType), Callback);
}

void UFacialTherapyApi::SetCurrentPatientModeByTypeLocal(EFacialTherapySessionMode ModeType)
{
	if (StoredPatientId <= 0)
	{
		LoadStoredAuth();
	}

	FString ModeString;
	if (!ConvertModeType(ModeType, ModeString))
	{
		return;
	}

	StoredMode = ModeString;
	SaveStoredAuth();

	if (StoredPatientId <= 0 || AccessToken.IsEmpty())
	{
		return;
	}

	FFacialTherapyModeCallback SilentCallback;
	SetPatientMode(StoredPatientId, ConvertModeTypeToLegacyValue(ModeType), SilentCallback);
}

void UFacialTherapyApi::CreateSession(int32 PatientId, const FString& ExerciseName, const FString& Mode, const FFacialTherapySessionCallback& Callback)
{
	if (AccessToken.IsEmpty())
	{
		FFacialTherapySessionResponse EmptyResponse;
		Callback.ExecuteIfBound(false, TEXT("No access token is stored."), EmptyResponse);
		return;
	}

	TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetNumberField(TEXT("patient_id"), PatientId);
	JsonObject->SetStringField(TEXT("exercise_name"), ExerciseName);
	JsonObject->SetStringField(TEXT("mode"), Mode);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, TEXT("/sessions/"), TEXT("POST"), SerializeJsonObject(JsonObject), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		FFacialTherapySessionResponse ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback.ExecuteIfBound(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonStruct(Response->GetContentAsString(), ParsedResponse))
		{
			Callback.ExecuteIfBound(false, TEXT("Failed to parse session response."), ParsedResponse);
			return;
		}

		Callback.ExecuteIfBound(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::UpdateSession(int32 SessionId, int32 AccuracyScore, const FString& CompletedAtIso, const FFacialTherapySessionCallback& Callback)
{
	if (AccessToken.IsEmpty())
	{
		FFacialTherapySessionResponse EmptyResponse;
		Callback.ExecuteIfBound(false, TEXT("No access token is stored."), EmptyResponse);
		return;
	}

	TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetNumberField(TEXT("accuracy_score"), FMath::Clamp(AccuracyScore, 0, 100));
	if (!CompletedAtIso.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("completed_at"), CompletedAtIso);
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, FString::Printf(TEXT("/sessions/%d"), SessionId), TEXT("PUT"), SerializeJsonObject(JsonObject), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		FFacialTherapySessionResponse ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback.ExecuteIfBound(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonStruct(Response->GetContentAsString(), ParsedResponse))
		{
			Callback.ExecuteIfBound(false, TEXT("Failed to parse session response."), ParsedResponse);
			return;
		}

		Callback.ExecuteIfBound(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::CompleteSession(int32 SessionId, int32 AccuracyScore, const FFacialTherapySessionCallback& Callback)
{
	if (AccessToken.IsEmpty())
	{
		FFacialTherapySessionResponse EmptyResponse;
		Callback.ExecuteIfBound(false, TEXT("No access token is stored."), EmptyResponse);
		return;
	}

	const FString Path = FString::Printf(TEXT("/sessions/%d/complete?accuracy_score=%d"), SessionId, FMath::Clamp(AccuracyScore, 0, 100));
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, Path, TEXT("POST"), FString(), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		FFacialTherapySessionResponse ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback.ExecuteIfBound(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonStruct(Response->GetContentAsString(), ParsedResponse))
		{
			Callback.ExecuteIfBound(false, TEXT("Failed to parse session completion response."), ParsedResponse);
			return;
		}

		Callback.ExecuteIfBound(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::SendJymminActivityNative(float Value)
{
	TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetNumberField(TEXT("value"), FMath::Clamp(Value, 0.f, 1.f));

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, TEXT("/ws/jymmin/activity"), TEXT("POST"), SerializeJsonObject(JsonObject), false, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			UE_LOG(LogTemp, Warning, TEXT("Failed to send Jymmin activity: %s"), *ExtractErrorMessage(Response));
		}
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::GetPatientSessions(int32 PatientId, int32 Limit, const FFacialTherapySessionListCallback& Callback)
{
	if (AccessToken.IsEmpty())
	{
		FFacialTherapySessionListResponse EmptyResponse;
		Callback.ExecuteIfBound(false, TEXT("No access token is stored."), EmptyResponse);
		return;
	}

	const FString Path = FString::Printf(TEXT("/sessions/patient/%d?limit=%d"), PatientId, FMath::Max(Limit, 1));
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, Path, TEXT("GET"), FString(), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		FFacialTherapySessionListResponse ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback.ExecuteIfBound(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonArray(Response->GetContentAsString(), ParsedResponse.items))
		{
			Callback.ExecuteIfBound(false, TEXT("Failed to parse patient sessions response."), ParsedResponse);
			return;
		}

		Callback.ExecuteIfBound(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::GetExercisesForSelectedPhase(int32 PhaseNumber, const FFacialTherapyPhaseExerciseListCallback& Callback)
{
	const int32 PatientId = GetStoredPatientId();
	if (PatientId <= 0)
	{
		FFacialTherapyPhaseExerciseListResponse EmptyResponse;
		Callback.ExecuteIfBound(false, TEXT("No stored patient is available."), EmptyResponse);
		return;
	}

	UFacialTherapyApi::GetCurrentProgressNative(PatientId, [Callback, PhaseNumber](bool bProgressSuccess, const FString& ProgressError, const FFacialTherapyPatientPhaseProgressResponse& ProgressResponse) mutable
	{
		if (!bProgressSuccess)
		{
			FFacialTherapyPhaseExerciseListResponse EmptyResponse;
			Callback.ExecuteIfBound(false, ProgressError, EmptyResponse);
			return;
		}

		TSet<int32> MasteredExercises;
		for (int32 Value : ProgressResponse.exercises_mastered)
		{
			MasteredExercises.Add(Value);
		}

		TSet<int32> MasteredPairs;
		for (int32 Value : ProgressResponse.pairs_mastered)
		{
			MasteredPairs.Add(Value);
		}

		TSet<FString> MasteredSequences;
		for (const FString& Value : ProgressResponse.sequences_mastered)
		{
			MasteredSequences.Add(Value.ToUpper());
		}

		FFacialTherapyPhaseExerciseListResponse Response;
		Response.phase_number = PhaseNumber;

		auto AddItem = [&Response](int32 ItemId, FName RowName, const FString& Title, const FString& Description, const FString& ItemType,
			int32 HoldSeconds, int32 Repetitions, int32 RestBetweenSeconds, int32 RestAfterSeconds, int32 PairNumber, const FString& SequenceCode,
			const TArray<FName>& Expressions, bool bCompleted)
		{
			FFacialTherapyPhaseExerciseItem Item;
			Item.row_name = RowName;
			Item.item_id = ItemId;
			Item.phase_number = Response.phase_number;
			Item.title = Title;
			Item.description = Description;
			Item.item_type = ItemType;
			Item.hold_duration_seconds = HoldSeconds;
			Item.repetitions = Repetitions;
			Item.rest_between_seconds = RestBetweenSeconds;
			Item.rest_after_seconds = RestAfterSeconds;
			Item.pair_number = PairNumber;
			Item.sequence_code = SequenceCode;
			Item.expressions = Expressions;
			Item.b_completed_before = bCompleted;
			Response.items.Add(Item);
		};

		if (PhaseNumber == 1)
		{
			Response.phase_name = TEXT("Phase 1: Foundations");
			Response.list_heading = TEXT("Select an exercise to practice");
			Response.instructions = TEXT("Practice one facial expression at a time. Hold each expression for 15 seconds.");

			struct FPhase1Exercise { FName Name; int32 Id; FString Title; };
			static const FPhase1Exercise Phase1Exercises[] = {
				{TEXT("Fear"), 4, TEXT("Fear")},
				{TEXT("Smicheeks"), 9, TEXT("Smile + Cheeks")},
				{TEXT("Sad"), 6, TEXT("Sadness")},
				{TEXT("Surprise"), 2, TEXT("Surprised")},
				{TEXT("Pout"), 7, TEXT("Pout")},
				{TEXT("Puffcheeks"), 8, TEXT("Puff Cheeks")},
				{TEXT("Smile"), 5, TEXT("Smile")},
				{TEXT("Disgust"), 3, TEXT("Disgust")},
				{TEXT("Angry"), 1, TEXT("Angry")}
			};

			for (const FPhase1Exercise& Entry : Phase1Exercises)
			{
				TArray<FName> Expressions;
				Expressions.Add(Entry.Name);
				AddItem(Entry.Id, Entry.Name, Entry.Title, TEXT("Isolated expression practice."), TEXT("SingleExercise"),
					15, 3, 10, 10, 0, FString(), Expressions, MasteredExercises.Contains(Entry.Id));
			}
		}
		else if (PhaseNumber == 2)
		{
			Response.phase_name = TEXT("Phase 2: Antagonistic Pairs");
			Response.list_heading = TEXT("Select a pair to practice");
			Response.instructions = TEXT("Alternate between two expressions. Hold each expression for 20 seconds.");

			AddItem(201, TEXT("Pair1"), TEXT("Pair 1: Surprised - Angry"), TEXT("Alternate between Surprised and Angry."), TEXT("Pair"),
				20, 4, 5, 10, 1, FString(), {TEXT("Surprise"), TEXT("Angry")}, MasteredPairs.Contains(1));
			AddItem(202, TEXT("Pair2"), TEXT("Pair 2: Smile - Sadness"), TEXT("Alternate between Smile and Sadness."), TEXT("Pair"),
				20, 4, 5, 10, 2, FString(), {TEXT("Smile"), TEXT("Sad")}, MasteredPairs.Contains(2));
			AddItem(203, TEXT("Pair3"), TEXT("Pair 3: Disgust - Neutral"), TEXT("Practice Disgust, then relax to neutral."), TEXT("Pair"),
				20, 4, 5, 10, 3, FString(), {TEXT("Disgust"), TEXT("Neutral")}, MasteredPairs.Contains(3));
			AddItem(204, TEXT("Pair4"), TEXT("Pair 4: Fear - Surprised"), TEXT("Alternate between Fear and Surprised."), TEXT("Pair"),
				20, 4, 5, 10, 4, FString(), {TEXT("Fear"), TEXT("Surprise")}, MasteredPairs.Contains(4));
		}
		else if (PhaseNumber == 3)
		{
			Response.phase_name = TEXT("Phase 3: Complex Sequences");
			Response.list_heading = TEXT("Select a sequence to practice");
			Response.instructions = TEXT("Practice a multi-expression sequence. Hold each expression for 25 seconds.");

			AddItem(301, TEXT("SequenceA"), TEXT("Sequence A: Upper Face"), TEXT("Surprised, Angry, then Surprised again."), TEXT("Sequence"),
				25, 2, 5, 20, 0, TEXT("A"), {TEXT("Surprise"), TEXT("Angry"), TEXT("Surprise")}, MasteredSequences.Contains(TEXT("A")));
			AddItem(302, TEXT("SequenceB"), TEXT("Sequence B: Lower Face"), TEXT("Smile, Sadness, Pout, then Smile again."), TEXT("Sequence"),
				25, 2, 5, 20, 0, TEXT("B"), {TEXT("Smile"), TEXT("Sad"), TEXT("Pout"), TEXT("Smile")}, MasteredSequences.Contains(TEXT("B")));
			AddItem(303, TEXT("SequenceC"), TEXT("Sequence C: Whole Face"), TEXT("Fear, Disgust, then Surprised."), TEXT("Sequence"),
				25, 2, 5, 20, 0, TEXT("C"), {TEXT("Fear"), TEXT("Disgust"), TEXT("Surprise")}, MasteredSequences.Contains(TEXT("C")));
			AddItem(304, TEXT("SequenceD"), TEXT("Sequence D: Special Exercises"), TEXT("Puff Cheeks, then Smile + Cheeks."), TEXT("Sequence"),
				25, 2, 5, 20, 0, TEXT("D"), {TEXT("Puffcheeks"), TEXT("Smicheeks")}, MasteredSequences.Contains(TEXT("D")));
		}
		else if (PhaseNumber == 4)
		{
			Response.phase_name = TEXT("Phase 4: Mastery");
			Response.list_heading = TEXT("Select a mastery exercise");
			Response.instructions = TEXT("Practice advanced sequences or targeted isolated exercises with 25-second holds.");

			AddItem(401, TEXT("MasterySequences"), TEXT("Mastery: Complex Sequences"), TEXT("Practice two complex sequences from Phase 3."), TEXT("Mixed"),
				25, 2, 5, 20, 0, TEXT("MASTERY_SEQ"), {TEXT("Surprise"), TEXT("Angry"), TEXT("Surprise"), TEXT("Smile"), TEXT("Sad"), TEXT("Pout"), TEXT("Smile")}, false);
			AddItem(402, TEXT("MasteryTargeted"), TEXT("Mastery: Targeted Practice"), TEXT("Practice Disgust, Sadness, and Fear with longer holds."), TEXT("Mixed"),
				25, 4, 10, 20, 0, TEXT("MASTERY_TARGET"), {TEXT("Disgust"), TEXT("Sad"), TEXT("Fear")}, false);
		}
		else
		{
			Response.phase_number = 5;
			Response.phase_name = TEXT("Maintenance");
			Response.list_heading = TEXT("Select a maintenance session");
			Response.instructions = TEXT("Keep the program active with a shorter flexible practice session.");

			AddItem(501, TEXT("MaintenanceShort"), TEXT("Short Maintenance"), TEXT("One sequence plus two isolated exercises."), TEXT("Mixed"),
				25, 2, 5, 20, 0, TEXT("MAINT_SHORT"), {TEXT("Surprise"), TEXT("Angry"), TEXT("Surprise"), TEXT("Smile"), TEXT("Pout")}, false);
			AddItem(502, TEXT("MaintenanceIntensive"), TEXT("Intensive Maintenance"), TEXT("Three sequences plus targeted practice."), TEXT("Mixed"),
				25, 2, 5, 20, 0, TEXT("MAINT_INTENSIVE"), {TEXT("Surprise"), TEXT("Angry"), TEXT("Surprise"), TEXT("Smile"), TEXT("Sad"), TEXT("Pout"), TEXT("Fear"), TEXT("Disgust"), TEXT("Puffcheeks"), TEXT("Smicheeks")}, false);
		}

		Callback.ExecuteIfBound(true, FString(), Response);
	});
}

void UFacialTherapyApi::GetTherapySessionHistory(int32 PatientId, int32 Limit, const FFacialTherapyTherapySessionListCallback& Callback)
{
	if (AccessToken.IsEmpty())
	{
		FFacialTherapyTherapySessionListResponse EmptyResponse;
		Callback.ExecuteIfBound(false, TEXT("No access token is stored."), EmptyResponse);
		return;
	}

	const FString Path = FString::Printf(TEXT("/therapy/session/history/%d?limit=%d"), PatientId, FMath::Max(Limit, 1));
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, Path, TEXT("GET"), FString(), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		FFacialTherapyTherapySessionListResponse ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback.ExecuteIfBound(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonArray(Response->GetContentAsString(), ParsedResponse.items))
		{
			Callback.ExecuteIfBound(false, TEXT("Failed to parse therapy session history response."), ParsedResponse);
			return;
		}

		Callback.ExecuteIfBound(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::GetProgressOverview(int32 PatientId, const FFacialTherapyOverviewCallback& Callback)
{
	if (AccessToken.IsEmpty())
	{
		FFacialTherapyProgressOverview EmptyResponse;
		Callback.ExecuteIfBound(false, TEXT("No access token is stored."), EmptyResponse);
		return;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, FString::Printf(TEXT("/therapy/overview/%d"), PatientId), TEXT("GET"), FString(), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		FFacialTherapyProgressOverview ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback.ExecuteIfBound(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonStruct(Response->GetContentAsString(), ParsedResponse))
		{
			Callback.ExecuteIfBound(false, TEXT("Failed to parse progress overview response."), ParsedResponse);
			return;
		}

		Callback.ExecuteIfBound(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::GetCurrentProgress(int32 PatientId, const FFacialTherapyProgressCallback& Callback)
{
	if (AccessToken.IsEmpty())
	{
		FFacialTherapyPatientPhaseProgressResponse EmptyResponse;
		Callback.ExecuteIfBound(false, TEXT("No access token is stored."), EmptyResponse);
		return;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, FString::Printf(TEXT("/therapy/progress/%d"), PatientId), TEXT("GET"), FString(), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		FFacialTherapyPatientPhaseProgressResponse ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback.ExecuteIfBound(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonStruct(Response->GetContentAsString(), ParsedResponse))
		{
			Callback.ExecuteIfBound(false, TEXT("Failed to parse progress response."), ParsedResponse);
			return;
		}

		Callback.ExecuteIfBound(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::UpdateProgress(int32 PatientId, int32 ExerciseId, int32 PairNumber, const FString& SequenceCode, bool bIncrementSession, const FFacialTherapyProgressUpdateCallback& Callback)
{
	if (AccessToken.IsEmpty())
	{
		FFacialTherapyProgressUpdateResponse EmptyResponse;
		Callback.ExecuteIfBound(false, TEXT("No access token is stored."), EmptyResponse);
		return;
	}

	TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	if (ExerciseId > 0)
	{
		JsonObject->SetNumberField(TEXT("exercise_id"), ExerciseId);
	}
	if (PairNumber > 0)
	{
		JsonObject->SetNumberField(TEXT("pair_number"), PairNumber);
	}
	if (!SequenceCode.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("sequence_code"), SequenceCode);
	}
	JsonObject->SetBoolField(TEXT("increment_session"), bIncrementSession);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, FString::Printf(TEXT("/therapy/progress/%d"), PatientId), TEXT("PUT"), SerializeJsonObject(JsonObject), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		FFacialTherapyProgressUpdateResponse ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback.ExecuteIfBound(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonStruct(Response->GetContentAsString(), ParsedResponse))
		{
			Callback.ExecuteIfBound(false, TEXT("Failed to parse progress update response."), ParsedResponse);
			return;
		}

		Callback.ExecuteIfBound(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::GetFrequencyHistory(int32 PatientId, int32 Weeks, const FFacialTherapyFrequencyCallback& Callback)
{
	if (AccessToken.IsEmpty())
	{
		FFacialTherapyFrequencyHistoryResponse EmptyResponse;
		Callback.ExecuteIfBound(false, TEXT("No access token is stored."), EmptyResponse);
		return;
	}

	const FString Path = FString::Printf(TEXT("/therapy/frequency/%d?weeks=%d"), PatientId, FMath::Clamp(Weeks, 1, 12));
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, Path, TEXT("GET"), FString(), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		FFacialTherapyFrequencyHistoryResponse ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback.ExecuteIfBound(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonStruct(Response->GetContentAsString(), ParsedResponse))
		{
			Callback.ExecuteIfBound(false, TEXT("Failed to parse frequency response."), ParsedResponse);
			return;
		}

		Callback.ExecuteIfBound(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::GetStreakSummary(int32 PatientId, const FFacialTherapyStreakCallback& Callback)
{
	if (AccessToken.IsEmpty())
	{
		FFacialTherapyStreakSummary EmptyResponse;
		Callback.ExecuteIfBound(false, TEXT("No access token is stored."), EmptyResponse);
		return;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, FString::Printf(TEXT("/therapy/streak/%d"), PatientId), TEXT("GET"), FString(), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		FFacialTherapyStreakSummary ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback.ExecuteIfBound(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonStruct(Response->GetContentAsString(), ParsedResponse))
		{
			Callback.ExecuteIfBound(false, TEXT("Failed to parse streak response."), ParsedResponse);
			return;
		}

		Callback.ExecuteIfBound(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::GetSchedulePreference(int32 PatientId, const FFacialTherapySchedulePreferenceCallback& Callback)
{
	if (AccessToken.IsEmpty())
	{
		FFacialTherapySchedulePreferenceResponse EmptyResponse;
		Callback.ExecuteIfBound(false, TEXT("No access token is stored."), EmptyResponse);
		return;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, FString::Printf(TEXT("/therapy/patient/%d/schedule"), PatientId), TEXT("GET"), FString(), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		FFacialTherapySchedulePreferenceResponse ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback.ExecuteIfBound(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonStruct(Response->GetContentAsString(), ParsedResponse))
		{
			Callback.ExecuteIfBound(false, TEXT("Failed to parse schedule response."), ParsedResponse);
			return;
		}

		Callback.ExecuteIfBound(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::SetSchedulePreference(int32 PatientId, const TArray<int32>& RestDays, const FString& PreferredTime, bool bReminderEnabled, int32 ReminderMinutesBefore, const FString& CustomReminderMessage, const FFacialTherapySchedulePreferenceCallback& Callback)
{
	if (AccessToken.IsEmpty())
	{
		FFacialTherapySchedulePreferenceResponse EmptyResponse;
		Callback.ExecuteIfBound(false, TEXT("No access token is stored."), EmptyResponse);
		return;
	}

	TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> RestDayValues;
	for (const int32 RestDay : RestDays)
	{
		RestDayValues.Add(MakeShared<FJsonValueNumber>(RestDay));
	}
	JsonObject->SetArrayField(TEXT("rest_days"), RestDayValues);
	if (!PreferredTime.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("preferred_time"), PreferredTime);
	}
	JsonObject->SetBoolField(TEXT("reminder_enabled"), bReminderEnabled);
	JsonObject->SetNumberField(TEXT("reminder_minutes_before"), FMath::Clamp(ReminderMinutesBefore, 5, 120));
	if (!CustomReminderMessage.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("custom_reminder_message"), CustomReminderMessage);
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, FString::Printf(TEXT("/therapy/patient/%d/schedule"), PatientId), TEXT("PUT"), SerializeJsonObject(JsonObject), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		FFacialTherapySchedulePreferenceResponse ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback.ExecuteIfBound(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonStruct(Response->GetContentAsString(), ParsedResponse))
		{
			Callback.ExecuteIfBound(false, TEXT("Failed to parse schedule response."), ParsedResponse);
			return;
		}

		Callback.ExecuteIfBound(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::DeleteSchedulePreference(int32 PatientId, const FFacialTherapyBasicCallback& Callback)
{
	if (AccessToken.IsEmpty())
	{
		Callback.ExecuteIfBound(false, TEXT("No access token is stored."));
		return;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, FString::Printf(TEXT("/therapy/patient/%d/schedule"), PatientId), TEXT("DELETE"), FString(), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback.ExecuteIfBound(false, ExtractErrorMessage(Response));
			return;
		}

		Callback.ExecuteIfBound(true, FString());
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::GetScheduleSummary(int32 PatientId, const FFacialTherapyScheduleSummaryCallback& Callback)
{
	if (AccessToken.IsEmpty())
	{
		FFacialTherapySchedulePreferenceSummary EmptyResponse;
		Callback.ExecuteIfBound(false, TEXT("No access token is stored."), EmptyResponse);
		return;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateRequest(ApiBaseUrl, FString::Printf(TEXT("/therapy/patient/%d/schedule/summary"), PatientId), TEXT("GET"), FString(), true, AccessToken);
	Request->OnProcessRequestComplete().BindLambda([Callback](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		FFacialTherapySchedulePreferenceSummary ParsedResponse;
		if (!IsSuccessfulResponse(Response, bConnectedSuccessfully))
		{
			Callback.ExecuteIfBound(false, ExtractErrorMessage(Response), ParsedResponse);
			return;
		}

		if (!ParseJsonStruct(Response->GetContentAsString(), ParsedResponse))
		{
			Callback.ExecuteIfBound(false, TEXT("Failed to parse schedule summary response."), ParsedResponse);
			return;
		}

		Callback.ExecuteIfBound(true, FString(), ParsedResponse);
	});
	Request->ProcessRequest();
}

void UFacialTherapyApi::GenerateCurrentSessionCheckInDataNative(const FString& Stage, TFunction<void(bool, const FString&, const FString&)> Callback)
{
	GetCurrentPatientForUENative([Stage, Callback = MoveTemp(Callback)](bool bSuccess, const FString& ErrorMessage, const FFacialTherapyUEPatientResponse& PatientResponse) mutable
	{
		if (!bSuccess)
		{
			Callback(false, ErrorMessage, FString());
			return;
		}

		FString NormalizedStage = Stage.TrimStartAndEnd().ToLower();
		if (!NormalizedStage.Equals(TEXT("after"), ESearchCase::IgnoreCase))
		{
			NormalizedStage = TEXT("before");
		}

		FString PatientName = PatientResponse.name.TrimStartAndEnd();
		if (PatientName.IsEmpty())
		{
			PatientName = TEXT("Patient");
		}

		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("generated_at"), FDateTime::UtcNow().ToIso8601());
		Root->SetStringField(TEXT("stage"), NormalizedStage);

		TSharedRef<FJsonObject> TherapistObject = MakeShared<FJsonObject>();
		TherapistObject->SetStringField(TEXT("name"), TEXT("Marie"));
		TherapistObject->SetStringField(TEXT("avatar_url"), TEXT("../Shared/user_avatar.png"));
		TherapistObject->SetStringField(TEXT("initials"), TEXT("M"));
		Root->SetObjectField(TEXT("therapist"), TherapistObject);

		TSharedRef<FJsonObject> PatientObject = MakeShared<FJsonObject>();
		PatientObject->SetStringField(TEXT("name"), PatientName);
		PatientObject->SetNumberField(TEXT("patient_id"), PatientResponse.patient_id);
		Root->SetObjectField(TEXT("patient"), PatientObject);

		TSharedRef<FJsonObject> CopyObject = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> BeforeObject = MakeShared<FJsonObject>();
		BeforeObject->SetStringField(TEXT("eyebrow"), TEXT("Before the session"));
		BeforeObject->SetStringField(TEXT("title"), FString::Printf(TEXT("Hello %s."), *PatientName));
		BeforeObject->SetStringField(TEXT("question"), TEXT("How are you feeling today?"));
		BeforeObject->SetStringField(TEXT("hint"), TEXT("Your answer helps the therapist understand how you are doing before the exercise."));
		CopyObject->SetObjectField(TEXT("before"), BeforeObject);

		TSharedRef<FJsonObject> AfterObject = MakeShared<FJsonObject>();
		AfterObject->SetStringField(TEXT("eyebrow"), TEXT("After the session"));
		AfterObject->SetStringField(TEXT("title"), FString::Printf(TEXT("Well done, %s."), *PatientName));
		AfterObject->SetStringField(TEXT("question"), TEXT("How are you feeling now?"));
		AfterObject->SetStringField(TEXT("hint"), TEXT("Your answer will be saved with today's session summary."));
		CopyObject->SetObjectField(TEXT("after"), AfterObject);
		Root->SetObjectField(TEXT("copy"), CopyObject);

		struct FMoodDefinition
		{
			const TCHAR* Id;
			const TCHAR* Label;
			const TCHAR* Tone;
			const TCHAR* Icon;
		};

		const FMoodDefinition MoodDefinitions[] =
		{
			{ TEXT("very_good"), TEXT("Very good"), TEXT("success"), TEXT("smile") },
			{ TEXT("good"), TEXT("Good"), TEXT("info"), TEXT("smile") },
			{ TEXT("sad"), TEXT("Sad"), TEXT("warning"), TEXT("sad") },
			{ TEXT("not_good"), TEXT("Not good"), TEXT("muted"), TEXT("sad") }
		};

		TArray<TSharedPtr<FJsonValue>> MoodValues;
		for (const FMoodDefinition& MoodDefinition : MoodDefinitions)
		{
			TSharedRef<FJsonObject> MoodObject = MakeShared<FJsonObject>();
			MoodObject->SetStringField(TEXT("id"), MoodDefinition.Id);
			MoodObject->SetStringField(TEXT("label"), MoodDefinition.Label);
			MoodObject->SetStringField(TEXT("tone"), MoodDefinition.Tone);
			MoodObject->SetStringField(TEXT("icon"), MoodDefinition.Icon);
			MoodValues.Add(MakeShared<FJsonValueObject>(MoodObject));
		}
		Root->SetArrayField(TEXT("moods"), MoodValues);
		Root->SetStringField(TEXT("selected_mood"), FString());

		TSharedRef<FJsonObject> NextActionObject = MakeShared<FJsonObject>();
		NextActionObject->SetStringField(TEXT("label"), TEXT("Continue"));
		NextActionObject->SetStringField(TEXT("target"), NormalizedStage.Equals(TEXT("after"), ESearchCase::IgnoreCase)
			? TEXT("../PatientDashboard/facial_therapy_patient.html")
			: TEXT("../CameraCheck/camera_check.html"));
		Root->SetObjectField(TEXT("next_action"), NextActionObject);

		const FString JsonOutput = SerializeJsonObject(Root);
		const FString JsonPath = UFacialTherapyApi::GetSessionCheckInJsonPath();
		const FString ScriptPath = UFacialTherapyApi::GetSessionCheckInScriptPath();
		const FString ScriptOutput = FString::Printf(TEXT("window.dashboardData = %s;"), *JsonOutput);
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		PlatformFile.CreateDirectoryTree(*FPaths::GetPath(JsonPath));

		if (!FFileHelper::SaveStringToFile(JsonOutput, *JsonPath))
		{
			Callback(false, TEXT("Failed to write session check-in data file."), FString());
			return;
		}

		if (!FFileHelper::SaveStringToFile(ScriptOutput, *ScriptPath))
		{
			Callback(false, TEXT("Failed to write session check-in script file."), FString());
			return;
		}

		Callback(true, FString(), JsonPath);
	});
}

void UFacialTherapyApi::GenerateCurrentSessionCheckInData(const FString& Stage, const FFacialTherapyDashboardCallback& Callback)
{
	GenerateCurrentSessionCheckInDataNative(Stage, [Callback](bool bSuccess, const FString& ErrorMessage, const FString& JsonFilePath)
	{
		Callback.ExecuteIfBound(bSuccess, ErrorMessage, JsonFilePath);
	});
}

void UFacialTherapyApi::GenerateCurrentCameraCheckData(const FFacialTherapyDashboardCallback& Callback)
{
	GetCurrentPatientForUENative([Callback](bool bSuccess, const FString& ErrorMessage, const FFacialTherapyUEPatientResponse& PatientResponse)
	{
		if (!bSuccess)
		{
			Callback.ExecuteIfBound(false, ErrorMessage, FString());
			return;
		}

		FString PatientName = PatientResponse.name.TrimStartAndEnd();
		if (PatientName.IsEmpty())
		{
			PatientName = TEXT("Patient");
		}

		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("generated_at"), FDateTime::UtcNow().ToIso8601());
		Root->SetStringField(TEXT("state"), TEXT("manual"));
		Root->SetStringField(TEXT("eyebrow"), TEXT("Camera check"));
		Root->SetStringField(TEXT("title"), TEXT("Is your face clearly visible?"));

		TSharedRef<FJsonObject> PatientObject = MakeShared<FJsonObject>();
		PatientObject->SetStringField(TEXT("name"), PatientName);
		PatientObject->SetNumberField(TEXT("patient_id"), PatientResponse.patient_id);
		Root->SetObjectField(TEXT("patient"), PatientObject);

		TSharedRef<FJsonObject> CameraObject = MakeShared<FJsonObject>();
		CameraObject->SetStringField(TEXT("image_url"), FString());
		CameraObject->SetStringField(TEXT("label"), TEXT("Camera preview"));
		Root->SetObjectField(TEXT("camera"), CameraObject);

		TSharedRef<FJsonObject> MessageObject = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> GoodObject = MakeShared<FJsonObject>();
		GoodObject->SetStringField(TEXT("title"), TEXT("Your position is good."));
		GoodObject->SetStringField(TEXT("body"), TEXT("You can start the exercise now."));
		MessageObject->SetObjectField(TEXT("good"), GoodObject);

		TSharedRef<FJsonObject> BadObject = MakeShared<FJsonObject>();
		BadObject->SetStringField(TEXT("title"), TEXT("Confirm the camera setup."));
		BadObject->SetStringField(TEXT("body"), TEXT("Make sure your face is visible, well lit, and inside the frame."));
		MessageObject->SetObjectField(TEXT("bad"), BadObject);
		Root->SetObjectField(TEXT("message"), MessageObject);

		struct FCheckDefinition
		{
			const TCHAR* Key;
			const TCHAR* Label;
			bool bOk;
		};

		const FCheckDefinition CheckDefinitions[] =
		{
			{ TEXT("faceVisible"), TEXT("Face fully visible"), false },
			{ TEXT("lightingOk"), TEXT("Sufficient lighting"), false },
			{ TEXT("faceAligned"), TEXT("Face aligned in the box"), false }
		};

		TArray<TSharedPtr<FJsonValue>> CheckValues;
		for (const FCheckDefinition& CheckDefinition : CheckDefinitions)
		{
			TSharedRef<FJsonObject> CheckObject = MakeShared<FJsonObject>();
			CheckObject->SetStringField(TEXT("key"), CheckDefinition.Key);
			CheckObject->SetStringField(TEXT("label"), CheckDefinition.Label);
			CheckObject->SetBoolField(TEXT("ok"), CheckDefinition.bOk);
			CheckValues.Add(MakeShared<FJsonValueObject>(CheckObject));
		}
		Root->SetArrayField(TEXT("checks"), CheckValues);

		TSharedRef<FJsonObject> PrimaryActionObject = MakeShared<FJsonObject>();
		PrimaryActionObject->SetStringField(TEXT("label"), TEXT("Start Exercise"));
		PrimaryActionObject->SetStringField(TEXT("target"), TEXT("../ExerciseSelection/exercise_selection.html"));
		Root->SetObjectField(TEXT("primary_action"), PrimaryActionObject);

		TSharedRef<FJsonObject> SecondaryActionObject = MakeShared<FJsonObject>();
		SecondaryActionObject->SetStringField(TEXT("label"), TEXT("Try again"));
		SecondaryActionObject->SetStringField(TEXT("target"), TEXT("camera_check.html"));
		Root->SetObjectField(TEXT("secondary_action"), SecondaryActionObject);

		const FString JsonOutput = SerializeJsonObject(Root);
		const FString JsonPath = UFacialTherapyApi::GetCameraCheckJsonPath();
		const FString ScriptPath = UFacialTherapyApi::GetCameraCheckScriptPath();
		const FString ScriptOutput = FString::Printf(TEXT("window.dashboardData = %s;"), *JsonOutput);
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		PlatformFile.CreateDirectoryTree(*FPaths::GetPath(JsonPath));

		if (!FFileHelper::SaveStringToFile(JsonOutput, *JsonPath))
		{
			Callback.ExecuteIfBound(false, TEXT("Failed to write camera check data file."), FString());
			return;
		}

		if (!FFileHelper::SaveStringToFile(ScriptOutput, *ScriptPath))
		{
			Callback.ExecuteIfBound(false, TEXT("Failed to write camera check script file."), FString());
			return;
		}

		Callback.ExecuteIfBound(true, FString(), JsonPath);
	});
}

void UFacialTherapyApi::GenerateCurrentExerciseSelectionDataNative(TFunction<void(bool, const FString&, const FString&)> Callback)
{
	if (AccessToken.IsEmpty())
	{
		LoadStoredAuth();
	}

	if (AccessToken.IsEmpty())
	{
		Callback(false, TEXT("No stored access token is available. Login first."), FString());
		return;
	}

	struct FExerciseSelectionState
	{
		FFacialTherapyCurrentUserResponse User;
		FFacialTherapyPatientResponse Patient;
		FFacialTherapyProgressOverview Overview;
		FFacialTherapyPatientPhaseProgressResponse CurrentProgress;
		bool bHasPatient = false;
		bool bHasOverview = false;
		bool bHasCurrentProgress = false;
	};

	TSharedRef<FExerciseSelectionState, ESPMode::ThreadSafe> State = MakeShared<FExerciseSelectionState, ESPMode::ThreadSafe>();

	auto FinalizeRoadmap = [State, Callback]() mutable
	{
		const int32 PatientId = State->bHasPatient ? State->Patient.id : UFacialTherapyApi::GetStoredPatientId();
		FString PatientName = State->bHasPatient ? State->Patient.name.TrimStartAndEnd() : State->User.username.TrimStartAndEnd();
		if (PatientName.IsEmpty())
		{
			PatientName = TEXT("Patient");
		}

		const int32 OverviewPhase = State->bHasOverview ? State->Overview.current_phase : 0;
		const int32 ProgressPhase = State->bHasCurrentProgress ? State->CurrentProgress.phase_number : 0;
		const int32 CurrentPhase = FMath::Clamp((OverviewPhase > 0) ? OverviewPhase : FMath::Max(ProgressPhase, 1), 1, 5);

		TSet<int32> CompletedPhases;
		if (State->bHasOverview)
		{
			for (int32 Phase : State->Overview.phases_completed)
			{
				CompletedPhases.Add(Phase);
			}
		}

		TSet<int32> MasteredExercises;
		TSet<int32> MasteredPairs;
		TSet<FString> MasteredSequences;
		if (State->bHasCurrentProgress)
		{
			for (int32 Value : State->CurrentProgress.exercises_mastered)
			{
				MasteredExercises.Add(Value);
			}
			for (int32 Value : State->CurrentProgress.pairs_mastered)
			{
				MasteredPairs.Add(Value);
			}
			for (const FString& Value : State->CurrentProgress.sequences_mastered)
			{
				MasteredSequences.Add(Value.ToUpper());
			}
		}

		struct FRoadmapItem
		{
			int32 PhaseNumber;
			int32 ItemId;
			const TCHAR* Label;
			const TCHAR* Detail;
			int32 ExerciseId;
			int32 PairNumber;
			const TCHAR* SequenceCode;
		};

		static const FRoadmapItem RoadmapItems[] =
		{
			{ 1, 101, TEXT("Fear"), TEXT("Isolated expression practice"), 4, 0, TEXT("") },
			{ 1, 102, TEXT("Smile + Cheeks"), TEXT("Isolated expression practice"), 9, 0, TEXT("") },
			{ 1, 103, TEXT("Sadness"), TEXT("Isolated expression practice"), 6, 0, TEXT("") },
			{ 1, 104, TEXT("Surprised"), TEXT("Isolated expression practice"), 2, 0, TEXT("") },
			{ 1, 105, TEXT("Pout"), TEXT("Isolated expression practice"), 7, 0, TEXT("") },
			{ 1, 106, TEXT("Puff Cheeks"), TEXT("Isolated expression practice"), 8, 0, TEXT("") },
			{ 1, 107, TEXT("Smile"), TEXT("Isolated expression practice"), 5, 0, TEXT("") },
			{ 1, 108, TEXT("Disgust"), TEXT("Isolated expression practice"), 3, 0, TEXT("") },
			{ 1, 109, TEXT("Angry"), TEXT("Isolated expression practice"), 1, 0, TEXT("") },
			{ 2, 201, TEXT("Surprised - Angry"), TEXT("Raise forehead vs furrow forehead"), 0, 1, TEXT("") },
			{ 2, 202, TEXT("Smile - Sadness"), TEXT("Mouth corners up vs down"), 0, 2, TEXT("") },
			{ 2, 203, TEXT("Disgust - Neutral"), TEXT("Activation followed by relaxation"), 0, 3, TEXT("") },
			{ 2, 204, TEXT("Fear - Surprised"), TEXT("Eyes squeezed vs wide"), 0, 4, TEXT("") },
			{ 3, 301, TEXT("Sequence A: Upper Face"), TEXT("Surprised, Angry, then Surprised again"), 0, 0, TEXT("A") },
			{ 3, 302, TEXT("Sequence B: Lower Face"), TEXT("Smile, Sadness, Pout, then Smile again"), 0, 0, TEXT("B") },
			{ 3, 303, TEXT("Sequence C: Whole Face"), TEXT("Fear, Disgust, then Surprised"), 0, 0, TEXT("C") },
			{ 3, 304, TEXT("Sequence D: Special Exercises"), TEXT("Puff Cheeks, then Smile + Cheeks"), 0, 0, TEXT("D") },
			{ 4, 401, TEXT("Mastery: Complex Sequences"), TEXT("Practice two complex sequences from Phase 3"), 0, 0, TEXT("MASTERY_SEQ") },
			{ 4, 402, TEXT("Mastery: Targeted Practice"), TEXT("Practice targeted isolated exercises"), 0, 0, TEXT("MASTERY_TARGET") },
			{ 5, 501, TEXT("Short Maintenance"), TEXT("One sequence plus two isolated exercises"), 0, 0, TEXT("MAINT_SHORT") },
			{ 5, 502, TEXT("Intensive Maintenance"), TEXT("Three sequences plus targeted practice"), 0, 0, TEXT("MAINT_INTENSIVE") }
		};

		struct FPhaseDefinition
		{
			int32 Number;
			const TCHAR* Title;
			const TCHAR* Label;
			const TCHAR* Type;
			const TCHAR* Summary;
			int32 HoldSeconds;
		};

		static const FPhaseDefinition PhaseDefinitions[] =
		{
			{ 1, TEXT("Foundations"), TEXT("Week 1"), TEXT("Single expressions"), TEXT("Build control with isolated expression holds."), 15 },
			{ 2, TEXT("Consolidation"), TEXT("Week 3"), TEXT("Antagonistic pairs"), TEXT("Alternate between opposing expressions with smooth transitions."), 20 },
			{ 3, TEXT("Intensification"), TEXT("Week 5"), TEXT("Complex sequences"), TEXT("Practice multi-expression movement sequences."), 25 },
			{ 4, TEXT("Mastery"), TEXT("Week 7"), TEXT("Advanced mastery"), TEXT("Combine advanced sequences with targeted practice."), 25 },
			{ 5, TEXT("Maintenance"), TEXT("Ongoing"), TEXT("Maintenance"), TEXT("Keep progress active with flexible practice."), 25 }
		};

		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("generated_at"), FDateTime::UtcNow().ToIso8601());
		Root->SetNumberField(TEXT("current_phase"), CurrentPhase);
		Root->SetStringField(TEXT("current_item_id"), FString());
		Root->SetStringField(TEXT("primary_target"), TEXT("../GuidedExercise/guided_exercise.html"));

		TSharedRef<FJsonObject> PatientObject = MakeShared<FJsonObject>();
		PatientObject->SetStringField(TEXT("name"), PatientName);
		PatientObject->SetNumberField(TEXT("patient_id"), PatientId);
		Root->SetObjectField(TEXT("patient"), PatientObject);

		TSharedRef<FJsonObject> TherapistObject = MakeShared<FJsonObject>();
		TherapistObject->SetStringField(TEXT("name"), TEXT("Marie"));
		TherapistObject->SetStringField(TEXT("avatar_url"), TEXT("../Shared/user_avatar.png"));
		TherapistObject->SetStringField(TEXT("initials"), TEXT("M"));
		Root->SetObjectField(TEXT("therapist"), TherapistObject);

		FString CurrentItemId;
		TArray<TSharedPtr<FJsonValue>> PhaseValues;
		for (const FPhaseDefinition& PhaseDefinition : PhaseDefinitions)
		{
			TArray<const FRoadmapItem*> PhaseItems;
			for (const FRoadmapItem& RoadmapItem : RoadmapItems)
			{
				if (RoadmapItem.PhaseNumber == PhaseDefinition.Number)
				{
					PhaseItems.Add(&RoadmapItem);
				}
			}

			FString PhaseStatus = TEXT("hidden");
			if (CompletedPhases.Contains(PhaseDefinition.Number) || PhaseDefinition.Number < CurrentPhase)
			{
				PhaseStatus = TEXT("completed");
			}
			else if (PhaseDefinition.Number == CurrentPhase)
			{
				PhaseStatus = TEXT("current");
			}
			else if (PhaseDefinition.Number == CurrentPhase + 1)
			{
				PhaseStatus = TEXT("locked");
			}

			bool bAssignedCurrentItem = false;
			TArray<TSharedPtr<FJsonValue>> ItemValues;
			for (const FRoadmapItem* RoadmapItem : PhaseItems)
			{
				const FString SequenceCode = FString(RoadmapItem->SequenceCode).ToUpper();
				const bool bItemMastered = (RoadmapItem->ExerciseId > 0 && MasteredExercises.Contains(RoadmapItem->ExerciseId))
					|| (RoadmapItem->PairNumber > 0 && MasteredPairs.Contains(RoadmapItem->PairNumber))
					|| (!SequenceCode.IsEmpty() && MasteredSequences.Contains(SequenceCode));

				FString ItemState = TEXT("locked");
				if (PhaseStatus == TEXT("completed") || bItemMastered)
				{
					ItemState = TEXT("completed");
				}
				else if (PhaseStatus == TEXT("current") && !bAssignedCurrentItem)
				{
					ItemState = TEXT("current");
					bAssignedCurrentItem = true;
					if (CurrentItemId.IsEmpty())
					{
						CurrentItemId = FString::FromInt(RoadmapItem->ItemId);
					}
				}

				TSharedRef<FJsonObject> ItemObject = MakeShared<FJsonObject>();
				ItemObject->SetStringField(TEXT("id"), FString::FromInt(RoadmapItem->ItemId));
				ItemObject->SetStringField(TEXT("label"), RoadmapItem->Label);
				ItemObject->SetStringField(TEXT("detail"), RoadmapItem->Detail);
				ItemObject->SetStringField(TEXT("state"), ItemState);
				ItemObject->SetNumberField(TEXT("exercise_id"), RoadmapItem->ExerciseId);
				ItemObject->SetNumberField(TEXT("pair_number"), RoadmapItem->PairNumber);
				ItemObject->SetStringField(TEXT("sequence_code"), RoadmapItem->SequenceCode);
				ItemValues.Add(MakeShared<FJsonValueObject>(ItemObject));
			}

			TSharedRef<FJsonObject> PhaseObject = MakeShared<FJsonObject>();
			PhaseObject->SetNumberField(TEXT("number"), PhaseDefinition.Number);
			PhaseObject->SetStringField(TEXT("title"), PhaseDefinition.Title);
			PhaseObject->SetStringField(TEXT("label"), PhaseDefinition.Label);
			PhaseObject->SetStringField(TEXT("type"), PhaseDefinition.Type);
			PhaseObject->SetNumberField(TEXT("hold_duration_seconds"), PhaseDefinition.HoldSeconds);
			PhaseObject->SetStringField(TEXT("status"), PhaseStatus);
			PhaseObject->SetStringField(TEXT("summary"), PhaseDefinition.Summary);
			PhaseObject->SetArrayField(TEXT("items"), ItemValues);
			PhaseValues.Add(MakeShared<FJsonValueObject>(PhaseObject));
		}
		Root->SetArrayField(TEXT("phases"), PhaseValues);
		Root->SetStringField(TEXT("current_item_id"), CurrentItemId);

		const FString JsonOutput = SerializeJsonObject(Root);
		const FString JsonPath = UFacialTherapyApi::GetExerciseSelectionJsonPath();
		const FString ScriptPath = UFacialTherapyApi::GetExerciseSelectionScriptPath();
		const FString ScriptOutput = FString::Printf(TEXT("window.dashboardData = %s;"), *JsonOutput);
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		PlatformFile.CreateDirectoryTree(*FPaths::GetPath(JsonPath));

		if (!FFileHelper::SaveStringToFile(JsonOutput, *JsonPath))
		{
			Callback(false, TEXT("Failed to write exercise selection data file."), FString());
			return;
		}

		if (!FFileHelper::SaveStringToFile(ScriptOutput, *ScriptPath))
		{
			Callback(false, TEXT("Failed to write exercise selection script file."), FString());
			return;
		}

		Callback(true, FString(), JsonPath);
	};

	UFacialTherapyApi::GetCurrentUserNative([State, Callback, FinalizeRoadmap](bool bSuccess, const FString& ErrorMessage, const FFacialTherapyCurrentUserResponse& Response) mutable
	{
		if (!bSuccess)
		{
			Callback(false, ErrorMessage, FString());
			return;
		}

		State->User = Response;
		const int32 PatientId = (Response.patient_id > 0) ? Response.patient_id : UFacialTherapyApi::GetStoredPatientId();
		if (PatientId <= 0)
		{
			Callback(false, TEXT("No stored patient id is available. Login first."), FString());
			return;
		}

		UFacialTherapyApi::GetPatientProfileNative(PatientId, [State, Callback, FinalizeRoadmap, PatientId](bool bPatientSuccess, const FString&, const FFacialTherapyPatientResponse& PatientResponse) mutable
		{
			if (bPatientSuccess)
			{
				State->Patient = PatientResponse;
				State->bHasPatient = true;
			}

			UFacialTherapyApi::GetProgressOverviewNative(PatientId, [State, Callback, FinalizeRoadmap, PatientId](bool bOverviewSuccess, const FString& OverviewError, const FFacialTherapyProgressOverview& OverviewResponse) mutable
			{
				if (!bOverviewSuccess)
				{
					Callback(false, OverviewError, FString());
					return;
				}

				State->Overview = OverviewResponse;
				State->bHasOverview = true;

				UFacialTherapyApi::GetCurrentProgressNative(PatientId, [State, Callback, FinalizeRoadmap](bool bProgressSuccess, const FString& ProgressError, const FFacialTherapyPatientPhaseProgressResponse& ProgressResponse) mutable
				{
					if (!bProgressSuccess)
					{
						Callback(false, ProgressError, FString());
						return;
					}

					State->CurrentProgress = ProgressResponse;
					State->bHasCurrentProgress = true;
					FinalizeRoadmap();
				});
			});
		});
	});
}

void UFacialTherapyApi::GenerateCurrentExerciseSelectionData(const FFacialTherapyDashboardCallback& Callback)
{
	GenerateCurrentExerciseSelectionDataNative([Callback](bool bSuccess, const FString& ErrorMessage, const FString& JsonFilePath)
	{
		Callback.ExecuteIfBound(bSuccess, ErrorMessage, JsonFilePath);
	});
}

UFacialTherapyWebBridge* UFacialTherapyApi::GetWebBridge()
{
	if (!WebBridgeInstance)
	{
		WebBridgeInstance = NewObject<UFacialTherapyWebBridge>(GetTransientPackage());
		WebBridgeInstance->AddToRoot();
	}

	return WebBridgeInstance;
}

bool UFacialTherapyApi::BindWebBridge(UWebBrowser* WebBrowser)
{
	if (!WebBrowser)
	{
		return false;
	}

	const TSharedPtr<SWidget> CachedWidget = WebBrowser->GetCachedWidget();
	const TSharedPtr<SWebBrowser> SlateBrowserWidget = StaticCastSharedPtr<SWebBrowser>(CachedWidget);
	if (!SlateBrowserWidget.IsValid())
	{
		return false;
	}

	UFacialTherapyWebBridge* Bridge = GetWebBridge();
	Bridge->SetBoundWebBrowser(WebBrowser);
	SlateBrowserWidget->UnbindUObject(TEXT("UEInterface"), Bridge, true);
	SlateBrowserWidget->BindUObject(TEXT("UEInterface"), Bridge, true);
	return true;
}

void UFacialTherapyApi::GenerateCurrentPhaseProgressData(const FFacialTherapyDashboardCallback& Callback)
{
	if (AccessToken.IsEmpty())
	{
		LoadStoredAuth();
	}

	if (AccessToken.IsEmpty())
	{
		Callback.ExecuteIfBound(false, TEXT("No stored access token is available. Login first."), FString());
		return;
	}

	struct FPhaseProgressState
	{
		FFacialTherapyCurrentUserResponse User;
		FFacialTherapyPatientResponse Patient;
		FFacialTherapyProgressOverview Overview;
		FFacialTherapyPatientPhaseProgressResponse CurrentProgress;
		FFacialTherapyTherapySessionListResponse TherapyHistory;
		FFacialTherapyStreakSummary Streak;
		FFacialTherapySchedulePreferenceSummary ScheduleSummary;
		TArray<FString> Warnings;
		bool bHasUser = false;
		bool bHasPatient = false;
		bool bHasOverview = false;
		bool bHasCurrentProgress = false;
		bool bHasTherapyHistory = false;
		bool bHasStreak = false;
		bool bHasScheduleSummary = false;
		bool bHasPatientContext = false;
	};

	TSharedRef<FPhaseProgressState, ESPMode::ThreadSafe> State = MakeShared<FPhaseProgressState, ESPMode::ThreadSafe>();

	auto FinalizeDashboard = [State, Callback]()
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("generated_at"), FDateTime::UtcNow().ToIso8601());
		Root->SetStringField(TEXT("dashboard_type"), TEXT("phase_progress"));
		Root->SetBoolField(TEXT("has_patient_context"), State->bHasPatientContext);

		TSharedRef<FJsonObject> UserObject = MakeShared<FJsonObject>();
		UserObject->SetNumberField(TEXT("user_id"), State->User.user_id);
		UserObject->SetStringField(TEXT("username"), State->User.username);
		UserObject->SetStringField(TEXT("role"), State->User.role);
		UserObject->SetNumberField(TEXT("patient_id"), State->User.patient_id);
		Root->SetObjectField(TEXT("user"), UserObject);

		TSharedRef<FJsonObject> PatientObject = MakeShared<FJsonObject>();
		if (State->bHasPatient)
		{
			PatientObject->SetNumberField(TEXT("id"), State->Patient.id);
			PatientObject->SetNumberField(TEXT("user_id"), State->Patient.user_id);
			PatientObject->SetStringField(TEXT("name"), State->Patient.name);
			PatientObject->SetStringField(TEXT("birthdate"), State->Patient.birthdate);
			PatientObject->SetNumberField(TEXT("age"), State->Patient.age);
			PatientObject->SetStringField(TEXT("illness"), State->Patient.illness);
			PatientObject->SetStringField(TEXT("limitations"), State->Patient.limitations);
			PatientObject->SetNumberField(TEXT("avatar_id"), State->Patient.avatar_id);
			PatientObject->SetStringField(TEXT("email"), State->Patient.email);
			PatientObject->SetStringField(TEXT("created_at"), State->Patient.created_at);
		}
		Root->SetObjectField(TEXT("patient"), PatientObject);

		TSharedRef<FJsonObject> OverviewObject = MakeShared<FJsonObject>();
		if (State->bHasOverview)
		{
			OverviewObject->SetNumberField(TEXT("patient_id"), State->Overview.patient_id);
			OverviewObject->SetStringField(TEXT("patient_name"), State->Overview.patient_name);
			OverviewObject->SetNumberField(TEXT("therapy_program_id"), State->Overview.therapy_program_id);
			OverviewObject->SetStringField(TEXT("enrolled_since"), State->Overview.enrolled_since);
			OverviewObject->SetNumberField(TEXT("current_phase"), State->Overview.current_phase);
			OverviewObject->SetStringField(TEXT("phase_name"), State->Overview.phase_name);
			OverviewObject->SetNumberField(TEXT("current_week"), State->Overview.current_week);
			OverviewObject->SetBoolField(TEXT("is_active"), State->Overview.is_active);
			OverviewObject->SetNumberField(TEXT("total_sessions_completed"), State->Overview.total_sessions_completed);
			OverviewObject->SetNumberField(TEXT("sessions_this_week"), State->Overview.sessions_this_week);
			OverviewObject->SetNumberField(TEXT("overall_success_rate"), State->Overview.overall_success_rate);
			OverviewObject->SetNumberField(TEXT("overall_accuracy"), State->Overview.overall_accuracy);
			OverviewObject->SetStringField(TEXT("last_session_date"), State->Overview.last_session_date);
			OverviewObject->SetBoolField(TEXT("needs_attention"), State->Overview.needs_attention);
			OverviewObject->SetStringField(TEXT("attention_reason"), State->Overview.attention_reason);
			TArray<TSharedPtr<FJsonValue>> CompletedPhases;
			for (int32 Phase : State->Overview.phases_completed)
			{
				CompletedPhases.Add(MakeShared<FJsonValueNumber>(Phase));
			}
			OverviewObject->SetArrayField(TEXT("phases_completed"), CompletedPhases);
		}
		Root->SetObjectField(TEXT("overview"), OverviewObject);

		TSharedRef<FJsonObject> CurrentProgressObject = MakeShared<FJsonObject>();
		if (State->bHasCurrentProgress)
		{
			CurrentProgressObject->SetNumberField(TEXT("id"), State->CurrentProgress.id);
			CurrentProgressObject->SetNumberField(TEXT("phase_number"), State->CurrentProgress.phase_number);
			TArray<TSharedPtr<FJsonValue>> ExercisesMastered;
			for (int32 Value : State->CurrentProgress.exercises_mastered)
			{
				ExercisesMastered.Add(MakeShared<FJsonValueNumber>(Value));
			}
			CurrentProgressObject->SetArrayField(TEXT("exercises_mastered"), ExercisesMastered);
			TArray<TSharedPtr<FJsonValue>> PairsMastered;
			for (int32 Value : State->CurrentProgress.pairs_mastered)
			{
				PairsMastered.Add(MakeShared<FJsonValueNumber>(Value));
			}
			CurrentProgressObject->SetArrayField(TEXT("pairs_mastered"), PairsMastered);
			TArray<TSharedPtr<FJsonValue>> SequencesMastered;
			for (const FString& Value : State->CurrentProgress.sequences_mastered)
			{
				SequencesMastered.Add(MakeShared<FJsonValueString>(Value));
			}
			CurrentProgressObject->SetArrayField(TEXT("sequences_mastered"), SequencesMastered);
			CurrentProgressObject->SetNumberField(TEXT("sessions_this_week"), State->CurrentProgress.sessions_this_week);
			CurrentProgressObject->SetNumberField(TEXT("total_sessions"), State->CurrentProgress.total_sessions);
			CurrentProgressObject->SetStringField(TEXT("phase_started_at"), State->CurrentProgress.phase_started_at);
			CurrentProgressObject->SetStringField(TEXT("phase_completed_at"), State->CurrentProgress.phase_completed_at);
			CurrentProgressObject->SetBoolField(TEXT("mastery_achieved"), State->CurrentProgress.mastery_achieved);
		}
		Root->SetObjectField(TEXT("current_progress"), CurrentProgressObject);

		TSharedRef<FJsonObject> StreakObject = MakeShared<FJsonObject>();
		if (State->bHasStreak)
		{
			StreakObject->SetNumberField(TEXT("current_streak"), State->Streak.current_streak);
			StreakObject->SetNumberField(TEXT("longest_streak"), State->Streak.longest_streak);
			StreakObject->SetBoolField(TEXT("is_active_today"), State->Streak.is_active_today);
			StreakObject->SetNumberField(TEXT("days_since_last_session"), State->Streak.days_since_last_session);
		}
		Root->SetObjectField(TEXT("streak"), StreakObject);

		TSharedRef<FJsonObject> ScheduleSummaryObject = MakeShared<FJsonObject>();
		if (State->bHasScheduleSummary)
		{
			ScheduleSummaryObject->SetBoolField(TEXT("is_configured"), State->ScheduleSummary.is_configured);
			ScheduleSummaryObject->SetBoolField(TEXT("reminder_enabled"), State->ScheduleSummary.reminder_enabled);
			TArray<TSharedPtr<FJsonValue>> RestDayNames;
			for (const FString& RestDayName : State->ScheduleSummary.rest_days_names)
			{
				RestDayNames.Add(MakeShared<FJsonValueString>(RestDayName));
			}
			ScheduleSummaryObject->SetArrayField(TEXT("rest_days_names"), RestDayNames);
			ScheduleSummaryObject->SetStringField(TEXT("preferred_time_formatted"), State->ScheduleSummary.preferred_time_formatted);
		}
		Root->SetObjectField(TEXT("schedule_summary"), ScheduleSummaryObject);

		TArray<TSharedPtr<FJsonValue>> TherapyArray;
		if (State->bHasTherapyHistory)
		{
			for (const FFacialTherapyTherapySessionResponse& Session : State->TherapyHistory.items)
			{
				TSharedRef<FJsonObject> SessionObject = MakeShared<FJsonObject>();
				SessionObject->SetNumberField(TEXT("id"), Session.id);
				SessionObject->SetNumberField(TEXT("phase_number"), Session.phase_number);
				SessionObject->SetStringField(TEXT("session_type"), Session.session_type);
				SessionObject->SetStringField(TEXT("exercise_type"), Session.exercise_type);
				SessionObject->SetStringField(TEXT("generated_at"), Session.generated_at);
				SessionObject->SetStringField(TEXT("started_at"), Session.started_at);
				SessionObject->SetStringField(TEXT("completed_at"), Session.completed_at);
				SessionObject->SetNumberField(TEXT("overall_accuracy"), Session.overall_accuracy);
				SessionObject->SetNumberField(TEXT("exercises_completed"), Session.exercises_completed);
				SessionObject->SetNumberField(TEXT("exercises_total"), Session.exercises_total);
				TherapyArray.Add(MakeShared<FJsonValueObject>(SessionObject));
			}
		}
		Root->SetArrayField(TEXT("therapy_history"), TherapyArray);

		if (State->Warnings.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> WarningValues;
			for (const FString& Warning : State->Warnings)
			{
				WarningValues.Add(MakeShared<FJsonValueString>(Warning));
			}
			Root->SetArrayField(TEXT("warnings"), WarningValues);
		}

		const FString JsonOutput = SerializeJsonObject(Root);
		const FString JsonPath = UFacialTherapyApi::GetPhaseProgressJsonPath();
		const FString ScriptPath = UFacialTherapyApi::GetPhaseProgressScriptPath();
		const FString ScriptOutput = FString::Printf(TEXT("window.dashboardData = %s;"), *JsonOutput);
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		PlatformFile.CreateDirectoryTree(*FPaths::GetPath(JsonPath));

		if (!FFileHelper::SaveStringToFile(JsonOutput, *JsonPath))
		{
			Callback.ExecuteIfBound(false, TEXT("Failed to write phase progress data file."), FString());
			return;
		}

		if (!FFileHelper::SaveStringToFile(ScriptOutput, *ScriptPath))
		{
			Callback.ExecuteIfBound(false, TEXT("Failed to write phase progress script file."), FString());
			return;
		}

		Callback.ExecuteIfBound(true, FString(), JsonPath);
	};

	UFacialTherapyApi::GetCurrentUserNative([State, Callback, FinalizeDashboard](bool bSuccess, const FString& ErrorMessage, const FFacialTherapyCurrentUserResponse& Response) mutable
	{
		if (!bSuccess)
		{
			Callback.ExecuteIfBound(false, ErrorMessage, FString());
			return;
		}

		State->User = Response;
		State->bHasUser = true;

		const int32 PatientId = (Response.patient_id > 0) ? Response.patient_id : UFacialTherapyApi::GetStoredPatientId();
		if (PatientId <= 0)
		{
			Callback.ExecuteIfBound(false, TEXT("No stored patient id is available. Login first."), FString());
			return;
		}

		State->bHasPatientContext = true;

		UFacialTherapyApi::GetPatientProfileNative(PatientId, [State, Callback, FinalizeDashboard, PatientId](bool bPatientSuccess, const FString& PatientError, const FFacialTherapyPatientResponse& PatientResponse) mutable
		{
			if (bPatientSuccess)
			{
				State->Patient = PatientResponse;
				State->bHasPatient = true;
			}
			else if (!PatientError.IsEmpty())
			{
				State->Warnings.Add(FString::Printf(TEXT("Patient profile: %s"), *PatientError));
			}

			UFacialTherapyApi::GetProgressOverviewNative(PatientId, [State, Callback, FinalizeDashboard, PatientId](bool bOverviewSuccess, const FString& OverviewError, const FFacialTherapyProgressOverview& OverviewResponse) mutable
			{
				if (!bOverviewSuccess)
				{
					Callback.ExecuteIfBound(false, OverviewError, FString());
					return;
				}

				State->Overview = OverviewResponse;
				State->bHasOverview = true;

				UFacialTherapyApi::GetCurrentProgressNative(PatientId, [State, Callback, FinalizeDashboard, PatientId](bool bCurrentProgressSuccess, const FString& CurrentProgressError, const FFacialTherapyPatientPhaseProgressResponse& CurrentProgressResponse) mutable
				{
					if (!bCurrentProgressSuccess)
					{
						Callback.ExecuteIfBound(false, CurrentProgressError, FString());
						return;
					}

					State->CurrentProgress = CurrentProgressResponse;
					State->bHasCurrentProgress = true;

					UFacialTherapyApi::GetTherapySessionHistoryNative(PatientId, 100, [State, Callback, FinalizeDashboard, PatientId](bool bHistorySuccess, const FString& HistoryError, const FFacialTherapyTherapySessionListResponse& HistoryResponse) mutable
					{
						if (!bHistorySuccess)
						{
							Callback.ExecuteIfBound(false, HistoryError, FString());
							return;
						}

						State->TherapyHistory = HistoryResponse;
						State->bHasTherapyHistory = true;

						UFacialTherapyApi::GetStreakSummaryNative(PatientId, [State, Callback, FinalizeDashboard, PatientId](bool bStreakSuccess, const FString& StreakError, const FFacialTherapyStreakSummary& StreakResponse) mutable
						{
							if (bStreakSuccess)
							{
								State->Streak = StreakResponse;
								State->bHasStreak = true;
							}
							else if (!StreakError.IsEmpty())
							{
								State->Warnings.Add(FString::Printf(TEXT("Streak summary: %s"), *StreakError));
							}

							UFacialTherapyApi::GetScheduleSummaryNative(PatientId, [State, FinalizeDashboard](bool bScheduleSuccess, const FString& ScheduleError, const FFacialTherapySchedulePreferenceSummary& ScheduleResponse) mutable
							{
								if (bScheduleSuccess)
								{
									State->ScheduleSummary = ScheduleResponse;
									State->bHasScheduleSummary = true;
								}
								else if (!ScheduleError.IsEmpty())
								{
									State->Warnings.Add(FString::Printf(TEXT("Schedule summary: %s"), *ScheduleError));
								}

								FinalizeDashboard();
							});
						});
					});
				});
			});
		});
	});
}

void UFacialTherapyApi::GenerateCurrentPatientDashboardData(const FFacialTherapyDashboardCallback& Callback)
{
	if (StoredPatientId <= 0)
	{
		LoadStoredAuth();
	}

	if (StoredPatientId <= 0)
	{
		Callback.ExecuteIfBound(false, TEXT("No stored patient id is available. Login first."), FString());
		return;
	}

	if (AccessToken.IsEmpty())
	{
		LoadStoredAuth();
	}

	if (AccessToken.IsEmpty())
	{
		Callback.ExecuteIfBound(false, TEXT("No stored access token is available. Login first."), FString());
		return;
	}

	struct FDashboardState
	{
		FFacialTherapyUEPatientResponse Patient;
		FFacialTherapyProgressOverview Overview;
		FFacialTherapySessionListResponse Sessions;
		FFacialTherapyFrequencyHistoryResponse Frequency;
		FFacialTherapyStreakSummary Streak;
		FFacialTherapyTherapySessionListResponse TherapyHistory;
	};

	TSharedRef<FDashboardState, ESPMode::ThreadSafe> State = MakeShared<FDashboardState, ESPMode::ThreadSafe>();
	const int32 PatientId = StoredPatientId;

	GetCurrentPatientForUENative([State, Callback, PatientId](bool bPatientSuccess, const FString& PatientError, const FFacialTherapyUEPatientResponse& PatientResponse)
	{
		if (!bPatientSuccess)
		{
			Callback.ExecuteIfBound(false, PatientError, FString());
			return;
		}

			State->Patient = PatientResponse;

		UFacialTherapyApi::GetProgressOverviewNative(PatientId, [State, Callback, PatientId](bool bOverviewSuccess, const FString& OverviewError, const FFacialTherapyProgressOverview& OverviewResponse)
		{
			if (!bOverviewSuccess)
			{
				Callback.ExecuteIfBound(false, OverviewError, FString());
				return;
			}

			State->Overview = OverviewResponse;

			UFacialTherapyApi::GetPatientSessionsNative(PatientId, 100, [State, Callback, PatientId](bool bSessionsSuccess, const FString& SessionsError, const FFacialTherapySessionListResponse& SessionsResponse)
			{
				if (!bSessionsSuccess)
				{
					Callback.ExecuteIfBound(false, SessionsError, FString());
					return;
				}

				State->Sessions = SessionsResponse;

				UFacialTherapyApi::GetFrequencyHistoryNative(PatientId, 12, [State, Callback, PatientId](bool bFrequencySuccess, const FString& FrequencyError, const FFacialTherapyFrequencyHistoryResponse& FrequencyResponse)
				{
					if (!bFrequencySuccess)
					{
						Callback.ExecuteIfBound(false, FrequencyError, FString());
						return;
					}

					State->Frequency = FrequencyResponse;

					UFacialTherapyApi::GetStreakSummaryNative(PatientId, [State, Callback, PatientId](bool bStreakSuccess, const FString& StreakError, const FFacialTherapyStreakSummary& StreakResponse)
					{
						if (!bStreakSuccess)
						{
							Callback.ExecuteIfBound(false, StreakError, FString());
							return;
						}

						State->Streak = StreakResponse;

						UFacialTherapyApi::GetTherapySessionHistoryNative(PatientId, 12, [State, Callback](bool bHistorySuccess, const FString& HistoryError, const FFacialTherapyTherapySessionListResponse& HistoryResponse)
						{
							if (!bHistorySuccess)
							{
								Callback.ExecuteIfBound(false, HistoryError, FString());
								return;
							}

							State->TherapyHistory = HistoryResponse;

							TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
							Root->SetStringField(TEXT("generated_at"), FDateTime::UtcNow().ToIso8601());

							TSharedRef<FJsonObject> PatientObject = MakeShared<FJsonObject>();
							PatientObject->SetNumberField(TEXT("patient_id"), State->Patient.patient_id);
							PatientObject->SetStringField(TEXT("name"), State->Patient.name);
							PatientObject->SetNumberField(TEXT("avatar_id"), State->Patient.avatar_id);
							PatientObject->SetStringField(TEXT("illness"), State->Patient.illness);
							PatientObject->SetStringField(TEXT("limitations"), State->Patient.limitations);
							Root->SetObjectField(TEXT("patient"), PatientObject);

							TSharedRef<FJsonObject> OverviewObject = MakeShared<FJsonObject>();
							OverviewObject->SetNumberField(TEXT("patient_id"), State->Overview.patient_id);
							OverviewObject->SetStringField(TEXT("patient_name"), State->Overview.patient_name);
							OverviewObject->SetNumberField(TEXT("therapy_program_id"), State->Overview.therapy_program_id);
							OverviewObject->SetStringField(TEXT("enrolled_since"), State->Overview.enrolled_since);
							OverviewObject->SetNumberField(TEXT("current_phase"), State->Overview.current_phase);
							OverviewObject->SetStringField(TEXT("phase_name"), State->Overview.phase_name);
							OverviewObject->SetNumberField(TEXT("current_week"), State->Overview.current_week);
							OverviewObject->SetBoolField(TEXT("is_active"), State->Overview.is_active);
							OverviewObject->SetNumberField(TEXT("total_sessions_completed"), State->Overview.total_sessions_completed);
							OverviewObject->SetNumberField(TEXT("sessions_this_week"), State->Overview.sessions_this_week);
							OverviewObject->SetNumberField(TEXT("overall_success_rate"), State->Overview.overall_success_rate);
							OverviewObject->SetNumberField(TEXT("overall_accuracy"), State->Overview.overall_accuracy);
							OverviewObject->SetStringField(TEXT("last_session_date"), State->Overview.last_session_date);
							OverviewObject->SetBoolField(TEXT("needs_attention"), State->Overview.needs_attention);
							OverviewObject->SetStringField(TEXT("attention_reason"), State->Overview.attention_reason);
							TArray<TSharedPtr<FJsonValue>> CompletedPhases;
							for (int32 Phase : State->Overview.phases_completed)
							{
								CompletedPhases.Add(MakeShared<FJsonValueNumber>(Phase));
							}
							OverviewObject->SetArrayField(TEXT("phases_completed"), CompletedPhases);
							Root->SetObjectField(TEXT("overview"), OverviewObject);

							TSharedRef<FJsonObject> StreakObject = MakeShared<FJsonObject>();
							StreakObject->SetNumberField(TEXT("current_streak"), State->Streak.current_streak);
							StreakObject->SetNumberField(TEXT("longest_streak"), State->Streak.longest_streak);
							StreakObject->SetBoolField(TEXT("is_active_today"), State->Streak.is_active_today);
							StreakObject->SetNumberField(TEXT("days_since_last_session"), State->Streak.days_since_last_session);
							Root->SetObjectField(TEXT("streak"), StreakObject);

							TArray<TSharedPtr<FJsonValue>> FrequencyArray;
							for (const FFacialTherapyWeeklyFrequencyData& Week : State->Frequency.weeks)
							{
								TSharedRef<FJsonObject> WeekObject = MakeShared<FJsonObject>();
								WeekObject->SetStringField(TEXT("week_start"), Week.week_start);
								WeekObject->SetNumberField(TEXT("week_number"), Week.week_number);
								WeekObject->SetNumberField(TEXT("session_count"), Week.session_count);
								WeekObject->SetNumberField(TEXT("avg_accuracy"), Week.avg_accuracy);
								FrequencyArray.Add(MakeShared<FJsonValueObject>(WeekObject));
							}
							TSharedRef<FJsonObject> FrequencyObject = MakeShared<FJsonObject>();
							FrequencyObject->SetNumberField(TEXT("patient_id"), State->Frequency.patient_id);
							FrequencyObject->SetNumberField(TEXT("total_sessions"), State->Frequency.total_sessions);
							FrequencyObject->SetNumberField(TEXT("average_weekly_sessions"), State->Frequency.average_weekly_sessions);
							FrequencyObject->SetArrayField(TEXT("weeks"), FrequencyArray);
							Root->SetObjectField(TEXT("frequency"), FrequencyObject);

							TArray<TSharedPtr<FJsonValue>> SessionsArray;
							for (const FFacialTherapySessionResponse& Session : State->Sessions.items)
							{
								TSharedRef<FJsonObject> SessionObject = MakeShared<FJsonObject>();
								SessionObject->SetNumberField(TEXT("id"), Session.id);
								SessionObject->SetStringField(TEXT("exercise_name"), Session.exercise_name);
								SessionObject->SetNumberField(TEXT("accuracy_score"), Session.accuracy_score);
								SessionObject->SetStringField(TEXT("mode"), Session.mode);
								SessionObject->SetStringField(TEXT("started_at"), Session.started_at);
								SessionObject->SetStringField(TEXT("completed_at"), Session.completed_at);
								SessionsArray.Add(MakeShared<FJsonValueObject>(SessionObject));
							}
							Root->SetArrayField(TEXT("sessions"), SessionsArray);

							TArray<TSharedPtr<FJsonValue>> TherapyArray;
							for (const FFacialTherapyTherapySessionResponse& Session : State->TherapyHistory.items)
							{
								TSharedRef<FJsonObject> SessionObject = MakeShared<FJsonObject>();
								SessionObject->SetNumberField(TEXT("id"), Session.id);
								SessionObject->SetNumberField(TEXT("phase_number"), Session.phase_number);
								SessionObject->SetStringField(TEXT("session_type"), Session.session_type);
								SessionObject->SetStringField(TEXT("exercise_type"), Session.exercise_type);
								SessionObject->SetStringField(TEXT("generated_at"), Session.generated_at);
								SessionObject->SetStringField(TEXT("started_at"), Session.started_at);
								SessionObject->SetStringField(TEXT("completed_at"), Session.completed_at);
								SessionObject->SetNumberField(TEXT("overall_accuracy"), Session.overall_accuracy);
								SessionObject->SetNumberField(TEXT("exercises_completed"), Session.exercises_completed);
								SessionObject->SetNumberField(TEXT("exercises_total"), Session.exercises_total);
								TherapyArray.Add(MakeShared<FJsonValueObject>(SessionObject));
							}
							Root->SetArrayField(TEXT("therapy_history"), TherapyArray);

							const FString JsonOutput = SerializeJsonObject(Root);
							const FString JsonPath = UFacialTherapyApi::GetDashboardJsonPath();
							const FString ScriptPath = UFacialTherapyApi::GetDashboardScriptPath();
							const FString ScriptOutput = FString::Printf(TEXT("window.dashboardData = %s;"), *JsonOutput);
							IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
							PlatformFile.CreateDirectoryTree(*FPaths::GetPath(JsonPath));

							if (!FFileHelper::SaveStringToFile(JsonOutput, *JsonPath))
							{
								Callback.ExecuteIfBound(false, TEXT("Failed to write dashboard data file."), FString());
								return;
							}

							if (!FFileHelper::SaveStringToFile(ScriptOutput, *ScriptPath))
							{
								Callback.ExecuteIfBound(false, TEXT("Failed to write dashboard script file."), FString());
								return;
							}

							Callback.ExecuteIfBound(true, FString(), JsonPath);
						});
					});
				});
			});
		});
	});
}

void UFacialTherapyApi::GenerateCurrentUserDashboardData(const FFacialTherapyDashboardCallback& Callback)
{
	if (AccessToken.IsEmpty())
	{
		LoadStoredAuth();
	}

	if (AccessToken.IsEmpty())
	{
		Callback.ExecuteIfBound(false, TEXT("No stored access token is available. Login first."), FString());
		return;
	}

	struct FUserDashboardState
	{
		FFacialTherapyCurrentUserResponse User;
		FFacialTherapyPatientResponse Patient;
		FFacialTherapyProgressOverview Overview;
		FFacialTherapySessionListResponse Sessions;
		FFacialTherapyFrequencyHistoryResponse Frequency;
		FFacialTherapyStreakSummary Streak;
		FFacialTherapySchedulePreferenceSummary ScheduleSummary;
		TArray<FString> Warnings;
		bool bHasUser = false;
		bool bHasPatient = false;
		bool bHasOverview = false;
		bool bHasSessions = false;
		bool bHasFrequency = false;
		bool bHasStreak = false;
		bool bHasScheduleSummary = false;
		bool bHasPatientContext = false;
	};

	TSharedRef<FUserDashboardState, ESPMode::ThreadSafe> State = MakeShared<FUserDashboardState, ESPMode::ThreadSafe>();

	auto FinalizeDashboard = [State, Callback]()
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("generated_at"), FDateTime::UtcNow().ToIso8601());
		Root->SetStringField(TEXT("dashboard_type"), TEXT("user"));
		Root->SetBoolField(TEXT("has_patient_context"), State->bHasPatientContext);

		TSharedRef<FJsonObject> UserObject = MakeShared<FJsonObject>();
		UserObject->SetNumberField(TEXT("user_id"), State->User.user_id);
		UserObject->SetStringField(TEXT("username"), State->User.username);
		UserObject->SetStringField(TEXT("role"), State->User.role);
		UserObject->SetNumberField(TEXT("patient_id"), State->User.patient_id);
		Root->SetObjectField(TEXT("user"), UserObject);

		TSharedRef<FJsonObject> PatientObject = MakeShared<FJsonObject>();
		if (State->bHasPatient)
		{
			PatientObject->SetNumberField(TEXT("id"), State->Patient.id);
			PatientObject->SetNumberField(TEXT("user_id"), State->Patient.user_id);
			PatientObject->SetStringField(TEXT("name"), State->Patient.name);
			PatientObject->SetStringField(TEXT("birthdate"), State->Patient.birthdate);
			PatientObject->SetNumberField(TEXT("age"), State->Patient.age);
			PatientObject->SetStringField(TEXT("illness"), State->Patient.illness);
			PatientObject->SetStringField(TEXT("limitations"), State->Patient.limitations);
			PatientObject->SetNumberField(TEXT("avatar_id"), State->Patient.avatar_id);
			PatientObject->SetStringField(TEXT("email"), State->Patient.email);
			PatientObject->SetStringField(TEXT("created_at"), State->Patient.created_at);
		}
		Root->SetObjectField(TEXT("patient"), PatientObject);

		TSharedRef<FJsonObject> OverviewObject = MakeShared<FJsonObject>();
		if (State->bHasOverview)
		{
			OverviewObject->SetNumberField(TEXT("patient_id"), State->Overview.patient_id);
			OverviewObject->SetStringField(TEXT("patient_name"), State->Overview.patient_name);
			OverviewObject->SetNumberField(TEXT("therapy_program_id"), State->Overview.therapy_program_id);
			OverviewObject->SetStringField(TEXT("enrolled_since"), State->Overview.enrolled_since);
			OverviewObject->SetNumberField(TEXT("current_phase"), State->Overview.current_phase);
			OverviewObject->SetStringField(TEXT("phase_name"), State->Overview.phase_name);
			OverviewObject->SetNumberField(TEXT("current_week"), State->Overview.current_week);
			OverviewObject->SetBoolField(TEXT("is_active"), State->Overview.is_active);
			OverviewObject->SetNumberField(TEXT("total_sessions_completed"), State->Overview.total_sessions_completed);
			OverviewObject->SetNumberField(TEXT("sessions_this_week"), State->Overview.sessions_this_week);
			OverviewObject->SetNumberField(TEXT("overall_success_rate"), State->Overview.overall_success_rate);
			OverviewObject->SetNumberField(TEXT("overall_accuracy"), State->Overview.overall_accuracy);
			OverviewObject->SetStringField(TEXT("last_session_date"), State->Overview.last_session_date);
			OverviewObject->SetBoolField(TEXT("needs_attention"), State->Overview.needs_attention);
			OverviewObject->SetStringField(TEXT("attention_reason"), State->Overview.attention_reason);
			TArray<TSharedPtr<FJsonValue>> CompletedPhases;
			for (int32 Phase : State->Overview.phases_completed)
			{
				CompletedPhases.Add(MakeShared<FJsonValueNumber>(Phase));
			}
			OverviewObject->SetArrayField(TEXT("phases_completed"), CompletedPhases);
		}
		Root->SetObjectField(TEXT("overview"), OverviewObject);

		TSharedRef<FJsonObject> StreakObject = MakeShared<FJsonObject>();
		if (State->bHasStreak)
		{
			StreakObject->SetNumberField(TEXT("current_streak"), State->Streak.current_streak);
			StreakObject->SetNumberField(TEXT("longest_streak"), State->Streak.longest_streak);
			StreakObject->SetBoolField(TEXT("is_active_today"), State->Streak.is_active_today);
			StreakObject->SetNumberField(TEXT("days_since_last_session"), State->Streak.days_since_last_session);
		}
		Root->SetObjectField(TEXT("streak"), StreakObject);

		TSharedRef<FJsonObject> FrequencyObject = MakeShared<FJsonObject>();
		if (State->bHasFrequency)
		{
			TArray<TSharedPtr<FJsonValue>> FrequencyArray;
			for (const FFacialTherapyWeeklyFrequencyData& Week : State->Frequency.weeks)
			{
				TSharedRef<FJsonObject> WeekObject = MakeShared<FJsonObject>();
				WeekObject->SetStringField(TEXT("week_start"), Week.week_start);
				WeekObject->SetNumberField(TEXT("week_number"), Week.week_number);
				WeekObject->SetNumberField(TEXT("session_count"), Week.session_count);
				WeekObject->SetNumberField(TEXT("avg_accuracy"), Week.avg_accuracy);
				FrequencyArray.Add(MakeShared<FJsonValueObject>(WeekObject));
			}

			FrequencyObject->SetNumberField(TEXT("patient_id"), State->Frequency.patient_id);
			FrequencyObject->SetNumberField(TEXT("total_sessions"), State->Frequency.total_sessions);
			FrequencyObject->SetNumberField(TEXT("average_weekly_sessions"), State->Frequency.average_weekly_sessions);
			FrequencyObject->SetArrayField(TEXT("weeks"), FrequencyArray);
		}
		Root->SetObjectField(TEXT("frequency"), FrequencyObject);

		TSharedRef<FJsonObject> ScheduleSummaryObject = MakeShared<FJsonObject>();
		if (State->bHasScheduleSummary)
		{
			ScheduleSummaryObject->SetBoolField(TEXT("is_configured"), State->ScheduleSummary.is_configured);
			ScheduleSummaryObject->SetBoolField(TEXT("reminder_enabled"), State->ScheduleSummary.reminder_enabled);
			TArray<TSharedPtr<FJsonValue>> RestDayNames;
			for (const FString& RestDayName : State->ScheduleSummary.rest_days_names)
			{
				RestDayNames.Add(MakeShared<FJsonValueString>(RestDayName));
			}
			ScheduleSummaryObject->SetArrayField(TEXT("rest_days_names"), RestDayNames);
			ScheduleSummaryObject->SetStringField(TEXT("preferred_time_formatted"), State->ScheduleSummary.preferred_time_formatted);
		}
		Root->SetObjectField(TEXT("schedule_summary"), ScheduleSummaryObject);

		TArray<TSharedPtr<FJsonValue>> SessionsArray;
		if (State->bHasSessions)
		{
			for (const FFacialTherapySessionResponse& Session : State->Sessions.items)
			{
				TSharedRef<FJsonObject> SessionObject = MakeShared<FJsonObject>();
				SessionObject->SetNumberField(TEXT("id"), Session.id);
				SessionObject->SetStringField(TEXT("exercise_name"), Session.exercise_name);
				SessionObject->SetNumberField(TEXT("accuracy_score"), Session.accuracy_score);
				SessionObject->SetStringField(TEXT("mode"), Session.mode);
				SessionObject->SetStringField(TEXT("started_at"), Session.started_at);
				SessionObject->SetStringField(TEXT("completed_at"), Session.completed_at);
				SessionsArray.Add(MakeShared<FJsonValueObject>(SessionObject));
			}
		}
		Root->SetArrayField(TEXT("sessions"), SessionsArray);

		if (State->Warnings.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> WarningValues;
			for (const FString& Warning : State->Warnings)
			{
				WarningValues.Add(MakeShared<FJsonValueString>(Warning));
			}
			Root->SetArrayField(TEXT("warnings"), WarningValues);
		}

		const FString JsonOutput = SerializeJsonObject(Root);
		const FString JsonPath = UFacialTherapyApi::GetUserDashboardJsonPath();
		const FString ScriptPath = UFacialTherapyApi::GetUserDashboardScriptPath();
		const FString ScriptOutput = FString::Printf(TEXT("window.dashboardData = %s;"), *JsonOutput);
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		PlatformFile.CreateDirectoryTree(*FPaths::GetPath(JsonPath));

		if (!FFileHelper::SaveStringToFile(JsonOutput, *JsonPath))
		{
			Callback.ExecuteIfBound(false, TEXT("Failed to write user dashboard data file."), FString());
			return;
		}

		if (!FFileHelper::SaveStringToFile(ScriptOutput, *ScriptPath))
		{
			Callback.ExecuteIfBound(false, TEXT("Failed to write user dashboard script file."), FString());
			return;
		}

		Callback.ExecuteIfBound(true, FString(), JsonPath);
	};

	UFacialTherapyApi::GetCurrentUserNative([State, Callback, FinalizeDashboard](bool bSuccess, const FString& ErrorMessage, const FFacialTherapyCurrentUserResponse& Response) mutable
	{
		if (!bSuccess)
		{
			Callback.ExecuteIfBound(false, ErrorMessage, FString());
			return;
		}

		State->User = Response;
		State->bHasUser = true;

		const int32 PatientId = (Response.patient_id > 0) ? Response.patient_id : UFacialTherapyApi::GetStoredPatientId();
		if (PatientId <= 0)
		{
			FinalizeDashboard();
			return;
		}

		State->bHasPatientContext = true;

		UFacialTherapyApi::GetPatientProfileNative(PatientId, [State, Callback, FinalizeDashboard, PatientId](bool bPatientSuccess, const FString& PatientError, const FFacialTherapyPatientResponse& PatientResponse) mutable
		{
			if (bPatientSuccess)
			{
				State->Patient = PatientResponse;
				State->bHasPatient = true;
			}
			else if (!PatientError.IsEmpty())
			{
				State->Warnings.Add(FString::Printf(TEXT("Patient profile: %s"), *PatientError));
			}

			UFacialTherapyApi::GetProgressOverviewNative(PatientId, [State, FinalizeDashboard, PatientId](bool bOverviewSuccess, const FString& OverviewError, const FFacialTherapyProgressOverview& OverviewResponse) mutable
			{
				if (bOverviewSuccess)
				{
					State->Overview = OverviewResponse;
					State->bHasOverview = true;
				}
				else if (!OverviewError.IsEmpty())
				{
					State->Warnings.Add(FString::Printf(TEXT("Progress overview: %s"), *OverviewError));
				}

				UFacialTherapyApi::GetPatientSessionsNative(PatientId, 100, [State, FinalizeDashboard, PatientId](bool bSessionsSuccess, const FString& SessionsError, const FFacialTherapySessionListResponse& SessionsResponse) mutable
				{
					if (bSessionsSuccess)
					{
						State->Sessions = SessionsResponse;
						State->bHasSessions = true;
					}
					else if (!SessionsError.IsEmpty())
					{
						State->Warnings.Add(FString::Printf(TEXT("Session history: %s"), *SessionsError));
					}

					UFacialTherapyApi::GetFrequencyHistoryNative(PatientId, 12, [State, FinalizeDashboard, PatientId](bool bFrequencySuccess, const FString& FrequencyError, const FFacialTherapyFrequencyHistoryResponse& FrequencyResponse) mutable
					{
						if (bFrequencySuccess)
						{
							State->Frequency = FrequencyResponse;
							State->bHasFrequency = true;
						}
						else if (!FrequencyError.IsEmpty())
						{
							State->Warnings.Add(FString::Printf(TEXT("Frequency history: %s"), *FrequencyError));
						}

						UFacialTherapyApi::GetStreakSummaryNative(PatientId, [State, FinalizeDashboard, PatientId](bool bStreakSuccess, const FString& StreakError, const FFacialTherapyStreakSummary& StreakResponse) mutable
						{
							if (bStreakSuccess)
							{
								State->Streak = StreakResponse;
								State->bHasStreak = true;
							}
							else if (!StreakError.IsEmpty())
							{
								State->Warnings.Add(FString::Printf(TEXT("Streak summary: %s"), *StreakError));
							}

							UFacialTherapyApi::GetScheduleSummaryNative(PatientId, [State, FinalizeDashboard](bool bScheduleSuccess, const FString& ScheduleError, const FFacialTherapySchedulePreferenceSummary& ScheduleResponse) mutable
							{
								if (bScheduleSuccess)
								{
									State->ScheduleSummary = ScheduleResponse;
									State->bHasScheduleSummary = true;
								}
								else if (!ScheduleError.IsEmpty())
								{
									State->Warnings.Add(FString::Printf(TEXT("Schedule summary: %s"), *ScheduleError));
								}

								FinalizeDashboard();
							});
						});
					});
				});
			});
		});
	});
}
