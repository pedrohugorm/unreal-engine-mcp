#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for Data Asset MCP commands
 * Handles reading, writing, and creating Data Assets via UE reflection
 */
class UNREALMCP_API FEpicUnrealMCPDataAssetCommands
{
public:
	FEpicUnrealMCPDataAssetCommands();

	// Handle data asset commands
	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	// Command handlers
	TSharedPtr<FJsonObject> HandleReadDataAsset(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleWriteDataAsset(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleCreateDataAsset(const TSharedPtr<FJsonObject>& Params);

	// Core serialization: raw value pointer → JSON (handles all types including containers)
	TSharedPtr<FJsonValue> SerializeValueDirect(FProperty* Property, const void* ValuePtr);
	TSharedPtr<FJsonValue> SerializeStructProperty(FStructProperty* StructProp, const void* ValuePtr);

	// Top-level serialization: computes ValuePtr from ContainerPtr, then delegates
	TSharedPtr<FJsonValue> SerializeProperty(FProperty* Property, const void* ContainerPtr);

	// Core deserialization: JSON → raw value pointer (handles all types including containers)
	bool DeserializeValueDirect(FProperty* Property, void* ValuePtr, const TSharedPtr<FJsonValue>& JsonValue, FString& OutError);
	bool DeserializeStructProperty(FStructProperty* StructProp, void* ValuePtr, const TSharedPtr<FJsonObject>& JsonObject, FString& OutError);

	// Top-level deserialization: computes ValuePtr from ContainerPtr, then delegates
	bool DeserializeProperty(FProperty* Property, void* ContainerPtr, const TSharedPtr<FJsonValue>& JsonValue, FString& OutError);

	// Validation (recursive)
	bool ValidatePropertyValue(FProperty* Property, const TSharedPtr<FJsonValue>& JsonValue, FString& OutError);

	// Metadata
	TSharedPtr<FJsonObject> GetPropertyMetadata(FProperty* Property);
	FString GetPropertyTypeName(FProperty* Property);

	// Utilities
	UObject* LoadDataAsset(const FString& AssetPath, FString& OutError);
	bool IsDataAssetProperty(FProperty* Property);
};