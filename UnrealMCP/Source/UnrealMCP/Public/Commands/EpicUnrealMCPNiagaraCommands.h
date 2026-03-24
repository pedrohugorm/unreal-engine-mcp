#pragma once

#include "CoreMinimal.h"
#include "Json.h"

class UNiagaraSystem;
class UNiagaraStackEntry;
class UNiagaraStackFunctionInput;
class FNiagaraSystemViewModel;
struct FNiagaraTypeDefinition;

/**
 * Handler class for Niagara MCP commands
 * Reads and edits existing Niagara Systems via FNiagaraSystemViewModel (stack layer)
 */
class UNREALMCP_API FEpicUnrealMCPNiagaraCommands
{
public:
	FEpicUnrealMCPNiagaraCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	// Command handlers
	TSharedPtr<FJsonObject> HandleReadNiagaraSystem(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleWriteNiagaraModuleInput(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddNiagaraModule(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRemoveNiagaraModule(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleListNiagaraModules(const TSharedPtr<FJsonObject>& Params);

	// Helpers
	UNiagaraSystem* LoadNiagaraSystem(const FString& AssetPath, FString& OutError);
	TSharedPtr<FNiagaraSystemViewModel> CreateSystemViewModel(UNiagaraSystem* System);
	TSharedPtr<FJsonObject> SerializeStackEntry(UNiagaraStackEntry* Entry, int32 Depth = 0);
	TSharedPtr<FJsonValue> SerializeInputValue(UNiagaraStackFunctionInput* Input);
	FString GetNiagaraTypeName(const FNiagaraTypeDefinition& TypeDef);
};