// Copyright (c) 2025 zolnoor. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actions/EditorAction.h"

// Forward declarations
class UAnimBlueprint;
class UAnimGraphNode_Base;
class UAnimStateNodeBase;
class UAnimStateTransitionNode;
class UEdGraph;
class UEdGraphNode;

// ============================================================================
// AnimGraphHelpers â€?Internal helper functions
// ============================================================================

namespace AnimGraphHelpers
{
	/** Validate that Blueprint is a UAnimBlueprint; sets OutError on failure */
	bool ValidateAnimBlueprint(UBlueprint* Blueprint, FString& OutError);

	/** Find a sub-graph by name in an AnimBlueprint (AnimGraph, StateMachine, State sub-graph) */
	UEdGraph* FindAnimSubGraph(UAnimBlueprint* AnimBP, const FString& GraphName, FString& OutError);

	/** Find a state node by name in a state machine graph */
	UAnimStateNodeBase* FindStateNode(UEdGraph* StateMachineGraph, const FString& StateName, FString& OutError);

	/** Find a transition node between two states in a state machine graph */
	UAnimStateTransitionNode* FindTransitionNode(UEdGraph* StateMachineGraph,
		const FString& SourceState, const FString& TargetState, FString& OutError);

	/** Serialize a node to JSON (reuses FGraphDescribeEnhancedAction compact/enhanced pattern) */
	TSharedPtr<FJsonObject> SerializeAnimNode(const UEdGraphNode* Node, bool bCompact);

	/** Identify animation asset references (AnimSequence, BlendSpace, etc.) and add to node JSON */
	void ExtractAnimAssetReferences(const UEdGraphNode* Node, TSharedPtr<FJsonObject>& OutNodeObj);

	/** Create an animation graph node by type string at the given position */
	UAnimGraphNode_Base* CreateAnimNodeByType(UEdGraph* Graph, const FString& NodeType,
		FVector2D Position, const TSharedPtr<FJsonObject>& Params, FString& OutError);
}

// ============================================================================
// Read-only Actions (RequiresSave = false)
// ============================================================================

/**
 * FListAnimGraphGraphsAction
 *
 * Lists all graphs in an Animation Blueprint (AnimGraph, EventGraph, StateMachines).
 *
 * Command: list_animgraph_graphs
 * Action ID: animgraph.list_graphs
 */
class UECLITOOL_API FListAnimGraphGraphsAction : public FBlueprintNodeAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("list_animgraph_graphs"); }
	virtual bool RequiresSave() const override { return false; }
};


/**
 * FDescribeAnimGraphTopologyAction
 *
 * Describes the node topology of a specified AnimGraph graph (nodes, pins, edges).
 *
 * Command: describe_animgraph_topology
 * Action ID: animgraph.describe_topology
 */
class UECLITOOL_API FDescribeAnimGraphTopologyAction : public FBlueprintNodeAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("describe_animgraph_topology"); }
	virtual bool RequiresSave() const override { return false; }
};


/**
 * FGetStateMachineStructureAction
 *
 * Reads the complete structure of a state machine (states, transitions, entry state).
 *
 * Command: get_state_machine_structure
 * Action ID: animgraph.get_state_machine
 */
class UECLITOOL_API FGetStateMachineStructureAction : public FBlueprintNodeAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("get_state_machine_structure"); }
	virtual bool RequiresSave() const override { return false; }
};


/**
 * FGetStateSubgraphAction
 *
 * Reads the internal sub-graph topology of a state node.
 *
 * Command: get_state_subgraph
 * Action ID: animgraph.get_state_subgraph
 */
class UECLITOOL_API FGetStateSubgraphAction : public FBlueprintNodeAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("get_state_subgraph"); }
	virtual bool RequiresSave() const override { return false; }
};


/**
 * FGetTransitionRuleAction
 *
 * Reads the condition expression sub-graph of a transition rule.
 *
 * Command: get_transition_rule
 * Action ID: animgraph.get_transition_rule
 */
class UECLITOOL_API FGetTransitionRuleAction : public FBlueprintNodeAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("get_transition_rule"); }
	virtual bool RequiresSave() const override { return false; }
};


// ============================================================================
// Create Action (inherits FEditorAction â€?no existing blueprint required)
// ============================================================================

/**
 * FCreateAnimBlueprintAction
 *
 * Creates a new Animation Blueprint asset with the specified skeleton.
 *
 * Command: create_anim_blueprint
 * Action ID: animgraph.create_blueprint
 */
class UECLITOOL_API FCreateAnimBlueprintAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("create_anim_blueprint"); }
};


// ============================================================================
// Write Actions (RequiresSave = true, use base class default)
// ============================================================================

/**
 * FAddStateMachineAction
 *
 * Adds a new state machine node to the AnimGraph.
 *
 * Command: add_state_machine
 * Action ID: animgraph.add_state_machine
 */
class UECLITOOL_API FAddStateMachineAction : public FBlueprintNodeAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("add_state_machine"); }
};


/**
 * FAddStateAction
 *
 * Adds a new state node to a state machine.
 *
 * Command: add_animgraph_state
 * Action ID: animgraph.add_state
 */
class UECLITOOL_API FAddStateAction : public FBlueprintNodeAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("add_animgraph_state"); }
};


/**
 * FRemoveStateAction
 *
 * Removes a state node and its associated transitions from a state machine.
 *
 * Command: remove_animgraph_state
 * Action ID: animgraph.remove_state
 */
class UECLITOOL_API FRemoveStateAction : public FBlueprintNodeAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("remove_animgraph_state"); }
};


/**
 * FAddTransitionRuleAction
 *
 * Adds a transition rule between two states in a state machine.
 *
 * Command: add_transition_rule
 * Action ID: animgraph.add_transition
 */
class UECLITOOL_API FAddTransitionRuleAction : public FBlueprintNodeAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("add_transition_rule"); }
};


/**
 * FRemoveTransitionRuleAction
 *
 * Removes a transition rule between two states in a state machine.
 *
 * Command: remove_transition_rule
 * Action ID: animgraph.remove_transition
 */
class UECLITOOL_API FRemoveTransitionRuleAction : public FBlueprintNodeAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("remove_transition_rule"); }
};


/**
 * FAddAnimNodeAction
 *
 * Adds an animation node (AnimSequencePlayer, BlendSpacePlayer, etc.) to a graph.
 *
 * Command: add_anim_node
 * Action ID: animgraph.add_node
 */
class UECLITOOL_API FAddAnimNodeAction : public FBlueprintNodeAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("add_anim_node"); }
};


/**
 * FSetAnimNodePropertyAction
 *
 * Sets a property on an animation graph node.
 *
 * Command: set_anim_node_property
 * Action ID: animgraph.set_node_property
 */
class UECLITOOL_API FSetAnimNodePropertyAction : public FBlueprintNodeAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("set_anim_node_property"); }
};


/**
 * FConnectAnimNodesAction
 *
 * Connects two AnimGraph node pins.
 *
 * Command: connect_anim_nodes
 * Action ID: animgraph.connect_nodes
 */
class UECLITOOL_API FConnectAnimNodesAction : public FBlueprintNodeAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("connect_anim_nodes"); }
};


/**
 * FDisconnectAnimNodeAction
 *
 * Disconnects all connections on a specified AnimGraph node pin.
 *
 * Command: disconnect_anim_node
 * Action ID: animgraph.disconnect_node
 */
class UECLITOOL_API FDisconnectAnimNodeAction : public FBlueprintNodeAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("disconnect_anim_node"); }
};


/**
 * FRenameStateAction
 *
 * Renames a state node in a state machine.
 *
 * Command: rename_animgraph_state
 * Action ID: animgraph.rename_state
 */
class UECLITOOL_API FRenameStateAction : public FBlueprintNodeAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("rename_animgraph_state"); }
};


/**
 * FSetTransitionPriorityAction
 *
 * Modifies the priority of a transition rule in a state machine.
 *
 * Command: set_transition_priority
 * Action ID: animgraph.set_transition_priority
 */
class UECLITOOL_API FSetTransitionPriorityAction : public FBlueprintNodeAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("set_transition_priority"); }
};


/**
 * FCompileAnimBlueprintAction
 *
 * Compiles an Animation Blueprint and returns diagnostic information.
 *
 * Command: compile_anim_blueprint
 * Action ID: animgraph.compile
 */
class UECLITOOL_API FCompileAnimBlueprintAction : public FBlueprintNodeAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("compile_anim_blueprint"); }
};
