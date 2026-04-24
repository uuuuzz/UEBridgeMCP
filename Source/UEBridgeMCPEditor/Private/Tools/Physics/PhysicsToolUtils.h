// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Dom/JsonObject.h"

class AActor;
class UPhysicalMaterial;
class UPhysicsConstraintComponent;
class UPrimitiveComponent;
class UWorld;
struct FMcpToolContext;

namespace PhysicsToolUtils
{
	UPrimitiveComponent* ResolvePrimitiveComponent(
		AActor* Actor,
		const FString& ComponentName,
		FString& OutErrorCode,
		FString& OutErrorMessage);

	UPhysicsConstraintComponent* ResolveConstraintComponent(
		AActor* Actor,
		const FString& ComponentName,
		FString& OutErrorCode,
		FString& OutErrorMessage);

	UPhysicsConstraintComponent* CreateConstraintComponent(
		AActor* Actor,
		const FString& RequestedName);

	bool TryLoadPhysicalMaterial(
		const FString& AssetPath,
		UPhysicalMaterial*& OutMaterial,
		FString& OutError);

	bool TryParseCollisionEnabled(
		const FString& Value,
		ECollisionEnabled::Type& OutType,
		FString& OutError);

	bool TryParseCollisionChannel(
		const FString& Value,
		ECollisionChannel& OutChannel,
		FString& OutError);

	bool TryParseCollisionResponse(
		const FString& Value,
		ECollisionResponse& OutResponse,
		FString& OutError);

	bool TryParseLinearMotion(
		const FString& Value,
		ELinearConstraintMotion& OutMotion,
		FString& OutError);

	bool TryParseAngularMotion(
		const FString& Value,
		EAngularConstraintMotion& OutMotion,
		FString& OutError);

	FString CollisionEnabledToString(ECollisionEnabled::Type Type);
	FString CollisionChannelToString(ECollisionChannel Channel);
	FString CollisionResponseToString(ECollisionResponse Response);
	FString LinearMotionToString(ELinearConstraintMotion Motion);
	FString AngularMotionToString(EAngularConstraintMotion Motion);

	TSharedPtr<FJsonObject> SerializePrimitiveComponent(UPrimitiveComponent* Component);
	TSharedPtr<FJsonObject> SerializeConstraintComponent(UPhysicsConstraintComponent* Component);
	TSharedPtr<FJsonObject> SerializeActorPhysics(AActor* Actor, bool bIncludeComponents, bool bIncludeConstraints);
	TSharedPtr<FJsonObject> SerializeWorldPhysics(UWorld* World);

	bool ApplyConstraintSettings(
		UPhysicsConstraintComponent* Constraint,
		const TSharedPtr<FJsonObject>& Arguments,
		FString& OutError);

	void FinalizeActorPhysicsEdit(AActor* Actor);
}
