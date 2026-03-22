#include "Commands/EpicUnrealMCPDataAssetCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Engine/DataAsset.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "UObject/EnumProperty.h"
#include "UObject/TextProperty.h"
#include "UObject/SoftObjectPtr.h"
#include "Misc/PackageName.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

FEpicUnrealMCPDataAssetCommands::FEpicUnrealMCPDataAssetCommands()
{
}

TSharedPtr<FJsonObject> FEpicUnrealMCPDataAssetCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("read_data_asset"))
	{
		return HandleReadDataAsset(Params);
	}
	else if (CommandType == TEXT("write_data_asset"))
	{
		return HandleWriteDataAsset(Params);
	}
	else if (CommandType == TEXT("create_data_asset"))
	{
		return HandleCreateDataAsset(Params);
	}

	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown data asset command: %s"), *CommandType));
}

// ============================================================================
// Asset Loading Utility
// ============================================================================

UObject* FEpicUnrealMCPDataAssetCommands::LoadDataAsset(const FString& AssetPath, FString& OutError)
{
	FString FullPath = AssetPath;
	if (!FullPath.Contains(TEXT(".")))
	{
		FString AssetName = FPaths::GetBaseFilename(FullPath);
		FullPath = FString::Printf(TEXT("%s.%s"), *FullPath, *AssetName);
	}

	UObject* Asset = UEditorAssetLibrary::LoadAsset(FullPath);
	if (!Asset)
	{
		Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	}

	if (!Asset)
	{
		OutError = FString::Printf(TEXT("Failed to load asset at path: %s"), *AssetPath);
		return nullptr;
	}

	return Asset;
}

// ============================================================================
// Property Type Helpers
// ============================================================================

bool FEpicUnrealMCPDataAssetCommands::IsDataAssetProperty(FProperty* Property)
{
	UClass* OwnerClass = Property->GetOwnerClass();
	if (OwnerClass == UObject::StaticClass() || OwnerClass == UDataAsset::StaticClass() || OwnerClass == UPrimaryDataAsset::StaticClass())
	{
		return false;
	}
	if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient))
	{
		return false;
	}
	return true;
}

FString FEpicUnrealMCPDataAssetCommands::GetPropertyTypeName(FProperty* Property)
{
	if (CastField<FBoolProperty>(Property)) return TEXT("bool");
	if (FByteProperty* BP = CastField<FByteProperty>(Property))
	{
		UEnum* E = BP->GetIntPropertyEnum();
		return E ? FString::Printf(TEXT("TEnumAsByte<%s>"), *E->GetName()) : TEXT("uint8");
	}
	if (CastField<FIntProperty>(Property)) return TEXT("int32");
	if (CastField<FInt64Property>(Property)) return TEXT("int64");
	if (CastField<FUInt32Property>(Property)) return TEXT("uint32");
	if (CastField<FFloatProperty>(Property)) return TEXT("float");
	if (CastField<FDoubleProperty>(Property)) return TEXT("double");
	if (CastField<FStrProperty>(Property)) return TEXT("FString");
	if (CastField<FNameProperty>(Property)) return TEXT("FName");
	if (CastField<FTextProperty>(Property)) return TEXT("FText");
	if (FEnumProperty* EP = CastField<FEnumProperty>(Property))
	{
		UEnum* E = EP->GetEnum();
		return E ? E->GetName() : TEXT("enum");
	}
	if (FStructProperty* SP = CastField<FStructProperty>(Property)) return SP->Struct->GetName();
	if (FArrayProperty* AP = CastField<FArrayProperty>(Property))
		return FString::Printf(TEXT("TArray<%s>"), *GetPropertyTypeName(AP->Inner));
	if (FMapProperty* MP = CastField<FMapProperty>(Property))
		return FString::Printf(TEXT("TMap<%s, %s>"), *GetPropertyTypeName(MP->KeyProp), *GetPropertyTypeName(MP->ValueProp));
	if (FSetProperty* SeP = CastField<FSetProperty>(Property))
		return FString::Printf(TEXT("TSet<%s>"), *GetPropertyTypeName(SeP->ElementProp));
	if (FObjectProperty* OP = CastField<FObjectProperty>(Property))
		return FString::Printf(TEXT("%s*"), *OP->PropertyClass->GetName());
	if (FSoftObjectProperty* SOP = CastField<FSoftObjectProperty>(Property))
		return FString::Printf(TEXT("TSoftObjectPtr<%s>"), *SOP->PropertyClass->GetName());

	return Property->GetCPPType();
}

TSharedPtr<FJsonObject> FEpicUnrealMCPDataAssetCommands::GetPropertyMetadata(FProperty* Property)
{
	TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();

	if (Property->HasMetaData(TEXT("ClampMin")))
		Meta->SetNumberField(TEXT("ClampMin"), FCString::Atod(*Property->GetMetaData(TEXT("ClampMin"))));
	if (Property->HasMetaData(TEXT("ClampMax")))
		Meta->SetNumberField(TEXT("ClampMax"), FCString::Atod(*Property->GetMetaData(TEXT("ClampMax"))));
	if (Property->HasMetaData(TEXT("UIMin")))
		Meta->SetNumberField(TEXT("UIMin"), FCString::Atod(*Property->GetMetaData(TEXT("UIMin"))));
	if (Property->HasMetaData(TEXT("UIMax")))
		Meta->SetNumberField(TEXT("UIMax"), FCString::Atod(*Property->GetMetaData(TEXT("UIMax"))));

	UEnum* Enum = nullptr;
	if (FEnumProperty* EP = CastField<FEnumProperty>(Property)) Enum = EP->GetEnum();
	else if (FByteProperty* BP = CastField<FByteProperty>(Property)) Enum = BP->GetIntPropertyEnum();

	if (Enum)
	{
		TArray<TSharedPtr<FJsonValue>> EnumValues;
		for (int32 i = 0; i < Enum->NumEnums() - 1; i++)
		{
			if (!Enum->HasMetaData(TEXT("Hidden"), i))
			{
				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("name"), Enum->GetNameStringByIndex(i));
				Entry->SetNumberField(TEXT("value"), Enum->GetValueByIndex(i));
				FString Display = Enum->GetDisplayNameTextByIndex(i).ToString();
				if (!Display.IsEmpty()) Entry->SetStringField(TEXT("display_name"), Display);
				EnumValues.Add(MakeShared<FJsonValueObject>(Entry));
			}
		}
		Meta->SetArrayField(TEXT("enum_values"), EnumValues);
	}

	if (Property->HasMetaData(TEXT("ToolTip")))
		Meta->SetStringField(TEXT("tooltip"), Property->GetMetaData(TEXT("ToolTip")));
	if (Property->HasMetaData(TEXT("EditCondition")))
		Meta->SetStringField(TEXT("edit_condition"), Property->GetMetaData(TEXT("EditCondition")));

	return Meta->Values.Num() > 0 ? Meta : nullptr;
}

// ============================================================================
// Core Serialization: raw value pointer → JSON (single source of truth)
// ============================================================================

TSharedPtr<FJsonValue> FEpicUnrealMCPDataAssetCommands::SerializeValueDirect(FProperty* Property, const void* ValuePtr)
{
	if (FBoolProperty* P = CastField<FBoolProperty>(Property))
		return MakeShared<FJsonValueBoolean>(P->GetPropertyValue(ValuePtr));
	if (FByteProperty* P = CastField<FByteProperty>(Property))
	{
		UEnum* E = P->GetIntPropertyEnum();
		if (E) return MakeShared<FJsonValueString>(E->GetNameStringByValue(P->GetPropertyValue(ValuePtr)));
		return MakeShared<FJsonValueNumber>(P->GetPropertyValue(ValuePtr));
	}
	if (FIntProperty* P = CastField<FIntProperty>(Property))
		return MakeShared<FJsonValueNumber>(P->GetPropertyValue(ValuePtr));
	if (FInt64Property* P = CastField<FInt64Property>(Property))
		return MakeShared<FJsonValueNumber>(static_cast<double>(P->GetPropertyValue(ValuePtr)));
	if (FUInt32Property* P = CastField<FUInt32Property>(Property))
		return MakeShared<FJsonValueNumber>(static_cast<double>(P->GetPropertyValue(ValuePtr)));
	if (FFloatProperty* P = CastField<FFloatProperty>(Property))
		return MakeShared<FJsonValueNumber>(P->GetPropertyValue(ValuePtr));
	if (FDoubleProperty* P = CastField<FDoubleProperty>(Property))
		return MakeShared<FJsonValueNumber>(P->GetPropertyValue(ValuePtr));
	if (FStrProperty* P = CastField<FStrProperty>(Property))
		return MakeShared<FJsonValueString>(P->GetPropertyValue(ValuePtr));
	if (FNameProperty* P = CastField<FNameProperty>(Property))
		return MakeShared<FJsonValueString>(P->GetPropertyValue(ValuePtr).ToString());
	if (FTextProperty* P = CastField<FTextProperty>(Property))
		return MakeShared<FJsonValueString>(P->GetPropertyValue(ValuePtr).ToString());
	if (FEnumProperty* P = CastField<FEnumProperty>(Property))
	{
		UEnum* E = P->GetEnum();
		FNumericProperty* U = P->GetUnderlyingProperty();
		if (E && U) return MakeShared<FJsonValueString>(E->GetNameStringByValue(U->GetSignedIntPropertyValue(ValuePtr)));
		return MakeShared<FJsonValueNull>();
	}
	if (FStructProperty* P = CastField<FStructProperty>(Property))
		return SerializeStructProperty(P, ValuePtr);
	if (FArrayProperty* P = CastField<FArrayProperty>(Property))
	{
		FScriptArrayHelper H(P, ValuePtr);
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (int32 i = 0; i < H.Num(); i++)
			Arr.Add(SerializeValueDirect(P->Inner, H.GetRawPtr(i)));
		return MakeShared<FJsonValueArray>(Arr);
	}
	if (FMapProperty* P = CastField<FMapProperty>(Property))
	{
		FScriptMapHelper H(P, ValuePtr);
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (int32 i = 0; i < H.GetMaxIndex(); i++)
		{
			if (!H.IsValidIndex(i)) continue;
			TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
			E->SetField(TEXT("key"), SerializeValueDirect(P->KeyProp, H.GetKeyPtr(i)));
			E->SetField(TEXT("value"), SerializeValueDirect(P->ValueProp, H.GetValuePtr(i)));
			Arr.Add(MakeShared<FJsonValueObject>(E));
		}
		return MakeShared<FJsonValueArray>(Arr);
	}
	if (FSetProperty* P = CastField<FSetProperty>(Property))
	{
		FScriptSetHelper H(P, ValuePtr);
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (int32 i = 0; i < H.GetMaxIndex(); i++)
		{
			if (!H.IsValidIndex(i)) continue;
			Arr.Add(SerializeValueDirect(P->ElementProp, H.GetElementPtr(i)));
		}
		return MakeShared<FJsonValueArray>(Arr);
	}
	if (FObjectProperty* P = CastField<FObjectProperty>(Property))
	{
		UObject* O = P->GetPropertyValue(ValuePtr);
		return O ? MakeShared<FJsonValueString>(O->GetPathName()) : MakeShared<FJsonValueNull>();
	}
	if (FSoftObjectProperty* P = CastField<FSoftObjectProperty>(Property))
	{
		const FSoftObjectPtr& S = *static_cast<const FSoftObjectPtr*>(ValuePtr);
		FSoftObjectPath Path = S.ToSoftObjectPath();
		return Path.IsValid() ? MakeShared<FJsonValueString>(Path.ToString()) : MakeShared<FJsonValueNull>();
	}

	// Fallback
	FString Exported;
	Property->ExportTextItem_Direct(Exported, ValuePtr, nullptr, nullptr, PPF_None);
	return MakeShared<FJsonValueString>(Exported);
}

// Top-level entry: computes ValuePtr from ContainerPtr, then delegates
TSharedPtr<FJsonValue> FEpicUnrealMCPDataAssetCommands::SerializeProperty(FProperty* Property, const void* ContainerPtr)
{
	const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(ContainerPtr);
	return SerializeValueDirect(Property, ValuePtr);
}

TSharedPtr<FJsonValue> FEpicUnrealMCPDataAssetCommands::SerializeStructProperty(FStructProperty* StructProp, const void* ValuePtr)
{
	UScriptStruct* Struct = StructProp->Struct;
	FName N = Struct->GetFName();

	if (N == NAME_Vector)
	{
		const FVector& V = *static_cast<const FVector*>(ValuePtr);
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("X"), V.X); O->SetNumberField(TEXT("Y"), V.Y); O->SetNumberField(TEXT("Z"), V.Z);
		return MakeShared<FJsonValueObject>(O);
	}
	if (N == NAME_Rotator)
	{
		const FRotator& R = *static_cast<const FRotator*>(ValuePtr);
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("Pitch"), R.Pitch); O->SetNumberField(TEXT("Yaw"), R.Yaw); O->SetNumberField(TEXT("Roll"), R.Roll);
		return MakeShared<FJsonValueObject>(O);
	}
	if (N == NAME_Color)
	{
		const FColor& C = *static_cast<const FColor*>(ValuePtr);
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("R"), C.R); O->SetNumberField(TEXT("G"), C.G); O->SetNumberField(TEXT("B"), C.B); O->SetNumberField(TEXT("A"), C.A);
		return MakeShared<FJsonValueObject>(O);
	}
	if (N == NAME_LinearColor)
	{
		const FLinearColor& C = *static_cast<const FLinearColor*>(ValuePtr);
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("R"), C.R); O->SetNumberField(TEXT("G"), C.G); O->SetNumberField(TEXT("B"), C.B); O->SetNumberField(TEXT("A"), C.A);
		return MakeShared<FJsonValueObject>(O);
	}
	if (N == NAME_Vector2D)
	{
		const FVector2D& V = *static_cast<const FVector2D*>(ValuePtr);
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("X"), V.X); O->SetNumberField(TEXT("Y"), V.Y);
		return MakeShared<FJsonValueObject>(O);
	}
	if (N == NAME_Transform)
	{
		const FTransform& T = *static_cast<const FTransform*>(ValuePtr);
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		TSharedPtr<FJsonObject> L = MakeShared<FJsonObject>();
		L->SetNumberField(TEXT("X"), T.GetLocation().X); L->SetNumberField(TEXT("Y"), T.GetLocation().Y); L->SetNumberField(TEXT("Z"), T.GetLocation().Z);
		O->SetObjectField(TEXT("Location"), L);
		FRotator R = T.GetRotation().Rotator();
		TSharedPtr<FJsonObject> RO = MakeShared<FJsonObject>();
		RO->SetNumberField(TEXT("Pitch"), R.Pitch); RO->SetNumberField(TEXT("Yaw"), R.Yaw); RO->SetNumberField(TEXT("Roll"), R.Roll);
		O->SetObjectField(TEXT("Rotation"), RO);
		TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetNumberField(TEXT("X"), T.GetScale3D().X); S->SetNumberField(TEXT("Y"), T.GetScale3D().Y); S->SetNumberField(TEXT("Z"), T.GetScale3D().Z);
		O->SetObjectField(TEXT("Scale"), S);
		return MakeShared<FJsonValueObject>(O);
	}

	// Generic struct
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		FProperty* F = *It;
		const void* FV = F->ContainerPtrToValuePtr<void>(ValuePtr);
		Obj->SetField(F->GetName(), SerializeValueDirect(F, FV));
	}
	return MakeShared<FJsonValueObject>(Obj);
}


// ============================================================================
// Validation (byte/enum before numeric; recursive structs, arrays, maps, sets)
// ============================================================================

bool FEpicUnrealMCPDataAssetCommands::ValidatePropertyValue(FProperty* Property, const TSharedPtr<FJsonValue>& JsonValue, FString& OutError)
{
	if (!JsonValue.IsValid())
	{
		OutError = FString::Printf(TEXT("Null value for property '%s'"), *Property->GetName());
		return false;
	}

	// Bool
	if (CastField<FBoolProperty>(Property))
	{
		if (JsonValue->Type != EJson::Boolean && JsonValue->Type != EJson::Number)
		{
			OutError = FString::Printf(TEXT("Property '%s' expects a boolean"), *Property->GetName());
			return false;
		}
		return true;
	}

	// Byte enum — before generic numeric
	if (FByteProperty* BP = CastField<FByteProperty>(Property))
	{
		UEnum* E = BP->GetIntPropertyEnum();
		if (E)
		{
			if (JsonValue->Type == EJson::String)
			{
				FString Name = JsonValue->AsString();
				if (Name.Contains(TEXT("::"))) Name.Split(TEXT("::"), nullptr, &Name);
				if (E->GetValueByNameString(Name) == INDEX_NONE)
				{
					FString Valid;
					for (int32 i = 0; i < E->NumEnums() - 1; i++)
					{
						if (i > 0) Valid += TEXT(", ");
						Valid += E->GetNameStringByIndex(i);
					}
					OutError = FString::Printf(TEXT("Property '%s': '%s' is not a valid enum value. Valid: %s"), *Property->GetName(), *JsonValue->AsString(), *Valid);
					return false;
				}
				return true;
			}
			if (JsonValue->Type == EJson::Number) return true;
			OutError = FString::Printf(TEXT("Property '%s' expects a string (enum name) or number"), *Property->GetName());
			return false;
		}
		// Raw uint8 falls through to numeric
	}

	// Enum class — before generic numeric
	if (FEnumProperty* EP = CastField<FEnumProperty>(Property))
	{
		UEnum* E = EP->GetEnum();
		if (E && JsonValue->Type == EJson::String)
		{
			FString Name = JsonValue->AsString();
			if (Name.Contains(TEXT("::"))) Name.Split(TEXT("::"), nullptr, &Name);
			if (E->GetValueByNameString(Name) == INDEX_NONE)
			{
				FString Valid;
				for (int32 i = 0; i < E->NumEnums() - 1; i++)
				{
					if (i > 0) Valid += TEXT(", ");
					Valid += E->GetNameStringByIndex(i);
				}
				OutError = FString::Printf(TEXT("Property '%s': '%s' is not a valid enum value. Valid: %s"), *Property->GetName(), *JsonValue->AsString(), *Valid);
				return false;
			}
		}
		else if (JsonValue->Type != EJson::Number && JsonValue->Type != EJson::String)
		{
			OutError = FString::Printf(TEXT("Property '%s' expects a string (enum name) or number"), *Property->GetName());
			return false;
		}
		return true;
	}

	// Numeric (int, float, double, raw byte)
	if (CastField<FNumericProperty>(Property))
	{
		if (JsonValue->Type != EJson::Number)
		{
			OutError = FString::Printf(TEXT("Property '%s' expects a number"), *Property->GetName());
			return false;
		}
		double V = JsonValue->AsNumber();
		if (Property->HasMetaData(TEXT("ClampMin")))
		{
			double Min = FCString::Atod(*Property->GetMetaData(TEXT("ClampMin")));
			if (V < Min) { OutError = FString::Printf(TEXT("Property '%s': value %f is below ClampMin of %f"), *Property->GetName(), V, Min); return false; }
		}
		if (Property->HasMetaData(TEXT("ClampMax")))
		{
			double Max = FCString::Atod(*Property->GetMetaData(TEXT("ClampMax")));
			if (V > Max) { OutError = FString::Printf(TEXT("Property '%s': value %f exceeds ClampMax of %f"), *Property->GetName(), V, Max); return false; }
		}
		return true;
	}

	// Strings
	if (CastField<FStrProperty>(Property) || CastField<FNameProperty>(Property) || CastField<FTextProperty>(Property))
	{
		if (JsonValue->Type != EJson::String) { OutError = FString::Printf(TEXT("Property '%s' expects a string"), *Property->GetName()); return false; }
		return true;
	}

	// Struct — recursive
	if (FStructProperty* SP = CastField<FStructProperty>(Property))
	{
		if (JsonValue->Type != EJson::Object)
		{
			OutError = FString::Printf(TEXT("Property '%s' expects an object (struct %s)"), *Property->GetName(), *SP->Struct->GetName());
			return false;
		}
		const TSharedPtr<FJsonObject>& JO = JsonValue->AsObject();
		for (const auto& F : JO->Values)
		{
			FProperty* FP = SP->Struct->FindPropertyByName(*F.Key);
			if (!FP)
			{
				OutError = FString::Printf(TEXT("In struct '%s': unknown field '%s'"), *Property->GetName(), *F.Key);
				return false;
			}
			FString FE;
			if (!ValidatePropertyValue(FP, F.Value, FE))
			{
				OutError = FString::Printf(TEXT("In struct '%s' field '%s': %s"), *Property->GetName(), *F.Key, *FE);
				return false;
			}
		}
		return true;
	}

	// Array — recursive
	if (FArrayProperty* AP = CastField<FArrayProperty>(Property))
	{
		if (JsonValue->Type != EJson::Array) { OutError = FString::Printf(TEXT("Property '%s' expects an array"), *Property->GetName()); return false; }
		const auto& A = JsonValue->AsArray();
		for (int32 i = 0; i < A.Num(); i++)
		{
			FString EE;
			if (!ValidatePropertyValue(AP->Inner, A[i], EE))
			{
				OutError = FString::Printf(TEXT("In array '%s' element %d: %s"), *Property->GetName(), i, *EE);
				return false;
			}
		}
		return true;
	}

	// Set — recursive
	if (FSetProperty* SeP = CastField<FSetProperty>(Property))
	{
		if (JsonValue->Type != EJson::Array) { OutError = FString::Printf(TEXT("Property '%s' expects an array"), *Property->GetName()); return false; }
		const auto& A = JsonValue->AsArray();
		for (int32 i = 0; i < A.Num(); i++)
		{
			FString EE;
			if (!ValidatePropertyValue(SeP->ElementProp, A[i], EE))
			{
				OutError = FString::Printf(TEXT("In set '%s' element %d: %s"), *Property->GetName(), i, *EE);
				return false;
			}
		}
		return true;
	}

	// Map — recursive (keys and values)
	if (FMapProperty* MP = CastField<FMapProperty>(Property))
	{
		if (JsonValue->Type != EJson::Array) { OutError = FString::Printf(TEXT("Property '%s' (TMap) expects an array of {key, value} objects"), *Property->GetName()); return false; }
		const auto& A = JsonValue->AsArray();
		for (int32 i = 0; i < A.Num(); i++)
		{
			if (A[i]->Type != EJson::Object) { OutError = FString::Printf(TEXT("Map '%s' entry %d must be an object"), *Property->GetName(), i); return false; }
			const auto& E = A[i]->AsObject();
			if (!E->HasField(TEXT("key")) || !E->HasField(TEXT("value")))
			{
				OutError = FString::Printf(TEXT("Map '%s' entry %d must have 'key' and 'value' fields"), *Property->GetName(), i);
				return false;
			}
			FString KE;
			if (!ValidatePropertyValue(MP->KeyProp, E->TryGetField(TEXT("key")), KE))
			{
				OutError = FString::Printf(TEXT("In map '%s' entry %d key: %s"), *Property->GetName(), i, *KE);
				return false;
			}
			FString VE;
			if (!ValidatePropertyValue(MP->ValueProp, E->TryGetField(TEXT("value")), VE))
			{
				OutError = FString::Printf(TEXT("In map '%s' entry %d value: %s"), *Property->GetName(), i, *VE);
				return false;
			}
		}
		return true;
	}

	// Object/soft object
	if (CastField<FObjectProperty>(Property) || CastField<FSoftObjectProperty>(Property))
	{
		if (JsonValue->Type != EJson::String && JsonValue->Type != EJson::Null)
		{
			OutError = FString::Printf(TEXT("Property '%s' expects a string (asset path) or null"), *Property->GetName());
			return false;
		}
		return true;
	}

	return true;
}

// ============================================================================
// Core Deserialization: JSON → raw value pointer (single source of truth)
// ============================================================================

bool FEpicUnrealMCPDataAssetCommands::DeserializeValueDirect(FProperty* Property, void* ValuePtr, const TSharedPtr<FJsonValue>& JsonValue, FString& OutError)
{
	if (FBoolProperty* P = CastField<FBoolProperty>(Property))
	{
		P->SetPropertyValue(ValuePtr, JsonValue->Type == EJson::Boolean ? JsonValue->AsBool() : (JsonValue->AsNumber() != 0));
		return true;
	}
	if (FByteProperty* P = CastField<FByteProperty>(Property))
	{
		UEnum* E = P->GetIntPropertyEnum();
		if (E && JsonValue->Type == EJson::String)
		{
			FString N = JsonValue->AsString();
			if (N.Contains(TEXT("::"))) N.Split(TEXT("::"), nullptr, &N);
			int64 V = E->GetValueByNameString(N);
			if (V == INDEX_NONE) { OutError = FString::Printf(TEXT("Invalid enum value '%s'"), *N); return false; }
			P->SetPropertyValue(ValuePtr, static_cast<uint8>(V));
		}
		else
		{
			P->SetPropertyValue(ValuePtr, static_cast<uint8>(JsonValue->AsNumber()));
		}
		return true;
	}
	if (FIntProperty* P = CastField<FIntProperty>(Property)) { P->SetPropertyValue(ValuePtr, static_cast<int32>(JsonValue->AsNumber())); return true; }
	if (FInt64Property* P = CastField<FInt64Property>(Property)) { P->SetPropertyValue(ValuePtr, static_cast<int64>(JsonValue->AsNumber())); return true; }
	if (FUInt32Property* P = CastField<FUInt32Property>(Property)) { P->SetPropertyValue(ValuePtr, static_cast<uint32>(JsonValue->AsNumber())); return true; }
	if (FFloatProperty* P = CastField<FFloatProperty>(Property)) { P->SetPropertyValue(ValuePtr, static_cast<float>(JsonValue->AsNumber())); return true; }
	if (FDoubleProperty* P = CastField<FDoubleProperty>(Property)) { P->SetPropertyValue(ValuePtr, JsonValue->AsNumber()); return true; }
	if (FStrProperty* P = CastField<FStrProperty>(Property)) { P->SetPropertyValue(ValuePtr, JsonValue->AsString()); return true; }
	if (FNameProperty* P = CastField<FNameProperty>(Property)) { P->SetPropertyValue(ValuePtr, FName(*JsonValue->AsString())); return true; }
	if (FTextProperty* P = CastField<FTextProperty>(Property)) { P->SetPropertyValue(ValuePtr, FText::FromString(JsonValue->AsString())); return true; }
	if (FEnumProperty* P = CastField<FEnumProperty>(Property))
	{
		UEnum* E = P->GetEnum();
		FNumericProperty* U = P->GetUnderlyingProperty();
		if (E && U)
		{
			if (JsonValue->Type == EJson::String)
			{
				FString N = JsonValue->AsString();
				if (N.Contains(TEXT("::"))) N.Split(TEXT("::"), nullptr, &N);
				int64 V = E->GetValueByNameString(N);
				if (V == INDEX_NONE) { OutError = FString::Printf(TEXT("Invalid enum value '%s'"), *N); return false; }
				U->SetIntPropertyValue(ValuePtr, V);
			}
			else
			{
				U->SetIntPropertyValue(ValuePtr, static_cast<int64>(JsonValue->AsNumber()));
			}
			return true;
		}
		OutError = TEXT("Failed to resolve enum");
		return false;
	}
	if (FStructProperty* P = CastField<FStructProperty>(Property))
	{
		if (JsonValue->Type != EJson::Object) { OutError = FString::Printf(TEXT("Expected object for struct %s"), *P->Struct->GetName()); return false; }
		return DeserializeStructProperty(P, ValuePtr, JsonValue->AsObject(), OutError);
	}
	if (FArrayProperty* P = CastField<FArrayProperty>(Property))
	{
		if (JsonValue->Type != EJson::Array) { OutError = TEXT("Expected JSON array"); return false; }
		FScriptArrayHelper H(P, ValuePtr);
		const auto& Arr = JsonValue->AsArray();
		H.EmptyAndAddValues(Arr.Num());
		for (int32 i = 0; i < Arr.Num(); i++)
		{
			FString EE;
			if (!DeserializeValueDirect(P->Inner, H.GetRawPtr(i), Arr[i], EE))
			{
				OutError = FString::Printf(TEXT("Array[%d]: %s"), i, *EE);
				return false;
			}
		}
		return true;
	}
	if (FMapProperty* P = CastField<FMapProperty>(Property))
	{
		if (JsonValue->Type != EJson::Array) { OutError = TEXT("Expected JSON array of {key,value}"); return false; }
		FScriptMapHelper H(P, ValuePtr);
		H.EmptyValues();
		const auto& Arr = JsonValue->AsArray();
		for (int32 i = 0; i < Arr.Num(); i++)
		{
			if (Arr[i]->Type != EJson::Object) { OutError = FString::Printf(TEXT("Map entry %d not object"), i); return false; }
			const auto& Obj = Arr[i]->AsObject();
			if (!Obj->HasField(TEXT("key")) || !Obj->HasField(TEXT("value")))
			{
				OutError = FString::Printf(TEXT("Map entry %d must have 'key' and 'value'"), i);
				return false;
			}
			int32 Idx = H.AddDefaultValue_Invalid_NeedsRehash();
			FString KE, VE;
			if (!DeserializeValueDirect(P->KeyProp, H.GetKeyPtr(Idx), Obj->TryGetField(TEXT("key")), KE))
			{
				OutError = FString::Printf(TEXT("Map[%d] key: %s"), i, *KE);
				return false;
			}
			if (!DeserializeValueDirect(P->ValueProp, H.GetValuePtr(Idx), Obj->TryGetField(TEXT("value")), VE))
			{
				OutError = FString::Printf(TEXT("Map[%d] value: %s"), i, *VE);
				return false;
			}
		}
		H.Rehash();
		return true;
	}
	if (FSetProperty* P = CastField<FSetProperty>(Property))
	{
		if (JsonValue->Type != EJson::Array) { OutError = TEXT("Expected JSON array"); return false; }
		FScriptSetHelper H(P, ValuePtr);
		H.EmptyElements();
		const auto& Arr = JsonValue->AsArray();
		for (int32 i = 0; i < Arr.Num(); i++)
		{
			int32 Idx = H.AddDefaultValue_Invalid_NeedsRehash();
			FString EE;
			if (!DeserializeValueDirect(P->ElementProp, H.GetElementPtr(Idx), Arr[i], EE))
			{
				OutError = FString::Printf(TEXT("Set[%d]: %s"), i, *EE);
				return false;
			}
		}
		H.Rehash();
		return true;
	}
	if (FObjectProperty* P = CastField<FObjectProperty>(Property))
	{
		if (JsonValue->Type == EJson::Null) { P->SetPropertyValue(ValuePtr, nullptr); return true; }
		UObject* O = StaticLoadObject(P->PropertyClass, nullptr, *JsonValue->AsString());
		if (!O) { OutError = FString::Printf(TEXT("Failed to load object '%s'"), *JsonValue->AsString()); return false; }
		P->SetPropertyValue(ValuePtr, O);
		return true;
	}
	if (FSoftObjectProperty* P = CastField<FSoftObjectProperty>(Property))
	{
		if (JsonValue->Type == EJson::Null) { *static_cast<FSoftObjectPtr*>(ValuePtr) = FSoftObjectPtr(); return true; }
		*static_cast<FSoftObjectPtr*>(ValuePtr) = FSoftObjectPtr(FSoftObjectPath(JsonValue->AsString()));
		return true;
	}

	// Fallback: ImportText
	FString Text = JsonValue->AsString();
	if (!Property->ImportText_Direct(*Text, ValuePtr, nullptr, PPF_None))
	{
		OutError = FString::Printf(TEXT("Failed to import value '%s'"), *Text);
		return false;
	}
	return true;
}

// Top-level entry: computes ValuePtr from ContainerPtr, then delegates
bool FEpicUnrealMCPDataAssetCommands::DeserializeProperty(FProperty* Property, void* ContainerPtr, const TSharedPtr<FJsonValue>& JsonValue, FString& OutError)
{
	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(ContainerPtr);
	return DeserializeValueDirect(Property, ValuePtr, JsonValue, OutError);
}

bool FEpicUnrealMCPDataAssetCommands::DeserializeStructProperty(FStructProperty* StructProp, void* ValuePtr, const TSharedPtr<FJsonObject>& JsonObject, FString& OutError)
{
	UScriptStruct* Struct = StructProp->Struct;
	FName N = Struct->GetFName();

	if (N == NAME_Vector)
	{
		FVector& V = *static_cast<FVector*>(ValuePtr);
		V.X = JsonObject->GetNumberField(TEXT("X")); V.Y = JsonObject->GetNumberField(TEXT("Y")); V.Z = JsonObject->GetNumberField(TEXT("Z"));
		return true;
	}
	if (N == NAME_Rotator)
	{
		FRotator& R = *static_cast<FRotator*>(ValuePtr);
		R.Pitch = JsonObject->GetNumberField(TEXT("Pitch")); R.Yaw = JsonObject->GetNumberField(TEXT("Yaw")); R.Roll = JsonObject->GetNumberField(TEXT("Roll"));
		return true;
	}
	if (N == NAME_Color)
	{
		FColor& C = *static_cast<FColor*>(ValuePtr);
		C.R = static_cast<uint8>(JsonObject->GetNumberField(TEXT("R")));
		C.G = static_cast<uint8>(JsonObject->GetNumberField(TEXT("G")));
		C.B = static_cast<uint8>(JsonObject->GetNumberField(TEXT("B")));
		C.A = JsonObject->HasField(TEXT("A")) ? static_cast<uint8>(JsonObject->GetNumberField(TEXT("A"))) : 255;
		return true;
	}
	if (N == NAME_LinearColor)
	{
		FLinearColor& C = *static_cast<FLinearColor*>(ValuePtr);
		C.R = static_cast<float>(JsonObject->GetNumberField(TEXT("R")));
		C.G = static_cast<float>(JsonObject->GetNumberField(TEXT("G")));
		C.B = static_cast<float>(JsonObject->GetNumberField(TEXT("B")));
		C.A = JsonObject->HasField(TEXT("A")) ? static_cast<float>(JsonObject->GetNumberField(TEXT("A"))) : 1.0f;
		return true;
	}
	if (N == NAME_Vector2D)
	{
		FVector2D& V = *static_cast<FVector2D*>(ValuePtr);
		V.X = JsonObject->GetNumberField(TEXT("X")); V.Y = JsonObject->GetNumberField(TEXT("Y"));
		return true;
	}
	if (N == NAME_Transform)
	{
		FTransform& T = *static_cast<FTransform*>(ValuePtr);
		if (JsonObject->HasField(TEXT("Location")))
		{
			const auto& L = JsonObject->GetObjectField(TEXT("Location"));
			T.SetLocation(FVector(L->GetNumberField(TEXT("X")), L->GetNumberField(TEXT("Y")), L->GetNumberField(TEXT("Z"))));
		}
		if (JsonObject->HasField(TEXT("Rotation")))
		{
			const auto& R = JsonObject->GetObjectField(TEXT("Rotation"));
			T.SetRotation(FRotator(R->GetNumberField(TEXT("Pitch")), R->GetNumberField(TEXT("Yaw")), R->GetNumberField(TEXT("Roll"))).Quaternion());
		}
		if (JsonObject->HasField(TEXT("Scale")))
		{
			const auto& S = JsonObject->GetObjectField(TEXT("Scale"));
			T.SetScale3D(FVector(S->GetNumberField(TEXT("X")), S->GetNumberField(TEXT("Y")), S->GetNumberField(TEXT("Z"))));
		}
		return true;
	}

	// Generic struct
	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		FProperty* F = *It;
		if (JsonObject->HasField(F->GetName()))
		{
			TSharedPtr<FJsonValue> FV = JsonObject->TryGetField(F->GetName());
			if (FV.IsValid())
			{
				void* FieldPtr = F->ContainerPtrToValuePtr<void>(ValuePtr);
				if (!DeserializeValueDirect(F, FieldPtr, FV, OutError))
				{
					OutError = FString::Printf(TEXT("In struct field '%s': %s"), *F->GetName(), *OutError);
					return false;
				}
			}
		}
	}
	return true;
}

// ============================================================================
// Command Handlers
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPDataAssetCommands::HandleReadDataAsset(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	FString LoadError;
	UObject* Asset = LoadDataAsset(AssetPath, LoadError);
	if (!Asset) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(LoadError);

	UClass* C = Asset->GetClass();

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("class_name"), C->GetName());
	R->SetStringField(TEXT("class_path"), C->GetPathName());
	R->SetStringField(TEXT("asset_name"), Asset->GetName());
	R->SetStringField(TEXT("asset_path"), Asset->GetPathName());

	TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
	for (TFieldIterator<FProperty> It(C); It; ++It)
	{
		FProperty* P = *It;
		if (!IsDataAssetProperty(P)) continue;

		TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
		Info->SetStringField(TEXT("type"), GetPropertyTypeName(P));
		Info->SetField(TEXT("value"), SerializeProperty(P, Asset));
		TSharedPtr<FJsonObject> Meta = GetPropertyMetadata(P);
		if (Meta.IsValid()) Info->SetObjectField(TEXT("meta"), Meta);
		Props->SetObjectField(P->GetName(), Info);
	}
	R->SetObjectField(TEXT("properties"), Props);
	return R;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPDataAssetCommands::HandleWriteDataAsset(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	const TSharedPtr<FJsonObject>* PropsPtr;
	if (!Params->TryGetObjectField(TEXT("properties"), PropsPtr) || !PropsPtr->IsValid())
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'properties' parameter (expected JSON object)"));

	bool bSave = !Params->HasField(TEXT("save")) || Params->GetBoolField(TEXT("save"));

	FString LoadError;
	UObject* Asset = LoadDataAsset(AssetPath, LoadError);
	if (!Asset) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(LoadError);

	UClass* C = Asset->GetClass();

	// Phase 1: Validate ALL
	TArray<TPair<FProperty*, TSharedPtr<FJsonValue>>> Validated;
	for (const auto& Pair : (*PropsPtr)->Values)
	{
		FProperty* P = C->FindPropertyByName(*Pair.Key);
		if (!P)
		{
			FString Avail;
			int32 Cnt = 0;
			for (TFieldIterator<FProperty> It(C); It; ++It)
			{
				if (IsDataAssetProperty(*It))
				{
					if (Cnt++ > 0) Avail += TEXT(", ");
					Avail += (*It)->GetName();
				}
			}
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Property '%s' not found. Available: %s"), *Pair.Key, *Avail));
		}
		FString VE;
		if (!ValidatePropertyValue(P, Pair.Value, VE))
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(VE);
		Validated.Add(TPair<FProperty*, TSharedPtr<FJsonValue>>(P, Pair.Value));
	}

	// Phase 2: Apply
	TArray<FString> Modified;
	for (const auto& Pair : Validated)
	{
		FProperty* P = Pair.Key;
		Asset->PreEditChange(P);
		FString DE;
		if (!DeserializeProperty(P, Asset, Pair.Value, DE))
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to set '%s': %s"), *P->GetName(), *DE));
		FPropertyChangedEvent Evt(P);
		Asset->PostEditChangeProperty(Evt);
		Modified.Add(P->GetName());
	}

	// Phase 3: Save
	bool bSaved = false;
	if (bSave)
	{
		bSaved = UEditorAssetLibrary::SaveLoadedAsset(Asset);
		if (!bSaved) Asset->MarkPackageDirty();
	}
	else
	{
		Asset->MarkPackageDirty();
	}

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	TArray<TSharedPtr<FJsonValue>> MA;
	for (const FString& N : Modified) MA.Add(MakeShared<FJsonValueString>(N));
	R->SetArrayField(TEXT("modified_properties"), MA);
	R->SetBoolField(TEXT("asset_saved"), bSaved);
	return R;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPDataAssetCommands::HandleCreateDataAsset(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetName;
	if (!Params->TryGetStringField(TEXT("asset_name"), AssetName))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_name' parameter"));

	FString PackagePath;
	if (!Params->TryGetStringField(TEXT("package_path"), PackagePath))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'package_path' parameter"));

	FString ClassPath, DuplicateFrom;
	Params->TryGetStringField(TEXT("class_path"), ClassPath);
	Params->TryGetStringField(TEXT("duplicate_from"), DuplicateFrom);

	if (ClassPath.IsEmpty() && DuplicateFrom.IsEmpty())
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Either 'class_path' or 'duplicate_from' must be provided"));

	FString FullAssetPath = PackagePath / AssetName;
	if (UEditorAssetLibrary::DoesAssetExist(FullAssetPath))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset already exists: %s"), *FullAssetPath));

	UObject* NewAsset = nullptr;

	if (!DuplicateFrom.IsEmpty())
	{
		if (!UEditorAssetLibrary::DoesAssetExist(DuplicateFrom))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Source asset not found: %s"), *DuplicateFrom));
		}
		NewAsset = UEditorAssetLibrary::DuplicateAsset(DuplicateFrom, FullAssetPath);
		if (!NewAsset)
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to duplicate '%s' to '%s'"), *DuplicateFrom, *FullAssetPath));
	}
	else
	{
		UClass* AC = FindObject<UClass>(nullptr, *ClassPath);
		if (!AC) AC = LoadObject<UClass>(nullptr, *ClassPath);
		if (!AC) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Class not found: %s"), *ClassPath));
		if (!AC->IsChildOf(UDataAsset::StaticClass()))
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("'%s' is not a UDataAsset subclass"), *ClassPath));

		FString PkgName = PackagePath / AssetName;
		UPackage* Pkg = CreatePackage(*PkgName);
		if (!Pkg) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to create package: %s"), *PkgName));

		NewAsset = NewObject<UDataAsset>(Pkg, AC, *AssetName, RF_Public | RF_Standalone);
		if (!NewAsset) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to create asset of class '%s'"), *ClassPath));

		FAssetRegistryModule::AssetCreated(NewAsset);
		Pkg->MarkPackageDirty();
	}

	// Apply optional property overrides (report failures as warnings)
	TArray<FString> Warnings;
	const TSharedPtr<FJsonObject>* PropsPtr;
	if (Params->TryGetObjectField(TEXT("properties"), PropsPtr) && PropsPtr->IsValid())
	{
		UClass* NC = NewAsset->GetClass();
		for (const auto& Pair : (*PropsPtr)->Values)
		{
			FProperty* P = NC->FindPropertyByName(*Pair.Key);
			if (!P) { Warnings.Add(FString::Printf(TEXT("Property '%s' not found"), *Pair.Key)); continue; }
			FString VE;
			if (!ValidatePropertyValue(P, Pair.Value, VE)) { Warnings.Add(FString::Printf(TEXT("'%s': %s"), *Pair.Key, *VE)); continue; }
			NewAsset->PreEditChange(P);
			FString DE;
			if (!DeserializeProperty(P, NewAsset, Pair.Value, DE)) { Warnings.Add(FString::Printf(TEXT("'%s': %s"), *Pair.Key, *DE)); continue; }
			FPropertyChangedEvent Evt(P);
			NewAsset->PostEditChangeProperty(Evt);
		}
	}

	bool bSaved = UEditorAssetLibrary::SaveLoadedAsset(NewAsset);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("asset_name"), NewAsset->GetName());
	R->SetStringField(TEXT("asset_path"), NewAsset->GetPathName());
	R->SetStringField(TEXT("class_name"), NewAsset->GetClass()->GetName());
	R->SetBoolField(TEXT("asset_saved"), bSaved);
	R->SetStringField(TEXT("created_from"), DuplicateFrom.IsEmpty() ? TEXT("class") : TEXT("duplicate"));
	if (!DuplicateFrom.IsEmpty()) R->SetStringField(TEXT("source_asset"), DuplicateFrom);
	else R->SetStringField(TEXT("source_class"), ClassPath);

	if (Warnings.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> WA;
		for (const FString& W : Warnings) WA.Add(MakeShared<FJsonValueString>(W));
		R->SetArrayField(TEXT("property_warnings"), WA);
	}

	return R;
}