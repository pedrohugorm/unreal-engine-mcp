# Niagara System Editing — Design Spec

**Date:** 2026-03-23
**Status:** Approved

## Summary

Add MCP tools to read, edit module inputs, and add/remove modules on existing Niagara systems. Uses `FNiagaraSystemViewModel` (the same stack abstraction the editor UI uses) for safe, high-level access to the emitter/module/input hierarchy. Edit-only — no system creation from scratch.

## Decisions

| Decision | Choice |
|---|---|
| Scope | Edit existing systems only (no creation) |
| Approach | FNiagaraSystemViewModel (Approach B — stack layer) |
| Tools | 5 consolidated MCP tools |
| ViewModel lifecycle | Created per-command, destroyed after (stateless) |
| Input write scope | Local values only (linked/dynamic switched to local on write) |

## Architecture

```
AI Client
  ↓ MCP Protocol
Python MCP Tools (niagara_manager.py)
  ├── niagara_read(asset_path)
  ├── niagara_write(asset_path, emitter, module, inputs)
  ├── niagara_add_module(asset_path, emitter, group, module_path)
  ├── niagara_remove_module(asset_path, emitter, module)
  └── niagara_list_modules(filter?)
  ↓ TCP Socket (JSON commands)
C++ Plugin (FEpicUnrealMCPNiagaraCommands)
  ↓ FNiagaraSystemViewModel
Unreal Engine Niagara Editor APIs
```

## C++ Command Handler

### New Files

- `UnrealMCP/Source/UnrealMCP/Public/Commands/EpicUnrealMCPNiagaraCommands.h`
- `UnrealMCP/Source/UnrealMCP/Private/Commands/EpicUnrealMCPNiagaraCommands.cpp`

### Build.cs Change

Add to `PrivateDependencyModuleNames`:
```csharp
"Niagara", "NiagaraEditor"
```

### Class Structure

```cpp
class FEpicUnrealMCPNiagaraCommands
{
public:
    FEpicUnrealMCPNiagaraCommands();
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType,
                                           const TSharedPtr<FJsonObject>& Params);

private:
    // Command handlers
    TSharedPtr<FJsonObject> HandleReadNiagaraSystem(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleWriteNiagaraModuleInput(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddNiagaraModule(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRemoveNiagaraModule(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleListNiagaraModules(const TSharedPtr<FJsonObject>& Params);

    // Helpers
    UNiagaraSystem* LoadNiagaraSystem(const FString& AssetPath, FString& OutError);
    TSharedPtr<FJsonValue> SerializeInputValue(UNiagaraStackFunctionInput* Input);
    TSharedPtr<FJsonObject> SerializeStackEntry(UNiagaraStackEntry* Entry);
    FString GetNiagaraTypeName(const FNiagaraTypeDefinition& Type);
};
```

### Command: `read_niagara_system`

**Input:**
```json
{
    "type": "read_niagara_system",
    "params": {
        "asset_path": "/Game/Effects/NS_Explosion"
    }
}
```

**Output:**
```json
{
    "success": true,
    "system_name": "NS_Explosion",
    "emitters": [
        {
            "name": "Burst",
            "enabled": true,
            "groups": [
                {
                    "name": "Emitter Update",
                    "modules": [
                        {
                            "name": "Spawn Rate",
                            "enabled": true,
                            "inputs": [
                                {"name": "SpawnRate", "type": "float", "value": 100.0, "mode": "local"}
                            ]
                        }
                    ]
                },
                {
                    "name": "Particle Spawn",
                    "modules": [
                        {
                            "name": "Initialize Particle",
                            "enabled": true,
                            "inputs": [
                                {"name": "Lifetime", "type": "float", "value": 2.0, "mode": "local"},
                                {"name": "Color", "type": "FLinearColor", "value": {"R":1,"G":0.5,"B":0,"A":1}, "mode": "local"},
                                {"name": "Sprite Size", "type": "FVector2D", "value": {"X":10,"Y":10}, "mode": "linked", "linked_to": "Emitter.SpriteSize"}
                            ]
                        }
                    ]
                }
            ]
        }
    ],
    "user_parameters": [
        {"name": "SpawnRate", "type": "float", "value": 100.0}
    ]
}
```

**Implementation:**
1. Load UNiagaraSystem via UEditorAssetLibrary
2. Create FNiagaraSystemViewModel (headless, no toolkit)
3. Walk emitter handle view models → stack view models → stack entries
4. Serialize each group → module → input with type and value
5. Collect user parameters from the system's exposed parameters
6. Destroy ViewModel, return JSON

### Command: `write_niagara_module_input`

**Input:**
```json
{
    "type": "write_niagara_module_input",
    "params": {
        "asset_path": "/Game/Effects/NS_Explosion",
        "emitter": "Burst",
        "module": "Initialize Particle",
        "inputs": {
            "Lifetime": 5.0,
            "Color": {"R": 1, "G": 0, "B": 0, "A": 1}
        },
        "save": true
    }
}
```

**Implementation:**
1. Load system, create ViewModel
2. Find emitter by name in handle view models
3. Find module by name in the emitter's stack entries
4. For each input in the request:
   a. Find UNiagaraStackFunctionInput by name
   b. Validate type compatibility
   c. Set local value (switches from linked/dynamic if needed)
5. RequestCompile()
6. Save if requested
7. Return modified inputs list + previous mode for each

### Command: `add_niagara_module`

**Input:**
```json
{
    "type": "add_niagara_module",
    "params": {
        "asset_path": "/Game/Effects/NS_Explosion",
        "emitter": "Burst",
        "group": "Particle Update",
        "module_path": "/Niagara/Modules/AddVelocity"
    }
}
```

**Implementation:**
1. Load system, create ViewModel
2. Find the emitter's stack group by name
3. Load the module script asset from module_path
4. Use stack APIs to add the module to the group
5. RequestCompile(), save
6. Return the new module's name and its default inputs

### Command: `remove_niagara_module`

**Input:**
```json
{
    "type": "remove_niagara_module",
    "params": {
        "asset_path": "/Game/Effects/NS_Explosion",
        "emitter": "Burst",
        "module": "Add Velocity"
    }
}
```

**Implementation:**
1. Load system, create ViewModel
2. Find the module in the emitter's stack
3. Use stack APIs to remove it
4. RequestCompile(), save

### Command: `list_niagara_modules`

**Input:**
```json
{
    "type": "list_niagara_modules",
    "params": {
        "filter": "velocity"
    }
}
```

**Implementation:**
1. Query Asset Registry for UNiagaraScript assets of type Module
2. Filter by keyword if provided (case-insensitive name match)
3. Return name, asset path, and usage context (which groups it applies to) for each

**Output:**
```json
{
    "success": true,
    "modules": [
        {"name": "Add Velocity", "path": "/Niagara/Modules/AddVelocity", "description": "..."},
        {"name": "Add Velocity in Cone", "path": "/Niagara/Modules/AddVelocityInCone", "description": "..."}
    ]
}
```

## ViewModel Lifecycle

Per-command, stateless:
```
Load UNiagaraSystem
  → FNiagaraSystemViewModelOptions (EditMode, no toolkit)
  → Create FNiagaraSystemViewModel
  → Perform operation
  → RequestCompile() (for mutations)
  → UEditorAssetLibrary::SaveLoadedAsset()
  → Destroy ViewModel
```

Fallback: If headless ViewModel creation fails, fall back to direct graph walking for reads (`UNiagaraGraph` / `UNiagaraNodeFunctionCall`) and report an error for mutations.

## Input Value Type Mapping

| Niagara Type | JSON |
|---|---|
| `float` | number |
| `int32` | number |
| `bool` | boolean |
| `FVector` / `FVector3f` | `{"X", "Y", "Z"}` |
| `FVector2D` / `FVector2f` | `{"X", "Y"}` |
| `FLinearColor` | `{"R", "G", "B", "A"}` |
| `FQuat` / `FQuat4f` | `{"X", "Y", "Z", "W"}` |
| Enums | string (enum value name) |
| `UObject*` references | string (asset path) |
| Unknown | ExportText string fallback |

## Input Value Modes

| Mode | Read | Write |
|---|---|---|
| `local` | Serialize value directly | Set value directly |
| `linked` | Return linked parameter name | Switch to local, set value, report previous link |
| `dynamic` | Return dynamic input name | Switch to local, set value, report previous dynamic |
| `default` | Serialize default value | Set local override |

## Bridge Integration

In `EpicUnrealMCPBridge.h` — add `NiagaraCommands` member.

In `ExecuteCommand()` routing:
```cpp
else if (CommandType == TEXT("read_niagara_system") ||
         CommandType == TEXT("write_niagara_module_input") ||
         CommandType == TEXT("add_niagara_module") ||
         CommandType == TEXT("remove_niagara_module") ||
         CommandType == TEXT("list_niagara_modules"))
{
    ResultJson = NiagaraCommands->HandleCommand(CommandType, Params);
}
```

## Python MCP Tools

### New File: `Python/helpers/niagara_manager.py`

### Tools in `unreal_mcp_server_advanced.py`:

```python
@mcp.tool()
def niagara_read(asset_path: str) -> Dict[str, Any]:
    """Read a Niagara System's full structure: emitters, modules, and input values."""

@mcp.tool()
def niagara_write(asset_path: str, emitter: str, module: str, inputs: str, save: bool = True) -> Dict[str, Any]:
    """Set module input values on a Niagara System emitter."""

@mcp.tool()
def niagara_add_module(asset_path: str, emitter: str, group: str, module_path: str) -> Dict[str, Any]:
    """Add a module to a Niagara emitter's group (e.g. Particle Spawn, Particle Update)."""

@mcp.tool()
def niagara_remove_module(asset_path: str, emitter: str, module: str) -> Dict[str, Any]:
    """Remove a module from a Niagara emitter."""

@mcp.tool()
def niagara_list_modules(filter: str = "") -> Dict[str, Any]:
    """List available Niagara module assets, optionally filtered by keyword."""
```

## Test Plan

1. **Read test**: Call `niagara_read` on an existing Niagara system — verify emitters, modules, and input values are returned
2. **Write test**: Modify a module input (e.g., spawn rate or color), read back, verify
3. **Add module test**: Add a known module (e.g., "Add Velocity") to Particle Update, read back, verify it appears
4. **Remove module test**: Remove the added module, read back, verify it's gone
5. **List modules test**: Call `niagara_list_modules` with and without filter, verify results
6. **Validation test**: Attempt invalid operations (wrong emitter name, wrong type) — verify clear error messages
