#include "Commands/EpicUnrealMCPNiagaraCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// Niagara includes
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraScript.h"
#include "NiagaraTypes.h"
#include "NiagaraScriptSourceBase.h"

// NiagaraEditor includes
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "ViewModels/Stack/NiagaraStackEntry.h"
#include "ViewModels/Stack/NiagaraStackItemGroup.h"
#include "ViewModels/Stack/NiagaraStackScriptItemGroup.h"
#include "ViewModels/Stack/NiagaraStackModuleItem.h"
#include "ViewModels/Stack/NiagaraStackFunctionInput.h"
#include "ViewModels/Stack/INiagaraStackItemGroupAddUtilities.h"
#include "ViewModels/Stack/NiagaraStackRoot.h"

FEpicUnrealMCPNiagaraCommands::FEpicUnrealMCPNiagaraCommands()
{
}

TSharedPtr<FJsonObject> FEpicUnrealMCPNiagaraCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("read_niagara_system"))
		return HandleReadNiagaraSystem(Params);
	if (CommandType == TEXT("write_niagara_module_input"))
		return HandleWriteNiagaraModuleInput(Params);
	if (CommandType == TEXT("add_niagara_module"))
		return HandleAddNiagaraModule(Params);
	if (CommandType == TEXT("remove_niagara_module"))
		return HandleRemoveNiagaraModule(Params);
	if (CommandType == TEXT("list_niagara_modules"))
		return HandleListNiagaraModules(Params);

	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
		FString::Printf(TEXT("Unknown niagara command: %s"), *CommandType));
}

// ============================================================================
// Helpers
// ============================================================================

UNiagaraSystem* FEpicUnrealMCPNiagaraCommands::LoadNiagaraSystem(const FString& AssetPath, FString& OutError)
{
	FString FullPath = AssetPath;
	if (!FullPath.Contains(TEXT(".")))
	{
		FString Name = FPaths::GetBaseFilename(FullPath);
		FullPath = FString::Printf(TEXT("%s.%s"), *FullPath, *Name);
	}

	UObject* Asset = UEditorAssetLibrary::LoadAsset(FullPath);
	if (!Asset)
	{
		Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	}

	UNiagaraSystem* System = Cast<UNiagaraSystem>(Asset);
	if (!System)
	{
		OutError = FString::Printf(TEXT("Failed to load Niagara system at: %s"), *AssetPath);
	}
	return System;
}

TSharedPtr<FNiagaraSystemViewModel> FEpicUnrealMCPNiagaraCommands::CreateSystemViewModel(UNiagaraSystem* System)
{
	TSharedPtr<FNiagaraSystemViewModel> ViewModel = MakeShared<FNiagaraSystemViewModel>();

	FNiagaraSystemViewModelOptions Options;
	Options.bCanAutoCompile = false;
	Options.bCanSimulate = false;
	Options.bIsForDataProcessingOnly = true;
	Options.EditMode = ENiagaraSystemViewModelEditMode::SystemAsset;

	ViewModel->Initialize(*System, Options);
	return ViewModel;
}

FString FEpicUnrealMCPNiagaraCommands::GetNiagaraTypeName(const FNiagaraTypeDefinition& TypeDef)
{
	if (!TypeDef.IsValid()) return TEXT("unknown");
	return TypeDef.GetName();
}

TSharedPtr<FJsonValue> FEpicUnrealMCPNiagaraCommands::SerializeInputValue(UNiagaraStackFunctionInput* Input)
{
	if (!Input) return MakeShared<FJsonValueNull>();

	UNiagaraStackFunctionInput::EValueMode Mode = Input->GetValueMode();

	if (Mode == UNiagaraStackFunctionInput::EValueMode::Local)
	{
		TSharedPtr<const FStructOnScope> LocalValue = Input->GetLocalValueStruct();
		if (!LocalValue.IsValid() || !LocalValue->IsValid())
			return MakeShared<FJsonValueNull>();

		const FNiagaraTypeDefinition& Type = Input->GetInputType();
		const void* Data = LocalValue->GetStructMemory();
		const UScriptStruct* Struct = Cast<UScriptStruct>(LocalValue->GetStruct());

		if (!Data || !Struct)
			return MakeShared<FJsonValueNull>();

		// Check common types via the struct
		FName StructName = Struct->GetFName();

		// Float (FNiagaraFloat wraps a float)
		if (Type == FNiagaraTypeDefinition::GetFloatDef())
		{
			float Val = *static_cast<const float*>(Data);
			return MakeShared<FJsonValueNumber>(Val);
		}
		if (Type == FNiagaraTypeDefinition::GetIntDef())
		{
			int32 Val = *static_cast<const int32*>(Data);
			return MakeShared<FJsonValueNumber>(Val);
		}
		if (Type == FNiagaraTypeDefinition::GetBoolDef())
		{
			// Niagara stores bools as int32 (FNiagaraBool)
			int32 Val = *static_cast<const int32*>(Data);
			return MakeShared<FJsonValueBoolean>(Val != 0);
		}

		// Vector types
		if (StructName == TEXT("NiagaraPosition") || StructName == NAME_Vector || StructName == TEXT("Vector3f"))
		{
			// Niagara positions/vectors are 3 floats
			const float* Floats = static_cast<const float*>(Data);
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetNumberField(TEXT("X"), Floats[0]);
			Obj->SetNumberField(TEXT("Y"), Floats[1]);
			Obj->SetNumberField(TEXT("Z"), Floats[2]);
			return MakeShared<FJsonValueObject>(Obj);
		}

		if (StructName == NAME_Vector2D || StructName == TEXT("Vector2f"))
		{
			const float* Floats = static_cast<const float*>(Data);
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetNumberField(TEXT("X"), Floats[0]);
			Obj->SetNumberField(TEXT("Y"), Floats[1]);
			return MakeShared<FJsonValueObject>(Obj);
		}

		if (StructName == NAME_LinearColor || StructName == TEXT("NiagaraLinearColor"))
		{
			const float* Floats = static_cast<const float*>(Data);
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetNumberField(TEXT("R"), Floats[0]);
			Obj->SetNumberField(TEXT("G"), Floats[1]);
			Obj->SetNumberField(TEXT("B"), Floats[2]);
			Obj->SetNumberField(TEXT("A"), Floats[3]);
			return MakeShared<FJsonValueObject>(Obj);
		}

		if (StructName == NAME_Quat || StructName == TEXT("Quat4f"))
		{
			const float* Floats = static_cast<const float*>(Data);
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetNumberField(TEXT("X"), Floats[0]);
			Obj->SetNumberField(TEXT("Y"), Floats[1]);
			Obj->SetNumberField(TEXT("Z"), Floats[2]);
			Obj->SetNumberField(TEXT("W"), Floats[3]);
			return MakeShared<FJsonValueObject>(Obj);
		}

		// Fallback: export as text
		FString ExportedText;
		Struct->ExportText(ExportedText, Data, nullptr, nullptr, PPF_None, nullptr);
		return MakeShared<FJsonValueString>(ExportedText);
	}

	// For non-local modes, return a descriptor
	return MakeShared<FJsonValueNull>();
}

// ============================================================================
// Read
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPNiagaraCommands::HandleReadNiagaraSystem(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	FString LoadError;
	UNiagaraSystem* System = LoadNiagaraSystem(AssetPath, LoadError);
	if (!System) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(LoadError);

	TSharedPtr<FNiagaraSystemViewModel> ViewModel = CreateSystemViewModel(System);
	if (ViewModel->GetEmitterHandleViewModels().Num() == 0 && System->GetNumEmitters() > 0)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to initialize system view model"));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("system_name"), System->GetName());
	Result->SetStringField(TEXT("asset_path"), System->GetPathName());

	// Serialize emitters
	TArray<TSharedPtr<FJsonValue>> EmittersArray;
	const auto& EmitterHandleVMs = ViewModel->GetEmitterHandleViewModels();

	for (const TSharedRef<FNiagaraEmitterHandleViewModel>& HandleVM : EmitterHandleVMs)
	{
		TSharedPtr<FJsonObject> EmitterObj = MakeShared<FJsonObject>();
		EmitterObj->SetStringField(TEXT("name"), HandleVM->GetName().ToString());
		EmitterObj->SetBoolField(TEXT("enabled"), HandleVM->GetIsEnabled());

		// Get the stack view model for this emitter
		TSharedPtr<FNiagaraEmitterViewModel> EmitterVM = HandleVM->GetEmitterViewModel();

		// Walk the stack to get groups and modules
		// We use the emitter handle's stack which shows all groups
		TArray<TSharedPtr<FJsonValue>> GroupsArray;

		// Create a stack view model for this emitter
		UNiagaraStackViewModel* StackVM = NewObject<UNiagaraStackViewModel>();
		FNiagaraStackViewModelOptions StackOpts(false, true); // no system info, yes emitter info
		StackVM->InitializeWithViewModels(ViewModel, HandleVM, StackOpts);

		// Get root entries and traverse
		UNiagaraStackEntry* Root = StackVM->GetRootEntry();
		if (Root)
		{
			TArray<UNiagaraStackEntry*> Children;
			Root->GetFilteredChildren(Children);

			for (UNiagaraStackEntry* Child : Children)
			{
				// Check if this is an item group (Particle Spawn, Particle Update, etc.)
				UNiagaraStackItemGroup* Group = Cast<UNiagaraStackItemGroup>(Child);
				if (!Group) continue;

				TSharedPtr<FJsonObject> GroupObj = MakeShared<FJsonObject>();
				GroupObj->SetStringField(TEXT("name"), Group->GetDisplayName().ToString());

				// Get modules in this group
				TArray<UNiagaraStackModuleItem*> Modules;
				Group->GetFilteredChildrenOfType<UNiagaraStackModuleItem>(Modules);

				TArray<TSharedPtr<FJsonValue>> ModulesArray;
				for (UNiagaraStackModuleItem* Module : Modules)
				{
					TSharedPtr<FJsonObject> ModuleObj = MakeShared<FJsonObject>();
					ModuleObj->SetStringField(TEXT("name"), Module->GetDisplayName().ToString());
					ModuleObj->SetBoolField(TEXT("enabled"), Module->GetIsEnabled());

					// Get module inputs
					TArray<UNiagaraStackFunctionInput*> Inputs;
					Module->GetParameterInputs(Inputs);

					TArray<TSharedPtr<FJsonValue>> InputsArray;
					for (UNiagaraStackFunctionInput* Input : Inputs)
					{
						TSharedPtr<FJsonObject> InputObj = MakeShared<FJsonObject>();
						InputObj->SetStringField(TEXT("name"), Input->GetDisplayName().ToString());
						InputObj->SetStringField(TEXT("type"), GetNiagaraTypeName(Input->GetInputType()));

						UNiagaraStackFunctionInput::EValueMode Mode = Input->GetValueMode();
						FString ModeStr;
						switch (Mode)
						{
						case UNiagaraStackFunctionInput::EValueMode::Local: ModeStr = TEXT("local"); break;
						case UNiagaraStackFunctionInput::EValueMode::Linked: ModeStr = TEXT("linked"); break;
						case UNiagaraStackFunctionInput::EValueMode::Dynamic: ModeStr = TEXT("dynamic"); break;
						case UNiagaraStackFunctionInput::EValueMode::Data: ModeStr = TEXT("data"); break;
						case UNiagaraStackFunctionInput::EValueMode::Expression: ModeStr = TEXT("expression"); break;
						case UNiagaraStackFunctionInput::EValueMode::DefaultFunction: ModeStr = TEXT("default_function"); break;
						default: ModeStr = TEXT("other"); break;
						}
						InputObj->SetStringField(TEXT("mode"), ModeStr);

						// Serialize value based on mode
						if (Mode == UNiagaraStackFunctionInput::EValueMode::Local)
						{
							InputObj->SetField(TEXT("value"), SerializeInputValue(Input));
						}
						else if (Mode == UNiagaraStackFunctionInput::EValueMode::Linked)
						{
							InputObj->SetStringField(TEXT("linked_to"), Input->GetLinkedParameterValue().GetName().ToString());
						}

						InputsArray.Add(MakeShared<FJsonValueObject>(InputObj));
					}

					ModuleObj->SetArrayField(TEXT("inputs"), InputsArray);
					ModulesArray.Add(MakeShared<FJsonValueObject>(ModuleObj));
				}

				GroupObj->SetArrayField(TEXT("modules"), ModulesArray);
				GroupsArray.Add(MakeShared<FJsonValueObject>(GroupObj));
			}
		}

		StackVM->Finalize();

		EmitterObj->SetArrayField(TEXT("groups"), GroupsArray);
		EmittersArray.Add(MakeShared<FJsonValueObject>(EmitterObj));
	}

	Result->SetArrayField(TEXT("emitters"), EmittersArray);

	// Serialize user parameters
	TArray<TSharedPtr<FJsonValue>> UserParamsArray;
	auto UserParams = System->GetExposedParameters().ReadParameterVariables();
	for (const auto& ParamWithOffset : UserParams)
	{
		TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
		ParamObj->SetStringField(TEXT("name"), ParamWithOffset.GetName().ToString());
		ParamObj->SetStringField(TEXT("type"), GetNiagaraTypeName(ParamWithOffset.GetType()));
		UserParamsArray.Add(MakeShared<FJsonValueObject>(ParamObj));
	}
	Result->SetArrayField(TEXT("user_parameters"), UserParamsArray);

	return Result;
}

// ============================================================================
// Write Module Input
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPNiagaraCommands::HandleWriteNiagaraModuleInput(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	FString EmitterName;
	if (!Params->TryGetStringField(TEXT("emitter"), EmitterName))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'emitter' parameter"));

	FString ModuleName;
	if (!Params->TryGetStringField(TEXT("module"), ModuleName))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'module' parameter"));

	const TSharedPtr<FJsonObject>* InputsPtr;
	if (!Params->TryGetObjectField(TEXT("inputs"), InputsPtr) || !InputsPtr->IsValid())
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'inputs' parameter"));

	bool bSave = !Params->HasField(TEXT("save")) || Params->GetBoolField(TEXT("save"));

	FString LoadError;
	UNiagaraSystem* System = LoadNiagaraSystem(AssetPath, LoadError);
	if (!System) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(LoadError);

	TSharedPtr<FNiagaraSystemViewModel> ViewModel = CreateSystemViewModel(System);
	if (ViewModel->GetEmitterHandleViewModels().Num() == 0 && System->GetNumEmitters() > 0)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to initialize system view model"));
	}

	// Find the emitter
	TSharedPtr<FNiagaraEmitterHandleViewModel> TargetEmitterVM;
	for (const auto& HandleVM : ViewModel->GetEmitterHandleViewModels())
	{
		if (HandleVM->GetName().ToString() == EmitterName)
		{
			TargetEmitterVM = HandleVM;
			break;
		}
	}
	if (!TargetEmitterVM.IsValid())
	{
		FString Available;
		for (const auto& H : ViewModel->GetEmitterHandleViewModels())
		{
			if (!Available.IsEmpty()) Available += TEXT(", ");
			Available += H->GetName().ToString();
		}
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Emitter '%s' not found. Available: %s"), *EmitterName, *Available));
	}

	// Build stack for this emitter
	UNiagaraStackViewModel* StackVM = NewObject<UNiagaraStackViewModel>();
	StackVM->InitializeWithViewModels(ViewModel, TargetEmitterVM, FNiagaraStackViewModelOptions(false, true));

	// Find the module
	UNiagaraStackModuleItem* TargetModule = nullptr;
	UNiagaraStackEntry* Root = StackVM->GetRootEntry();
	if (Root)
	{
		TArray<UNiagaraStackModuleItem*> AllModules;
		Root->GetFilteredChildrenOfType<UNiagaraStackModuleItem>(AllModules, true);
		for (UNiagaraStackModuleItem* M : AllModules)
		{
			if (M->GetDisplayName().ToString() == ModuleName)
			{
				TargetModule = M;
				break;
			}
		}
	}

	if (!TargetModule)
	{
		StackVM->Finalize();
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Module '%s' not found in emitter '%s'"), *ModuleName, *EmitterName));
	}

	// Get module inputs
	TArray<UNiagaraStackFunctionInput*> ModuleInputs;
	TargetModule->GetParameterInputs(ModuleInputs);

	// Apply input changes
	TArray<FString> Modified;
	for (const auto& Pair : (*InputsPtr)->Values)
	{
		const FString& InputName = Pair.Key;
		const TSharedPtr<FJsonValue>& JsonVal = Pair.Value;

		UNiagaraStackFunctionInput* TargetInput = nullptr;
		for (UNiagaraStackFunctionInput* I : ModuleInputs)
		{
			if (I->GetDisplayName().ToString() == InputName)
			{
				TargetInput = I;
				break;
			}
		}

		if (!TargetInput)
		{
			FString Available;
			for (UNiagaraStackFunctionInput* I : ModuleInputs)
			{
				if (!Available.IsEmpty()) Available += TEXT(", ");
				Available += I->GetDisplayName().ToString();
			}
			StackVM->Finalize();
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Input '%s' not found on module '%s'. Available: %s"), *InputName, *ModuleName, *Available));
		}

		// Get the current local value struct to know the type
		TSharedPtr<const FStructOnScope> CurrentValue = TargetInput->GetLocalValueStruct();
		const UScriptStruct* Struct = CurrentValue.IsValid() ? Cast<UScriptStruct>(CurrentValue->GetStruct()) : nullptr;

		if (!Struct)
		{
			// If no local value exists, we need to create one from the input type
			// For now, skip inputs that don't have a local value struct
			UE_LOG(LogTemp, Warning, TEXT("Input '%s' has no local value struct, skipping"), *InputName);
			continue;
		}

		// Create a mutable copy
		TSharedRef<FStructOnScope> NewValue = MakeShared<FStructOnScope>(Struct);
		FMemory::Memcpy(NewValue->GetStructMemory(), CurrentValue->GetStructMemory(), Struct->GetStructureSize());

		void* Data = NewValue->GetStructMemory();
		const FNiagaraTypeDefinition& Type = TargetInput->GetInputType();

		bool bSet = false;
		if (Type == FNiagaraTypeDefinition::GetFloatDef() && JsonVal->Type == EJson::Number)
		{
			*static_cast<float*>(Data) = static_cast<float>(JsonVal->AsNumber());
			bSet = true;
		}
		else if (Type == FNiagaraTypeDefinition::GetIntDef() && JsonVal->Type == EJson::Number)
		{
			*static_cast<int32*>(Data) = static_cast<int32>(JsonVal->AsNumber());
			bSet = true;
		}
		else if (Type == FNiagaraTypeDefinition::GetBoolDef())
		{
			int32 BoolVal = (JsonVal->Type == EJson::Boolean) ? (JsonVal->AsBool() ? 1 : 0) : static_cast<int32>(JsonVal->AsNumber());
			*static_cast<int32*>(Data) = BoolVal;
			bSet = true;
		}
		else if (JsonVal->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject>& JObj = JsonVal->AsObject();
			FName StructName = Struct->GetFName();

			if (StructName == TEXT("NiagaraPosition") || StructName == NAME_Vector || StructName == TEXT("Vector3f"))
			{
				float* Floats = static_cast<float*>(Data);
				Floats[0] = static_cast<float>(JObj->GetNumberField(TEXT("X")));
				Floats[1] = static_cast<float>(JObj->GetNumberField(TEXT("Y")));
				Floats[2] = static_cast<float>(JObj->GetNumberField(TEXT("Z")));
				bSet = true;
			}
			else if (StructName == NAME_Vector2D || StructName == TEXT("Vector2f"))
			{
				float* Floats = static_cast<float*>(Data);
				Floats[0] = static_cast<float>(JObj->GetNumberField(TEXT("X")));
				Floats[1] = static_cast<float>(JObj->GetNumberField(TEXT("Y")));
				bSet = true;
			}
			else if (StructName == NAME_LinearColor || StructName == TEXT("NiagaraLinearColor"))
			{
				float* Floats = static_cast<float*>(Data);
				Floats[0] = static_cast<float>(JObj->GetNumberField(TEXT("R")));
				Floats[1] = static_cast<float>(JObj->GetNumberField(TEXT("G")));
				Floats[2] = static_cast<float>(JObj->GetNumberField(TEXT("B")));
				Floats[3] = JObj->HasField(TEXT("A")) ? static_cast<float>(JObj->GetNumberField(TEXT("A"))) : 1.0f;
				bSet = true;
			}
			else if (StructName == NAME_Quat || StructName == TEXT("Quat4f"))
			{
				float* Floats = static_cast<float*>(Data);
				Floats[0] = static_cast<float>(JObj->GetNumberField(TEXT("X")));
				Floats[1] = static_cast<float>(JObj->GetNumberField(TEXT("Y")));
				Floats[2] = static_cast<float>(JObj->GetNumberField(TEXT("Z")));
				Floats[3] = static_cast<float>(JObj->GetNumberField(TEXT("W")));
				bSet = true;
			}
		}

		if (bSet)
		{
			TargetInput->NotifyBeginLocalValueChange();
			TargetInput->SetLocalValue(NewValue);
			TargetInput->NotifyEndLocalValueChange();
			Modified.Add(InputName);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Could not set input '%s': type mismatch or unsupported type"), *InputName);
		}
	}

	// Compile and save
	System->RequestCompile(false);
	bool bSaved = false;
	if (bSave)
	{
		bSaved = UEditorAssetLibrary::SaveLoadedAsset(System);
	}

	StackVM->Finalize();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	TArray<TSharedPtr<FJsonValue>> ModArr;
	for (const FString& N : Modified) ModArr.Add(MakeShared<FJsonValueString>(N));
	Result->SetArrayField(TEXT("modified_inputs"), ModArr);
	Result->SetBoolField(TEXT("asset_saved"), bSaved);
	return Result;
}

// ============================================================================
// Add Module
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPNiagaraCommands::HandleAddNiagaraModule(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	FString EmitterName;
	if (!Params->TryGetStringField(TEXT("emitter"), EmitterName))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'emitter' parameter"));

	FString GroupName;
	if (!Params->TryGetStringField(TEXT("group"), GroupName))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'group' parameter"));

	FString ModulePath;
	if (!Params->TryGetStringField(TEXT("module_path"), ModulePath))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'module_path' parameter"));

	FString LoadError;
	UNiagaraSystem* System = LoadNiagaraSystem(AssetPath, LoadError);
	if (!System) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(LoadError);

	TSharedPtr<FNiagaraSystemViewModel> ViewModel = CreateSystemViewModel(System);
	if (ViewModel->GetEmitterHandleViewModels().Num() == 0 && System->GetNumEmitters() > 0)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to initialize system view model"));
	}

	// Find emitter
	TSharedPtr<FNiagaraEmitterHandleViewModel> TargetEmitterVM;
	for (const auto& H : ViewModel->GetEmitterHandleViewModels())
	{
		if (H->GetName().ToString() == EmitterName)
		{
			TargetEmitterVM = H;
			break;
		}
	}
	if (!TargetEmitterVM.IsValid())
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Emitter '%s' not found"), *EmitterName));

	// Build stack
	UNiagaraStackViewModel* StackVM = NewObject<UNiagaraStackViewModel>();
	StackVM->InitializeWithViewModels(ViewModel, TargetEmitterVM, FNiagaraStackViewModelOptions(false, true));

	// Find the group
	UNiagaraStackEntry* Root = StackVM->GetRootEntry();
	UNiagaraStackScriptItemGroup* TargetGroup = nullptr;
	if (Root)
	{
		TArray<UNiagaraStackScriptItemGroup*> Groups;
		Root->GetFilteredChildrenOfType<UNiagaraStackScriptItemGroup>(Groups, true);
		for (UNiagaraStackScriptItemGroup* G : Groups)
		{
			if (G->GetDisplayName().ToString() == GroupName)
			{
				TargetGroup = G;
				break;
			}
		}
	}

	if (!TargetGroup)
	{
		StackVM->Finalize();
		FString Available;
		if (Root)
		{
			TArray<UNiagaraStackScriptItemGroup*> Groups;
			Root->GetFilteredChildrenOfType<UNiagaraStackScriptItemGroup>(Groups, true);
			for (UNiagaraStackScriptItemGroup* G : Groups)
			{
				if (!Available.IsEmpty()) Available += TEXT(", ");
				Available += G->GetDisplayName().ToString();
			}
		}
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Group '%s' not found. Available: %s"), *GroupName, *Available));
	}

	// Use the group's add utilities to find and add the module
	INiagaraStackItemGroupAddUtilities* AddUtils = TargetGroup->GetAddUtilities();
	if (!AddUtils)
	{
		StackVM->Finalize();
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Group does not support adding modules"));
	}

	TArray<TSharedRef<INiagaraStackItemGroupAddAction>> AddActions;
	AddUtils->GenerateAddActions(AddActions);

	TSharedPtr<INiagaraStackItemGroupAddAction> MatchingAction;
	for (const auto& Action : AddActions)
	{
		// Match by display name or by checking if the module path matches
		FString ActionName = Action->GetDisplayName().ToString();
		if (ActionName == FPaths::GetBaseFilename(ModulePath) || ModulePath.Contains(ActionName))
		{
			MatchingAction = Action;
			break;
		}
	}

	if (!MatchingAction.IsValid())
	{
		StackVM->Finalize();
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Module '%s' not found in available actions for group '%s'"), *ModulePath, *GroupName));
	}

	AddUtils->ExecuteAddAction(MatchingAction.ToSharedRef(), INDEX_NONE);

	System->RequestCompile(false);
	bool bSaved = UEditorAssetLibrary::SaveLoadedAsset(System);

	StackVM->Finalize();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("added_module"), MatchingAction->GetDisplayName().ToString());
	Result->SetStringField(TEXT("to_group"), GroupName);
	Result->SetBoolField(TEXT("asset_saved"), bSaved);
	return Result;
}

// ============================================================================
// Remove Module
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPNiagaraCommands::HandleRemoveNiagaraModule(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	FString EmitterName;
	if (!Params->TryGetStringField(TEXT("emitter"), EmitterName))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'emitter' parameter"));

	FString ModuleName;
	if (!Params->TryGetStringField(TEXT("module"), ModuleName))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'module' parameter"));

	FString LoadError;
	UNiagaraSystem* System = LoadNiagaraSystem(AssetPath, LoadError);
	if (!System) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(LoadError);

	TSharedPtr<FNiagaraSystemViewModel> ViewModel = CreateSystemViewModel(System);
	if (ViewModel->GetEmitterHandleViewModels().Num() == 0 && System->GetNumEmitters() > 0)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to initialize system view model"));
	}

	// Find emitter
	TSharedPtr<FNiagaraEmitterHandleViewModel> TargetEmitterVM;
	for (const auto& H : ViewModel->GetEmitterHandleViewModels())
	{
		if (H->GetName().ToString() == EmitterName)
		{
			TargetEmitterVM = H;
			break;
		}
	}
	if (!TargetEmitterVM.IsValid())
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Emitter '%s' not found"), *EmitterName));

	UNiagaraStackViewModel* StackVM = NewObject<UNiagaraStackViewModel>();
	StackVM->InitializeWithViewModels(ViewModel, TargetEmitterVM, FNiagaraStackViewModelOptions(false, true));

	UNiagaraStackModuleItem* TargetModule = nullptr;
	UNiagaraStackEntry* Root = StackVM->GetRootEntry();
	if (Root)
	{
		TArray<UNiagaraStackModuleItem*> AllModules;
		Root->GetFilteredChildrenOfType<UNiagaraStackModuleItem>(AllModules, true);
		for (UNiagaraStackModuleItem* M : AllModules)
		{
			if (M->GetDisplayName().ToString() == ModuleName)
			{
				TargetModule = M;
				break;
			}
		}
	}

	if (!TargetModule)
	{
		StackVM->Finalize();
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Module '%s' not found in emitter '%s'"), *ModuleName, *EmitterName));
	}

	FText DeleteMsg;
	if (!TargetModule->TestCanDeleteWithMessage(DeleteMsg))
	{
		StackVM->Finalize();
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Cannot delete module '%s': %s"), *ModuleName, *DeleteMsg.ToString()));
	}

	TargetModule->Delete();

	System->RequestCompile(false);
	bool bSaved = UEditorAssetLibrary::SaveLoadedAsset(System);

	StackVM->Finalize();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("removed_module"), ModuleName);
	Result->SetBoolField(TEXT("asset_saved"), bSaved);
	return Result;
}

// ============================================================================
// List Available Modules
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPNiagaraCommands::HandleListNiagaraModules(const TSharedPtr<FJsonObject>& Params)
{
	FString Filter;
	Params->TryGetStringField(TEXT("filter"), Filter);

	FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

	TArray<FAssetData> AssetDatas;
	AssetRegistry.Get().GetAssetsByClass(UNiagaraScript::StaticClass()->GetClassPathName(), AssetDatas);

	TArray<TSharedPtr<FJsonValue>> ModulesArray;
	for (const FAssetData& AD : AssetDatas)
	{
		FString Name = AD.AssetName.ToString();
		FString Path = AD.GetSoftObjectPath().ToString();

		// Apply filter
		if (!Filter.IsEmpty() && !Name.Contains(Filter, ESearchCase::IgnoreCase))
			continue;

		TSharedPtr<FJsonObject> ModObj = MakeShared<FJsonObject>();
		ModObj->SetStringField(TEXT("name"), Name);
		ModObj->SetStringField(TEXT("path"), Path);

		// Try to get description from asset tag
		FAssetTagValueRef DescTag = AD.TagsAndValues.FindTag(FName("Description"));
		if (DescTag.IsSet())
		{
			ModObj->SetStringField(TEXT("description"), DescTag.GetValue());
		}

		ModulesArray.Add(MakeShared<FJsonValueObject>(ModObj));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("count"), ModulesArray.Num());
	Result->SetArrayField(TEXT("modules"), ModulesArray);
	return Result;
}