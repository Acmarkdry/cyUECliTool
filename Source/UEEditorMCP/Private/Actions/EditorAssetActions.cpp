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
// FSaveAllAction
// ============================================================================

TSharedPtr<FJsonObject> FSaveAllAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	bool bOnlyMaps = GetOptionalBool(Params, TEXT("only_maps"), false);

	int32 SavedCount = 0;
	TArray<FString> SavedPackages;

	if (bOnlyMaps)
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (World)
		{
			UPackage* WorldPackage = World->GetOutermost();
			if (WorldPackage && WorldPackage->IsDirty())
			{
				FString PackageFilename;
				if (FPackageName::TryConvertLongPackageNameToFilename(
					WorldPackage->GetName(), PackageFilename, FPackageName::GetMapPackageExtension()))
				{
					FSavePackageArgs SaveArgs;
					SaveArgs.TopLevelFlags = RF_Standalone;
					if (UPackage::SavePackage(WorldPackage, World, *PackageFilename, SaveArgs))
					{
						SavedCount++;
						SavedPackages.Add(WorldPackage->GetName());
					}
				}
			}
		}
	}
	else
	{
		TArray<UPackage*> DirtyPackages;
		FEditorFileUtils::GetDirtyPackages(DirtyPackages);

		for (UPackage* Package : DirtyPackages)
		{
			if (!Package) continue;

			FString PackageFilename;
			FString PackageName = Package->GetName();
			bool bIsMap = Package->ContainsMap();
			FString Extension = bIsMap ?
				FPackageName::GetMapPackageExtension() :
				FPackageName::GetAssetPackageExtension();

			if (FPackageName::TryConvertLongPackageNameToFilename(PackageName, PackageFilename, Extension))
			{
				FSavePackageArgs SaveArgs;
				SaveArgs.TopLevelFlags = RF_Standalone;

				UObject* AssetToSave = bIsMap ? Package->FindAssetInPackage() : nullptr;

				if (UPackage::SavePackage(Package, AssetToSave, *PackageFilename, SaveArgs))
				{
					SavedCount++;
					SavedPackages.Add(PackageName);
					UE_LOG(LogMCP, Log, TEXT("UEEditorMCP SaveAll: Saved %s"), *PackageName);
				}
			}
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("saved_count"), SavedCount);

	TArray<TSharedPtr<FJsonValue>> PackagesArray;
	for (const FString& PkgName : SavedPackages)
	{
		PackagesArray.Add(MakeShared<FJsonValueString>(PkgName));
	}
	Result->SetArrayField(TEXT("saved_packages"), PackagesArray);

	return CreateSuccessResponse(Result);
}


// ========================================================================
// FListAssetsAction
// ========================================================================

bool FListAssetsAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("path")))
	{
		OutError = TEXT("Missing required 'path' parameter (e.g. /Game/UI)");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FListAssetsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Path = Params->GetStringField(TEXT("path"));
	bool bRecursive = true;
	if (Params->HasField(TEXT("recursive")))
	{
		bRecursive = Params->GetBoolField(TEXT("recursive"));
	}
	FString ClassFilter;
	if (Params->HasField(TEXT("class_filter")))
	{
		ClassFilter = Params->GetStringField(TEXT("class_filter"));
	}
	FString NameContains;
	if (Params->HasField(TEXT("name_contains")))
	{
		NameContains = Params->GetStringField(TEXT("name_contains"));
	}
	int32 MaxResults = 500;
	if (Params->HasField(TEXT("max_results")))
	{
		MaxResults = static_cast<int32>(Params->GetNumberField(TEXT("max_results")));
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*Path));
	Filter.bRecursivePaths = bRecursive;

	// Apply class filter if specified
	if (!ClassFilter.IsEmpty())
	{
		// Support common short names
		if (ClassFilter == TEXT("Blueprint") || ClassFilter == TEXT("UBlueprint"))
		{
			Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
			Filter.bRecursiveClasses = true;
		}
		else
		{
			// Try to find the class by name
			UClass* FilterClass = FindFirstObject<UClass>(*ClassFilter, EFindFirstObjectOptions::ExactClass, ELogVerbosity::Warning, TEXT("FListAssetsAction"));
			if (!FilterClass)
			{
				// Try with U prefix
				FilterClass = FindFirstObject<UClass>(*(FString(TEXT("U")) + ClassFilter), EFindFirstObjectOptions::ExactClass, ELogVerbosity::Warning, TEXT("FListAssetsAction"));
			}
			if (FilterClass)
			{
				Filter.ClassPaths.Add(FilterClass->GetClassPathName());
				Filter.bRecursiveClasses = true;
			}
		}
	}

	TArray<FAssetData> AssetList;
	AssetRegistry.GetAssets(Filter, AssetList);

	TArray<TSharedPtr<FJsonValue>> AssetsArray;
	int32 Count = 0;

	for (const FAssetData& AssetData : AssetList)
	{
		if (Count >= MaxResults) break;

		FString AssetName = AssetData.AssetName.ToString();

		// Name filter
		if (!NameContains.IsEmpty() && !AssetName.Contains(NameContains))
		{
			continue;
		}

		TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
		AssetObj->SetStringField(TEXT("asset_name"), AssetName);
		AssetObj->SetStringField(TEXT("asset_path"), AssetData.GetObjectPathString());
		AssetObj->SetStringField(TEXT("package_path"), AssetData.PackagePath.ToString());
		AssetObj->SetStringField(TEXT("asset_class"), AssetData.AssetClassPath.GetAssetName().ToString());

		AssetsArray.Add(MakeShared<FJsonValueObject>(AssetObj));
		Count++;
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("count"), Count);
	Result->SetNumberField(TEXT("total_unfiltered"), AssetList.Num());
	Result->SetStringField(TEXT("path"), Path);
	Result->SetArrayField(TEXT("assets"), AssetsArray);

	return CreateSuccessResponse(Result);
}


// ========================================================================
// FRenameAssetsAction
// ========================================================================

bool FRenameAssetsAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	const bool bHasBatch = Params->HasField(TEXT("items"));
	const bool bHasSingle = Params->HasField(TEXT("old_asset_path"));

	if (!bHasBatch && !bHasSingle)
	{
		OutError = TEXT("Provide either 'items' for batch rename or single fields 'old_asset_path', 'new_package_path', 'new_name'");
		return false;
	}

	if (bHasBatch)
	{
		const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
		if (!Params->TryGetArrayField(TEXT("items"), ItemsArray) || !ItemsArray || ItemsArray->Num() == 0)
		{
			OutError = TEXT("'items' must be a non-empty array");
			return false;
		}

		for (int32 Index = 0; Index < ItemsArray->Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& ItemValue = (*ItemsArray)[Index];
			if (!ItemValue.IsValid() || ItemValue->Type != EJson::Object)
			{
				OutError = FString::Printf(TEXT("items[%d] must be an object"), Index);
				return false;
			}

			const TSharedPtr<FJsonObject> ItemObject = ItemValue->AsObject();
			if (!ItemObject.IsValid())
			{
				OutError = FString::Printf(TEXT("items[%d] must be an object"), Index);
				return false;
			}

			FString OldAssetPath;
			FString NewPackagePath;
			FString NewName;
			if (!ItemObject->TryGetStringField(TEXT("old_asset_path"), OldAssetPath) || OldAssetPath.IsEmpty())
			{
				OutError = FString::Printf(TEXT("items[%d].old_asset_path is required"), Index);
				return false;
			}
			if (!ItemObject->TryGetStringField(TEXT("new_package_path"), NewPackagePath) || NewPackagePath.IsEmpty())
			{
				OutError = FString::Printf(TEXT("items[%d].new_package_path is required"), Index);
				return false;
			}
			if (!ItemObject->TryGetStringField(TEXT("new_name"), NewName) || NewName.IsEmpty())
			{
				OutError = FString::Printf(TEXT("items[%d].new_name is required"), Index);
				return false;
			}
		}

		return true;
	}

	FString OldAssetPath;
	FString NewPackagePath;
	FString NewName;
	if (!GetRequiredString(Params, TEXT("old_asset_path"), OldAssetPath, OutError))
	{
		return false;
	}
	if (!GetRequiredString(Params, TEXT("new_package_path"), NewPackagePath, OutError))
	{
		return false;
	}
	if (!GetRequiredString(Params, TEXT("new_name"), NewName, OutError))
	{
		return false;
	}

	return true;
}

TSharedPtr<FJsonObject> FRenameAssetsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	struct FRenameRequestItem
	{
		FString OldAssetPath;
		FString NewPackagePath;
		FString NewName;
	};

	TArray<FRenameRequestItem> RequestItems;

	if (Params->HasField(TEXT("items")))
	{
		const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
		Params->TryGetArrayField(TEXT("items"), ItemsArray);
		if (ItemsArray)
		{
			for (const TSharedPtr<FJsonValue>& ItemValue : *ItemsArray)
			{
				if (!ItemValue.IsValid() || ItemValue->Type != EJson::Object)
				{
					continue;
				}

				const TSharedPtr<FJsonObject> ItemObject = ItemValue->AsObject();
				if (!ItemObject.IsValid())
				{
					continue;
				}

				FRenameRequestItem RequestItem;
				if (!ItemObject->TryGetStringField(TEXT("old_asset_path"), RequestItem.OldAssetPath))
				{
					continue;
				}
				if (!ItemObject->TryGetStringField(TEXT("new_package_path"), RequestItem.NewPackagePath))
				{
					continue;
				}
				if (!ItemObject->TryGetStringField(TEXT("new_name"), RequestItem.NewName))
				{
					continue;
				}

				RequestItems.Add(RequestItem);
			}
		}
	}
	else
	{
		FRenameRequestItem RequestItem;
		FString Error;
		GetRequiredString(Params, TEXT("old_asset_path"), RequestItem.OldAssetPath, Error);
		GetRequiredString(Params, TEXT("new_package_path"), RequestItem.NewPackagePath, Error);
		GetRequiredString(Params, TEXT("new_name"), RequestItem.NewName, Error);
		RequestItems.Add(RequestItem);
	}

	if (RequestItems.Num() == 0)
	{
		return CreateErrorResponse(TEXT("No valid rename items were provided"), TEXT("invalid_params"));
	}

	const bool bAutoFixupRedirectors = GetOptionalBool(Params, TEXT("auto_fixup_redirectors"), true);
	const bool bAllowUIPrompts = GetOptionalBool(Params, TEXT("allow_ui_prompts"), false);
	const bool bRequestedCheckoutDialogPrompt = GetOptionalBool(Params, TEXT("checkout_dialog_prompt"), false);
	const FString FixupModeRaw = GetOptionalString(Params, TEXT("fixup_mode"), TEXT("delete")).ToLower();

	ERedirectFixupMode FixupMode = ERedirectFixupMode::DeleteFixedUpRedirectors;
	if (FixupModeRaw == TEXT("leave"))
	{
		FixupMode = ERedirectFixupMode::LeaveFixedUpRedirectors;
	}
	else if (FixupModeRaw == TEXT("prompt"))
	{
		FixupMode = ERedirectFixupMode::PromptForDeletingRedirectors;
	}

	if (!bAllowUIPrompts && FixupMode == ERedirectFixupMode::PromptForDeletingRedirectors)
	{
		UE_LOG(LogMCP, Warning, TEXT("rename_assets: fixup_mode='prompt' requested, but allow_ui_prompts=false; forcing fixup_mode='delete' for non-interactive execution"));
		FixupMode = ERedirectFixupMode::DeleteFixedUpRedirectors;
	}

	const bool bEffectiveCheckoutDialogPrompt = bAllowUIPrompts && bRequestedCheckoutDialogPrompt;

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	TArray<FAssetRenameData> RenameDataList;
	RenameDataList.Reserve(RequestItems.Num());

	TArray<FString> OldObjectPaths;
	OldObjectPaths.Reserve(RequestItems.Num());

	for (const FRenameRequestItem& RequestItem : RequestItems)
	{
		if (!FPackageName::IsValidLongPackageName(RequestItem.NewPackagePath))
		{
			return CreateErrorResponse(
				FString::Printf(TEXT("Invalid new_package_path: %s"), *RequestItem.NewPackagePath),
				TEXT("invalid_package_path")
			);
		}

		UObject* Asset = LoadObject<UObject>(nullptr, *RequestItem.OldAssetPath);
		if (!Asset)
		{
			return CreateErrorResponse(
				FString::Printf(TEXT("Asset not found: %s"), *RequestItem.OldAssetPath),
				TEXT("asset_not_found")
			);
		}

		OldObjectPaths.Add(Asset->GetPathName());
		RenameDataList.Emplace(Asset, RequestItem.NewPackagePath, RequestItem.NewName);
	}

	const bool bRenameSucceeded = AssetTools.RenameAssets(RenameDataList);
	if (!bRenameSucceeded)
	{
		return CreateErrorResponse(TEXT("RenameAssets failed. Check destination path/name conflicts and source-control state."), TEXT("rename_failed"));
	}

	int32 FoundRedirectorCount = 0;
	int32 FixedRedirectorCount = 0;
	int32 SilentlyDeletedRedirectorCount = 0;
	int32 KeptRedirectorCount = 0;
	TArray<UObjectRedirector*> RedirectorsToFix;
	TArray<TSharedPtr<FJsonValue>> KeptRedirectorsArray;

	if (bAutoFixupRedirectors)
	{
		for (const FString& OldObjectPath : OldObjectPaths)
		{
			UObject* LoadedObject = LoadObject<UObject>(nullptr, *OldObjectPath);
			UObjectRedirector* Redirector = Cast<UObjectRedirector>(LoadedObject);
			if (Redirector)
			{
				RedirectorsToFix.Add(Redirector);
				FoundRedirectorCount++;
			}
		}

		if (RedirectorsToFix.Num() > 0)
		{
			if (bAllowUIPrompts)
			{
				AssetTools.FixupReferencers(RedirectorsToFix, bEffectiveCheckoutDialogPrompt, FixupMode);
				FixedRedirectorCount = RedirectorsToFix.Num();
			}
			else
			{
				FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
				IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

				for (const FString& OldObjectPath : OldObjectPaths)
				{
					const FString OldPackagePath = FPackageName::ObjectPathToPackageName(OldObjectPath);
					if (OldPackagePath.IsEmpty())
					{
						continue;
					}

					TArray<FName> Referencers;
					AssetRegistry.GetReferencers(FName(*OldPackagePath), Referencers);

					int32 ExternalReferencerCount = 0;
					for (const FName& Referencer : Referencers)
					{
						if (Referencer.ToString() != OldPackagePath)
						{
							ExternalReferencerCount++;
						}
					}

					if (ExternalReferencerCount == 0)
					{
						if (UEditorAssetLibrary::DoesAssetExist(OldPackagePath) && UEditorAssetLibrary::DeleteAsset(OldPackagePath))
						{
							SilentlyDeletedRedirectorCount++;
						}
						else
						{
							KeptRedirectorCount++;
							TSharedPtr<FJsonObject> KeptObj = MakeShared<FJsonObject>();
							KeptObj->SetStringField(TEXT("redirector_package"), OldPackagePath);
							KeptObj->SetStringField(TEXT("reason"), TEXT("delete_failed"));
							KeptRedirectorsArray.Add(MakeShared<FJsonValueObject>(KeptObj));
						}
					}
					else
					{
						KeptRedirectorCount++;
						TSharedPtr<FJsonObject> KeptObj = MakeShared<FJsonObject>();
						KeptObj->SetStringField(TEXT("redirector_package"), OldPackagePath);
						KeptObj->SetStringField(TEXT("reason"), TEXT("still_referenced"));
						KeptObj->SetNumberField(TEXT("referencer_count"), ExternalReferencerCount);
						KeptRedirectorsArray.Add(MakeShared<FJsonValueObject>(KeptObj));
					}
				}
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> RenamedItemsArray;
	for (const FAssetRenameData& RenameData : RenameDataList)
	{
		TSharedPtr<FJsonObject> ItemObject = MakeShared<FJsonObject>();
		ItemObject->SetStringField(TEXT("old_asset_path"), RenameData.OldObjectPath.ToString());
		ItemObject->SetStringField(TEXT("new_asset_path"), RenameData.NewObjectPath.ToString());
		ItemObject->SetStringField(TEXT("new_package_path"), RenameData.NewPackagePath);
		ItemObject->SetStringField(TEXT("new_name"), RenameData.NewName);
		RenamedItemsArray.Add(MakeShared<FJsonValueObject>(ItemObject));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("renamed_count"), RenameDataList.Num());
	Result->SetBoolField(TEXT("auto_fixup_redirectors"), bAutoFixupRedirectors);
	Result->SetBoolField(TEXT("allow_ui_prompts"), bAllowUIPrompts);
	Result->SetBoolField(TEXT("checkout_dialog_prompt_effective"), bEffectiveCheckoutDialogPrompt);
	Result->SetStringField(
		TEXT("fixup_mode_effective"),
		FixupMode == ERedirectFixupMode::LeaveFixedUpRedirectors
			? TEXT("leave")
			: (FixupMode == ERedirectFixupMode::PromptForDeletingRedirectors ? TEXT("prompt") : TEXT("delete"))
	);
	Result->SetNumberField(TEXT("redirectors_found"), FoundRedirectorCount);
	Result->SetNumberField(TEXT("redirectors_fixup_attempted"), FixedRedirectorCount);
	Result->SetNumberField(TEXT("redirectors_deleted_silently"), SilentlyDeletedRedirectorCount);
	Result->SetNumberField(TEXT("redirectors_kept"), KeptRedirectorCount);
	Result->SetArrayField(TEXT("kept_redirectors"), KeptRedirectorsArray);
	Result->SetArrayField(TEXT("renamed_items"), RenamedItemsArray);

	return CreateSuccessResponse(Result);
}


// ========================================================================
// FGetSelectedAssetThumbnailAction
// ========================================================================

bool FGetSelectedAssetThumbnailAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (Params->HasField(TEXT("size")))
	{
		const double SizeValue = Params->GetNumberField(TEXT("size"));
		if (SizeValue < 1.0)
		{
			OutError = TEXT("Parameter 'size' must be greater than 0");
			return false;
		}
	}

	auto ValidateStringArrayField = [&Params, &OutError](const TCHAR* FieldName) -> bool
	{
		if (!Params->HasField(FieldName))
		{
			return true;
		}

		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Params->TryGetArrayField(FieldName, Values) || !Values)
		{
			OutError = FString::Printf(TEXT("'%s' must be an array of strings"), FieldName);
			return false;
		}

		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			FString Parsed;
			if (!Value.IsValid() || !Value->TryGetString(Parsed) || Parsed.IsEmpty())
			{
				OutError = FString::Printf(TEXT("'%s' must contain only non-empty strings"), FieldName);
				return false;
			}
		}

		return true;
	};

	if (!ValidateStringArrayField(TEXT("asset_paths"))) return false;
	if (!ValidateStringArrayField(TEXT("asset_ids"))) return false;
	if (!ValidateStringArrayField(TEXT("ids"))) return false;

	return true;
}

TSharedPtr<FJsonObject> FGetSelectedAssetThumbnailAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	const int32 RequestedSize = Params->HasField(TEXT("size"))
		? static_cast<int32>(Params->GetNumberField(TEXT("size")))
		: 256;
	const int32 ThumbnailSize = FMath::Clamp(RequestedSize, 1, 256);

	TArray<FString> TargetAssetPaths;

	auto CollectStringArrayField = [&Params, &TargetAssetPaths](const TCHAR* FieldName)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Params->TryGetArrayField(FieldName, Values) || !Values)
		{
			return;
		}

		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			FString Parsed;
			if (Value.IsValid() && Value->TryGetString(Parsed) && !Parsed.IsEmpty())
			{
				TargetAssetPaths.Add(Parsed);
			}
		}
	};

	if (Params->HasField(TEXT("asset_path")))
	{
		const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
		if (!AssetPath.IsEmpty())
		{
			TargetAssetPaths.Add(AssetPath);
		}
	}

	CollectStringArrayField(TEXT("asset_paths"));
	CollectStringArrayField(TEXT("asset_ids"));
	CollectStringArrayField(TEXT("ids"));

	TArray<FString> UniqueTargetAssetPaths;
	for (const FString& PathItem : TargetAssetPaths)
	{
		if (!PathItem.IsEmpty())
		{
			UniqueTargetAssetPaths.AddUnique(PathItem);
		}
	}
	TargetAssetPaths = MoveTemp(UniqueTargetAssetPaths);

	bool bFromSelection = false;
	int32 SelectedCount = 0;

	if (TargetAssetPaths.IsEmpty())
	{
		TArray<FAssetData> SelectedAssets;
		AssetSelectionUtils::GetSelectedAssets(SelectedAssets);
		SelectedCount = SelectedAssets.Num();

		if (SelectedAssets.IsEmpty())
		{
			return CreateErrorResponse(
				TEXT("No selected asset found in Content Browser. Select assets or provide asset_path/asset_paths/asset_ids/ids."),
				TEXT("no_selection")
			);
		}

		for (const FAssetData& SelectedAsset : SelectedAssets)
		{
			TargetAssetPaths.Add(SelectedAsset.GetObjectPathString());
		}
		bFromSelection = true;
	}

	TArray<TSharedPtr<FJsonValue>> ThumbnailItems;
	int32 SucceededCount = 0;
	int32 FailedCount = 0;

	for (const FString& AssetPath : TargetAssetPaths)
	{
		TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("requested_asset"), AssetPath);
		Item->SetStringField(TEXT("mime_type"), TEXT("image/png"));
		Item->SetStringField(TEXT("image_format"), TEXT("png"));

		UObject* TargetObject = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
		if (!TargetObject)
		{
			Item->SetBoolField(TEXT("success"), false);
			Item->SetStringField(TEXT("error"), TEXT("asset_not_found"));
			Item->SetStringField(TEXT("error_message"), FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
			ThumbnailItems.Add(MakeShared<FJsonValueObject>(Item));
			++FailedCount;
			continue;
		}

		FObjectThumbnail RenderedThumbnail;
		ThumbnailTools::RenderThumbnail(
			TargetObject,
			static_cast<uint32>(ThumbnailSize),
			static_cast<uint32>(ThumbnailSize),
			ThumbnailTools::EThumbnailTextureFlushMode::NeverFlush,
			nullptr,
			&RenderedThumbnail
		);

		if (RenderedThumbnail.IsEmpty() || !RenderedThumbnail.HasValidImageData())
		{
			Item->SetBoolField(TEXT("success"), false);
			Item->SetStringField(TEXT("error"), TEXT("thumbnail_render_failed"));
			Item->SetStringField(TEXT("error_message"), FString::Printf(TEXT("Failed to render thumbnail: %s"), *TargetObject->GetPathName()));
			ThumbnailItems.Add(MakeShared<FJsonValueObject>(Item));
			++FailedCount;
			continue;
		}

		const int32 ImageWidth = RenderedThumbnail.GetImageWidth();
		const int32 ImageHeight = RenderedThumbnail.GetImageHeight();
		if (ImageWidth <= 0 || ImageHeight <= 0)
		{
			Item->SetBoolField(TEXT("success"), false);
			Item->SetStringField(TEXT("error"), TEXT("invalid_thumbnail"));
			Item->SetStringField(TEXT("error_message"), TEXT("Rendered thumbnail has invalid dimensions"));
			ThumbnailItems.Add(MakeShared<FJsonValueObject>(Item));
			++FailedCount;
			continue;
		}

		const TArray<uint8>& RawImageBytes = RenderedThumbnail.GetUncompressedImageData();
		const int64 PixelCount = static_cast<int64>(ImageWidth) * static_cast<int64>(ImageHeight);
		const int64 ExpectedBytes = PixelCount * static_cast<int64>(sizeof(FColor));
		if (RawImageBytes.Num() < ExpectedBytes)
		{
			Item->SetBoolField(TEXT("success"), false);
			Item->SetStringField(TEXT("error"), TEXT("invalid_thumbnail_buffer"));
			Item->SetStringField(TEXT("error_message"), TEXT("Rendered thumbnail buffer is smaller than expected"));
			ThumbnailItems.Add(MakeShared<FJsonValueObject>(Item));
			++FailedCount;
			continue;
		}

		TArray<FColor> ColorPixels;
		ColorPixels.SetNumUninitialized(static_cast<int32>(PixelCount));
		FMemory::Memcpy(ColorPixels.GetData(), RawImageBytes.GetData(), static_cast<SIZE_T>(ExpectedBytes));

		TArray<uint8> CompressedPngBytes;
		FImageUtils::ThumbnailCompressImageArray(ImageWidth, ImageHeight, ColorPixels, CompressedPngBytes);

		if (CompressedPngBytes.IsEmpty())
		{
			Item->SetBoolField(TEXT("success"), false);
			Item->SetStringField(TEXT("error"), TEXT("png_compress_failed"));
			Item->SetStringField(TEXT("error_message"), TEXT("Failed to compress thumbnail to PNG"));
			ThumbnailItems.Add(MakeShared<FJsonValueObject>(Item));
			++FailedCount;
			continue;
		}

		Item->SetBoolField(TEXT("success"), true);
		Item->SetStringField(TEXT("asset_name"), TargetObject->GetName());
		Item->SetStringField(TEXT("asset_path"), TargetObject->GetPathName());
		Item->SetStringField(TEXT("asset_class"), TargetObject->GetClass() ? TargetObject->GetClass()->GetName() : TEXT("Unknown"));
		Item->SetNumberField(TEXT("width"), ImageWidth);
		Item->SetNumberField(TEXT("height"), ImageHeight);
		Item->SetNumberField(TEXT("image_byte_size"), CompressedPngBytes.Num());
		Item->SetStringField(TEXT("image_base64"), FBase64::Encode(CompressedPngBytes));

		ThumbnailItems.Add(MakeShared<FJsonValueObject>(Item));
		++SucceededCount;
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("from_selection"), bFromSelection);
	Result->SetNumberField(TEXT("selected_count"), SelectedCount);
	Result->SetNumberField(TEXT("requested_size"), RequestedSize);
	Result->SetNumberField(TEXT("applied_size"), ThumbnailSize);
	Result->SetNumberField(TEXT("requested_assets"), TargetAssetPaths.Num());
	Result->SetNumberField(TEXT("succeeded"), SucceededCount);
	Result->SetNumberField(TEXT("failed"), FailedCount);
	Result->SetArrayField(TEXT("thumbnails"), ThumbnailItems);

	if (ThumbnailItems.Num() > 0)
	{
		const TSharedPtr<FJsonObject>* FirstItem = nullptr;
		if (ThumbnailItems[0]->TryGetObject(FirstItem) && FirstItem && (*FirstItem)->GetBoolField(TEXT("success")))
		{
			// Backward compatibility fields for existing single-item consumers.
			Result->SetStringField(TEXT("asset_name"), (*FirstItem)->GetStringField(TEXT("asset_name")));
			Result->SetStringField(TEXT("asset_path"), (*FirstItem)->GetStringField(TEXT("asset_path")));
			Result->SetStringField(TEXT("asset_class"), (*FirstItem)->GetStringField(TEXT("asset_class")));
			Result->SetNumberField(TEXT("width"), (*FirstItem)->GetNumberField(TEXT("width")));
			Result->SetNumberField(TEXT("height"), (*FirstItem)->GetNumberField(TEXT("height")));
			Result->SetStringField(TEXT("image_format"), TEXT("png"));
			Result->SetStringField(TEXT("mime_type"), TEXT("image/png"));
			Result->SetNumberField(TEXT("image_byte_size"), (*FirstItem)->GetNumberField(TEXT("image_byte_size")));
			Result->SetStringField(TEXT("image_base64"), (*FirstItem)->GetStringField(TEXT("image_base64")));
		}
	}

	return CreateSuccessResponse(Result);
}


// ========================================================================
// FGetSelectedAssetsAction
// ========================================================================

TSharedPtr<FJsonObject> FGetSelectedAssetsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	TArray<FAssetData> SelectedAssets;
	AssetSelectionUtils::GetSelectedAssets(SelectedAssets);

	if (SelectedAssets.IsEmpty())
	{
		return CreateErrorResponse(
			TEXT("No assets are currently selected in the Content Browser."),
			TEXT("no_selection")
		);
	}

	TArray<TSharedPtr<FJsonValue>> AssetsArray;

	for (const FAssetData& AssetData : SelectedAssets)
	{
		TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
		AssetObj->SetStringField(TEXT("asset_name"), AssetData.AssetName.ToString());
		AssetObj->SetStringField(TEXT("asset_path"), AssetData.GetObjectPathString());
		AssetObj->SetStringField(TEXT("package_name"), AssetData.PackageName.ToString());
		AssetObj->SetStringField(TEXT("package_path"), AssetData.PackagePath.ToString());
		AssetObj->SetStringField(TEXT("asset_class"), AssetData.AssetClassPath.GetAssetName().ToString());
		AssetObj->SetStringField(TEXT("asset_class_path"), AssetData.AssetClassPath.ToString());
		AssetsArray.Add(MakeShared<FJsonValueObject>(AssetObj));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("count"), SelectedAssets.Num());
	Result->SetArrayField(TEXT("assets"), AssetsArray);

	return CreateSuccessResponse(Result);
}


// ========================================================================
// FGetBlueprintSummaryAction
// ========================================================================

bool FGetBlueprintSummaryAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("blueprint_name")) && !Params->HasField(TEXT("asset_path")))
	{
		OutError = TEXT("Missing required 'blueprint_name' or 'asset_path' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FGetBlueprintSummaryAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UBlueprint* Blueprint = nullptr;

	// Load blueprint by name or path
	if (Params->HasField(TEXT("asset_path")))
	{
		FString AssetPath = Params->GetStringField(TEXT("asset_path"));
		Blueprint = Cast<UBlueprint>(StaticLoadObject(UBlueprint::StaticClass(), nullptr, *AssetPath));
	}
	
	if (!Blueprint && Params->HasField(TEXT("blueprint_name")))
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint_name"));
		Blueprint = FMCPCommonUtils::FindBlueprint(BlueprintName);
	}

	if (!Blueprint)
	{
		return CreateErrorResponse(TEXT("Blueprint not found"));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
	Result->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());

	// Parent class
	if (Blueprint->ParentClass)
	{
		Result->SetStringField(TEXT("parent_class"), Blueprint->ParentClass->GetName());
		Result->SetStringField(TEXT("parent_class_path"), Blueprint->ParentClass->GetPathName());
	}

	// Blueprint type
	FString TypeStr;
	switch (Blueprint->BlueprintType)
	{
		case BPTYPE_Normal: TypeStr = TEXT("Normal"); break;
		case BPTYPE_Const: TypeStr = TEXT("Const"); break;
		case BPTYPE_MacroLibrary: TypeStr = TEXT("MacroLibrary"); break;
		case BPTYPE_Interface: TypeStr = TEXT("Interface"); break;
		case BPTYPE_LevelScript: TypeStr = TEXT("LevelScript"); break;
		case BPTYPE_FunctionLibrary: TypeStr = TEXT("FunctionLibrary"); break;
		default: TypeStr = TEXT("Unknown"); break;
	}
	Result->SetStringField(TEXT("blueprint_type"), TypeStr);

	// Compile status
	FString CompileStatus;
	switch (Blueprint->Status)
	{
		case BS_UpToDate: CompileStatus = TEXT("UpToDate"); break;
		case BS_Dirty: CompileStatus = TEXT("Dirty"); break;
		case BS_Error: CompileStatus = TEXT("Error"); break;
		case BS_BeingCreated: CompileStatus = TEXT("BeingCreated"); break;
		default: CompileStatus = TEXT("Unknown"); break;
	}
	Result->SetStringField(TEXT("compile_status"), CompileStatus);

	// ---- Variables ----
	TArray<TSharedPtr<FJsonValue>> VarsArray;
	for (const FBPVariableDescription& VarDesc : Blueprint->NewVariables)
	{
		TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
		VarObj->SetStringField(TEXT("name"), VarDesc.VarName.ToString());
		VarObj->SetStringField(TEXT("type"), VarDesc.VarType.PinCategory.ToString());

		if (VarDesc.VarType.PinSubCategoryObject.IsValid())
		{
			VarObj->SetStringField(TEXT("sub_type"), VarDesc.VarType.PinSubCategoryObject->GetName());
		}

		// Container type
		if (VarDesc.VarType.IsArray())
		{
			VarObj->SetStringField(TEXT("container"), TEXT("Array"));
		}
		else if (VarDesc.VarType.IsSet())
		{
			VarObj->SetStringField(TEXT("container"), TEXT("Set"));
		}
		else if (VarDesc.VarType.IsMap())
		{
			VarObj->SetStringField(TEXT("container"), TEXT("Map"));
		}

		VarObj->SetBoolField(TEXT("is_instance_editable"), VarDesc.PropertyFlags & CPF_Edit ? true : false);
		VarObj->SetBoolField(TEXT("is_blueprint_read_only"), VarDesc.PropertyFlags & CPF_BlueprintReadOnly ? true : false);

		if (!VarDesc.Category.IsEmpty())
		{
			VarObj->SetStringField(TEXT("category"), VarDesc.Category.ToString());
		}
		if (!VarDesc.DefaultValue.IsEmpty())
		{
			VarObj->SetStringField(TEXT("default_value"), VarDesc.DefaultValue);
		}

		VarsArray.Add(MakeShared<FJsonValueObject>(VarObj));
	}
	Result->SetArrayField(TEXT("variables"), VarsArray);

	// ---- Functions / Graphs ----
	TArray<TSharedPtr<FJsonValue>> FunctionsArray;
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (!Graph) continue;
		TSharedPtr<FJsonObject> FuncObj = MakeShared<FJsonObject>();
		FuncObj->SetStringField(TEXT("name"), Graph->GetName());
		FuncObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());

		// Try to get access specifier and descriptions from function entry
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_FunctionEntry* FuncEntry = Cast<UK2Node_FunctionEntry>(Node))
			{
				// Collect parameter pins
				TArray<TSharedPtr<FJsonValue>> ParamsArray;
				for (UEdGraphPin* Pin : FuncEntry->Pins)
				{
					if (Pin && Pin->Direction == EGPD_Output && Pin->PinName != UEdGraphSchema_K2::PN_Then)
					{
						TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
						PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
						PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
						if (Pin->PinType.PinSubCategoryObject.IsValid())
						{
							PinObj->SetStringField(TEXT("sub_type"), Pin->PinType.PinSubCategoryObject->GetName());
						}
						ParamsArray.Add(MakeShared<FJsonValueObject>(PinObj));
					}
				}
				FuncObj->SetArrayField(TEXT("parameters"), ParamsArray);
				break;
			}
		}

		FunctionsArray.Add(MakeShared<FJsonValueObject>(FuncObj));
	}
	Result->SetArrayField(TEXT("functions"), FunctionsArray);

	// ---- Macros ----
	TArray<TSharedPtr<FJsonValue>> MacrosArray;
	for (UEdGraph* Graph : Blueprint->MacroGraphs)
	{
		if (!Graph) continue;
		TSharedPtr<FJsonObject> MacroObj = MakeShared<FJsonObject>();
		MacroObj->SetStringField(TEXT("name"), Graph->GetName());
		MacroObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
		MacrosArray.Add(MakeShared<FJsonValueObject>(MacroObj));
	}
	Result->SetArrayField(TEXT("macros"), MacrosArray);

	// ---- Event Graphs ----
	TArray<TSharedPtr<FJsonValue>> EventGraphsArray;
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (!Graph) continue;
		TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
		GraphObj->SetStringField(TEXT("name"), Graph->GetName());
		GraphObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());

		// Collect event nodes and key node types
		TArray<TSharedPtr<FJsonValue>> EventNodes;
		int32 FunctionCallCount = 0;
		int32 VarGetCount = 0;
		int32 VarSetCount = 0;
		int32 CustomEventCount = 0;

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
			{
				TSharedPtr<FJsonObject> EvObj = MakeShared<FJsonObject>();
				EvObj->SetStringField(TEXT("event_name"), EventNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
				EvObj->SetStringField(TEXT("node_id"), EventNode->NodeGuid.ToString());
				EventNodes.Add(MakeShared<FJsonValueObject>(EvObj));
			}
			else if (Cast<UK2Node_CustomEvent>(Node))
			{
				CustomEventCount++;
				TSharedPtr<FJsonObject> EvObj = MakeShared<FJsonObject>();
				EvObj->SetStringField(TEXT("event_name"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
				EvObj->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString());
				EvObj->SetStringField(TEXT("type"), TEXT("CustomEvent"));
				EventNodes.Add(MakeShared<FJsonValueObject>(EvObj));
			}
			else if (Cast<UK2Node_CallFunction>(Node))
			{
				FunctionCallCount++;
			}
			else if (Cast<UK2Node_VariableGet>(Node))
			{
				VarGetCount++;
			}
			else if (Cast<UK2Node_VariableSet>(Node))
			{
				VarSetCount++;
			}
		}

		GraphObj->SetArrayField(TEXT("events"), EventNodes);

		TSharedPtr<FJsonObject> StatsObj = MakeShared<FJsonObject>();
		StatsObj->SetNumberField(TEXT("function_calls"), FunctionCallCount);
		StatsObj->SetNumberField(TEXT("variable_gets"), VarGetCount);
		StatsObj->SetNumberField(TEXT("variable_sets"), VarSetCount);
		StatsObj->SetNumberField(TEXT("custom_events"), CustomEventCount);
		GraphObj->SetObjectField(TEXT("stats"), StatsObj);

		EventGraphsArray.Add(MakeShared<FJsonValueObject>(GraphObj));
	}
	Result->SetArrayField(TEXT("event_graphs"), EventGraphsArray);

	// ---- Components (from SCS) ----
	TArray<TSharedPtr<FJsonValue>> ComponentsArray;
	if (Blueprint->SimpleConstructionScript)
	{
		TArray<USCS_Node*> AllNodes = Blueprint->SimpleConstructionScript->GetAllNodes();
		for (USCS_Node* SCSNode : AllNodes)
		{
			if (!SCSNode) continue;
			TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
			CompObj->SetStringField(TEXT("name"), SCSNode->GetVariableName().ToString());
			if (SCSNode->ComponentClass)
			{
				CompObj->SetStringField(TEXT("class"), SCSNode->ComponentClass->GetName());
			}
			// Parent info via ParentComponentOrVariableName
			if (!SCSNode->ParentComponentOrVariableName.IsNone())
			{
				CompObj->SetStringField(TEXT("parent"), SCSNode->ParentComponentOrVariableName.ToString());
			}
			ComponentsArray.Add(MakeShared<FJsonValueObject>(CompObj));
		}
	}
	Result->SetArrayField(TEXT("components"), ComponentsArray);

	// ---- Interfaces ----
	TArray<TSharedPtr<FJsonValue>> InterfacesArray;
	for (const FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
	{
		if (InterfaceDesc.Interface)
		{
			InterfacesArray.Add(MakeShared<FJsonValueString>(InterfaceDesc.Interface->GetName()));
		}
	}
	Result->SetArrayField(TEXT("implemented_interfaces"), InterfacesArray);

	return CreateSuccessResponse(Result);
}


