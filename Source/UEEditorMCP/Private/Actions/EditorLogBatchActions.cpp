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



// ============================================================================
// P2: FGetEditorLogsAction
// ============================================================================

namespace
{
ELogVerbosity::Type ParseMinVerbosity(const FString& VerbosityStr)
{
	if (VerbosityStr.Equals(TEXT("Fatal"), ESearchCase::IgnoreCase)) return ELogVerbosity::Fatal;
	if (VerbosityStr.Equals(TEXT("Error"), ESearchCase::IgnoreCase)) return ELogVerbosity::Error;
	if (VerbosityStr.Equals(TEXT("Warning"), ESearchCase::IgnoreCase)) return ELogVerbosity::Warning;
	if (VerbosityStr.Equals(TEXT("Display"), ESearchCase::IgnoreCase)) return ELogVerbosity::Display;
	if (VerbosityStr.Equals(TEXT("Log"), ESearchCase::IgnoreCase)) return ELogVerbosity::Log;
	if (VerbosityStr.Equals(TEXT("Verbose"), ESearchCase::IgnoreCase)) return ELogVerbosity::Verbose;
	if (VerbosityStr.Equals(TEXT("VeryVerbose"), ESearchCase::IgnoreCase)) return ELogVerbosity::VeryVerbose;
	return ELogVerbosity::All;
}

FString VerbosityToString(ELogVerbosity::Type Verbosity)
{
	switch (Verbosity)
	{
	case ELogVerbosity::Fatal: return TEXT("Fatal");
	case ELogVerbosity::Error: return TEXT("Error");
	case ELogVerbosity::Warning: return TEXT("Warning");
	case ELogVerbosity::Display: return TEXT("Display");
	case ELogVerbosity::Log: return TEXT("Log");
	case ELogVerbosity::Verbose: return TEXT("Verbose");
	default: return TEXT("VeryVerbose");
	}
}

uint64 ParseLiveCursor(const FString& Cursor)
{
	if (!Cursor.StartsWith(TEXT("live:")))
	{
		return 0;
	}

	uint64 Seq = 0;
	LexFromString(Seq, *Cursor.RightChop(5));
	return Seq;
}
}

TSharedPtr<FJsonObject> FGetEditorLogsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	const int32 Count = static_cast<int32>(GetOptionalNumber(Params, TEXT("count"), 100.0));
	const FString CategoryFilter = GetOptionalString(Params, TEXT("category"));

	// Parse verbosity filter
	const ELogVerbosity::Type MinVerbosity = ParseMinVerbosity(GetOptionalString(Params, TEXT("min_verbosity")));

	TArray<FMCPLogCapture::FLogEntry> Entries = FMCPLogCapture::Get().GetRecent(Count, CategoryFilter, MinVerbosity);

	TArray<TSharedPtr<FJsonValue>> LinesArray;
	for (const FMCPLogCapture::FLogEntry& Entry : Entries)
	{
		TSharedPtr<FJsonObject> LineObj = MakeShared<FJsonObject>();
		LineObj->SetNumberField(TEXT("timestamp"), Entry.Timestamp);
		LineObj->SetStringField(TEXT("category"), Entry.Category.ToString());

		// Convert verbosity to string
		LineObj->SetStringField(TEXT("verbosity"), VerbosityToString(Entry.Verbosity));
		LineObj->SetStringField(TEXT("message"), Entry.Message);

		LinesArray.Add(MakeShared<FJsonValueObject>(LineObj));
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetArrayField(TEXT("lines"), LinesArray);
	ResultData->SetNumberField(TEXT("total"), static_cast<double>(LinesArray.Num()));
	ResultData->SetNumberField(TEXT("total_captured"), static_cast<double>(FMCPLogCapture::Get().GetTotalCaptured()));
	ResultData->SetBoolField(TEXT("capturing"), FMCPLogCapture::Get().IsCapturing());
	return CreateSuccessResponse(ResultData);
}

TSharedPtr<FJsonObject> FGetUnrealLogsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FMCPLogCapture& Capture = FMCPLogCapture::Get();
	if (!Capture.IsCapturing())
	{
		return CreateErrorResponse(TEXT("Live log capture is not active"), TEXT("capture_inactive"));
	}

	const int32 TailLines = FMath::Clamp(static_cast<int32>(GetOptionalNumber(Params, TEXT("tail_lines"), 200.0)), 20, 2000);
	const int32 MaxBytes = FMath::Clamp(static_cast<int32>(GetOptionalNumber(Params, TEXT("max_bytes"), 65536.0)), 8192, 1024 * 1024);
	const bool bIncludeMeta = GetOptionalBool(Params, TEXT("include_meta"), true);
	const bool bRequireRecent = GetOptionalBool(Params, TEXT("require_recent"), false);
	const double RecentWindowSeconds = FMath::Max(0.0, GetOptionalNumber(Params, TEXT("recent_window_seconds"), 2.0));

	if (bRequireRecent && !Capture.HasRecentData(RecentWindowSeconds))
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("No live logs in the last %.2f seconds"), RecentWindowSeconds),
			TEXT("no_recent_live_data"));
	}

	const FString Cursor = GetOptionalString(Params, TEXT("cursor"));
	const uint64 AfterSeq = ParseLiveCursor(Cursor);

	TArray<FString> CategoryFilters;
	const FString CategoryFilterSingle = GetOptionalString(Params, TEXT("filter_category"));
	if (!CategoryFilterSingle.IsEmpty())
	{
		CategoryFilters.Add(CategoryFilterSingle);
	}

	const TArray<TSharedPtr<FJsonValue>>* CategoryFilterArray = GetOptionalArray(Params, TEXT("filter_categories"));
	if (CategoryFilterArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *CategoryFilterArray)
		{
			FString CategoryValue;
			if (Value.IsValid() && Value->TryGetString(CategoryValue) && !CategoryValue.IsEmpty())
			{
				CategoryFilters.Add(CategoryValue);
			}
		}
	}

	const ELogVerbosity::Type MinVerbosity = ParseMinVerbosity(GetOptionalString(Params, TEXT("filter_min_verbosity")));
	const FString ContainsFilter = GetOptionalString(Params, TEXT("filter_contains"));

	bool bTruncated = false;
	uint64 LastSeq = Capture.GetLatestSeq();
	TArray<FMCPLogCapture::FLogEntry> Entries = Capture.GetSince(
		AfterSeq,
		TailLines,
		MaxBytes,
		CategoryFilters,
		MinVerbosity,
		ContainsFilter,
		bTruncated,
		LastSeq);

	if (AfterSeq == 0)
	{
		if (Entries.Num() > TailLines)
		{
			const int32 Start = Entries.Num() - TailLines;
			Entries.RemoveAt(0, Start);
			bTruncated = true;
		}
	}

	FString Content;
	int32 BytesReturned = 0;
	for (const FMCPLogCapture::FLogEntry& Entry : Entries)
	{
		const FString Line = FString::Printf(
			TEXT("[%s][%s][%s] %s\n"),
			*Entry.TimestampUtc.ToIso8601(),
			*VerbosityToString(Entry.Verbosity),
			*Entry.Category.ToString(),
			*Entry.Message);

		const int32 LineBytes = FTCHARToUTF8(*Line).Length();
		if (BytesReturned + LineBytes > MaxBytes)
		{
			bTruncated = true;
			break;
		}

		Content.Append(Line);
		BytesReturned += LineBytes;
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("source"), TEXT("live"));
	ResultData->SetBoolField(TEXT("isLive"), true);
	ResultData->SetStringField(TEXT("filePath"), TEXT(""));
	ResultData->SetStringField(TEXT("projectLogDir"), FPaths::ProjectLogDir());
	ResultData->SetStringField(TEXT("cursor"), FString::Printf(TEXT("live:%llu"), LastSeq));
	ResultData->SetBoolField(TEXT("truncated"), bTruncated);
	ResultData->SetNumberField(TEXT("linesReturned"), Entries.Num());
	ResultData->SetNumberField(TEXT("bytesReturned"), BytesReturned);
	ResultData->SetStringField(TEXT("lastUpdateUtc"), Capture.GetLastReceivedUtc().ToIso8601());
	ResultData->SetStringField(TEXT("content"), Content);

	TArray<TSharedPtr<FJsonValue>> Notes;
	if (AfterSeq > 0)
	{
		Notes.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("incremental_from_seq=%llu"), AfterSeq)));
	}
	if (bTruncated)
	{
		Notes.Add(MakeShared<FJsonValueString>(TEXT("response_truncated_by_limits")));
	}
	if (bIncludeMeta)
	{
		ResultData->SetBoolField(TEXT("hasRecentLiveData"), Capture.HasRecentData(2.0));
		ResultData->SetNumberField(TEXT("totalCaptured"), static_cast<double>(Capture.GetTotalCaptured()));
	}
	ResultData->SetArrayField(TEXT("notes"), Notes);

	if (Entries.Num() == 0 && AfterSeq == 0)
	{
		return CreateErrorResponse(TEXT("Live log capture has no entries yet"), TEXT("live_buffer_empty"));
	}

	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// P2: FBatchExecuteAction
// ============================================================================

bool FBatchExecuteAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* Commands = GetOptionalArray(Params, TEXT("commands"));
	if (!Commands || Commands->Num() == 0)
	{
		OutError = TEXT("Missing or empty 'commands' array");
		return false;
	}
	if (Commands->Num() > MaxBatchSize)
	{
		OutError = FString::Printf(TEXT("Batch too large: %d commands (max %d)"), Commands->Num(), MaxBatchSize);
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FBatchExecuteAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	const TArray<TSharedPtr<FJsonValue>>* Commands = GetOptionalArray(Params, TEXT("commands"));
	const bool bStopOnError = GetOptionalBool(Params, TEXT("stop_on_error"), true);
	const bool bTransactional = GetOptionalBool(Params, TEXT("transactional"), false);

	// We need access to the Bridge to dispatch sub-commands
	UMCPBridge* Bridge = GEditor ? GEditor->GetEditorSubsystem<UMCPBridge>() : nullptr;
	if (!Bridge)
	{
		return CreateErrorResponse(TEXT("MCPBridge subsystem not available"));
	}

	const int32 Total = Commands->Num();
	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	int32 Succeeded = 0;
	int32 Failed = 0;
	int32 Executed = 0;
	bool bRolledBack = false;

	// P9: If transactional, wrap the entire batch in a single FScopedTransaction.
	// On any failure (with stop_on_error), undo the entire transaction.
	TUniquePtr<FScopedTransaction> ScopedTransaction;
	if (bTransactional && GEditor)
	{
		ScopedTransaction = MakeUnique<FScopedTransaction>(
			FText::FromString(FString::Printf(TEXT("MCP: batch_execute (%d commands)"), Total))
		);
		UE_LOG(LogMCP, Log, TEXT("Batch: transactional mode enabled — wrapping %d commands in single undo transaction"), Total);
	}

	for (int32 i = 0; i < Total; ++i)
	{
		const TSharedPtr<FJsonObject>* CmdObj = nullptr;
		if (!(*Commands)[i]->TryGetObject(CmdObj) || !CmdObj || !(*CmdObj).IsValid())
		{
			TSharedPtr<FJsonObject> ErrResult = MakeShared<FJsonObject>();
			ErrResult->SetNumberField(TEXT("index"), i);
			ErrResult->SetBoolField(TEXT("success"), false);
			ErrResult->SetStringField(TEXT("error"), TEXT("Invalid command object"));
			ResultsArray.Add(MakeShared<FJsonValueObject>(ErrResult));
			++Failed;
			++Executed;
			if (bStopOnError) break;
			continue;
		}

		FString CmdType;
		if (!(*CmdObj)->TryGetStringField(TEXT("type"), CmdType) || CmdType.IsEmpty())
		{
			TSharedPtr<FJsonObject> ErrResult = MakeShared<FJsonObject>();
			ErrResult->SetNumberField(TEXT("index"), i);
			ErrResult->SetBoolField(TEXT("success"), false);
			ErrResult->SetStringField(TEXT("error"), TEXT("Missing 'type' field"));
			ResultsArray.Add(MakeShared<FJsonValueObject>(ErrResult));
			++Failed;
			++Executed;
			if (bStopOnError) break;
			continue;
		}

		TSharedPtr<FJsonObject> CmdParams;
		const TSharedPtr<FJsonObject>* CmdParamsPtr = nullptr;
		if ((*CmdObj)->TryGetObjectField(TEXT("params"), CmdParamsPtr) && CmdParamsPtr)
		{
			CmdParams = *CmdParamsPtr;
		}
		else
		{
			CmdParams = MakeShared<FJsonObject>();
		}

		UE_LOG(LogMCP, Log, TEXT("Batch[%d/%d]: executing '%s'"), i + 1, Total, *CmdType);

		// Execute the sub-command via the Bridge (bypasses TCP, stays on GameThread)
		TSharedPtr<FJsonObject> SubResult = Bridge->ExecuteCommand(CmdType, CmdParams);

		// Wrap result with index
		if (SubResult.IsValid())
		{
			SubResult->SetNumberField(TEXT("index"), i);
			SubResult->SetStringField(TEXT("type"), CmdType);
		}

		bool bSubSuccess = false;
		if (SubResult.IsValid() && SubResult->TryGetBoolField(TEXT("success"), bSubSuccess) && bSubSuccess)
		{
			++Succeeded;
		}
		else
		{
			++Failed;
		}

		ResultsArray.Add(MakeShared<FJsonValueObject>(SubResult.IsValid() ? SubResult : MakeShared<FJsonObject>()));
		++Executed;

		if (!bSubSuccess && bStopOnError)
		{
			UE_LOG(LogMCP, Warning, TEXT("Batch stopped at command %d/%d ('%s') due to stop_on_error"), i + 1, Total, *CmdType);
			break;
		}
	}

	// P9: Transactional rollback — if there were failures, cancel the transaction
	// (undo all changes made by this batch) before it goes out of scope.
	if (bTransactional && Failed > 0 && ScopedTransaction.IsValid())
	{
		// Cancel the scoped transaction by destroying it first (triggers undo record),
		// then perform an undo to revert all changes.
		ScopedTransaction.Reset();

		if (GEditor && GEditor->Trans && GEditor->Trans->CanUndo())
		{
			GEditor->UndoTransaction();
			bRolledBack = true;
			UE_LOG(LogMCP, Warning, TEXT("Batch: transactional rollback — undid %d commands due to %d failure(s)"), Executed, Failed);
		}
		else
		{
			UE_LOG(LogMCP, Warning, TEXT("Batch: transactional rollback requested but undo not available"));
		}
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetNumberField(TEXT("total"), Total);
	ResultData->SetNumberField(TEXT("executed"), Executed);
	ResultData->SetNumberField(TEXT("succeeded"), Succeeded);
	ResultData->SetNumberField(TEXT("failed"), Failed);
	ResultData->SetArrayField(TEXT("results"), ResultsArray);
	ResultData->SetBoolField(TEXT("transactional"), bTransactional);
	ResultData->SetBoolField(TEXT("rolled_back"), bRolledBack);

	// The batch command itself always succeeds (it dispatched commands).
	// Partial sub-command failures are conveyed via failed > 0 and individual
	// result items' "success" fields — not by failing the batch itself.
	// This ensures the Python side receives structured data (results, total,
	// executed, etc.) regardless of sub-command outcomes.
	if (Failed > 0)
	{
		ResultData->SetBoolField(TEXT("has_failures"), true);
	}

	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// FEditorIsReadyAction
// ============================================================================

TSharedPtr<FJsonObject> FEditorIsReadyAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	// 1) GEditor exists and is valid
	bool bEditorValid = (GEditor != nullptr);
	Result->SetBoolField(TEXT("editor_valid"), bEditorValid);

	// 2) Editor world is available
	bool bWorldReady = false;
	if (bEditorValid)
	{
		UWorld* World = GEditor->GetEditorWorldContext(false).World();
		bWorldReady = (World != nullptr);
	}
	Result->SetBoolField(TEXT("world_ready"), bWorldReady);

	// 3) Asset registry has finished initial scan
	bool bAssetRegistryReady = false;
	if (FModuleManager::Get().IsModuleLoaded(TEXT("AssetRegistry")))
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		bAssetRegistryReady = !AssetRegistry.IsLoadingAssets();
	}
	Result->SetBoolField(TEXT("asset_registry_ready"), bAssetRegistryReady);

	// 4) Overall readiness: all critical subsystems ready
	bool bFullyReady = bEditorValid && bWorldReady && bAssetRegistryReady;
	Result->SetBoolField(TEXT("ready"), bFullyReady);

	// 5) Uptime info
	Result->SetNumberField(TEXT("engine_uptime_seconds"), FPlatformTime::Seconds());

	return CreateSuccessResponse(Result);
}


// ============================================================================
// FRequestEditorShutdownAction
// ============================================================================

TSharedPtr<FJsonObject> FRequestEditorShutdownAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	bool bForce = GetOptionalBool(Params, TEXT("force"), false);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("shutdown_requested"), true);
	Result->SetBoolField(TEXT("force"), bForce);

	// Schedule the exit on the next game-thread tick so we can send the response first
	AsyncTask(ENamedThreads::GameThread, [bForce]()
	{
		// Small delay to ensure the MCP response is sent before the process starts shutting down
		FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([bForce](float DeltaTime) -> bool
			{
				if (bForce)
				{
					// Force immediate exit - no save dialogs
					FPlatformMisc::RequestExitWithStatus(bForce, 0);
				}
				else
				{
					// Graceful shutdown - may show save dialog if -unattended is not set
					FPlatformMisc::RequestExit(false);
				}
				return false; // Don't tick again
			}),
			0.2f // 200ms delay to let TCP response flush
		);
	});

	return CreateSuccessResponse(Result);
}

