// Copyright (c) 2025 zolnoor. All rights reserved.
// Animation Analysis Actions — v0.4.0 platform extensions
// Requirements: 9.1, 9.2, 9.3, 10.1, 10.2, 10.3, 11.1, 11.2, 11.3, 12.1, 12.2

#pragma once

#include "CoreMinimal.h"
#include "Actions/EditorAction.h"

/**
 * FDescribeAnimBlueprintFullAction
 *
 * Animation blueprint full snapshot: state machines, anim asset references,
 * variables, skeleton reference, EventGraph summary.
 *
 * Command: describe_anim_blueprint_full
 * Requirements: 9.1, 9.2, 9.3
 */
class UECLITOOL_API FDescribeAnimBlueprintFullAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("DescribeAnimBlueprintFull"); }
	virtual bool RequiresSave() const override { return false; }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * FAnimDescribeMontageAction
 *
 * Montage structure query: sections, slot name, notifies, anim sequences.
 *
 * Command: anim_describe_montage
 * Requirements: 10.1, 10.3
 */
class UECLITOOL_API FAnimDescribeMontageAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("AnimDescribeMontage"); }
	virtual bool RequiresSave() const override { return false; }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * FAnimDescribeBlendSpaceAction
 *
 * BlendSpace structure query: axis config, sample points, anim sequences.
 *
 * Command: anim_describe_blendspace
 * Requirements: 10.2, 10.3
 */
class UECLITOOL_API FAnimDescribeBlendSpaceAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("AnimDescribeBlendSpace"); }
	virtual bool RequiresSave() const override { return false; }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * FAnimListNotifiesAction
 *
 * List AnimNotify and AnimNotifyState in an animation asset.
 *
 * Command: anim_list_notifies
 * Requirements: 11.1
 */
class UECLITOOL_API FAnimListNotifiesAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("AnimListNotifies"); }
	virtual bool RequiresSave() const override { return false; }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * FAnimAddNotifyAction
 *
 * Add AnimNotify at specified time on an animation asset.
 *
 * Command: anim_add_notify
 * Requirements: 11.2
 */
class UECLITOOL_API FAnimAddNotifyAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("AnimAddNotify"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * FAnimRemoveNotifyAction
 *
 * Remove AnimNotify from an animation asset.
 *
 * Command: anim_remove_notify
 * Requirements: 11.3
 */
class UECLITOOL_API FAnimRemoveNotifyAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("AnimRemoveNotify"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * FAnimGetSkeletonHierarchyAction
 *
 * Skeleton bone hierarchy query with optional compact mode.
 *
 * Command: anim_get_skeleton_hierarchy
 * Requirements: 12.1, 12.2
 */
class UECLITOOL_API FAnimGetSkeletonHierarchyAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("AnimGetSkeletonHierarchy"); }
	virtual bool RequiresSave() const override { return false; }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};
