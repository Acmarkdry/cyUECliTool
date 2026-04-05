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



// Helper to find actor by name
static AActor* FindActorByName(UWorld* World, const FString& ActorName)
{
	if (!World) return nullptr;

	TArray<AActor*> AllActors;
	UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);

	for (AActor* Actor : AllActors)
	{
		if (Actor && Actor->GetName() == ActorName)
		{
			return Actor;
		}
	}
	return nullptr;
}


// ============================================================================
// FGetActorsInLevelAction
// ============================================================================

TSharedPtr<FJsonObject> FGetActorsInLevelAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No editor world available"), TEXT("no_world"));
	}

	TArray<AActor*> AllActors;
	UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);

	TArray<TSharedPtr<FJsonValue>> ActorArray;
	for (AActor* Actor : AllActors)
	{
		if (Actor)
		{
			ActorArray.Add(FMCPCommonUtils::ActorToJsonValue(Actor));
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("actors"), ActorArray);
	return CreateSuccessResponse(Result);
}


// ============================================================================
// FFindActorsByNameAction
// ============================================================================

bool FFindActorsByNameAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Pattern;
	return GetRequiredString(Params, TEXT("pattern"), Pattern, OutError);
}

TSharedPtr<FJsonObject> FFindActorsByNameAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Pattern, Error;
	GetRequiredString(Params, TEXT("pattern"), Pattern, Error);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No editor world available"), TEXT("no_world"));
	}

	TArray<AActor*> AllActors;
	UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);

	TArray<TSharedPtr<FJsonValue>> MatchingActors;
	for (AActor* Actor : AllActors)
	{
		if (Actor && Actor->GetName().Contains(Pattern))
		{
			MatchingActors.Add(FMCPCommonUtils::ActorToJsonValue(Actor));
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("actors"), MatchingActors);
	return CreateSuccessResponse(Result);
}


// ============================================================================
// FSpawnActorAction
// ============================================================================

bool FSpawnActorAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Name, Type;
	if (!GetRequiredString(Params, TEXT("name"), Name, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("type"), Type, OutError)) return false;
	return true;
}

TSharedPtr<FJsonObject> FSpawnActorAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString ActorName, ActorType, Error;
	GetRequiredString(Params, TEXT("name"), ActorName, Error);
	GetRequiredString(Params, TEXT("type"), ActorType, Error);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No editor world available"), TEXT("no_world"));
	}

	// Resolve actor class
	UClass* ActorClass = ResolveActorClass(ActorType);
	if (!ActorClass)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Unknown actor type: %s"), *ActorType),
			TEXT("invalid_type")
		);
	}

	// Delete existing actor with same name
	AActor* Existing = FindActorByName(World, ActorName);
	if (Existing)
	{
		World->DestroyActor(Existing);
	}

	// Parse transform
	FVector Location = FMCPCommonUtils::GetVectorFromJson(Params, TEXT("location"));
	FRotator Rotation = FMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation"));
	FVector Scale = Params->HasField(TEXT("scale")) ?
		FMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale")) : FVector(1, 1, 1);

	// Spawn
	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = FName(*ActorName);
	SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;

	AActor* NewActor = World->SpawnActor<AActor>(ActorClass, Location, Rotation, SpawnParams);
	if (!NewActor)
	{
		return CreateErrorResponse(TEXT("Failed to spawn actor"), TEXT("spawn_failed"));
	}

	NewActor->SetActorScale3D(Scale);
	NewActor->SetActorLabel(*ActorName);
	Context.LastCreatedActorName = ActorName;

	// Mark level dirty so auto-save works
	Context.MarkPackageDirty(World->GetOutermost());

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Spawned actor '%s' of type '%s'"), *ActorName, *ActorType);

	return CreateSuccessResponse(FMCPCommonUtils::ActorToJsonObject(NewActor));
}

UClass* FSpawnActorAction::ResolveActorClass(const FString& TypeName) const
{
	if (TypeName == TEXT("StaticMeshActor")) return AStaticMeshActor::StaticClass();
	if (TypeName == TEXT("PointLight")) return APointLight::StaticClass();
	if (TypeName == TEXT("SpotLight")) return ASpotLight::StaticClass();
	if (TypeName == TEXT("DirectionalLight")) return ADirectionalLight::StaticClass();
	if (TypeName == TEXT("CameraActor")) return ACameraActor::StaticClass();
	if (TypeName == TEXT("Actor")) return AActor::StaticClass();
	return nullptr;
}


// ============================================================================
// FDeleteActorAction
// ============================================================================

bool FDeleteActorAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Name;
	return GetRequiredString(Params, TEXT("name"), Name, OutError);
}

TSharedPtr<FJsonObject> FDeleteActorAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString ActorName, Error;
	GetRequiredString(Params, TEXT("name"), ActorName, Error);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No editor world available"), TEXT("no_world"));
	}

	AActor* Actor = FindActorByName(World, ActorName);
	if (!Actor)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Actor not found: %s"), *ActorName),
			TEXT("not_found")
		);
	}

	// Store info before deletion
	TSharedPtr<FJsonObject> ActorInfo = FMCPCommonUtils::ActorToJsonObject(Actor);

	// Use editor-proper destruction which handles World Partition external actors
	bool bDestroyed = World->EditorDestroyActor(Actor, true);
	if (!bDestroyed)
	{
		// Fallback to regular destroy
		Actor->Destroy();
	}

	// Mark world dirty
	Context.MarkPackageDirty(World->GetOutermost());

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Deleted actor '%s'"), *ActorName);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetObjectField(TEXT("deleted_actor"), ActorInfo);
	return CreateSuccessResponse(Result);
}


// ============================================================================
// FSetActorTransformAction
// ============================================================================

bool FSetActorTransformAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Name;
	return GetRequiredString(Params, TEXT("name"), Name, OutError);
}

TSharedPtr<FJsonObject> FSetActorTransformAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString ActorName, Error;
	GetRequiredString(Params, TEXT("name"), ActorName, Error);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No editor world available"), TEXT("no_world"));
	}

	AActor* Actor = FindActorByName(World, ActorName);
	if (!Actor)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Actor not found: %s"), *ActorName),
			TEXT("not_found")
		);
	}

	// Update transform
	FTransform Transform = Actor->GetTransform();

	if (Params->HasField(TEXT("location")))
	{
		Transform.SetLocation(FMCPCommonUtils::GetVectorFromJson(Params, TEXT("location")));
	}
	if (Params->HasField(TEXT("rotation")))
	{
		Transform.SetRotation(FQuat(FMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation"))));
	}
	if (Params->HasField(TEXT("scale")))
	{
		Transform.SetScale3D(FMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale")));
	}

	Actor->SetActorTransform(Transform);

	// Mark level dirty so auto-save works
	Context.MarkPackageDirty(World->GetOutermost());

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Set transform on actor '%s'"), *ActorName);

	return CreateSuccessResponse(FMCPCommonUtils::ActorToJsonObject(Actor));
}


// ============================================================================
// FGetActorPropertiesAction
// ============================================================================

bool FGetActorPropertiesAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Name;
	return GetRequiredString(Params, TEXT("name"), Name, OutError);
}

// --- Property value serialization helper ---
TSharedPtr<FJsonValue> FGetActorPropertiesAction::PropertyValueToJson(FProperty* Property, const void* ValuePtr)
{
	if (!Property || !ValuePtr)
	{
		return MakeShared<FJsonValueNull>();
	}

	if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		return MakeShared<FJsonValueBoolean>(BoolProp->GetPropertyValue(ValuePtr));
	}
	else if (FIntProperty* IntProp = CastField<FIntProperty>(Property))
	{
		return MakeShared<FJsonValueNumber>(IntProp->GetPropertyValue(ValuePtr));
	}
	else if (FInt64Property* Int64Prop = CastField<FInt64Property>(Property))
	{
		return MakeShared<FJsonValueNumber>(static_cast<double>(Int64Prop->GetPropertyValue(ValuePtr)));
	}
	else if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Property))
	{
		return MakeShared<FJsonValueNumber>(FloatProp->GetPropertyValue(ValuePtr));
	}
	else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Property))
	{
		return MakeShared<FJsonValueNumber>(DoubleProp->GetPropertyValue(ValuePtr));
	}
	else if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
	{
		uint8 Val = ByteProp->GetPropertyValue(ValuePtr);
		UEnum* EnumDef = ByteProp->Enum;
		if (EnumDef)
		{
			FString EnumName = EnumDef->GetNameStringByValue(Val);
			return MakeShared<FJsonValueString>(EnumName);
		}
		return MakeShared<FJsonValueNumber>(Val);
	}
	else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
	{
		UEnum* EnumDef = EnumProp->GetEnum();
		FNumericProperty* UnderlyingProp = EnumProp->GetUnderlyingProperty();
		if (EnumDef && UnderlyingProp)
		{
			int64 Val = UnderlyingProp->GetSignedIntPropertyValue(ValuePtr);
			FString EnumName = EnumDef->GetNameStringByValue(Val);
			return MakeShared<FJsonValueString>(EnumName);
		}
		return MakeShared<FJsonValueNull>();
	}
	else if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
	{
		return MakeShared<FJsonValueString>(StrProp->GetPropertyValue(ValuePtr));
	}
	else if (FNameProperty* NameProp = CastField<FNameProperty>(Property))
	{
		return MakeShared<FJsonValueString>(NameProp->GetPropertyValue(ValuePtr).ToString());
	}
	else if (FTextProperty* TextProp = CastField<FTextProperty>(Property))
	{
		return MakeShared<FJsonValueString>(TextProp->GetPropertyValue(ValuePtr).ToString());
	}
	else if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		if (StructProp->Struct == TBaseStructure<FVector>::Get())
		{
			const FVector& Vec = *(const FVector*)ValuePtr;
			TArray<TSharedPtr<FJsonValue>> Arr;
			Arr.Add(MakeShared<FJsonValueNumber>(Vec.X));
			Arr.Add(MakeShared<FJsonValueNumber>(Vec.Y));
			Arr.Add(MakeShared<FJsonValueNumber>(Vec.Z));
			return MakeShared<FJsonValueArray>(Arr);
		}
		else if (StructProp->Struct == TBaseStructure<FRotator>::Get())
		{
			const FRotator& Rot = *(const FRotator*)ValuePtr;
			TArray<TSharedPtr<FJsonValue>> Arr;
			Arr.Add(MakeShared<FJsonValueNumber>(Rot.Pitch));
			Arr.Add(MakeShared<FJsonValueNumber>(Rot.Yaw));
			Arr.Add(MakeShared<FJsonValueNumber>(Rot.Roll));
			return MakeShared<FJsonValueArray>(Arr);
		}
		else if (StructProp->Struct == TBaseStructure<FLinearColor>::Get())
		{
			const FLinearColor& Color = *(const FLinearColor*)ValuePtr;
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetNumberField(TEXT("r"), Color.R);
			Obj->SetNumberField(TEXT("g"), Color.G);
			Obj->SetNumberField(TEXT("b"), Color.B);
			Obj->SetNumberField(TEXT("a"), Color.A);
			return MakeShared<FJsonValueObject>(Obj);
		}
		else if (StructProp->Struct == TBaseStructure<FColor>::Get())
		{
			const FColor& Color = *(const FColor*)ValuePtr;
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetNumberField(TEXT("r"), Color.R);
			Obj->SetNumberField(TEXT("g"), Color.G);
			Obj->SetNumberField(TEXT("b"), Color.B);
			Obj->SetNumberField(TEXT("a"), Color.A);
			return MakeShared<FJsonValueObject>(Obj);
		}
		else if (StructProp->Struct == TBaseStructure<FTransform>::Get())
		{
			const FTransform& Transform = *(const FTransform*)ValuePtr;
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			FVector Loc = Transform.GetLocation();
			FRotator Rot = Transform.Rotator();
			FVector Scale = Transform.GetScale3D();

			TArray<TSharedPtr<FJsonValue>> LocArr, RotArr, ScaleArr;
			LocArr.Add(MakeShared<FJsonValueNumber>(Loc.X));
			LocArr.Add(MakeShared<FJsonValueNumber>(Loc.Y));
			LocArr.Add(MakeShared<FJsonValueNumber>(Loc.Z));
			RotArr.Add(MakeShared<FJsonValueNumber>(Rot.Pitch));
			RotArr.Add(MakeShared<FJsonValueNumber>(Rot.Yaw));
			RotArr.Add(MakeShared<FJsonValueNumber>(Rot.Roll));
			ScaleArr.Add(MakeShared<FJsonValueNumber>(Scale.X));
			ScaleArr.Add(MakeShared<FJsonValueNumber>(Scale.Y));
			ScaleArr.Add(MakeShared<FJsonValueNumber>(Scale.Z));

			Obj->SetArrayField(TEXT("location"), LocArr);
			Obj->SetArrayField(TEXT("rotation"), RotArr);
			Obj->SetArrayField(TEXT("scale"), ScaleArr);
			return MakeShared<FJsonValueObject>(Obj);
		}
		else
		{
			// Generic struct: use ExportText
			FString ExportedValue;
			StructProp->ExportTextItem_Direct(ExportedValue, ValuePtr, nullptr, nullptr, PPF_None);
			return MakeShared<FJsonValueString>(ExportedValue);
		}
	}
	else if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Property))
	{
		UObject* ObjValue = ObjProp->GetObjectPropertyValue(ValuePtr);
		if (ObjValue)
		{
			return MakeShared<FJsonValueString>(ObjValue->GetPathName());
		}
		return MakeShared<FJsonValueString>(TEXT("None"));
	}
	else if (FClassProperty* ClassProp = CastField<FClassProperty>(Property))
	{
		UClass* ClassValue = Cast<UClass>(ClassProp->GetObjectPropertyValue(ValuePtr));
		if (ClassValue)
		{
			return MakeShared<FJsonValueString>(ClassValue->GetPathName());
		}
		return MakeShared<FJsonValueString>(TEXT("None"));
	}
	else if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
	{
		FScriptArrayHelper ArrayHelper(ArrayProp, ValuePtr);
		TArray<TSharedPtr<FJsonValue>> Arr;
		int32 Count = FMath::Min(ArrayHelper.Num(), 100); // Cap at 100 elements
		for (int32 i = 0; i < Count; ++i)
		{
			Arr.Add(PropertyValueToJson(ArrayProp->Inner, ArrayHelper.GetRawPtr(i)));
		}
		return MakeShared<FJsonValueArray>(Arr);
	}

	// Fallback: use ExportText
	FString ExportedValue;
	Property->ExportTextItem_Direct(ExportedValue, ValuePtr, nullptr, nullptr, PPF_None);
	return MakeShared<FJsonValueString>(ExportedValue);
}

// --- Property type string helper ---
FString FGetActorPropertiesAction::GetPropertyTypeString(FProperty* Property)
{
	if (!Property) return TEXT("Unknown");

	if (CastField<FBoolProperty>(Property)) return TEXT("Boolean");
	if (CastField<FIntProperty>(Property)) return TEXT("Int32");
	if (CastField<FInt64Property>(Property)) return TEXT("Int64");
	if (CastField<FFloatProperty>(Property)) return TEXT("Float");
	if (CastField<FDoubleProperty>(Property)) return TEXT("Double");
	if (CastField<FStrProperty>(Property)) return TEXT("String");
	if (CastField<FNameProperty>(Property)) return TEXT("Name");
	if (CastField<FTextProperty>(Property)) return TEXT("Text");

	if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
	{
		if (ByteProp->Enum) return FString::Printf(TEXT("Enum(%s)"), *ByteProp->Enum->GetName());
		return TEXT("Byte");
	}
	if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
	{
		if (EnumProp->GetEnum()) return FString::Printf(TEXT("Enum(%s)"), *EnumProp->GetEnum()->GetName());
		return TEXT("Enum");
	}
	if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		return FString::Printf(TEXT("Struct(%s)"), *StructProp->Struct->GetName());
	}
	if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Property))
	{
		return FString::Printf(TEXT("Object(%s)"), *ObjProp->PropertyClass->GetName());
	}
	if (FClassProperty* ClassProp = CastField<FClassProperty>(Property))
	{
		return FString::Printf(TEXT("Class(%s)"), *ClassProp->MetaClass->GetName());
	}
	if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
	{
		return FString::Printf(TEXT("Array(%s)"), *GetPropertyTypeString(ArrayProp->Inner));
	}
	if (FSetProperty* SetProp = CastField<FSetProperty>(Property))
	{
		return FString::Printf(TEXT("Set(%s)"), *GetPropertyTypeString(SetProp->ElementProp));
	}
	if (FMapProperty* MapProp = CastField<FMapProperty>(Property))
	{
		return FString::Printf(TEXT("Map(%s, %s)"),
			*GetPropertyTypeString(MapProp->KeyProp),
			*GetPropertyTypeString(MapProp->ValueProp));
	}
	if (CastField<FSoftObjectProperty>(Property)) return TEXT("SoftObjectReference");
	if (CastField<FSoftClassProperty>(Property)) return TEXT("SoftClassReference");
	if (CastField<FWeakObjectProperty>(Property)) return TEXT("WeakObjectReference");
	if (CastField<FInterfaceProperty>(Property)) return TEXT("Interface");
	if (CastField<FDelegateProperty>(Property)) return TEXT("Delegate");
	if (CastField<FMulticastDelegateProperty>(Property)) return TEXT("MulticastDelegate");

	return Property->GetCPPType();
}

TSharedPtr<FJsonObject> FGetActorPropertiesAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString ActorName, Error;
	GetRequiredString(Params, TEXT("name"), ActorName, Error);

	const bool bDetailed = GetOptionalBool(Params, TEXT("detailed"), false);
	const bool bEditableOnly = GetOptionalBool(Params, TEXT("editable_only"), false);
	const FString CategoryFilter = GetOptionalString(Params, TEXT("category"));

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No editor world available"), TEXT("no_world"));
	}

	AActor* Actor = FindActorByName(World, ActorName);
	if (!Actor)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Actor not found: %s"), *ActorName),
			TEXT("not_found")
		);
	}

	// Always include basic info (backward compatible)
	TSharedPtr<FJsonObject> Result = FMCPCommonUtils::ActorToJsonObject(Actor);

	if (bDetailed)
	{
		// Determine if this is a Blueprint-generated actor
		UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(Actor->GetClass());
		if (BPGC)
		{
			Result->SetBoolField(TEXT("is_blueprint_actor"), true);
			if (BPGC->ClassGeneratedBy)
			{
				Result->SetStringField(TEXT("blueprint_name"), BPGC->ClassGeneratedBy->GetName());
				Result->SetStringField(TEXT("blueprint_path"), BPGC->ClassGeneratedBy->GetPathName());
			}
		}
		else
		{
			Result->SetBoolField(TEXT("is_blueprint_actor"), false);
		}

		// Enumerate ALL FProperty fields on the actor's class
		TArray<TSharedPtr<FJsonValue>> PropertiesArray;
		UClass* ActorClass = Actor->GetClass();

		// Determine where native AActor properties end (to flag blueprint variables)
		UClass* NativeStopClass = AActor::StaticClass();

		for (TFieldIterator<FProperty> PropIt(ActorClass); PropIt; ++PropIt)
		{
			FProperty* Property = *PropIt;
			if (!Property) continue;

			// Filter: editable_only
			const bool bIsEditable = Property->HasAnyPropertyFlags(CPF_Edit);
			if (bEditableOnly && !bIsEditable) continue;

			// Filter: category
			FString PropCategory = Property->GetMetaData(TEXT("Category"));
			if (!CategoryFilter.IsEmpty() && !PropCategory.Contains(CategoryFilter)) continue;

			// Skip deprecated and transient unless explicitly asked
			if (Property->HasAnyPropertyFlags(CPF_Deprecated)) continue;

			// Determine if this is a Blueprint variable (defined in Generated class, not in native)
			bool bIsBlueprintVar = false;
			UClass* OwnerClass = Property->GetOwnerClass();
			if (OwnerClass && BPGC)
			{
				bIsBlueprintVar = OwnerClass->IsChildOf(BPGC) || OwnerClass == BPGC;
				// Also check if owner is any BP generated class in the hierarchy
				if (!bIsBlueprintVar)
				{
					bIsBlueprintVar = (Cast<UBlueprintGeneratedClass>(OwnerClass) != nullptr);
				}
			}

			TSharedPtr<FJsonObject> PropObj = MakeShared<FJsonObject>();
			PropObj->SetStringField(TEXT("name"), Property->GetName());
			PropObj->SetStringField(TEXT("type"), GetPropertyTypeString(Property));
			PropObj->SetBoolField(TEXT("is_editable"), bIsEditable);
			PropObj->SetBoolField(TEXT("is_blueprint_variable"), bIsBlueprintVar);

			if (!PropCategory.IsEmpty())
			{
				PropObj->SetStringField(TEXT("category"), PropCategory);
			}

			// Property flags of interest
			PropObj->SetBoolField(TEXT("is_visible_in_defaults"), Property->HasAnyPropertyFlags(CPF_Edit | CPF_EditConst));
			PropObj->SetBoolField(TEXT("is_read_only"), Property->HasAnyPropertyFlags(CPF_EditConst));
			PropObj->SetBoolField(TEXT("is_blueprint_visible"), Property->HasAnyPropertyFlags(CPF_BlueprintVisible));
			PropObj->SetBoolField(TEXT("is_expose_on_spawn"), Property->HasAnyPropertyFlags(CPF_ExposeOnSpawn));
			PropObj->SetBoolField(TEXT("is_replicated"), Property->HasAnyPropertyFlags(CPF_Net));

			if (OwnerClass)
			{
				PropObj->SetStringField(TEXT("owner_class"), OwnerClass->GetName());
			}

			// Serialize the value
			const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Actor);
			PropObj->SetField(TEXT("value"), PropertyValueToJson(Property, ValuePtr));

			PropertiesArray.Add(MakeShared<FJsonValueObject>(PropObj));
		}

		Result->SetArrayField(TEXT("properties"), PropertiesArray);
		Result->SetNumberField(TEXT("property_count"), PropertiesArray.Num());
	}

	return CreateSuccessResponse(Result);
}


// ============================================================================
// FSetActorPropertyAction
// ============================================================================

bool FSetActorPropertyAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Name, PropertyName;
	if (!GetRequiredString(Params, TEXT("name"), Name, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("property_name"), PropertyName, OutError)) return false;
	if (!Params->HasField(TEXT("property_value")))
	{
		OutError = TEXT("Missing 'property_value' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FSetActorPropertyAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString ActorName, PropertyName, Error;
	GetRequiredString(Params, TEXT("name"), ActorName, Error);
	GetRequiredString(Params, TEXT("property_name"), PropertyName, Error);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No editor world available"), TEXT("no_world"));
	}

	AActor* Actor = FindActorByName(World, ActorName);
	if (!Actor)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Actor not found: %s"), *ActorName),
			TEXT("not_found")
		);
	}

	// Look up the property - this works for BOTH native C++ and Blueprint-generated properties
	// because Blueprint variables are compiled into the UBlueprintGeneratedClass as FProperty
	FProperty* Property = Actor->GetClass()->FindPropertyByName(*PropertyName);
	if (!Property)
	{
		// Also try case-insensitive search
		for (TFieldIterator<FProperty> PropIt(Actor->GetClass()); PropIt; ++PropIt)
		{
			if (PropIt->GetName().Equals(PropertyName, ESearchCase::IgnoreCase))
			{
				Property = *PropIt;
				break;
			}
		}
	}

	if (!Property)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Property '%s' not found on actor '%s' (class: %s)"), *PropertyName, *ActorName, *Actor->GetClass()->GetName()),
			TEXT("property_not_found")
		);
	}

	TSharedPtr<FJsonValue> JsonValue = Params->Values.FindRef(TEXT("property_value"));

	FString ErrorMessage;
	if (!FMCPCommonUtils::SetObjectProperty(Actor, Property->GetName(), JsonValue, ErrorMessage))
	{
		return CreateErrorResponse(ErrorMessage, TEXT("property_set_failed"));
	}

	// Mark level dirty so auto-save works
	Context.MarkPackageDirty(World->GetOutermost());

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Set property '%s' on actor '%s'"), *PropertyName, *ActorName);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("actor"), ActorName);
	Result->SetStringField(TEXT("property"), Property->GetName());
	Result->SetStringField(TEXT("property_type"), Property->GetCPPType());
	Result->SetBoolField(TEXT("success"), true);
	return CreateSuccessResponse(Result);
}


// ============================================================================
// FFocusViewportAction
// ============================================================================

bool FFocusViewportAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	bool HasTarget = Params->HasField(TEXT("target"));
	bool HasLocation = Params->HasField(TEXT("location"));

	if (!HasTarget && !HasLocation)
	{
		OutError = TEXT("Either 'target' or 'location' must be provided");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FFocusViewportAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FLevelEditorViewportClient* ViewportClient = nullptr;
	if (GEditor && GEditor->GetActiveViewport())
	{
		ViewportClient = (FLevelEditorViewportClient*)GEditor->GetActiveViewport()->GetClient();
	}

	if (!ViewportClient)
	{
		return CreateErrorResponse(TEXT("Failed to get active viewport"), TEXT("no_viewport"));
	}

	float Distance = GetOptionalNumber(Params, TEXT("distance"), 1000.0f);
	FVector TargetLocation(0, 0, 0);

	if (Params->HasField(TEXT("target")))
	{
		FString TargetActorName = Params->GetStringField(TEXT("target"));
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		AActor* TargetActor = FindActorByName(World, TargetActorName);

		if (!TargetActor)
		{
			return CreateErrorResponse(
				FString::Printf(TEXT("Actor not found: %s"), *TargetActorName),
				TEXT("not_found")
			);
		}
		TargetLocation = TargetActor->GetActorLocation();
	}
	else
	{
		TargetLocation = FMCPCommonUtils::GetVectorFromJson(Params, TEXT("location"));
	}

	ViewportClient->SetViewLocation(TargetLocation - FVector(Distance, 0, 0));

	if (Params->HasField(TEXT("orientation")))
	{
		ViewportClient->SetViewRotation(FMCPCommonUtils::GetRotatorFromJson(Params, TEXT("orientation")));
	}

	ViewportClient->Invalidate();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	return CreateSuccessResponse(Result);
}


// ============================================================================
// FGetViewportTransformAction
// ============================================================================

TSharedPtr<FJsonObject> FGetViewportTransformAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FLevelEditorViewportClient* ViewportClient = nullptr;
	if (GEditor && GEditor->GetActiveViewport())
	{
		ViewportClient = (FLevelEditorViewportClient*)GEditor->GetActiveViewport()->GetClient();
	}

	if (!ViewportClient)
	{
		return CreateErrorResponse(TEXT("Failed to get active viewport"), TEXT("no_viewport"));
	}

	FVector Location = ViewportClient->GetViewLocation();
	FRotator Rotation = ViewportClient->GetViewRotation();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	TArray<TSharedPtr<FJsonValue>> LocationArray;
	LocationArray.Add(MakeShared<FJsonValueNumber>(Location.X));
	LocationArray.Add(MakeShared<FJsonValueNumber>(Location.Y));
	LocationArray.Add(MakeShared<FJsonValueNumber>(Location.Z));
	Result->SetArrayField(TEXT("location"), LocationArray);

	TArray<TSharedPtr<FJsonValue>> RotationArray;
	RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Pitch));
	RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Yaw));
	RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Roll));
	Result->SetArrayField(TEXT("rotation"), RotationArray);

	return CreateSuccessResponse(Result);
}


// ============================================================================
// FSetViewportTransformAction
// ============================================================================

bool FSetViewportTransformAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("location")) && !Params->HasField(TEXT("rotation")))
	{
		OutError = TEXT("At least 'location' or 'rotation' must be provided");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FSetViewportTransformAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FLevelEditorViewportClient* ViewportClient = nullptr;
	if (GEditor && GEditor->GetActiveViewport())
	{
		ViewportClient = (FLevelEditorViewportClient*)GEditor->GetActiveViewport()->GetClient();
	}

	if (!ViewportClient)
	{
		return CreateErrorResponse(TEXT("Failed to get active viewport"), TEXT("no_viewport"));
	}

	if (Params->HasField(TEXT("location")))
	{
		ViewportClient->SetViewLocation(FMCPCommonUtils::GetVectorFromJson(Params, TEXT("location")));
	}
	if (Params->HasField(TEXT("rotation")))
	{
		ViewportClient->SetViewRotation(FMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation")));
	}

	ViewportClient->Invalidate();

	// Return new state
	FVector Location = ViewportClient->GetViewLocation();
	FRotator Rotation = ViewportClient->GetViewRotation();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	TArray<TSharedPtr<FJsonValue>> LocationArray;
	LocationArray.Add(MakeShared<FJsonValueNumber>(Location.X));
	LocationArray.Add(MakeShared<FJsonValueNumber>(Location.Y));
	LocationArray.Add(MakeShared<FJsonValueNumber>(Location.Z));
	Result->SetArrayField(TEXT("location"), LocationArray);

	TArray<TSharedPtr<FJsonValue>> RotationArray;
	RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Pitch));
	RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Yaw));
	RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Roll));
	Result->SetArrayField(TEXT("rotation"), RotationArray);

	return CreateSuccessResponse(Result);
}


