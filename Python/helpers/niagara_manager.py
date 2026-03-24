"""
Filename: niagara_manager.py
Description: Python wrapper for Niagara system read/write/module operations
"""

import logging
from typing import Dict, Any, Optional

logger = logging.getLogger("NiagaraManager")


def read_niagara_system(unreal_connection, asset_path: str) -> Dict[str, Any]:
    """Read a Niagara System's full structure: emitters, modules, and input values."""
    response = unreal_connection.send_command("read_niagara_system", {
        "asset_path": asset_path
    })
    if not response:
        return {"success": False, "error": "No response from Unreal Engine"}
    return response


def write_niagara_module_input(
    unreal_connection,
    asset_path: str,
    emitter: str,
    module: str,
    inputs: Dict[str, Any],
    save: bool = True,
) -> Dict[str, Any]:
    """Set module input values on a Niagara System emitter."""
    response = unreal_connection.send_command("write_niagara_module_input", {
        "asset_path": asset_path,
        "emitter": emitter,
        "module": module,
        "inputs": inputs,
        "save": save,
    })
    if not response:
        return {"success": False, "error": "No response from Unreal Engine"}
    return response


def add_niagara_module(
    unreal_connection,
    asset_path: str,
    emitter: str,
    group: str,
    module_path: str,
) -> Dict[str, Any]:
    """Add a module to a Niagara emitter's group."""
    response = unreal_connection.send_command("add_niagara_module", {
        "asset_path": asset_path,
        "emitter": emitter,
        "group": group,
        "module_path": module_path,
    })
    if not response:
        return {"success": False, "error": "No response from Unreal Engine"}
    return response


def remove_niagara_module(
    unreal_connection,
    asset_path: str,
    emitter: str,
    module: str,
) -> Dict[str, Any]:
    """Remove a module from a Niagara emitter."""
    response = unreal_connection.send_command("remove_niagara_module", {
        "asset_path": asset_path,
        "emitter": emitter,
        "module": module,
    })
    if not response:
        return {"success": False, "error": "No response from Unreal Engine"}
    return response


def list_niagara_modules(
    unreal_connection,
    filter: str = "",
) -> Dict[str, Any]:
    """List available Niagara module assets."""
    params = {}
    if filter:
        params["filter"] = filter
    response = unreal_connection.send_command("list_niagara_modules", params)
    if not response:
        return {"success": False, "error": "No response from Unreal Engine"}
    return response
