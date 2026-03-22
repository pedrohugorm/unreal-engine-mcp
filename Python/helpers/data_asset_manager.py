"""
Filename: data_asset_manager.py
Description: Python wrapper for Data Asset read/write/create operations
"""

import json
import logging
from typing import Dict, Any, Optional

logger = logging.getLogger("DataAssetManager")


def read_data_asset(
    unreal_connection,
    asset_path: str,
) -> Dict[str, Any]:
    """
    Read a Data Asset's class info, properties, and values.

    Args:
        unreal_connection: Connection to Unreal Engine
        asset_path: UE content path (e.g. "/Game/Developers/pedro/DA_WS_NPC_TEST")

    Returns:
        Dictionary containing:
            - success (bool): Whether operation succeeded
            - class_name (str): Name of the UDataAsset subclass
            - class_path (str): Full class path
            - asset_name (str): Asset name
            - asset_path (str): Full asset path
            - properties (dict): Property name -> {type, value, meta?}
            - error (str): Error message if failed
    """
    response = unreal_connection.send_command("read_data_asset", {
        "asset_path": asset_path
    })

    if not response:
        return {"success": False, "error": "No response from Unreal Engine"}

    return response


def write_data_asset(
    unreal_connection,
    asset_path: str,
    properties: Dict[str, Any],
    save: bool = True,
) -> Dict[str, Any]:
    """
    Edit one or more properties on a Data Asset.

    Validates all values before applying any changes (atomic).

    Args:
        unreal_connection: Connection to Unreal Engine
        asset_path: UE content path
        properties: Dict of property_name -> new_value
        save: Whether to save after editing (default True)

    Returns:
        Dictionary containing:
            - success (bool): Whether operation succeeded
            - modified_properties (list): Names of modified properties
            - asset_saved (bool): Whether the asset was saved to disk
            - error (str): Error message if failed
    """
    response = unreal_connection.send_command("write_data_asset", {
        "asset_path": asset_path,
        "properties": properties,
        "save": save,
    })

    if not response:
        return {"success": False, "error": "No response from Unreal Engine"}

    return response


def create_data_asset(
    unreal_connection,
    asset_name: str,
    package_path: str,
    class_path: Optional[str] = None,
    duplicate_from: Optional[str] = None,
    properties: Optional[Dict[str, Any]] = None,
) -> Dict[str, Any]:
    """
    Create a new Data Asset from a class or by duplicating an existing one.

    Args:
        unreal_connection: Connection to Unreal Engine
        asset_name: Name for the new asset
        package_path: UE package path (e.g. "/Game/Developers/pedro")
        class_path: UClass path for new asset (e.g. "/Script/SpaceGame.MyDataAsset")
        duplicate_from: Asset path to duplicate from (alternative to class_path)
        properties: Optional property overrides to apply after creation

    Returns:
        Dictionary containing:
            - success (bool): Whether operation succeeded
            - asset_name (str): Name of created asset
            - asset_path (str): Full path of created asset
            - class_name (str): Class name
            - created_from (str): "duplicate" or "class"
            - asset_saved (bool): Whether the asset was saved to disk
            - error (str): Error message if failed
    """
    params = {
        "asset_name": asset_name,
        "package_path": package_path,
    }

    if class_path:
        params["class_path"] = class_path
    if duplicate_from:
        params["duplicate_from"] = duplicate_from
    if properties:
        params["properties"] = properties

    response = unreal_connection.send_command("create_data_asset", params)

    if not response:
        return {"success": False, "error": "No response from Unreal Engine"}

    return response
