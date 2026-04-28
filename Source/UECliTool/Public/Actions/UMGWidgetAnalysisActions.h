// Copyright (c) 2025 zolnoor. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actions/EditorAction.h"

/**
 * Widget Blueprint full snapshot: component hierarchy tree, event bindings,
 * animations, MVVM bindings, variable list.
 * Command: describe_widget_blueprint_full
 */
class UECLITOOL_API FDescribeWidgetBlueprintFullAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("DescribeWidgetBlueprintFull"); }
	virtual bool RequiresSave() const override { return false; }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * List UMG animations in a Widget Blueprint.
 * Command: widget_list_animations
 */
class UECLITOOL_API FWidgetListAnimationsAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("WidgetListAnimations"); }
	virtual bool RequiresSave() const override { return false; }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Create a UMG animation in a Widget Blueprint.
 * Command: widget_create_animation
 */
class UECLITOOL_API FWidgetCreateAnimationAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("WidgetCreateAnimation"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Add an animation property track to an existing UMG animation.
 * Command: widget_add_animation_track
 */
class UECLITOOL_API FWidgetAddAnimationTrackAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("WidgetAddAnimationTrack"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Query child Widget references from a Widget Blueprint.
 * Command: widget_get_references
 */
class UECLITOOL_API FWidgetGetReferencesAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("WidgetGetReferences"); }
	virtual bool RequiresSave() const override { return false; }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Query referencer relationships for a Widget Blueprint.
 * Command: widget_get_referencers
 */
class UECLITOOL_API FWidgetGetReferencersAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("WidgetGetReferencers"); }
	virtual bool RequiresSave() const override { return false; }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Batch query style properties (font, color, padding, margin) for all
 * components in a Widget Blueprint.
 * Command: widget_batch_get_styles
 */
class UECLITOOL_API FWidgetBatchGetStylesAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("WidgetBatchGetStyles"); }
	virtual bool RequiresSave() const override { return false; }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};
