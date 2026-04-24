// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;
class UNiagaraComponent;
class UNiagaraSystem;
struct FNiagaraEmitterHandle;
struct FNiagaraParameterStore;
struct FNiagaraTypeDefinition;
struct FNiagaraUserRedirectionParameterStore;
struct FNiagaraVariable;

namespace NiagaraToolUtils
{
	FString NormalizeUserParameterName(const FString& Name);
	FString StripUserNamespace(const FString& Name);

	bool TryResolveType(const FString& TypeName, FNiagaraTypeDefinition& OutType, FString& OutError);
	FString TypeToString(const FNiagaraTypeDefinition& Type);

	bool TryFindUserParameter(
		const FNiagaraUserRedirectionParameterStore& Store,
		const FString& Name,
		FNiagaraVariable& OutVariable);

	TSharedPtr<FJsonObject> SerializeVariable(
		const FNiagaraVariable& Variable,
		const FNiagaraParameterStore* Store = nullptr);

	TArray<TSharedPtr<FJsonValue>> SerializeUserParameters(const FNiagaraUserRedirectionParameterStore& Store);
	TSharedPtr<FJsonObject> SerializeEmitterHandle(const FNiagaraEmitterHandle& Handle, bool bIncludeRenderers);
	TSharedPtr<FJsonObject> SerializeSystemSummary(UNiagaraSystem* System, bool bIncludeEmitters, bool bIncludeUserParameters);

	bool SetParameterStoreValue(
		FNiagaraParameterStore& Store,
		const FNiagaraVariable& Variable,
		const TSharedPtr<FJsonValue>& Value,
		FString& OutError);

	bool ApplyComponentOverride(
		UNiagaraComponent* Component,
		const FString& Name,
		const FString& Type,
		const TSharedPtr<FJsonValue>& Value,
		FString& OutError);

	TSharedPtr<FJsonObject> CompileSystem(UNiagaraSystem* System, bool bForce = true);
	bool SaveAsset(UObject* Asset, TArray<TSharedPtr<FJsonValue>>& OutWarnings);
}
