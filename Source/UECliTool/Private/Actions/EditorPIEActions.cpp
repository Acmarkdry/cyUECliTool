// Copyright (c) 2025 zolnoor. All rights reserved.

#include "Actions/EditorActions.h"
#include "MCPCommonUtils.h"
#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Camera/CameraActor.h"
#include "Kismet/GameplayStatics.h"
#include "FileHelpers.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "AssetSelection.h"
#include "ObjectTools.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "EditorAssetLibrary.h"
#include "ImageUtils.h"
#include "Misc/Base64.h"
#include "Misc/PackageName.h"
#include "MCPLogCapture.h"
#include "MCPBridge.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/UnrealType.h"
#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformMisc.h"
#include "Misc/DateTime.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "EngineUtils.h"
#include "Selection.h"



// =========================================================================
// P6: PIE Control Actions
// =========================================================================

// ---- P6.1 FStartPIEAction ----

bool FStartPIEAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!GEditor)
	{
		OutError = TEXT("GEditor is not available");
		return false;
	}
	if (GEditor->PlayWorld)
	{
		OutError = TEXT("A PIE session is already running. Stop it first with editor.stop_pie.");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FStartPIEAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString ModeStr = GetOptionalString(Params, TEXT("mode"), TEXT("SelectedViewport"));

	FRequestPlaySessionParams SessionParams;
	SessionParams.SessionDestination = EPlaySessionDestinationType::InProcess;

	if (ModeStr.Equals(TEXT("Simulate"), ESearchCase::IgnoreCase))
	{
		SessionParams.WorldType = EPlaySessionWorldType::SimulateInEditor;
	}
	else
	{
		SessionParams.WorldType = EPlaySessionWorldType::PlayInEditor;
	}

	// Configure play mode via LevelEditorPlaySettings
	ULevelEditorPlaySettings* PlaySettings = GetMutableDefault<ULevelEditorPlaySettings>();
	if (PlaySettings)
	{
		if (ModeStr.Equals(TEXT("NewWindow"), ESearchCase::IgnoreCase))
		{
			PlaySettings->LastExecutedPlayModeType = PlayMode_InEditorFloating;
		}
		else if (ModeStr.Equals(TEXT("Simulate"), ESearchCase::IgnoreCase))
		{
			PlaySettings->LastExecutedPlayModeType = PlayMode_Simulate;
		}
		else // SelectedViewport (default)
		{
			PlaySettings->LastExecutedPlayModeType = PlayMode_InViewPort;
		}
	}

	GEditor->RequestPlaySession(SessionParams);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("mode"), ModeStr);
	Result->SetStringField(TEXT("message"), TEXT("PIE session requested"));
	Result->SetBoolField(TEXT("is_async"), true);

	UE_LOG(LogMCP, Log, TEXT("PIE start requested (mode=%s)"), *ModeStr);

	return CreateSuccessResponse(Result);
}

// ---- P6.2 FStopPIEAction ----

TSharedPtr<FJsonObject> FStopPIEAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!GEditor || !GEditor->PlayWorld)
	{
		Result->SetStringField(TEXT("state"), TEXT("already_stopped"));
		Result->SetStringField(TEXT("message"), TEXT("No PIE session is currently running"));
		return CreateSuccessResponse(Result);
	}

	GEditor->RequestEndPlayMap();

	Result->SetStringField(TEXT("state"), TEXT("stop_requested"));
	Result->SetStringField(TEXT("message"), TEXT("PIE stop requested"));

	UE_LOG(LogMCP, Log, TEXT("PIE stop requested"));

	return CreateSuccessResponse(Result);
}

// ---- P6.3 FGetPIEStateAction ----

TSharedPtr<FJsonObject> FGetPIEStateAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!GEditor)
	{
		Result->SetStringField(TEXT("state"), TEXT("Stopped"));
		return CreateSuccessResponse(Result);
	}

	if (GEditor->PlayWorld)
	{
		Result->SetStringField(TEXT("state"), TEXT("Running"));
		Result->SetStringField(TEXT("world_name"), GEditor->PlayWorld->GetName());
		Result->SetBoolField(TEXT("is_paused"), GEditor->PlayWorld->IsPaused());
		Result->SetBoolField(TEXT("is_simulating"), GEditor->IsSimulateInEditorInProgress());

		// Duration: use engine uptime difference (best available method)
		double CurrentTime = FPlatformTime::Seconds();
		Result->SetNumberField(TEXT("engine_time"), CurrentTime);

		// Check if a request to end is already queued
		Result->SetBoolField(TEXT("is_play_session_in_progress"), GEditor->IsPlaySessionInProgress());
	}
	else
	{
		Result->SetStringField(TEXT("state"), TEXT("Stopped"));
		Result->SetBoolField(TEXT("is_play_session_in_progress"), GEditor->IsPlaySessionInProgress());
	}

	return CreateSuccessResponse(Result);
}


// =========================================================================
// P6: Log Enhancement Actions
// =========================================================================

#include "MCPLogCapture.h"

// ---- P6.4 FClearLogsAction ----

TSharedPtr<FJsonObject> FClearLogsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FMCPLogCapture& LogCapture = FMCPLogCapture::Get();

	// Get pre-clear stats
	int64 PrevCount = LogCapture.GetTotalCaptured();
	uint64 PrevSeq = LogCapture.GetLatestSeq();

	// Optionally insert a session tag before clearing
	FString Tag = GetOptionalString(Params, TEXT("tag"), TEXT(""));
	if (!Tag.IsEmpty())
	{
		UE_LOG(LogMCP, Log, TEXT("[SESSION] %s"), *Tag);
	}

	// Clear the ring buffer
	LogCapture.Clear();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("cleared_total_captured"), static_cast<double>(PrevCount));
	Result->SetStringField(TEXT("previous_cursor"), FString::Printf(TEXT("live:%llu"), PrevSeq));
	Result->SetStringField(TEXT("new_cursor"), FString::Printf(TEXT("live:%llu"), LogCapture.GetLatestSeq()));
	if (!Tag.IsEmpty())
	{
		Result->SetStringField(TEXT("session_tag"), Tag);
	}
	Result->SetStringField(TEXT("message"), TEXT("Log buffer cleared"));

	UE_LOG(LogMCP, Log, TEXT("Log buffer cleared (prev total=%lld, prev seq=%llu)"), PrevCount, PrevSeq);

	return CreateSuccessResponse(Result);
}

