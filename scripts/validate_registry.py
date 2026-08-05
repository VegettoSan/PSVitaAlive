#!/usr/bin/env python3

import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REGISTRY_PATH = ROOT / "registry" / "retired_ids.json"

DIRECTORIES = {
    "apps": ROOT / "apps",
    "authors": ROOT / "authors",
    "categories": ROOT / "categories",
}


def error(errors, message):
    errors.append(message)


def run_git(*arguments):
    result = subprocess.run(
        ["git", *arguments],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip())
    return result.stdout


def get_base_revision():
    event_name = os.environ.get("GITHUB_EVENT_NAME", "")
    event_path = os.environ.get("GITHUB_EVENT_PATH", "")

    if event_path and Path(event_path).is_file():
        try:
            with open(event_path, "r", encoding="utf-8") as handle:
                event = json.load(handle)

            if event_name == "push":
                revision = event.get("before", "")
                if revision and revision != "0" * 40:
                    return revision

            if event_name == "pull_request":
                revision = (
                    event.get("pull_request", {})
                    .get("base", {})
                    .get("sha", "")
                )
                if revision:
                    return revision
        except Exception:
            pass

    try:
        revision = run_git("rev-parse", "HEAD^").strip()
        if revision:
            return revision
    except Exception:
        pass

    return None


def load_json_file(path):
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def load_current_state():
    state = {"apps": {}, "authors": {}, "categories": {}}

    for kind, directory in DIRECTORIES.items():
        if not directory.exists():
            continue

        for path in sorted(directory.glob("*.json")):
            relative = str(path.relative_to(ROOT))
            try:
                state[kind][relative] = load_json_file(path)
            except Exception:
                continue

    return state


def load_base_state(revision):
    state = {"apps": {}, "authors": {}, "categories": {}}

    if not revision:
        return state

    try:
        output = run_git(
            "ls-tree",
            "-r",
            "--name-only",
            revision,
            "--",
            "apps",
            "authors",
            "categories",
        )
    except Exception:
        return state

    for relative in output.splitlines():
        relative = relative.strip()
        if not relative.endswith(".json"):
            continue

        parts = Path(relative).parts
        if not parts or parts[0] not in state:
            continue

        try:
            content = run_git("show", f"{revision}:{relative}")
            state[parts[0]][relative] = json.loads(content)
        except Exception:
            continue

    return state


def load_registry(errors):
    if not REGISTRY_PATH.exists():
        error(
            errors,
            "registry/retired_ids.json: file does not exist",
        )
        return {}

    try:
        registry = load_json_file(REGISTRY_PATH)
    except Exception as exc:
        error(
            errors,
            f"registry/retired_ids.json: invalid JSON: {exc}",
        )
        return {}

    if not isinstance(registry, dict):
        error(
            errors,
            "registry/retired_ids.json: root must be an object",
        )
        return {}

    if registry.get("version") != 2:
        error(
            errors,
            "registry/retired_ids.json: 'version' must be 2",
        )

    required = {
        "apps": {"ids", "title_ids", "restorable"},
        "authors": {"ids", "restorable"},
        "categories": {"ids"},
        "subcategories": {"ids"},
    }

    for section, fields in required.items():
        value = registry.get(section)

        if not isinstance(value, dict):
            error(
                errors,
                f"registry/retired_ids.json: '{section}' must be an object",
            )
            continue

        for field in fields:
            values = value.get(field)

            if not isinstance(values, list):
                error(
                    errors,
                    (
                        "registry/retired_ids.json: "
                        f"'{section}.{field}' must be an array"
                    ),
                )
                continue

            if field == "restorable":
                continue

            if len(values) != len(set(values)):
                error(
                    errors,
                    (
                        "registry/retired_ids.json: "
                        f"'{section}.{field}' contains duplicate values"
                    ),
                )

            for item in values:
                if not isinstance(item, str) or not item.strip():
                    error(
                        errors,
                        (
                            "registry/retired_ids.json: "
                            f"'{section}.{field}' contains an invalid ID"
                        ),
                    )

    validate_restorable_apps(registry, errors)
    validate_restorable_authors(registry, errors)

    return registry


def validate_restorable_apps(registry, errors):
    items = (
        registry.get("apps", {})
        .get("restorable", [])
    )

    if not isinstance(items, list):
        return

    seen = set()
    retired_app_ids = set(
        registry.get("apps", {}).get("ids", [])
    )
    retired_title_ids = set(
        registry.get("apps", {}).get("title_ids", [])
    )

    for index, item in enumerate(items):
        path = f"registry/retired_ids.json:apps.restorable[{index}]"

        if not isinstance(item, dict):
            error(errors, f"{path}: must be an object")
            continue

        for field in ("id", "title_id", "author_id"):
            if not isinstance(item.get(field), str) or not item[field].strip():
                error(
                    errors,
                    f"{path}: '{field}' must be a non-empty string",
                )

        if not all(
            isinstance(item.get(field), str) and item[field].strip()
            for field in ("id", "title_id", "author_id")
        ):
            continue

        identity = (
            item["id"],
            item["title_id"],
            item["author_id"],
        )

        if identity in seen:
            error(
                errors,
                f"{path}: duplicate restoration identity",
            )
        else:
            seen.add(identity)

        if item["id"] not in retired_app_ids:
            error(
                errors,
                (
                    f"{path}: app id '{item['id']}' must also "
                    "exist in apps.ids"
                ),
            )

        if item["title_id"] not in retired_title_ids:
            error(
                errors,
                (
                    f"{path}: title_id '{item['title_id']}' must also "
                    "exist in apps.title_ids"
                ),
            )


def validate_restorable_authors(registry, errors):
    items = (
        registry.get("authors", {})
        .get("restorable", [])
    )

    if not isinstance(items, list):
        return

    seen = set()
    retired_author_ids = set(
        registry.get("authors", {}).get("ids", [])
    )

    for index, item in enumerate(items):
        path = f"registry/retired_ids.json:authors.restorable[{index}]"

        if not isinstance(item, dict):
            error(errors, f"{path}: must be an object")
            continue

        if not isinstance(item.get("id"), str) or not item["id"].strip():
            error(
                errors,
                f"{path}: 'id' must be a non-empty string",
            )
            continue

        if item["id"] in seen:
            error(
                errors,
                f"{path}: duplicate restoration identity",
            )
        else:
            seen.add(item["id"])

        if item["id"] not in retired_author_ids:
            error(
                errors,
                (
                    f"{path}: author id '{item['id']}' must also "
                    "exist in authors.ids"
                ),
            )


def registry_sets(registry):
    return {
        "app_ids": set(registry.get("apps", {}).get("ids", [])),
        "title_ids": set(registry.get("apps", {}).get("title_ids", [])),
        "author_ids": set(registry.get("authors", {}).get("ids", [])),
        "category_ids": set(registry.get("categories", {}).get("ids", [])),
        "subcategory_ids": set(
            registry.get("subcategories", {}).get("ids", [])
        ),
    }


def current_sets(state):
    app_ids = set()
    title_ids = set()
    author_ids = set()
    category_ids = set()
    subcategory_ids = set()

    for app in state["apps"].values():
        if isinstance(app, dict):
            if isinstance(app.get("id"), str):
                app_ids.add(app["id"])
            if isinstance(app.get("title_id"), str):
                title_ids.add(app["title_id"])

    for author in state["authors"].values():
        if isinstance(author, dict) and isinstance(author.get("id"), str):
            author_ids.add(author["id"])

    for category in state["categories"].values():
        if not isinstance(category, dict):
            continue

        category_id = category.get("id")
        if not isinstance(category_id, str):
            continue

        category_ids.add(category_id)

        subcategories = category.get("subcategories", [])
        if isinstance(subcategories, list):
            for item in subcategories:
                if not isinstance(item, dict):
                    continue

                sub_id = item.get("id")
                if isinstance(sub_id, str):
                    subcategory_ids.add(f"{category_id}:{sub_id}")

    return {
        "app_ids": app_ids,
        "title_ids": title_ids,
        "author_ids": author_ids,
        "category_ids": category_ids,
        "subcategory_ids": subcategory_ids,
    }


def require_retired(errors, registry, section, field, value, description):
    values = set(
        registry.get(section, {}).get(field, [])
    )

    if value not in values:
        error(
            errors,
            (
                f"{description}: identifier '{value}' was removed or changed "
                "but is not registered in registry/retired_ids.json "
                f"under {section}.{field}"
            ),
        )


def restorable_app_identities(registry):
    return {
        (
            item.get("id"),
            item.get("title_id"),
            item.get("author_id"),
        )
        for item in registry.get("apps", {}).get("restorable", [])
        if isinstance(item, dict)
    }


def restorable_author_ids(registry):
    return {
        item.get("id")
        for item in registry.get("authors", {}).get("restorable", [])
        if isinstance(item, dict)
    }


def validate_registry_against_current(errors, registry, current):
    retired = registry_sets(registry)
    current_ids = current_sets(current)

    restorable_apps = restorable_app_identities(registry)
    restorable_authors = restorable_author_ids(registry)

    for value in sorted(retired["app_ids"] & current_ids["app_ids"]):
        matches = [
            app
            for app in current["apps"].values()
            if isinstance(app, dict) and app.get("id") == value
        ]

        allowed = any(
            (
                app.get("id"),
                app.get("title_id"),
                app.get("author_id"),
            )
            in restorable_apps
            for app in matches
        )

        if not allowed:
            error(
                errors,
                (
                    "registry/retired_ids.json: "
                    f"identifier '{value}' in apps.ids is still active "
                    "and is not a registered restoration"
                ),
            )

    for value in sorted(retired["title_ids"] & current_ids["title_ids"]):
        matches = [
            app
            for app in current["apps"].values()
            if isinstance(app, dict) and app.get("title_id") == value
        ]

        allowed = any(
            (
                app.get("id"),
                app.get("title_id"),
                app.get("author_id"),
            )
            in restorable_apps
            for app in matches
        )

        if not allowed:
            error(
                errors,
                (
                    "registry/retired_ids.json: "
                    f"identifier '{value}' in apps.title_ids is still active "
                    "and is not a registered restoration"
                ),
            )

    for value in sorted(retired["author_ids"] & current_ids["author_ids"]):
        matches = [
            author
            for author in current["authors"].values()
            if isinstance(author, dict) and author.get("id") == value
        ]

        if not any(
            author.get("id") in restorable_authors
            for author in matches
        ):
            error(
                errors,
                (
                    "registry/retired_ids.json: "
                    f"identifier '{value}' in authors.ids is still active "
                    "and is not a registered restoration"
                ),
            )

    for value in sorted(retired["category_ids"] & current_ids["category_ids"]):
        error(
            errors,
            (
                "registry/retired_ids.json: "
                f"identifier '{value}' in categories.ids is still active"
            ),
        )

    for value in sorted(
        retired["subcategory_ids"] & current_ids["subcategory_ids"]
    ):
        error(
            errors,
            (
                "registry/retired_ids.json: "
                f"identifier '{value}' in subcategories.ids is still active"
            ),
        )


def validate_removed_identifiers(errors, registry, previous, current):
    if not previous:
        return

    retired = registry_sets(registry)
    current_ids = current_sets(current)
    restorable_apps = restorable_app_identities(registry)
    restorable_authors = restorable_author_ids(registry)

    for path, old_app in previous["apps"].items():
        if not isinstance(old_app, dict):
            continue

        old_id = old_app.get("id")
        old_title_id = old_app.get("title_id")
        new_app = current["apps"].get(path)

        if new_app is not None:
            new_id = new_app.get("id") if isinstance(new_app, dict) else None
            new_title_id = (
                new_app.get("title_id")
                if isinstance(new_app, dict)
                else None
            )

            if old_id != new_id and isinstance(old_id, str):
                require_retired(
                    errors,
                    registry,
                    "apps",
                    "ids",
                    old_id,
                    f"{path}.id",
                )

            if old_title_id != new_title_id and isinstance(old_title_id, str):
                require_retired(
                    errors,
                    registry,
                    "apps",
                    "title_ids",
                    old_title_id,
                    f"{path}.title_id",
                )

            continue

        if isinstance(old_id, str):
            if old_id in current_ids["app_ids"]:
                current_apps = [
                    app
                    for app in current["apps"].values()
                    if isinstance(app, dict) and app.get("id") == old_id
                ]

                allowed = any(
                    (
                        app.get("id"),
                        app.get("title_id"),
                        app.get("author_id"),
                    )
                    in restorable_apps
                    for app in current_apps
                )

                if not allowed:
                    error(
                        errors,
                        (
                            f"{path}: application id '{old_id}' was reused "
                            "by another application"
                        ),
                    )
            elif old_id not in retired["app_ids"]:
                require_retired(
                    errors,
                    registry,
                    "apps",
                    "ids",
                    old_id,
                    path,
                )

        if isinstance(old_title_id, str):
            if old_title_id in current_ids["title_ids"]:
                current_apps = [
                    app
                    for app in current["apps"].values()
                    if isinstance(app, dict)
                    and app.get("title_id") == old_title_id
                ]

                allowed = any(
                    (
                        app.get("id"),
                        app.get("title_id"),
                        app.get("author_id"),
                    )
                    in restorable_apps
                    for app in current_apps
                )

                if not allowed:
                    error(
                        errors,
                        (
                            f"{path}: title_id '{old_title_id}' was reused "
                            "by another application"
                        ),
                    )
            elif old_title_id not in retired["title_ids"]:
                require_retired(
                    errors,
                    registry,
                    "apps",
                    "title_ids",
                    old_title_id,
                    path,
                )

    for path, old_author in previous["authors"].items():
        if not isinstance(old_author, dict):
            continue

        old_id = old_author.get("id")
        new_author = current["authors"].get(path)

        if new_author is not None:
            new_id = (
                new_author.get("id")
                if isinstance(new_author, dict)
                else None
            )

            if old_id != new_id and isinstance(old_id, str):
                require_retired(
                    errors,
                    registry,
                    "authors",
                    "ids",
                    old_id,
                    f"{path}.id",
                )

        elif isinstance(old_id, str):
            if old_id in current_ids["author_ids"]:
                if old_id not in restorable_authors:
                    error(
                        errors,
                        (
                            f"{path}: author id '{old_id}' was reused "
                            "by another author"
                        ),
                    )
            elif old_id not in retired["author_ids"]:
                require_retired(
                    errors,
                    registry,
                    "authors",
                    "ids",
                    old_id,
                    path,
                )

    for path, old_category in previous["categories"].items():
        if not isinstance(old_category, dict):
            continue

        old_category_id = old_category.get("id")
        new_category = current["categories"].get(path)

        old_sub_ids = set()
        old_subcategories = old_category.get("subcategories", [])

        if isinstance(old_subcategories, list):
            for item in old_subcategories:
                if (
                    isinstance(item, dict)
                    and isinstance(item.get("id"), str)
                    and isinstance(old_category_id, str)
                ):
                    old_sub_ids.add(
                        f"{old_category_id}:{item['id']}"
                    )

        new_sub_ids = set()

        if isinstance(new_category, dict):
            new_category_id = new_category.get("id")
            new_subcategories = new_category.get("subcategories", [])

            if isinstance(new_subcategories, list):
                for item in new_subcategories:
                    if (
                        isinstance(item, dict)
                        and isinstance(item.get("id"), str)
                        and isinstance(new_category_id, str)
                    ):
                        new_sub_ids.add(
                            f"{new_category_id}:{item['id']}"
                        )

        if new_category is not None:
            new_category_id = (
                new_category.get("id")
                if isinstance(new_category, dict)
                else None
            )

            if (
                old_category_id != new_category_id
                and isinstance(old_category_id, str)
            ):
                require_retired(
                    errors,
                    registry,
                    "categories",
                    "ids",
                    old_category_id,
                    f"{path}.id",
                )

        elif isinstance(old_category_id, str):
            if old_category_id in current_ids["category_ids"]:
                error(
                    errors,
                    (
                        f"{path}: category id '{old_category_id}' was reused "
                        "by another category"
                    ),
                )
            elif old_category_id not in retired["category_ids"]:
                require_retired(
                    errors,
                    registry,
                    "categories",
                    "ids",
                    old_category_id,
                    path,
                )

        for sub_id in sorted(old_sub_ids - new_sub_ids):
            if sub_id in current_ids["subcategory_ids"]:
                error(
                    errors,
                    f"{path}: subcategory id '{sub_id}' was reused",
                )
            elif sub_id not in retired["subcategory_ids"]:
                require_retired(
                    errors,
                    registry,
                    "subcategories",
                    "ids",
                    sub_id,
                    path,
                )


def main():
    errors = []

    registry = load_registry(errors)
    current = load_current_state()

    validate_registry_against_current(
        errors,
        registry,
        current,
    )

    base_revision = get_base_revision()
    previous = load_base_state(base_revision)

    validate_removed_identifiers(
        errors,
        registry,
        previous,
        current,
    )

    if errors:
        print()
        print("VitaHub historical ID validation failed:")
        print()

        for item in errors:
            print(f"- {item}")

        print()
        print(f"{len(errors)} error(s) found.")
        return 1

    print()
    print("VitaHub historical ID validation passed.")
    print()
    return 0


if __name__ == "__main__":
    sys.exit(main())