#!/usr/bin/env python3
from __future__ import annotations

import json
from pathlib import Path


class OverrideError(ValueError):
    pass


PROTECTED = {"id", "title_id", "author_ids", "category_id", "subcategory_ids", "status"}
CONTROLLED = {"version", "version_date", "size"}
SCALAR_FIELDS = {"name", "description", "long_description", "requirements", "icon", "changelog"}
ARRAY_FIELDS = {"screenshots", "links"}


def load_overrides(root: Path) -> dict[str, dict]:
    directory = root / "catalog_overrides"
    result = {}
    if not directory.exists():
        return result
    for path in sorted(directory.glob("*.json")):
        with path.open("r", encoding="utf-8") as handle:
            value = json.load(handle)
        if not isinstance(value, dict):
            raise OverrideError(f"{path}: root must be an object")
        app_id = value.get("id")
        if not isinstance(app_id, str) or not app_id:
            raise OverrideError(f"{path}: id must be a non-empty string")
        if path.stem != app_id:
            raise OverrideError(f"{path}: filename must match id '{app_id}'")
        invalid = (set(value) - {"id"} - SCALAR_FIELDS - ARRAY_FIELDS)
        if invalid:
            raise OverrideError(f"{path}: unsupported override fields: {sorted(invalid)}")
        for field in PROTECTED | CONTROLLED:
            if field in value:
                raise OverrideError(f"{path}: '{field}' is not overrideable")
        result[app_id] = value
    return result


def _merge_list(base, operation):
    if "replace" in operation:
        return list(operation["replace"])
    result = list(base or [])
    for item in operation.get("remove", []):
        result = [existing for existing in result if existing != item]
    for item in operation.get("add", []):
        if item not in result:
            result.append(item)
    return result


def apply_override(app: dict, override: dict) -> dict:
    result = dict(app)
    for field in SCALAR_FIELDS:
        if field in override:
            result[field] = override[field]
    for field in ARRAY_FIELDS:
        if field in override:
            operation = override[field]
            if not isinstance(operation, dict):
                raise OverrideError(f"override {app.get('id')}: {field} must be an object")
            result[field] = _merge_list(result.get(field), operation)
    return result
