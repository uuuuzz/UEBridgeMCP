// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Physics/PhysicsToolUtils.h"

#include "Utils/McpAssetModifier.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "PhysicsEngine/ConstraintInstance.h"

namespace
{
	const TArray<ECollisionChannel>& StandardChannels()
	{
		static const TArray<ECollisionChannel> Channels = {
			ECC_WorldStatic,
			ECC_WorldDynamic,
			ECC_Pawn,
			ECC_Visibility,
			ECC_Camera,
			ECC_PhysicsBody,
			ECC_Vehicle,
			ECC_Destructible
		};
		return Channels;
	}

	FString ObjectPath(const UObject* Object)
	{
		return Object ? Object->GetPathName() : FString();
	}

	TSharedPtr<FJsonObject> VectorToJson(const FVector& Vector)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetNumberField(TEXT("x"), Vector.X);
		Object->SetNumberField(TEXT("y"), Vector.Y);
		Object->SetNumberField(TEXT("z"), Vector.Z);
		return Object;
	}

	TSharedPtr<FJsonObject> ComponentRefToJson(UPrimitiveComponent* Component, const FName& BoneName)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetStringField(TEXT("component_name"), Component ? Component->GetName() : FString());
		Object->SetStringField(TEXT("component_class"), Component ? Component->GetClass()->GetName() : FString());
		Object->SetStringField(TEXT("owner_name"), Component && Component->GetOwner() ? Component->GetOwner()->GetActorNameOrLabel() : FString());
		Object->SetStringField(TEXT("bone_name"), BoneName.ToString());
		Object->SetBoolField(TEXT("valid"), Component != nullptr);
		return Object;
	}

	bool HasField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
	{
		return Object.IsValid() && Object->HasField(FieldName);
	}

	bool TryReadFloatField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, float& OutValue)
	{
		double Number = 0.0;
		if (Object.IsValid() && Object->TryGetNumberField(FieldName, Number))
		{
			OutValue = static_cast<float>(Number);
			return true;
		}
		return false;
	}

	bool TryReadBoolField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, bool& OutValue)
	{
		return Object.IsValid() && Object->TryGetBoolField(FieldName, OutValue);
	}

	FString NormalizeName(FString Value)
	{
		Value.TrimStartAndEndInline();
		Value.ReplaceInline(TEXT(" "), TEXT(""));
		Value.ReplaceInline(TEXT("_"), TEXT(""));
		Value.ReplaceInline(TEXT("-"), TEXT(""));
		return Value.ToLower();
	}

	void AddPhysicalMaterialFields(TSharedPtr<FJsonObject> Object, UPhysicalMaterial* Material)
	{
		Object->SetStringField(TEXT("physical_material"), ObjectPath(Material));
		Object->SetStringField(TEXT("physical_material_name"), Material ? Material->GetName() : FString());
	}
}

namespace PhysicsToolUtils
{
	UPrimitiveComponent* ResolvePrimitiveComponent(
		AActor* Actor,
		const FString& ComponentName,
		FString& OutErrorCode,
		FString& OutErrorMessage)
	{
		if (!Actor)
		{
			OutErrorCode = TEXT("UEBMCP_ACTOR_NOT_FOUND");
			OutErrorMessage = TEXT("Actor is required");
			return nullptr;
		}

		if (!ComponentName.IsEmpty())
		{
			UActorComponent* FoundComponent = FMcpAssetModifier::FindComponentByName(Actor, ComponentName);
			if (!FoundComponent)
			{
				OutErrorCode = TEXT("UEBMCP_COMPONENT_NOT_FOUND");
				OutErrorMessage = FString::Printf(TEXT("Component '%s' was not found on actor '%s'"), *ComponentName, *Actor->GetActorNameOrLabel());
				return nullptr;
			}

			UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(FoundComponent);
			if (!Primitive)
			{
				OutErrorCode = TEXT("UEBMCP_INVALID_COMPONENT_TYPE");
				OutErrorMessage = FString::Printf(TEXT("Component '%s' is not a PrimitiveComponent"), *ComponentName);
				return nullptr;
			}
			return Primitive;
		}

		if (UPrimitiveComponent* RootPrimitive = Cast<UPrimitiveComponent>(Actor->GetRootComponent()))
		{
			return RootPrimitive;
		}

		TArray<UPrimitiveComponent*> PrimitiveComponents;
		Actor->GetComponents(PrimitiveComponents);
		for (UPrimitiveComponent* Primitive : PrimitiveComponents)
		{
			if (Primitive)
			{
				return Primitive;
			}
		}

		OutErrorCode = TEXT("UEBMCP_COMPONENT_NOT_FOUND");
		OutErrorMessage = FString::Printf(TEXT("Actor '%s' does not have a PrimitiveComponent"), *Actor->GetActorNameOrLabel());
		return nullptr;
	}

	UPhysicsConstraintComponent* ResolveConstraintComponent(
		AActor* Actor,
		const FString& ComponentName,
		FString& OutErrorCode,
		FString& OutErrorMessage)
	{
		if (!Actor)
		{
			OutErrorCode = TEXT("UEBMCP_ACTOR_NOT_FOUND");
			OutErrorMessage = TEXT("Actor is required");
			return nullptr;
		}

		if (!ComponentName.IsEmpty())
		{
			UActorComponent* FoundComponent = FMcpAssetModifier::FindComponentByName(Actor, ComponentName);
			if (!FoundComponent)
			{
				OutErrorCode = TEXT("UEBMCP_COMPONENT_NOT_FOUND");
				OutErrorMessage = FString::Printf(TEXT("Constraint component '%s' was not found"), *ComponentName);
				return nullptr;
			}

			UPhysicsConstraintComponent* Constraint = Cast<UPhysicsConstraintComponent>(FoundComponent);
			if (!Constraint)
			{
				OutErrorCode = TEXT("UEBMCP_INVALID_COMPONENT_TYPE");
				OutErrorMessage = FString::Printf(TEXT("Component '%s' is not a PhysicsConstraintComponent"), *ComponentName);
				return nullptr;
			}
			return Constraint;
		}

		TArray<UPhysicsConstraintComponent*> Constraints;
		Actor->GetComponents(Constraints);
		if (Constraints.Num() > 0 && Constraints[0])
		{
			return Constraints[0];
		}

		OutErrorCode = TEXT("UEBMCP_COMPONENT_NOT_FOUND");
		OutErrorMessage = FString::Printf(TEXT("Actor '%s' does not have a PhysicsConstraintComponent"), *Actor->GetActorNameOrLabel());
		return nullptr;
	}

	UPhysicsConstraintComponent* CreateConstraintComponent(AActor* Actor, const FString& RequestedName)
	{
		if (!Actor)
		{
			return nullptr;
		}

		const FName BaseName(*(!RequestedName.IsEmpty() ? RequestedName : TEXT("PhysicsConstraintMCPComponent")));
		const FName UniqueName = MakeUniqueObjectName(Actor, UPhysicsConstraintComponent::StaticClass(), BaseName);
		UPhysicsConstraintComponent* Constraint = NewObject<UPhysicsConstraintComponent>(Actor, UniqueName, RF_Transactional);
		if (!Constraint)
		{
			return nullptr;
		}

		Actor->AddInstanceComponent(Constraint);
		if (USceneComponent* Root = Actor->GetRootComponent())
		{
			Constraint->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
		}
		Constraint->RegisterComponent();
		return Constraint;
	}

	bool TryLoadPhysicalMaterial(
		const FString& AssetPath,
		UPhysicalMaterial*& OutMaterial,
		FString& OutError)
	{
		OutMaterial = FMcpAssetModifier::LoadAssetByPath<UPhysicalMaterial>(AssetPath, OutError);
		return OutMaterial != nullptr;
	}

	bool TryParseCollisionEnabled(
		const FString& Value,
		ECollisionEnabled::Type& OutType,
		FString& OutError)
	{
		const FString Normalized = NormalizeName(Value);
		if (Normalized == TEXT("nocollision") || Normalized == TEXT("none"))
		{
			OutType = ECollisionEnabled::NoCollision;
			return true;
		}
		if (Normalized == TEXT("queryonly"))
		{
			OutType = ECollisionEnabled::QueryOnly;
			return true;
		}
		if (Normalized == TEXT("physicsonly"))
		{
			OutType = ECollisionEnabled::PhysicsOnly;
			return true;
		}
		if (Normalized == TEXT("queryandphysics") || Normalized == TEXT("collisionenabled"))
		{
			OutType = ECollisionEnabled::QueryAndPhysics;
			return true;
		}
		if (Normalized == TEXT("probeonly"))
		{
			OutType = ECollisionEnabled::ProbeOnly;
			return true;
		}
		if (Normalized == TEXT("queryandprobe"))
		{
			OutType = ECollisionEnabled::QueryAndProbe;
			return true;
		}

		OutError = FString::Printf(TEXT("Unsupported collision_enabled value '%s'"), *Value);
		return false;
	}

	bool TryParseCollisionChannel(
		const FString& Value,
		ECollisionChannel& OutChannel,
		FString& OutError)
	{
		const FString Normalized = NormalizeName(Value);
		if (Normalized == TEXT("worldstatic") || Normalized == TEXT("eccworldstatic")) { OutChannel = ECC_WorldStatic; return true; }
		if (Normalized == TEXT("worlddynamic") || Normalized == TEXT("eccworlddynamic")) { OutChannel = ECC_WorldDynamic; return true; }
		if (Normalized == TEXT("pawn") || Normalized == TEXT("eccpawn")) { OutChannel = ECC_Pawn; return true; }
		if (Normalized == TEXT("visibility") || Normalized == TEXT("eccvisibility")) { OutChannel = ECC_Visibility; return true; }
		if (Normalized == TEXT("camera") || Normalized == TEXT("ecccamera")) { OutChannel = ECC_Camera; return true; }
		if (Normalized == TEXT("physicsbody") || Normalized == TEXT("eccphysicsbody")) { OutChannel = ECC_PhysicsBody; return true; }
		if (Normalized == TEXT("vehicle") || Normalized == TEXT("eccvehicle")) { OutChannel = ECC_Vehicle; return true; }
		if (Normalized == TEXT("destructible") || Normalized == TEXT("eccdestructible")) { OutChannel = ECC_Destructible; return true; }

		OutError = FString::Printf(TEXT("Unsupported collision channel '%s'"), *Value);
		return false;
	}

	bool TryParseCollisionResponse(
		const FString& Value,
		ECollisionResponse& OutResponse,
		FString& OutError)
	{
		const FString Normalized = NormalizeName(Value);
		if (Normalized == TEXT("ignore")) { OutResponse = ECR_Ignore; return true; }
		if (Normalized == TEXT("overlap")) { OutResponse = ECR_Overlap; return true; }
		if (Normalized == TEXT("block")) { OutResponse = ECR_Block; return true; }

		OutError = FString::Printf(TEXT("Unsupported collision response '%s'"), *Value);
		return false;
	}

	bool TryParseLinearMotion(
		const FString& Value,
		ELinearConstraintMotion& OutMotion,
		FString& OutError)
	{
		const FString Normalized = NormalizeName(Value);
		if (Normalized == TEXT("free")) { OutMotion = LCM_Free; return true; }
		if (Normalized == TEXT("limited")) { OutMotion = LCM_Limited; return true; }
		if (Normalized == TEXT("locked")) { OutMotion = LCM_Locked; return true; }

		OutError = FString::Printf(TEXT("Unsupported linear constraint motion '%s'"), *Value);
		return false;
	}

	bool TryParseAngularMotion(
		const FString& Value,
		EAngularConstraintMotion& OutMotion,
		FString& OutError)
	{
		const FString Normalized = NormalizeName(Value);
		if (Normalized == TEXT("free")) { OutMotion = ACM_Free; return true; }
		if (Normalized == TEXT("limited")) { OutMotion = ACM_Limited; return true; }
		if (Normalized == TEXT("locked")) { OutMotion = ACM_Locked; return true; }

		OutError = FString::Printf(TEXT("Unsupported angular constraint motion '%s'"), *Value);
		return false;
	}

	FString CollisionEnabledToString(ECollisionEnabled::Type Type)
	{
		switch (Type)
		{
		case ECollisionEnabled::NoCollision: return TEXT("NoCollision");
		case ECollisionEnabled::QueryOnly: return TEXT("QueryOnly");
		case ECollisionEnabled::PhysicsOnly: return TEXT("PhysicsOnly");
		case ECollisionEnabled::QueryAndPhysics: return TEXT("QueryAndPhysics");
		case ECollisionEnabled::ProbeOnly: return TEXT("ProbeOnly");
		case ECollisionEnabled::QueryAndProbe: return TEXT("QueryAndProbe");
		default: return TEXT("Unknown");
		}
	}

	FString CollisionChannelToString(ECollisionChannel Channel)
	{
		switch (Channel)
		{
		case ECC_WorldStatic: return TEXT("WorldStatic");
		case ECC_WorldDynamic: return TEXT("WorldDynamic");
		case ECC_Pawn: return TEXT("Pawn");
		case ECC_Visibility: return TEXT("Visibility");
		case ECC_Camera: return TEXT("Camera");
		case ECC_PhysicsBody: return TEXT("PhysicsBody");
		case ECC_Vehicle: return TEXT("Vehicle");
		case ECC_Destructible: return TEXT("Destructible");
		default: return StaticEnum<ECollisionChannel>()->GetNameStringByValue(static_cast<int64>(Channel));
		}
	}

	FString CollisionResponseToString(ECollisionResponse Response)
	{
		switch (Response)
		{
		case ECR_Ignore: return TEXT("Ignore");
		case ECR_Overlap: return TEXT("Overlap");
		case ECR_Block: return TEXT("Block");
		default: return TEXT("Unknown");
		}
	}

	FString LinearMotionToString(ELinearConstraintMotion Motion)
	{
		switch (Motion)
		{
		case LCM_Free: return TEXT("Free");
		case LCM_Limited: return TEXT("Limited");
		case LCM_Locked: return TEXT("Locked");
		default: return TEXT("Unknown");
		}
	}

	FString AngularMotionToString(EAngularConstraintMotion Motion)
	{
		switch (Motion)
		{
		case ACM_Free: return TEXT("Free");
		case ACM_Limited: return TEXT("Limited");
		case ACM_Locked: return TEXT("Locked");
		default: return TEXT("Unknown");
		}
	}

	TSharedPtr<FJsonObject> SerializePrimitiveComponent(UPrimitiveComponent* Component)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		if (!Component)
		{
			Object->SetBoolField(TEXT("valid"), false);
			return Object;
		}

		Object->SetBoolField(TEXT("valid"), true);
		Object->SetStringField(TEXT("name"), Component->GetName());
		Object->SetStringField(TEXT("class"), Component->GetClass()->GetName());
		Object->SetStringField(TEXT("owner_name"), Component->GetOwner() ? Component->GetOwner()->GetActorNameOrLabel() : FString());
		Object->SetBoolField(TEXT("is_root"), Component->GetOwner() && Component->GetOwner()->GetRootComponent() == Component);

		if (const USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
		{
			Object->SetStringField(TEXT("mobility"), SceneComponent->Mobility == EComponentMobility::Movable ? TEXT("Movable") :
				(SceneComponent->Mobility == EComponentMobility::Stationary ? TEXT("Stationary") : TEXT("Static")));
			Object->SetObjectField(TEXT("world_location"), VectorToJson(SceneComponent->GetComponentLocation()));
		}

		Object->SetStringField(TEXT("collision_profile"), Component->GetCollisionProfileName().ToString());
		Object->SetStringField(TEXT("collision_enabled"), CollisionEnabledToString(Component->GetCollisionEnabled()));
		Object->SetStringField(TEXT("object_type"), CollisionChannelToString(Component->GetCollisionObjectType()));

		TSharedPtr<FJsonObject> Responses = MakeShareable(new FJsonObject);
		for (ECollisionChannel Channel : StandardChannels())
		{
			Responses->SetStringField(CollisionChannelToString(Channel), CollisionResponseToString(Component->GetCollisionResponseToChannel(Channel)));
		}
		Object->SetObjectField(TEXT("responses"), Responses);

		Object->SetBoolField(TEXT("simulate_physics"), Component->IsSimulatingPhysics());
		Object->SetBoolField(TEXT("gravity_enabled"), Component->IsGravityEnabled());
		Object->SetNumberField(TEXT("mass_kg"), Component->GetMass());
		Object->SetNumberField(TEXT("mass_scale"), Component->GetMassScale());
		Object->SetNumberField(TEXT("linear_damping"), Component->GetLinearDamping());
		Object->SetNumberField(TEXT("angular_damping"), Component->GetAngularDamping());

		AddPhysicalMaterialFields(Object, Component->GetBodyInstance() ? Component->GetBodyInstance()->GetSimplePhysicalMaterial() : nullptr);
		Object->SetStringField(TEXT("physical_material_override"), ObjectPath(Component->GetBodyInstance() ? Component->GetBodyInstance()->GetSimplePhysicalMaterial() : nullptr));
		return Object;
	}

	TSharedPtr<FJsonObject> SerializeConstraintComponent(UPhysicsConstraintComponent* Component)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		if (!Component)
		{
			Object->SetBoolField(TEXT("valid"), false);
			return Object;
		}

		UPrimitiveComponent* Component1 = nullptr;
		UPrimitiveComponent* Component2 = nullptr;
		FName Bone1;
		FName Bone2;
		Component->GetConstrainedComponents(Component1, Bone1, Component2, Bone2);

		Object->SetBoolField(TEXT("valid"), true);
		Object->SetStringField(TEXT("name"), Component->GetName());
		Object->SetStringField(TEXT("class"), Component->GetClass()->GetName());
		Object->SetStringField(TEXT("owner_name"), Component->GetOwner() ? Component->GetOwner()->GetActorNameOrLabel() : FString());
		Object->SetObjectField(TEXT("component1"), ComponentRefToJson(Component1, Bone1));
		Object->SetObjectField(TEXT("component2"), ComponentRefToJson(Component2, Bone2));
		Object->SetBoolField(TEXT("disable_collision"), Component->ConstraintInstance.ProfileInstance.bDisableCollision);
		Object->SetBoolField(TEXT("broken"), Component->IsBroken());
		Object->SetBoolField(TEXT("projection_enabled"), Component->ConstraintInstance.ProfileInstance.bEnableProjection);

		TSharedPtr<FJsonObject> Linear = MakeShareable(new FJsonObject);
		Linear->SetStringField(TEXT("x_motion"), LinearMotionToString(Component->ConstraintInstance.GetLinearXMotion()));
		Linear->SetStringField(TEXT("y_motion"), LinearMotionToString(Component->ConstraintInstance.GetLinearYMotion()));
		Linear->SetStringField(TEXT("z_motion"), LinearMotionToString(Component->ConstraintInstance.GetLinearZMotion()));
		Linear->SetNumberField(TEXT("limit_cm"), Component->ConstraintInstance.GetLinearLimit());
		Object->SetObjectField(TEXT("linear_limit"), Linear);

		TSharedPtr<FJsonObject> Angular = MakeShareable(new FJsonObject);
		Angular->SetStringField(TEXT("swing1_motion"), AngularMotionToString(Component->ConstraintInstance.GetAngularSwing1Motion()));
		Angular->SetStringField(TEXT("swing2_motion"), AngularMotionToString(Component->ConstraintInstance.GetAngularSwing2Motion()));
		Angular->SetStringField(TEXT("twist_motion"), AngularMotionToString(Component->ConstraintInstance.GetAngularTwistMotion()));
		Angular->SetNumberField(TEXT("swing1_degrees"), Component->ConstraintInstance.GetAngularSwing1Limit());
		Angular->SetNumberField(TEXT("swing2_degrees"), Component->ConstraintInstance.GetAngularSwing2Limit());
		Angular->SetNumberField(TEXT("twist_degrees"), Component->ConstraintInstance.GetAngularTwistLimit());
		Object->SetObjectField(TEXT("angular_limit"), Angular);
		return Object;
	}

	TSharedPtr<FJsonObject> SerializeActorPhysics(AActor* Actor, bool bIncludeComponents, bool bIncludeConstraints)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		if (!Actor)
		{
			Object->SetBoolField(TEXT("valid"), false);
			return Object;
		}

		Object->SetBoolField(TEXT("valid"), true);
		Object->SetStringField(TEXT("actor_name"), Actor->GetName());
		Object->SetStringField(TEXT("actor_label"), Actor->GetActorNameOrLabel());
		Object->SetStringField(TEXT("actor_class"), Actor->GetClass()->GetName());

		TArray<UPrimitiveComponent*> PrimitiveComponents;
		Actor->GetComponents(PrimitiveComponents);
		int32 SimulatingCount = 0;
		for (UPrimitiveComponent* Component : PrimitiveComponents)
		{
			if (Component && Component->IsSimulatingPhysics())
			{
				++SimulatingCount;
			}
		}
		Object->SetNumberField(TEXT("primitive_component_count"), PrimitiveComponents.Num());
		Object->SetNumberField(TEXT("simulating_component_count"), SimulatingCount);

		TArray<UPhysicsConstraintComponent*> Constraints;
		Actor->GetComponents(Constraints);
		Object->SetNumberField(TEXT("constraint_component_count"), Constraints.Num());

		if (bIncludeComponents)
		{
			TArray<TSharedPtr<FJsonValue>> ComponentsJson;
			for (UPrimitiveComponent* Component : PrimitiveComponents)
			{
				ComponentsJson.Add(MakeShareable(new FJsonValueObject(SerializePrimitiveComponent(Component))));
			}
			Object->SetArrayField(TEXT("primitive_components"), ComponentsJson);
		}

		if (bIncludeConstraints)
		{
			TArray<TSharedPtr<FJsonValue>> ConstraintsJson;
			for (UPhysicsConstraintComponent* Constraint : Constraints)
			{
				ConstraintsJson.Add(MakeShareable(new FJsonValueObject(SerializeConstraintComponent(Constraint))));
			}
			Object->SetArrayField(TEXT("constraint_components"), ConstraintsJson);
		}

		return Object;
	}

	TSharedPtr<FJsonObject> SerializeWorldPhysics(UWorld* World)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		if (!World)
		{
			Object->SetBoolField(TEXT("valid"), false);
			return Object;
		}

		int32 ActorCount = 0;
		int32 PrimitiveCount = 0;
		int32 SimulatingCount = 0;
		int32 ConstraintCount = 0;
		TArray<TSharedPtr<FJsonValue>> SimulatingComponents;

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor)
			{
				continue;
			}

			++ActorCount;
			TArray<UPrimitiveComponent*> Primitives;
			Actor->GetComponents(Primitives);
			PrimitiveCount += Primitives.Num();
			for (UPrimitiveComponent* Primitive : Primitives)
			{
				if (Primitive && Primitive->IsSimulatingPhysics())
				{
					++SimulatingCount;
					TSharedPtr<FJsonObject> Item = MakeShareable(new FJsonObject);
					Item->SetStringField(TEXT("actor_name"), Actor->GetActorNameOrLabel());
					Item->SetStringField(TEXT("component_name"), Primitive->GetName());
					SimulatingComponents.Add(MakeShareable(new FJsonValueObject(Item)));
				}
			}

			TArray<UPhysicsConstraintComponent*> Constraints;
			Actor->GetComponents(Constraints);
			ConstraintCount += Constraints.Num();
		}

		Object->SetBoolField(TEXT("valid"), true);
		Object->SetStringField(TEXT("world_name"), World->GetName());
		Object->SetStringField(TEXT("world_path"), World->GetPathName());
		Object->SetNumberField(TEXT("actor_count"), ActorCount);
		Object->SetNumberField(TEXT("primitive_component_count"), PrimitiveCount);
		Object->SetNumberField(TEXT("simulating_component_count"), SimulatingCount);
		Object->SetNumberField(TEXT("constraint_component_count"), ConstraintCount);
		Object->SetArrayField(TEXT("simulating_components"), SimulatingComponents);
		return Object;
	}

	bool ApplyConstraintSettings(
		UPhysicsConstraintComponent* Constraint,
		const TSharedPtr<FJsonObject>& Arguments,
		FString& OutError)
	{
		if (!Constraint || !Arguments.IsValid())
		{
			OutError = TEXT("Constraint and arguments are required");
			return false;
		}

		bool bDisableCollision = false;
		if (TryReadBoolField(Arguments, TEXT("disable_collision"), bDisableCollision))
		{
			Constraint->SetDisableCollision(bDisableCollision);
		}

		const TSharedPtr<FJsonObject>* LinearObject = nullptr;
		if (Arguments->TryGetObjectField(TEXT("linear_limit"), LinearObject) && LinearObject && (*LinearObject).IsValid())
		{
			float Limit = Constraint->ConstraintInstance.GetLinearLimit();
			TryReadFloatField(*LinearObject, TEXT("limit_cm"), Limit);

			FString MotionName;
			ELinearConstraintMotion Motion = LCM_Limited;
			if ((*LinearObject)->TryGetStringField(TEXT("motion"), MotionName))
			{
				if (!TryParseLinearMotion(MotionName, Motion, OutError))
				{
					return false;
				}
				Constraint->SetLinearXLimit(Motion, Limit);
				Constraint->SetLinearYLimit(Motion, Limit);
				Constraint->SetLinearZLimit(Motion, Limit);
			}

			if ((*LinearObject)->TryGetStringField(TEXT("x_motion"), MotionName))
			{
				if (!TryParseLinearMotion(MotionName, Motion, OutError)) { return false; }
				Constraint->SetLinearXLimit(Motion, Limit);
			}
			if ((*LinearObject)->TryGetStringField(TEXT("y_motion"), MotionName))
			{
				if (!TryParseLinearMotion(MotionName, Motion, OutError)) { return false; }
				Constraint->SetLinearYLimit(Motion, Limit);
			}
			if ((*LinearObject)->TryGetStringField(TEXT("z_motion"), MotionName))
			{
				if (!TryParseLinearMotion(MotionName, Motion, OutError)) { return false; }
				Constraint->SetLinearZLimit(Motion, Limit);
			}
		}

		const TSharedPtr<FJsonObject>* AngularObject = nullptr;
		if (Arguments->TryGetObjectField(TEXT("angular_limit"), AngularObject) && AngularObject && (*AngularObject).IsValid())
		{
			FString MotionName;
			EAngularConstraintMotion Motion = ACM_Limited;
			float Degrees = 45.0f;

			if ((*AngularObject)->TryGetStringField(TEXT("swing1_motion"), MotionName))
			{
				if (!TryParseAngularMotion(MotionName, Motion, OutError)) { return false; }
				TryReadFloatField(*AngularObject, TEXT("swing1_degrees"), Degrees);
				Constraint->SetAngularSwing1Limit(Motion, Degrees);
			}
			if ((*AngularObject)->TryGetStringField(TEXT("swing2_motion"), MotionName))
			{
				if (!TryParseAngularMotion(MotionName, Motion, OutError)) { return false; }
				Degrees = 45.0f;
				TryReadFloatField(*AngularObject, TEXT("swing2_degrees"), Degrees);
				Constraint->SetAngularSwing2Limit(Motion, Degrees);
			}
			if ((*AngularObject)->TryGetStringField(TEXT("twist_motion"), MotionName))
			{
				if (!TryParseAngularMotion(MotionName, Motion, OutError)) { return false; }
				Degrees = 45.0f;
				TryReadFloatField(*AngularObject, TEXT("twist_degrees"), Degrees);
				Constraint->SetAngularTwistLimit(Motion, Degrees);
			}
		}

		bool bProjectionEnabled = false;
		if (TryReadBoolField(Arguments, TEXT("projection_enabled"), bProjectionEnabled))
		{
			Constraint->SetProjectionEnabled(bProjectionEnabled);
		}

		bool bLinearBreakable = false;
		float LinearBreakThreshold = 0.0f;
		if (TryReadBoolField(Arguments, TEXT("linear_breakable"), bLinearBreakable) || TryReadFloatField(Arguments, TEXT("linear_break_threshold"), LinearBreakThreshold))
		{
			TryReadFloatField(Arguments, TEXT("linear_break_threshold"), LinearBreakThreshold);
			Constraint->SetLinearBreakable(bLinearBreakable, LinearBreakThreshold);
		}

		bool bAngularBreakable = false;
		float AngularBreakThreshold = 0.0f;
		if (TryReadBoolField(Arguments, TEXT("angular_breakable"), bAngularBreakable) || TryReadFloatField(Arguments, TEXT("angular_break_threshold"), AngularBreakThreshold))
		{
			TryReadFloatField(Arguments, TEXT("angular_break_threshold"), AngularBreakThreshold);
			Constraint->SetAngularBreakable(bAngularBreakable, AngularBreakThreshold);
		}

		return true;
	}

	void FinalizeActorPhysicsEdit(AActor* Actor)
	{
		if (!Actor)
		{
			return;
		}
		Actor->PostEditChange();
		if (UWorld* World = Actor->GetWorld())
		{
			World->MarkPackageDirty();
		}
	}
}
