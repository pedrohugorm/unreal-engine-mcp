# Data Asset CRUD — Design Spec

**Date:** 2026-03-22
**Status:** Approved

## Summary

Add MCP tools to read, edit, and create Unreal Engine Data Assets. Three consolidated MCP tools (`data_asset_read`, `data_asset_write`, `data_asset_create`) backed by a new C++ command handler class following the existing project pattern.

## Decisions

| Decision | Choice |
|---|---|
| Scope | Read + simple edits (no delete/rename) |
| Property depth | Primitives + structs + containers (TArray, TMap, TSet) |
| Validation | Metadata-driven (ClampMin/Max, enum values, type checks) |
| Tool organization | Consolidated (3 tools) |
| Asset creation | Both class-path and duplicate-from supported |
| Architecture | New command handler class (Approach 1) |

## Architecture

```
AI Client
  ↓ MCP Protocol
Python MCP Tools (data_asset_manager.py)
  ├── data_asset_read(asset_path)
  ├── data_asset_write(asset_path, properties)
  └── data_asset_create(name, package_path, class_path?, duplicate_from?, properties?)
  ↓ TCP Socket (JSON commands)
C++ Plugin (FEpicUnrealMCPDataAssetCommands)
  ├── HandleReadDataAsset
  ├── HandleWriteDataAsset
  └── HandleCreateDataAsset
  ↓ UE Reflection API
Unreal Engine Editor
```

## C++ Command Handler

### New Files

- `UnrealMCP/Source/UnrealMCP/Public/Commands/EpicUnrealMCPDataAssetCommands.h`
- `UnrealMCP/Source/UnrealMCP/Private/Commands/EpicUnrealMCPDataAssetCommands.cpp`

### Class Structure

```cpp
class FEpicUnrealMCPDataAssetCommands
{
public:
    FEpicUnrealMCPDataAssetCommands();
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType,
                                           const TSharedPtr<FJsonObject>& Params);

private:
    // Command handlers
    TSharedPtr<FJsonObject> HandleReadDataAsset(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleWriteDataAsset(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateDataAsset(const TSharedPtr<FJsonObject>& Params);

    // Property serialization
    TSharedPtr<FJsonValue> SerializeProperty(FProperty* Property, const void* ValuePtr);
    bool DeserializeProperty(FProperty* Property, void* ValuePtr,
                             const TSharedPtr<FJsonValue>& JsonValue, FString& OutError);

    // Validation
    bool ValidatePropertyValue(FProperty* Property,
                                const TSharedPtr<FJsonValue>& JsonValue,
                                FString& OutError);
    TSharedPtr<FJsonObject> GetPropertyMetadata(FProperty* Property);
};
```

### Command: `read_data_asset`

**Input:**
```json
{
    "type": "read_data_asset",
    "params": {
        "asset_path": "/Game/MyProject/DA_WS_NPC_TEST"
    }
}
```

**Output:**
```json
{
    "success": true,
    "class_name": "UWorldSystemNPCData",
    "class_path": "/Script/MyProject.WorldSystemNPCData",
    "asset_name": "DA_WS_NPC_TEST",
    "properties": {
        "NPCName": {"type": "FString", "value": "Test NPC"},
        "Health": {"type": "float", "value": 100.0, "meta": {"ClampMin": 0, "ClampMax": 1000}},
        "Behaviors": {"type": "TArray<FNPCBehavior>", "value": [...]},
        ...
    }
}
```

**Implementation:**
1. Load asset via `UEditorAssetLibrary::LoadAsset(asset_path)`
2. Get class via `Asset->GetClass()`
3. Iterate properties via `TFieldIterator<FProperty>(AssetClass)`
4. Skip properties from `UObject` and `UDataAsset` base classes
5. Serialize each property value + metadata to JSON

### Command: `write_data_asset`

**Input:**
```json
{
    "type": "write_data_asset",
    "params": {
        "asset_path": "/Game/MyProject/DA_WS_NPC_TEST",
        "properties": {
            "Health": 150.0,
            "NPCName": "Modified NPC"
        },
        "save": true
    }
}
```

**Output (success):**
```json
{
    "success": true,
    "modified_properties": ["Health", "NPCName"],
    "asset_saved": true
}
```

**Output (validation failure):**
```json
{
    "success": false,
    "error": "Validation failed for property 'Health': value 2000.0 exceeds ClampMax of 1000.0"
}
```

**Implementation:**
1. Load asset via `UEditorAssetLibrary::LoadAsset()`
2. For each property in the request:
   a. Find property via `GetClass()->FindPropertyByName()`
   b. Validate type compatibility and metadata constraints
   c. If any validation fails, abort entire operation (atomic — no partial writes)
3. If all valid: apply all changes wrapped in `PreEditChange`/`PostEditChangeProperty`
4. If `save` is true (default), save via `UEditorAssetLibrary::SaveLoadedAsset()`

### Command: `create_data_asset`

**Input (from class):**
```json
{
    "type": "create_data_asset",
    "params": {
        "asset_name": "DA_WS_NPC_New",
        "package_path": "/Game/MyProject",
        "class_path": "/Script/MyProject.WorldSystemNPCData",
        "properties": {"NPCName": "New NPC"}
    }
}
```

**Input (duplicate):**
```json
{
    "type": "create_data_asset",
    "params": {
        "asset_name": "DA_WS_NPC_Copy",
        "package_path": "/Game/MyProject",
        "duplicate_from": "/Game/MyProject/DA_WS_NPC_TEST",
        "properties": {"NPCName": "Copied NPC"}
    }
}
```

**Implementation:**
1. If `duplicate_from` is set: `UEditorAssetLibrary::DuplicateAsset(source, destination)`
2. If `class_path` is set: Find UClass, create asset via `UAssetToolsModule::CreateAsset()`
3. If `properties` provided: apply via the write handler logic
4. Save the new asset

### Property Type Support

| FProperty Subclass | JSON Type | Serialize | Deserialize | Validate |
|---|---|---|---|---|
| `FBoolProperty` | boolean | direct | direct | type check |
| `FIntProperty`, `FInt64Property`, `FUInt32Property`, `FByteProperty` | number | direct | direct | ClampMin/Max |
| `FFloatProperty`, `FDoubleProperty` | number | direct | direct | ClampMin/Max |
| `FStrProperty` | string | direct | direct | type check |
| `FNameProperty` | string | `.ToString()` | `FName()` | type check |
| `FTextProperty` | string | `.ToString()` | `FText::FromString()` | type check |
| `FEnumProperty` | string | enum name | lookup by name | valid enum value |
| `FStructProperty` | object | recurse fields | recurse fields | per-field validation |
| `FArrayProperty` | array | recurse inner | recurse inner | per-element |
| `FMapProperty` | array of {key,value} | recurse K/V | recurse K/V | per-entry |
| `FSetProperty` | array | recurse inner | recurse inner | per-element |
| `FSoftObjectProperty` | string | path string | resolve path | path exists |
| `FObjectProperty` | string | path string | resolve path | path exists |

**Known struct shortcuts:** `FVector` → `{X,Y,Z}`, `FRotator` → `{Pitch,Yaw,Roll}`, `FColor` → `{R,G,B,A}`, `FLinearColor` → `{R,G,B,A}`, `FVector2D` → `{X,Y}`.

### Validation Rules

1. **Type mismatch**: JSON number for string property → reject
2. **Numeric range**: Value outside ClampMin/ClampMax → reject with constraint info
3. **Enum value**: String not in enum → reject with list of valid values
4. **Property not found**: Unknown property name → reject with list of available properties
5. **Array element**: Validate each element against inner property type
6. **Struct field**: Validate each field recursively
7. **Atomic writes**: If any property fails validation, no properties are modified

## Bridge Integration

In `EpicUnrealMCPBridge.h`:
```cpp
TSharedPtr<FEpicUnrealMCPDataAssetCommands> DataAssetCommands;
```

In `EpicUnrealMCPBridge.cpp` constructor:
```cpp
DataAssetCommands = MakeShared<FEpicUnrealMCPDataAssetCommands>();
```

In `ExecuteCommand()` routing:
```cpp
else if (CommandType == TEXT("read_data_asset") ||
         CommandType == TEXT("write_data_asset") ||
         CommandType == TEXT("create_data_asset"))
{
    ResultJson = DataAssetCommands->HandleCommand(CommandType, Params);
}
```

## Python MCP Tools

### New Files

- `Python/helpers/data_asset_manager.py`

### Tool: `data_asset_read`

```python
@mcp.tool()
def data_asset_read(asset_path: str) -> str:
    """Read a Data Asset's class info, properties, and values.

    Args:
        asset_path: UE content path (e.g. "/Game/MyProject/DA_WS_NPC_TEST")
    """
```

### Tool: `data_asset_write`

```python
@mcp.tool()
def data_asset_write(asset_path: str, properties: dict, save: bool = True) -> str:
    """Edit one or more properties on a Data Asset.

    Validates all values before applying any changes (atomic).

    Args:
        asset_path: UE content path
        properties: Dict of property_name → new_value
        save: Whether to save after editing (default True)
    """
```

### Tool: `data_asset_create`

```python
@mcp.tool()
def data_asset_create(
    asset_name: str,
    package_path: str,
    class_path: str | None = None,
    duplicate_from: str | None = None,
    properties: dict | None = None,
) -> str:
    """Create a new Data Asset from a class or by duplicating an existing one.

    Args:
        asset_name: Name for the new asset
        package_path: UE package path (e.g. "/Game/MyProject")
        class_path: UClass path for new asset (e.g. "/Script/MyProject.MyDataAsset")
        duplicate_from: Asset path to duplicate from (alternative to class_path)
        properties: Optional property overrides to apply after creation
    """
```

## Test Plan

1. **Read test**: Call `data_asset_read` on `/Game/MyProject/DA_WS_NPC_TEST` — verify class name, property list, and values are returned
2. **Write test**: Edit a property on `DA_WS_NPC_TEST`, read it back, verify the value changed
3. **Validation test**: Attempt an invalid edit (wrong type, out of range) — verify rejection with clear error
4. **Create from duplicate**: Duplicate `DA_WS_NPC_TEST` → `DA_WS_NPC_TEST_COPY`, verify identical properties
5. **Create from class**: Create a new asset from the same class path, verify empty/default properties
6. **Container test**: Read/write array and struct properties if present on the test asset
