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



// ---- P6.5 FAssertLogAction ----

bool FAssertLogAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* Assertions = GetOptionalArray(Params, TEXT("assertions"));
	if (!Assertions || Assertions->Num() == 0)
	{
		OutError = TEXT("'assertions' array is required and must not be empty");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FAssertLogAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FMCPLogCapture& LogCapture = FMCPLogCapture::Get();

	// Parse optional cursor
	FString SinceCursor = GetOptionalString(Params, TEXT("since_cursor"), TEXT(""));
	uint64 AfterSeq = 0;
	if (SinceCursor.StartsWith(TEXT("live:")))
	{
		FString SeqStr = SinceCursor.Mid(5);
		AfterSeq = FCString::Strtoui64(*SeqStr, nullptr, 10);
	}

	// Gather log entries
	bool bTruncated = false;
	uint64 LastSeq = 0;
	TArray<FString> EmptyCategories;
	TArray<FMCPLogCapture::FLogEntry> LogEntries = LogCapture.GetSince(
		AfterSeq, 10000, 5 * 1024 * 1024,
		EmptyCategories, ELogVerbosity::All, TEXT(""),
		bTruncated, LastSeq);

	// Parse assertions and check
	const TArray<TSharedPtr<FJsonValue>>* Assertions = GetOptionalArray(Params, TEXT("assertions"));
	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	int32 PassedCount = 0;
	int32 FailedCount = 0;

	for (const TSharedPtr<FJsonValue>& AssertVal : *Assertions)
	{
		const TSharedPtr<FJsonObject>* AssertObjPtr;
		if (!AssertVal->TryGetObject(AssertObjPtr) || !AssertObjPtr)
		{
			continue;
		}

		const TSharedPtr<FJsonObject>& AssertObj = *AssertObjPtr;
		FString Keyword;
		if (!AssertObj->TryGetStringField(TEXT("keyword"), Keyword) || Keyword.IsEmpty())
		{
			continue;
		}

		int32 ExpectedCount = static_cast<int32>(AssertObj->GetNumberField(TEXT("expected_count")));
		FString Comparison = TEXT(">=");
		AssertObj->TryGetStringField(TEXT("comparison"), Comparison);
		FString CategoryFilter;
		AssertObj->TryGetStringField(TEXT("category"), CategoryFilter);

		// Count keyword occurrences
		int32 ActualCount = 0;
		for (const FMCPLogCapture::FLogEntry& Entry : LogEntries)
		{
			// Optional category filter
			if (!CategoryFilter.IsEmpty() && !Entry.Category.ToString().Contains(CategoryFilter))
			{
				continue;
			}
			if (Entry.Message.Contains(Keyword))
			{
				ActualCount++;
			}
		}

		// Evaluate comparison
		bool bPassed = false;
		if (Comparison == TEXT("=="))
		{
			bPassed = (ActualCount == ExpectedCount);
		}
		else if (Comparison == TEXT(">="))
		{
			bPassed = (ActualCount >= ExpectedCount);
		}
		else if (Comparison == TEXT("<="))
		{
			bPassed = (ActualCount <= ExpectedCount);
		}
		else if (Comparison == TEXT(">"))
		{
			bPassed = (ActualCount > ExpectedCount);
		}
		else if (Comparison == TEXT("<"))
		{
			bPassed = (ActualCount < ExpectedCount);
		}

		if (bPassed)
		{
			PassedCount++;
		}
		else
		{
			FailedCount++;
		}

		TSharedPtr<FJsonObject> AssertResult = MakeShared<FJsonObject>();
		AssertResult->SetStringField(TEXT("keyword"), Keyword);
		AssertResult->SetNumberField(TEXT("expected"), ExpectedCount);
		AssertResult->SetNumberField(TEXT("actual"), ActualCount);
		AssertResult->SetStringField(TEXT("comparison"), Comparison);
		AssertResult->SetBoolField(TEXT("passed"), bPassed);
		ResultsArray.Add(MakeShared<FJsonValueObject>(AssertResult));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("overall"), FailedCount == 0 ? TEXT("pass") : TEXT("fail"));
	Result->SetNumberField(TEXT("total_assertions"), ResultsArray.Num());
	Result->SetNumberField(TEXT("passed"), PassedCount);
	Result->SetNumberField(TEXT("failed"), FailedCount);
	Result->SetArrayField(TEXT("results"), ResultsArray);

	TSharedPtr<FJsonObject> LogRange = MakeShared<FJsonObject>();
	LogRange->SetNumberField(TEXT("from_seq"), static_cast<double>(AfterSeq));
	LogRange->SetNumberField(TEXT("to_seq"), static_cast<double>(LastSeq));
	LogRange->SetNumberField(TEXT("lines_scanned"), LogEntries.Num());
	Result->SetObjectField(TEXT("log_range"), LogRange);

	return CreateSuccessResponse(Result);
}


// =========================================================================
// P6: Outliner Management Actions
// =========================================================================

// ---- Shared helper: find actor by name or label ----

static AActor* FindActorInWorld(UWorld* World, const FString& ActorName)
{
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		// Match by name (GetName)
		if (Actor->GetName().Equals(ActorName, ESearchCase::IgnoreCase))
		{
			return Actor;
		}

		// Match by label (display name in Outliner)
		if (Actor->GetActorLabel().Equals(ActorName, ESearchCase::IgnoreCase))
		{
			return Actor;
		}
	}

	return nullptr;
}

// ---- P6.6 FRenameActorLabelAction ----

bool FRenameActorLabelAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	// Need either (actor_name + new_label) or items[]
	const TArray<TSharedPtr<FJsonValue>>* Items = GetOptionalArray(Params, TEXT("items"));
	if (Items && Items->Num() > 0)
	{
		return true;
	}

	FString ActorName = GetOptionalString(Params, TEXT("actor_name"));
	FString NewLabel = GetOptionalString(Params, TEXT("new_label"));
	if (ActorName.IsEmpty() || NewLabel.IsEmpty())
	{
		OutError = TEXT("Either 'items' array or both 'actor_name' and 'new_label' are required");
		return false;
	}
	return true;
}

AActor* FRenameActorLabelAction::FindActorByName(UWorld* World, const FString& ActorName) const
{
	return FindActorInWorld(World, ActorName);
}

TSharedPtr<FJsonObject> FRenameActorLabelAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No editor world available"));
	}

	struct FRenameItem
	{
		FString ActorName;
		FString NewLabel;
	};

	TArray<FRenameItem> ItemsList;
	const TArray<TSharedPtr<FJsonValue>>* Items = GetOptionalArray(Params, TEXT("items"));
	if (Items && Items->Num() > 0)
	{
		for (const TSharedPtr<FJsonValue>& ItemVal : *Items)
		{
			const TSharedPtr<FJsonObject>* ItemObjPtr;
			if (ItemVal->TryGetObject(ItemObjPtr) && ItemObjPtr)
			{
				FRenameItem Item;
				(*ItemObjPtr)->TryGetStringField(TEXT("actor_name"), Item.ActorName);
				(*ItemObjPtr)->TryGetStringField(TEXT("new_label"), Item.NewLabel);
				if (!Item.ActorName.IsEmpty() && !Item.NewLabel.IsEmpty())
				{
					ItemsList.Add(MoveTemp(Item));
				}
			}
		}
	}
	else
	{
		FRenameItem Item;
		Item.ActorName = GetOptionalString(Params, TEXT("actor_name"));
		Item.NewLabel = GetOptionalString(Params, TEXT("new_label"));
		ItemsList.Add(MoveTemp(Item));
	}

	FScopedTransaction Transaction(FText::FromString(TEXT("MCP Rename Actor Labels")));

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	int32 SuccessCount = 0;

	for (const FRenameItem& Item : ItemsList)
	{
		TSharedPtr<FJsonObject> ItemResult = MakeShared<FJsonObject>();
		ItemResult->SetStringField(TEXT("actor_name"), Item.ActorName);

		AActor* Actor = FindActorByName(World, Item.ActorName);
		if (!Actor)
		{
			ItemResult->SetBoolField(TEXT("success"), false);
			ItemResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor '%s' not found"), *Item.ActorName));
		}
		else
		{
			FString OldLabel = Actor->GetActorLabel();
			Actor->SetActorLabel(Item.NewLabel);
			ItemResult->SetBoolField(TEXT("success"), true);
			ItemResult->SetStringField(TEXT("old_label"), OldLabel);
			ItemResult->SetStringField(TEXT("new_label"), Item.NewLabel);
			SuccessCount++;
		}

		ResultsArray.Add(MakeShared<FJsonValueObject>(ItemResult));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("total"), ItemsList.Num());
	Result->SetNumberField(TEXT("succeeded"), SuccessCount);
	Result->SetArrayField(TEXT("results"), ResultsArray);

	return CreateSuccessResponse(Result);
}

// ---- P6.7 FSetActorFolderAction ----

bool FSetActorFolderAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* Items = GetOptionalArray(Params, TEXT("items"));
	if (Items && Items->Num() > 0)
	{
		return true;
	}

	FString ActorName = GetOptionalString(Params, TEXT("actor_name"));
	FString FolderPath = GetOptionalString(Params, TEXT("folder_path"));
	if (ActorName.IsEmpty())
	{
		OutError = TEXT("Either 'items' array or 'actor_name' is required");
		return false;
	}
	return true;
}

AActor* FSetActorFolderAction::FindActorByName(UWorld* World, const FString& ActorName) const
{
	return FindActorInWorld(World, ActorName);
}

TSharedPtr<FJsonObject> FSetActorFolderAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No editor world available"));
	}

	struct FFolderItem
	{
		FString ActorName;
		FString FolderPath;
	};

	TArray<FFolderItem> ItemsList;
	const TArray<TSharedPtr<FJsonValue>>* Items = GetOptionalArray(Params, TEXT("items"));
	if (Items && Items->Num() > 0)
	{
		for (const TSharedPtr<FJsonValue>& ItemVal : *Items)
		{
			const TSharedPtr<FJsonObject>* ItemObjPtr;
			if (ItemVal->TryGetObject(ItemObjPtr) && ItemObjPtr)
			{
				FFolderItem Item;
				(*ItemObjPtr)->TryGetStringField(TEXT("actor_name"), Item.ActorName);
				(*ItemObjPtr)->TryGetStringField(TEXT("folder_path"), Item.FolderPath);
				if (!Item.ActorName.IsEmpty())
				{
					ItemsList.Add(MoveTemp(Item));
				}
			}
		}
	}
	else
	{
		FFolderItem Item;
		Item.ActorName = GetOptionalString(Params, TEXT("actor_name"));
		Item.FolderPath = GetOptionalString(Params, TEXT("folder_path"));
		ItemsList.Add(MoveTemp(Item));
	}

	FScopedTransaction Transaction(FText::FromString(TEXT("MCP Set Actor Folders")));

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	int32 SuccessCount = 0;

	for (const FFolderItem& Item : ItemsList)
	{
		TSharedPtr<FJsonObject> ItemResult = MakeShared<FJsonObject>();
		ItemResult->SetStringField(TEXT("actor_name"), Item.ActorName);

		AActor* Actor = FindActorByName(World, Item.ActorName);
		if (!Actor)
		{
			ItemResult->SetBoolField(TEXT("success"), false);
			ItemResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor '%s' not found"), *Item.ActorName));
		}
		else
		{
			FString OldFolder = Actor->GetFolderPath().ToString();
			Actor->SetFolderPath(FName(*Item.FolderPath));
			ItemResult->SetBoolField(TEXT("success"), true);
			ItemResult->SetStringField(TEXT("old_folder"), OldFolder);
			ItemResult->SetStringField(TEXT("new_folder"), Item.FolderPath);
			SuccessCount++;
		}

		ResultsArray.Add(MakeShared<FJsonValueObject>(ItemResult));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("total"), ItemsList.Num());
	Result->SetNumberField(TEXT("succeeded"), SuccessCount);
	Result->SetArrayField(TEXT("results"), ResultsArray);

	return CreateSuccessResponse(Result);
}

// ---- P6.8 FSelectActorsAction ----

bool FSelectActorsAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ActorNames = GetOptionalArray(Params, TEXT("actor_names"));
	if (!ActorNames || ActorNames->Num() == 0)
	{
		OutError = TEXT("'actor_names' array is required and must not be empty");
		return false;
	}
	return true;
}

AActor* FSelectActorsAction::FindActorByName(UWorld* World, const FString& ActorName) const
{
	return FindActorInWorld(World, ActorName);
}

TSharedPtr<FJsonObject> FSelectActorsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No editor world available"));
	}

	FString Mode = GetOptionalString(Params, TEXT("mode"), TEXT("set"));
	const TArray<TSharedPtr<FJsonValue>>* ActorNames = GetOptionalArray(Params, TEXT("actor_names"));

	// If mode is "set", deselect all first
	if (Mode.Equals(TEXT("set"), ESearchCase::IgnoreCase))
	{
		GEditor->SelectNone(/*bNoteSelectionChange=*/false, /*bDeselectBSPSurfs=*/true);
	}

	int32 FoundCount = 0;
	int32 NotFoundCount = 0;
	TArray<FString> NotFoundNames;

	for (const TSharedPtr<FJsonValue>& NameVal : *ActorNames)
	{
		FString ActorName;
		if (!NameVal->TryGetString(ActorName) || ActorName.IsEmpty())
		{
			continue;
		}

		AActor* Actor = FindActorByName(World, ActorName);
		if (!Actor)
		{
			NotFoundCount++;
			NotFoundNames.Add(ActorName);
			continue;
		}

		if (Mode.Equals(TEXT("set"), ESearchCase::IgnoreCase) || Mode.Equals(TEXT("add"), ESearchCase::IgnoreCase))
		{
			GEditor->SelectActor(Actor, /*bInSelected=*/true, /*bNotify=*/false);
		}
		else if (Mode.Equals(TEXT("remove"), ESearchCase::IgnoreCase))
		{
			GEditor->SelectActor(Actor, /*bInSelected=*/false, /*bNotify=*/false);
		}
		else if (Mode.Equals(TEXT("toggle"), ESearchCase::IgnoreCase))
		{
			bool bIsSelected = Actor->IsSelected();
			GEditor->SelectActor(Actor, /*bInSelected=*/!bIsSelected, /*bNotify=*/false);
		}

		FoundCount++;
	}

	// Notify after all selection changes
	GEditor->NoteSelectionChange();

	// Count total selected
	int32 SelectedCount = 0;
	for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
	{
		SelectedCount++;
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("mode"), Mode);
	Result->SetNumberField(TEXT("requested"), ActorNames->Num());
	Result->SetNumberField(TEXT("found"), FoundCount);
	Result->SetNumberField(TEXT("not_found"), NotFoundCount);
	Result->SetNumberField(TEXT("selected_count"), SelectedCount);
	if (NotFoundNames.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> NotFoundArr;
		for (const FString& Name : NotFoundNames)
		{
			NotFoundArr.Add(MakeShared<FJsonValueString>(Name));
		}
		Result->SetArrayField(TEXT("not_found_names"), NotFoundArr);
	}

	return CreateSuccessResponse(Result);
}

// ---- P6.9 FGetOutlinerTreeAction ----

TSharedPtr<FJsonObject> FGetOutlinerTreeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No editor world available"));
	}

	FString ClassFilter = GetOptionalString(Params, TEXT("class_filter"));
	FString FolderFilter = GetOptionalString(Params, TEXT("folder_filter"));

	// Organize actors by folder
	TMap<FString, TArray<TSharedPtr<FJsonValue>>> FolderMap;
	TArray<TSharedPtr<FJsonValue>> UnfolderedActors;
	int32 TotalActors = 0;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor->IsA(AWorldSettings::StaticClass()))
		{
			continue;
		}

		FString ClassName = Actor->GetClass()->GetName();

		// Class filter
		if (!ClassFilter.IsEmpty() && !ClassName.Contains(ClassFilter))
		{
			continue;
		}

		FString FolderPath = Actor->GetFolderPath().ToString();

		// Folder filter
		if (!FolderFilter.IsEmpty() && !FolderPath.StartsWith(FolderFilter))
		{
			// If folder doesn't match and actor is not unfoldered, skip
			if (!FolderPath.IsEmpty())
			{
				continue;
			}
		}

		TSharedPtr<FJsonObject> ActorObj = MakeShared<FJsonObject>();
		ActorObj->SetStringField(TEXT("name"), Actor->GetName());
		ActorObj->SetStringField(TEXT("class"), ClassName);
		ActorObj->SetStringField(TEXT("label"), Actor->GetActorLabel());

		TotalActors++;

		if (FolderPath.IsEmpty())
		{
			UnfolderedActors.Add(MakeShared<FJsonValueObject>(ActorObj));
		}
		else
		{
			FolderMap.FindOrAdd(FolderPath).Add(MakeShared<FJsonValueObject>(ActorObj));
		}
	}

	// Build folders array
	TArray<TSharedPtr<FJsonValue>> FoldersArray;
	// Sort folder paths
	TArray<FString> FolderPaths;
	FolderMap.GetKeys(FolderPaths);
	FolderPaths.Sort();

	for (const FString& Path : FolderPaths)
	{
		TSharedPtr<FJsonObject> FolderObj = MakeShared<FJsonObject>();
		FolderObj->SetStringField(TEXT("path"), Path);
		FolderObj->SetArrayField(TEXT("actors"), FolderMap[Path]);
		FolderObj->SetNumberField(TEXT("actor_count"), FolderMap[Path].Num());
		FoldersArray.Add(MakeShared<FJsonValueObject>(FolderObj));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("total_actors"), TotalActors);
	Result->SetNumberField(TEXT("folder_count"), FoldersArray.Num());
	Result->SetArrayField(TEXT("folders"), FoldersArray);
	Result->SetArrayField(TEXT("unfoldered_actors"), UnfolderedActors);

	return CreateSuccessResponse(Result);
}


// ============================================================================
// FOpenAssetEditorAction — Open an asset editor and optionally focus it
// ============================================================================

bool FOpenAssetEditorAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		OutError = TEXT("'asset_path' is required (e.g. '/Game/Characters/BP_Hero')");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FOpenAssetEditorAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	bool bFocus = GetOptionalBool(Params, TEXT("focus"), true);

	// 1) Validate GEditor
	if (!GEditor)
	{
		return CreateErrorResponse(TEXT("GEditor is not available"), TEXT("editor_not_ready"));
	}

	// 2) Load the asset
	UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
	if (!Asset)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath),
			TEXT("asset_not_found")
		);
	}

	// 3) Get AssetEditorSubsystem
	UAssetEditorSubsystem* AssetEditorSS = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AssetEditorSS)
	{
		return CreateErrorResponse(TEXT("AssetEditorSubsystem is not available"), TEXT("subsystem_error"));
	}

	// 4) Open the editor
	bool bOpened = AssetEditorSS->OpenEditorForAsset(Asset);
	if (!bOpened)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Failed to open editor for asset: %s"), *AssetPath),
			TEXT("open_failed")
		);
	}

	// 5) Optionally focus the editor window
	FString EditorName = TEXT("Unknown");
	if (bFocus)
	{
		IAssetEditorInstance* EditorInstance = AssetEditorSS->FindEditorForAsset(Asset, /*bFocusIfOpen=*/ true);
		if (EditorInstance)
		{
			EditorName = EditorInstance->GetEditorName().ToString();
		}
	}
	else
	{
		IAssetEditorInstance* EditorInstance = AssetEditorSS->FindEditorForAsset(Asset, false);
		if (EditorInstance)
		{
			EditorName = EditorInstance->GetEditorName().ToString();
		}
	}

	// 6) Build result
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("asset_name"), Asset->GetName());
	Result->SetStringField(TEXT("asset_class"), Asset->GetClass()->GetName());
	Result->SetStringField(TEXT("editor_name"), EditorName);
	Result->SetBoolField(TEXT("focused"), bFocus);

	return CreateSuccessResponse(Result);
}


