// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Utils/McpV2ToolUtils.h"

#include "Session/McpEditorSessionManager.h"
#include "Utils/McpAssetModifier.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "UObject/FieldIterator.h"
#include "UObject/UnrealType.h"
#include "EngineUtils.h"

namespace
{
	TArray<TSharedPtr<FJsonValue>> VectorToArray(const FVector& Vector)
	{
		return {
			MakeShareable(new FJsonValueNumber(Vector.X)),
			MakeShareable(new FJsonValueNumber(Vector.Y)),
			MakeShareable(new FJsonValueNumber(Vector.Z))
		};
	}

	TArray<TSharedPtr<FJsonValue>> RotatorToArray(const FRotator& Rotator)
	{
		return {
			MakeShareable(new FJsonValueNumber(Rotator.Pitch)),
			MakeShareable(new FJsonValueNumber(Rotator.Yaw)),
			MakeShareable(new FJsonValueNumber(Rotator.Roll))
		};
	}

	TSharedPtr<FJsonObject> TransformToJson(const FTransform& Transform)
	{
		TSharedPtr<FJsonObject> TransformObject = MakeShareable(new FJsonObject);
		TransformObject->SetArrayField(TEXT("location"), VectorToArray(Transform.GetLocation()));
		TransformObject->SetArrayField(TEXT("rotation"), RotatorToArray(Transform.Rotator()));
		TransformObject->SetArrayField(TEXT("scale"), VectorToArray(Transform.GetScale3D()));
		return TransformObject;
	}

	FString GetPropertyTypeString(const FProperty* Property)
	{
		if (!Property)
		{
			return TEXT("unknown");
		}

		if (Property->IsA<FBoolProperty>()) return TEXT("bool");
		if (Property->IsA<FIntProperty>()) return TEXT("int32");
		if (Property->IsA<FInt64Property>()) return TEXT("int64");
		if (Property->IsA<FFloatProperty>()) return TEXT("float");
		if (Property->IsA<FDoubleProperty>()) return TEXT("double");
		if (Property->IsA<FNameProperty>()) return TEXT("FName");
		if (Property->IsA<FStrProperty>()) return TEXT("FString");
		if (Property->IsA<FTextProperty>()) return TEXT("FText");

		if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
		{
			return ObjectProperty->PropertyClass
				? FString::Printf(TEXT("TObjectPtr<%s>"), *ObjectProperty->PropertyClass->GetName())
				: TEXT("object");
		}

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			return StructProperty->Struct ? StructProperty->Struct->GetName() : TEXT("struct");
		}

		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			return FString::Printf(TEXT("TArray<%s>"), *GetPropertyTypeString(ArrayProperty->Inner));
		}

		return Property->GetClass()->GetName();
	}

	TSharedPtr<FJsonObject> SerializeProperty(FProperty* Property, void* ValuePointer, UObject* Owner)
	{
		if (!Property || !ValuePointer)
		{
			return nullptr;
		}

		TSharedPtr<FJsonObject> PropertyObject = MakeShareable(new FJsonObject);
		PropertyObject->SetStringField(TEXT("name"), Property->GetName());
		PropertyObject->SetStringField(TEXT("type"), GetPropertyTypeString(Property));

		// P0-S5: Delta 传 nullptr，避免与自身对比导致 ExportText_Direct 输出空串。
		FString Value;
		Property->ExportText_Direct(Value, ValuePointer, nullptr, Owner, PPF_None);
		PropertyObject->SetStringField(TEXT("value"), Value);
		return PropertyObject;
	}

	TSharedPtr<FJsonObject> SerializeComponent(UActorComponent* Component, bool bIncludeProperties, bool bIncludeInherited)
	{
		if (!Component)
		{
			return nullptr;
		}

		TSharedPtr<FJsonObject> ComponentObject = MakeShareable(new FJsonObject);
		ComponentObject->SetStringField(TEXT("name"), Component->GetName());
		ComponentObject->SetStringField(TEXT("class"), Component->GetClass()->GetName());
		ComponentObject->SetBoolField(TEXT("active"), Component->IsActive());

		if (const USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
		{
			ComponentObject->SetObjectField(TEXT("relative_transform"), TransformToJson(SceneComponent->GetRelativeTransform()));
		}

		if (bIncludeProperties)
		{
			TArray<TSharedPtr<FJsonValue>> PropertyArray;
			const EFieldIteratorFlags::SuperClassFlags SuperFlags = bIncludeInherited
				? EFieldIteratorFlags::IncludeSuper
				: EFieldIteratorFlags::ExcludeSuper;

			for (TFieldIterator<FProperty> It(Component->GetClass(), SuperFlags); It; ++It)
			{
				FProperty* Property = *It;
				if (!Property)
				{
					continue;
				}
				// P2-N6: 仅导出蓝图可见 / 可编辑的属性，避免一次请求返回几 MB 响应。
				if (!Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible))
				{
					continue;
				}
				void* ValuePointer = Property->ContainerPtrToValuePtr<void>(Component);
				TSharedPtr<FJsonObject> PropertyObject = SerializeProperty(Property, ValuePointer, Component);
				if (PropertyObject.IsValid())
				{
					PropertyArray.Add(MakeShareable(new FJsonValueObject(PropertyObject)));
				}
			}

			ComponentObject->SetArrayField(TEXT("properties"), PropertyArray);
		}

		return ComponentObject;
	}

	FString GetPinDirectionString(EEdGraphPinDirection Direction)
	{
		switch (Direction)
		{
		case EGPD_Input: return TEXT("input");
		case EGPD_Output: return TEXT("output");
		default: return TEXT("unknown");
		}
	}

	FString GetPinDefaultSource(UEdGraphPin* Pin)
	{
		if (!Pin)
		{
			return TEXT("none");
		}
		if (Pin->DefaultObject)
		{
			return TEXT("object");
		}
		if (!Pin->DefaultValue.IsEmpty())
		{
			return TEXT("literal");
		}
		return TEXT("none");
	}
}

namespace McpV2ToolUtils
{
	TSharedPtr<FJsonObject> MakeAssetHandle(const FString& AssetPath, const FString& AssetClass)
	{
		TSharedPtr<FJsonObject> Handle = MakeShareable(new FJsonObject);
		Handle->SetStringField(TEXT("kind"), TEXT("asset"));
		Handle->SetStringField(TEXT("asset_path"), AssetPath);
		Handle->SetStringField(TEXT("asset_class"), AssetClass);
		return Handle;
	}

	TSharedPtr<FJsonObject> MakeEntityHandle(const FString& Kind, const FString& SessionId, const FString& ResourcePath, const FString& EntityId, const FString& DisplayName)
	{
		TSharedPtr<FJsonObject> Handle = MakeShareable(new FJsonObject);
		Handle->SetStringField(TEXT("kind"), Kind);
		Handle->SetStringField(TEXT("session_id"), SessionId);
		Handle->SetStringField(TEXT("resource_path"), ResourcePath);
		Handle->SetStringField(TEXT("entity_id"), EntityId);
		Handle->SetStringField(TEXT("display_name"), DisplayName);
		return Handle;
	}

	FString GetBlueprintGraphType(const UBlueprint* Blueprint, const UEdGraph* Graph)
	{
		if (!Blueprint || !Graph)
		{
			return TEXT("unknown");
		}
		if (Blueprint->UbergraphPages.Contains(const_cast<UEdGraph*>(Graph))) return TEXT("event");
		if (Blueprint->FunctionGraphs.Contains(const_cast<UEdGraph*>(Graph))) return TEXT("function");
		if (Blueprint->MacroGraphs.Contains(const_cast<UEdGraph*>(Graph))) return TEXT("macro");

		const FString ClassName = Graph->GetClass()->GetName().ToLower();
		if (ClassName.Contains(TEXT("transition"))) return TEXT("transition");
		if (ClassName.Contains(TEXT("state"))) return TEXT("state");
		// P2-N1: 原代码两个分支判定同一字符串，此处修正为 animationgraph / animgraph 两种命名。
		if (ClassName.Contains(TEXT("animationgraph")) || ClassName.Contains(TEXT("animgraph"))) return TEXT("anim_graph");
		return TEXT("graph");
	}

	TSharedPtr<FJsonObject> BuildBlueprintSummary(UBlueprint* Blueprint, const FString& AssetPath, bool bIncludeNames)
	{
		// P2-N2: 入参 null 校验。
		if (!Blueprint)
		{
			return nullptr;
		}

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("name"), Blueprint->GetName());
		Result->SetStringField(TEXT("asset_path"), AssetPath);
		Result->SetObjectField(TEXT("asset_handle"), MakeAssetHandle(AssetPath, Blueprint->GetClass()->GetName()));
		Result->SetStringField(TEXT("parent_class"), Blueprint->ParentClass ? Blueprint->ParentClass->GetPathName() : TEXT(""));
		Result->SetNumberField(TEXT("event_graph_count"), Blueprint->UbergraphPages.Num());
		Result->SetNumberField(TEXT("function_count"), Blueprint->FunctionGraphs.Num());
		Result->SetNumberField(TEXT("macro_count"), Blueprint->MacroGraphs.Num());
		Result->SetNumberField(TEXT("variable_count"), Blueprint->NewVariables.Num());

		int32 ComponentCount = 0;
		TArray<TSharedPtr<FJsonValue>> ComponentNames;
		if (Blueprint->SimpleConstructionScript)
		{
			const TArray<USCS_Node*>& Nodes = Blueprint->SimpleConstructionScript->GetAllNodes();
			ComponentCount = Nodes.Num();
			if (bIncludeNames)
			{
				for (const USCS_Node* Node : Nodes)
				{
					if (Node)
					{
						ComponentNames.Add(MakeShareable(new FJsonValueString(Node->GetVariableName().ToString())));
					}
				}
			}
		}
		Result->SetNumberField(TEXT("component_count"), ComponentCount);

		if (bIncludeNames)
		{
			TArray<TSharedPtr<FJsonValue>> FunctionNames;
			for (const UEdGraph* Graph : Blueprint->FunctionGraphs)
			{
				if (Graph)
				{
					FunctionNames.Add(MakeShareable(new FJsonValueString(Graph->GetName())));
				}
			}

			TArray<TSharedPtr<FJsonValue>> VariableNames;
			for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
			{
				VariableNames.Add(MakeShareable(new FJsonValueString(Variable.VarName.ToString())));
			}

			Result->SetArrayField(TEXT("function_names"), FunctionNames);
			Result->SetArrayField(TEXT("variable_names"), VariableNames);
			Result->SetArrayField(TEXT("component_names"), ComponentNames);
		}

		return Result;
	}

	TSharedPtr<FJsonObject> BuildBlueprintGraphSummary(
		UBlueprint* Blueprint,
		const FString& AssetPath,
		const FString& SessionId,
		const FString& GraphNameFilter,
		const FString& GraphTypeFilter,
		bool bIncludeSampleNodes,
		int32 MaxSampleNodes)
	{
		// P2-N2: 入参 null 校验。
		if (!Blueprint)
		{
			return nullptr;
		}

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("asset_path"), AssetPath);
		Result->SetObjectField(TEXT("asset_handle"), MakeAssetHandle(AssetPath, Blueprint->GetClass()->GetName()));

		TArray<UEdGraph*> Graphs;
		FMcpAssetModifier::GetAllSearchableGraphs(Blueprint, Graphs);

		TArray<TSharedPtr<FJsonValue>> GraphArray;
		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph)
			{
				continue;
			}

			const FString GraphType = GetBlueprintGraphType(Blueprint, Graph);
			// P2-N4: 使用 MatchesPattern 支持通配符 *?，与 MCP 其他 filter 字段保持一致。
			if (!MatchesPattern(Graph->GetName(), GraphNameFilter))
			{
				continue;
			}
			if (!GraphTypeFilter.IsEmpty() && !GraphType.Equals(GraphTypeFilter, ESearchCase::IgnoreCase))
			{
				continue;
			}

			TSharedPtr<FJsonObject> GraphObject = MakeShareable(new FJsonObject);
			GraphObject->SetStringField(TEXT("name"), Graph->GetName());
			GraphObject->SetStringField(TEXT("type"), GraphType);
			GraphObject->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());

			if (bIncludeSampleNodes)
			{
				TArray<TSharedPtr<FJsonValue>> SampleNodes;
				int32 Added = 0;
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					if (!Node || Added >= MaxSampleNodes)
					{
						continue;
					}

					FMcpEditorSessionManager::Get().RememberBlueprintNode(SessionId, AssetPath, Node);
					TSharedPtr<FJsonObject> NodeObject = MakeShareable(new FJsonObject);
					NodeObject->SetStringField(TEXT("guid"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
					NodeObject->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
					NodeObject->SetObjectField(TEXT("handle"), MakeEntityHandle(TEXT("blueprint_node"), SessionId, AssetPath, Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens), Node->GetNodeTitle(ENodeTitleType::ListView).ToString()));
					SampleNodes.Add(MakeShareable(new FJsonValueObject(NodeObject)));
					++Added;
				}
				GraphObject->SetArrayField(TEXT("sample_nodes"), SampleNodes);
			}

			GraphArray.Add(MakeShareable(new FJsonValueObject(GraphObject)));
		}

		Result->SetArrayField(TEXT("graphs"), GraphArray);
		Result->SetNumberField(TEXT("graph_count"), GraphArray.Num());
		return Result;
	}

	TSharedPtr<FJsonObject> SerializeBlueprintNode(
		UEdGraphNode* Node,
		const FString& AssetPath,
		const FString& SessionId,
		const FString& GraphName,
		const FString& GraphType,
		bool bIncludePins,
		bool bIncludeConnections,
		bool bIncludeDefaults,
		bool bIncludePosition)
	{
		if (!Node)
		{
			return nullptr;
		}

		TSharedPtr<FJsonObject> NodeObject = MakeShareable(new FJsonObject);
		NodeObject->SetStringField(TEXT("guid"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
		NodeObject->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		NodeObject->SetStringField(TEXT("class"), Node->GetClass()->GetName());
		NodeObject->SetStringField(TEXT("graph_name"), GraphName);
		NodeObject->SetStringField(TEXT("graph_type"), GraphType);
		NodeObject->SetObjectField(TEXT("handle"), MakeEntityHandle(TEXT("blueprint_node"), SessionId, AssetPath, Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens), Node->GetNodeTitle(ENodeTitleType::ListView).ToString()));

		if (bIncludePosition)
		{
			TSharedPtr<FJsonObject> PositionObject = MakeShareable(new FJsonObject);
			PositionObject->SetNumberField(TEXT("x"), Node->NodePosX);
			PositionObject->SetNumberField(TEXT("y"), Node->NodePosY);
			NodeObject->SetObjectField(TEXT("position"), PositionObject);
		}

		if (bIncludePins)
		{
			TArray<TSharedPtr<FJsonValue>> PinsArray;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin)
				{
					continue;
				}

				TSharedPtr<FJsonObject> PinObject = MakeShareable(new FJsonObject);
				PinObject->SetStringField(TEXT("name"), Pin->PinName.ToString());
				PinObject->SetStringField(TEXT("direction"), GetPinDirectionString(Pin->Direction));
				PinObject->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
				if (Pin->PinType.PinSubCategoryObject.IsValid())
				{
					PinObject->SetStringField(TEXT("subcategory_object"), Pin->PinType.PinSubCategoryObject->GetPathName());
				}
				if (bIncludeDefaults)
				{
					PinObject->SetStringField(TEXT("default_value"), Pin->DefaultValue);
					PinObject->SetStringField(TEXT("default_source"), GetPinDefaultSource(Pin));
					if (Pin->DefaultObject)
					{
						PinObject->SetStringField(TEXT("default_object_path"), Pin->DefaultObject->GetPathName());
					}
					if (Pin->DefaultTextValue.IsEmptyOrWhitespace() == false)
					{
						PinObject->SetStringField(TEXT("default_text_value"), Pin->DefaultTextValue.ToString());
					}
				}

				if (bIncludeConnections)
				{
					TArray<TSharedPtr<FJsonValue>> ConnectionsArray;
					for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
					{
						if (!LinkedPin || !LinkedPin->GetOwningNode())
						{
							continue;
						}

						TSharedPtr<FJsonObject> ConnectionObject = MakeShareable(new FJsonObject);
						ConnectionObject->SetStringField(TEXT("node_guid"), LinkedPin->GetOwningNode()->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
						ConnectionObject->SetStringField(TEXT("pin_name"), LinkedPin->PinName.ToString());
						ConnectionsArray.Add(MakeShareable(new FJsonValueObject(ConnectionObject)));
					}
					PinObject->SetArrayField(TEXT("connections"), ConnectionsArray);
				}

				PinsArray.Add(MakeShareable(new FJsonValueObject(PinObject)));
			}

			NodeObject->SetArrayField(TEXT("pins"), PinsArray);
			NodeObject->SetNumberField(TEXT("pin_count"), PinsArray.Num());
		}

		return NodeObject;
	}

	bool MatchesPattern(const FString& Value, const FString& Filter)
	{
		if (Filter.IsEmpty())
		{
			return true;
		}
		if (Filter.Contains(TEXT("*")) || Filter.Contains(TEXT("?")))
		{
			return Value.MatchesWildcard(Filter, ESearchCase::IgnoreCase);
		}
		return Value.Contains(Filter, ESearchCase::IgnoreCase);
	}

	TSharedPtr<FJsonObject> BuildWorldSummary(UWorld* World, const FString& SessionId, bool bIncludeLevels, bool bIncludeSelection)
	{
		// P2-N2: 入参 null 校验。
		if (!World)
		{
			return nullptr;
		}

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("world_name"), World->GetName());
		Result->SetStringField(TEXT("world_path"), World->GetPathName());
		Result->SetStringField(TEXT("world_type"), LexToString(World->WorldType));

		int32 ActorCount = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (*It)
			{
				++ActorCount;
			}
		}
		Result->SetNumberField(TEXT("actor_count"), ActorCount);

		if (bIncludeLevels)
		{
			TArray<TSharedPtr<FJsonValue>> LevelsArray;
			for (ULevel* Level : World->GetLevels())
			{
				if (Level && Level->GetOuter())
				{
					LevelsArray.Add(MakeShareable(new FJsonValueString(Level->GetOuter()->GetPathName())));
				}
			}
			Result->SetArrayField(TEXT("levels"), LevelsArray);
		}

		// P2-N2: GEditor 在非编辑器场景可能为 null。
		if (bIncludeSelection && GEditor && GEditor->GetSelectedActors())
		{
			TArray<TSharedPtr<FJsonValue>> SelectedArray;
			for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
			{
				if (AActor* Actor = Cast<AActor>(*It))
				{
					FMcpEditorSessionManager::Get().RememberActor(SessionId, World->GetPathName(), Actor);
					SelectedArray.Add(MakeShareable(new FJsonValueObject(SerializeActorSummary(Actor, SessionId, false))));
				}
			}
			Result->SetArrayField(TEXT("selected_actors"), SelectedArray);
		}

		return Result;
	}

	TSharedPtr<FJsonObject> SerializeActorSummary(AActor* Actor, const FString& SessionId, bool bIncludeTransform)
	{
		if (!Actor)
		{
			return nullptr;
		}

		TSharedPtr<FJsonObject> ActorObject = MakeShareable(new FJsonObject);
		ActorObject->SetStringField(TEXT("name"), Actor->GetName());
		ActorObject->SetStringField(TEXT("label"), Actor->GetActorNameOrLabel());
		ActorObject->SetStringField(TEXT("class"), Actor->GetClass()->GetPathName());
		ActorObject->SetStringField(TEXT("object_path"), Actor->GetPathName());
		ActorObject->SetObjectField(TEXT("handle"), MakeEntityHandle(
			TEXT("actor"),
			SessionId,
			Actor->GetWorld() ? Actor->GetWorld()->GetPathName() : TEXT(""),
			Actor->GetPathName(),
			Actor->GetActorNameOrLabel()));
		if (bIncludeTransform)
		{
			ActorObject->SetObjectField(TEXT("transform"), TransformToJson(Actor->GetActorTransform()));
		}
		return ActorObject;
	}

	TSharedPtr<FJsonObject> SerializeActorDetail(AActor* Actor, const FString& SessionId, bool bIncludeComponents, bool bIncludeProperties, bool bIncludeInherited)
	{
		TSharedPtr<FJsonObject> Result = SerializeActorSummary(Actor, SessionId, true);
		if (!Result.IsValid())
		{
			return nullptr;
		}

		Result->SetArrayField(TEXT("tags"), [&Actor]()
		{
			TArray<TSharedPtr<FJsonValue>> TagsArray;
			for (const FName& Tag : Actor->Tags)
			{
				TagsArray.Add(MakeShareable(new FJsonValueString(Tag.ToString())));
			}
			return TagsArray;
		}());

		if (bIncludeComponents)
		{
			TArray<UActorComponent*> Components;
			Actor->GetComponents(Components);
			TArray<TSharedPtr<FJsonValue>> ComponentsArray;
			for (UActorComponent* Component : Components)
			{
				TSharedPtr<FJsonObject> ComponentObject = SerializeComponent(Component, bIncludeProperties, bIncludeInherited);
				if (ComponentObject.IsValid())
				{
					ComponentsArray.Add(MakeShareable(new FJsonValueObject(ComponentObject)));
				}
			}
			Result->SetArrayField(TEXT("components"), ComponentsArray);
		}

		if (bIncludeProperties)
		{
			TArray<TSharedPtr<FJsonValue>> PropertyArray;
			const EFieldIteratorFlags::SuperClassFlags SuperFlags = bIncludeInherited
				? EFieldIteratorFlags::IncludeSuper
				: EFieldIteratorFlags::ExcludeSuper;
			for (TFieldIterator<FProperty> It(Actor->GetClass(), SuperFlags); It; ++It)
			{
				FProperty* Property = *It;
				if (!Property)
				{
					continue;
				}
				// P2-N6: 只导出蓝图可见 / 可编辑的属性，避免响应爆炸。
				if (!Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible))
				{
					continue;
				}
				void* ValuePointer = Property->ContainerPtrToValuePtr<void>(Actor);
				TSharedPtr<FJsonObject> PropertyObject = SerializeProperty(Property, ValuePointer, Actor);
				if (PropertyObject.IsValid())
				{
					PropertyArray.Add(MakeShareable(new FJsonValueObject(PropertyObject)));
				}
			}
			Result->SetArrayField(TEXT("properties"), PropertyArray);
		}

		return Result;
	}
}
