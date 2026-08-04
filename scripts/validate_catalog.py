#!/usr/bin/env python3

import json
import re
import sys
import urllib.error
import urllib.request
from datetime import date
from pathlib import Path
from urllib.parse import urlparse


ROOT = Path(__file__).resolve().parents[1]

APP_DIR = ROOT / "apps"
AUTHOR_DIR = ROOT / "authors"
CATEGORY_DIR = ROOT / "categories"


APP_REQUIRED = {
    "id",
    "title_id",
    "name",
    "description",
    "long_description",
    "author_id",
    "category_id",
    "subcategory_ids",
    "version",
    "version_date",
    "requirements",
    "size",
    "status",
    "links",
}

AUTHOR_REQUIRED = {
    "id",
    "name",
    "avatar",
    "bio",
    "links",
}

CATEGORY_REQUIRED = {
    "id",
    "name",
    "description",
    "icon",
    "order",
    "subcategories",
}

VALID_STATUSES = {
    "Verified",
    "Legacy",
    "Archive",
}

IMAGE_EXTENSIONS = {
    ".png",
    ".jpg",
    ".jpeg",
    ".webp",
    ".gif",
    ".bmp",
}


def add_error(errors, path, message):
    errors.append(f"{path}: {message}")


def load_json_files(directory, errors):
    data = {}

    if not directory.exists():
        add_error(
            errors,
            str(directory.relative_to(ROOT)),
            "directory does not exist",
        )
        return data

    for path in sorted(directory.glob("*.json")):
        try:
            with path.open("r", encoding="utf-8") as handle:
                value = json.load(handle)

        except json.JSONDecodeError as exc:
            add_error(
                errors,
                str(path.relative_to(ROOT)),
                (
                    f"invalid JSON at line {exc.lineno}, "
                    f"column {exc.colno}: {exc.msg}"
                ),
            )
            continue

        except Exception as exc:
            add_error(
                errors,
                str(path.relative_to(ROOT)),
                (
                    f"cannot read file: "
                    f"{type(exc).__name__}: {exc}"
                ),
            )
            continue

        if not isinstance(value, dict):
            add_error(
                errors,
                str(path.relative_to(ROOT)),
                "root value must be a JSON object",
            )
            continue

        data[path.stem] = (path, value)

    return data


def validate_required(path, obj, required, errors):
    for field in sorted(required - obj.keys()):
        add_error(
            errors,
            path,
            f"missing required field '{field}'",
        )


def validate_id(value, field, path, errors):
    if not isinstance(value, str) or not value:
        add_error(
            errors,
            path,
            f"'{field}' must be a non-empty string",
        )
        return False

    if not re.match(r"^[a-z0-9_-]+$", value):
        add_error(
            errors,
            path,
            (
                f"'{field}' may only contain "
                "lowercase letters, numbers, "
                "hyphens and underscores"
            ),
        )
        return False

    return True


def validate_url(value, path, errors):
    if not isinstance(value, str) or not value.strip():
        add_error(
            errors,
            path,
            "URL must be a non-empty string",
        )
        return False

    parsed = urlparse(value)

    if parsed.scheme not in {"http", "https"}:
        add_error(
            errors,
            path,
            "URL must use http:// or https://",
        )
        return False

    if not parsed.netloc:
        add_error(
            errors,
            path,
            "URL must contain a valid host",
        )
        return False

    return True


def validate_remote(url, path, errors, expect_image=False):
    if not validate_url(url, path, errors):
        return

    headers = {
        "User-Agent": "VitaHub-Validator/1.0",
    }

    request = urllib.request.Request(
        url,
        method="HEAD",
        headers=headers,
    )

    try:
        with urllib.request.urlopen(
            request,
            timeout=15,
        ) as response:

            status = getattr(response, "status", 200)

            content_type = (
                response.headers
                .get("Content-Type", "")
                .lower()
            )

            if status >= 400:
                add_error(
                    errors,
                    path,
                    f"remote URL returned HTTP {status}",
                )
                return

            if expect_image and not content_type.startswith("image/"):
                add_error(
                    errors,
                    path,
                    (
                        "remote resource is not reported "
                        "as an image "
                        f"(Content-Type: "
                        f"{content_type or 'unknown'})"
                    ),
                )

            return

    except Exception:
        pass

    request = urllib.request.Request(
        url,
        method="GET",
        headers={
            **headers,
            "Range": "bytes=0-1023",
        },
    )

    try:
        with urllib.request.urlopen(
            request,
            timeout=15,
        ) as response:

            status = getattr(response, "status", 200)

            content_type = (
                response.headers
                .get("Content-Type", "")
                .lower()
            )

            if status >= 400:
                add_error(
                    errors,
                    path,
                    f"remote URL returned HTTP {status}",
                )
                return

            if expect_image and not content_type.startswith("image/"):
                add_error(
                    errors,
                    path,
                    (
                        "remote resource is not reported "
                        "as an image "
                        f"(Content-Type: "
                        f"{content_type or 'unknown'})"
                    ),
                )

    except urllib.error.HTTPError as exc:
        add_error(
            errors,
            path,
            f"remote URL returned HTTP {exc.code}",
        )

    except Exception as exc:
        add_error(
            errors,
            path,
            (
                "remote URL could not be reached: "
                f"{type(exc).__name__}: {exc}"
            ),
        )


def validate_resource(
    value,
    field_path,
    errors,
    expect_image=False,
    base_directory=None,
):
    if not isinstance(value, str) or not value.strip():
        add_error(
            errors,
            field_path,
            "resource must be a non-empty string",
        )
        return False

    parsed = urlparse(value)

    if parsed.scheme in {"http", "https"}:
        validate_remote(
            value,
            field_path,
            errors,
            expect_image=expect_image,
        )
        return True

    if parsed.scheme:
        add_error(
            errors,
            field_path,
            "resource URL must use http:// or https://",
        )
        return False

    if base_directory is None:
        base_directory = ROOT

    local = (
        base_directory / value
    ).resolve()

    try:
        local.relative_to(ROOT.resolve())

    except ValueError:
        add_error(
            errors,
            field_path,
            "local resource escapes the repository",
        )
        return False

    if not local.is_file():
        add_error(
            errors,
            field_path,
            (
                "local resource does not exist: "
                f"{value}"
            ),
        )
        return False

    if expect_image:
        if local.suffix.lower() not in IMAGE_EXTENSIONS:
            add_error(
                errors,
                field_path,
                (
                    "local image must use a supported "
                    "image extension"
                ),
            )
            return False

    return True


def validate_links(
    links,
    field_path,
    errors,
    require_name=True,
):
    if not isinstance(links, list):
        add_error(
            errors,
            field_path,
            "must be an array",
        )
        return

    if not links:
        add_error(
            errors,
            field_path,
            "must contain at least one link",
        )

    recommended_count = 0

    for index, link in enumerate(links):
        item_path = f"{field_path}[{index}]"

        if not isinstance(link, dict):
            add_error(
                errors,
                item_path,
                "must be an object",
            )
            continue

        required_fields = (
            ("type", "url")
            if not require_name
            else ("type", "name", "url")
        )

        for field in required_fields:
            if field not in link:
                add_error(
                    errors,
                    item_path,
                    f"missing required field '{field}'",
                )

        if "type" in link:
            if (
                not isinstance(link["type"], str)
                or not link["type"].strip()
            ):
                add_error(
                    errors,
                    f"{item_path}.type",
                    "must be a non-empty string",
                )

        if require_name and "name" in link:
            if (
                not isinstance(link["name"], str)
                or not link["name"].strip()
            ):
                add_error(
                    errors,
                    f"{item_path}.name",
                    "must be a non-empty string",
                )

        if "url" in link:
            validate_url(
                link["url"],
                f"{item_path}.url",
                errors,
            )

        if "recommended" in link:
            if not isinstance(
                link["recommended"],
                bool,
            ):
                add_error(
                    errors,
                    f"{item_path}.recommended",
                    "must be boolean",
                )

            elif link["recommended"]:
                recommended_count += 1

    if recommended_count > 1:
        add_error(
            errors,
            field_path,
            (
                "at most one link may have "
                "recommended=true"
            ),
        )


def validate_apps(
    apps,
    authors,
    categories,
    errors,
):
    seen_ids = {}
    seen_title_ids = {}

    category_subcategories = {}

    for stem, (_, category) in categories.items():
        subcategories = category.get(
            "subcategories",
            [],
        )

        if isinstance(subcategories, list):
            category_id = category.get(
                "id",
                stem,
            )

            category_subcategories[
                category_id
            ] = {
                item.get("id")
                for item in subcategories
                if (
                    isinstance(item, dict)
                    and isinstance(
                        item.get("id"),
                        str,
                    )
                )
            }

    for stem, (path, app) in apps.items():
        rel = str(path.relative_to(ROOT))

        validate_required(
            rel,
            app,
            APP_REQUIRED,
            errors,
        )

        app_id = app.get("id")

        if app_id is not None:
            if validate_id(
                app_id,
                "id",
                rel,
                errors,
            ):

                if app_id != stem:
                    add_error(
                        errors,
                        rel,
                        (
                            f"id '{app_id}' must match "
                            f"filename '{stem}.json'"
                        ),
                    )

                if app_id in seen_ids:
                    add_error(
                        errors,
                        rel,
                        (
                            f"duplicate id '{app_id}' "
                            f"(also in "
                            f"{seen_ids[app_id]})"
                        ),
                    )

                else:
                    seen_ids[app_id] = rel

        title_id = app.get("title_id")

        if (
            not isinstance(title_id, str)
            or not title_id.strip()
        ):
            add_error(
                errors,
                f"{rel}.title_id",
                "must be a non-empty string",
            )

        elif title_id in seen_title_ids:
            add_error(
                errors,
                f"{rel}.title_id",
                (
                    f"duplicate title_id '{title_id}' "
                    f"(also in "
                    f"{seen_title_ids[title_id]})"
                ),
            )

        else:
            seen_title_ids[title_id] = rel

        for field in (
            "name",
            "description",
            "long_description",
            "version",
            "requirements",
        ):
            value = app.get(field)

            if (
                not isinstance(value, str)
                or not value.strip()
            ):
                add_error(
                    errors,
                    f"{rel}.{field}",
                    "must be a non-empty string",
                )

        author_id = app.get("author_id")

        if (
            not isinstance(author_id, str)
            or author_id not in authors
        ):
            add_error(
                errors,
                f"{rel}.author_id",
                (
                    f"author '{author_id}' "
                    "does not exist in authors/"
                ),
            )

        category_id = app.get("category_id")

        if (
            not isinstance(category_id, str)
            or category_id not in categories
        ):
            add_error(
                errors,
                f"{rel}.category_id",
                (
                    f"category '{category_id}' "
                    "does not exist in categories/"
                ),
            )

        subcategories = app.get(
            "subcategory_ids"
        )

        if (
            not isinstance(subcategories, list)
            or not subcategories
        ):
            add_error(
                errors,
                f"{rel}.subcategory_ids",
                "must be a non-empty array",
            )

        else:
            if (
                len(set(subcategories))
                != len(subcategories)
            ):
                add_error(
                    errors,
                    f"{rel}.subcategory_ids",
                    (
                        "must not contain "
                        "duplicate IDs"
                    ),
                )

            if category_id in category_subcategories:
                allowed = category_subcategories[
                    category_id
                ]

                for subcategory in subcategories:
                    if not isinstance(
                        subcategory,
                        str,
                    ):
                        add_error(
                            errors,
                            f"{rel}.subcategory_ids",
                            (
                                f"subcategory "
                                f"'{subcategory}' "
                                "must be a string"
                            ),
                        )

                    elif subcategory not in allowed:
                        add_error(
                            errors,
                            f"{rel}.subcategory_ids",
                            (
                                f"subcategory "
                                f"'{subcategory}' "
                                f"does not belong "
                                f"to category "
                                f"'{category_id}'"
                            ),
                        )

        try:
            date.fromisoformat(
                app.get(
                    "version_date",
                    "",
                )
            )

        except (
            TypeError,
            ValueError,
        ):
            add_error(
                errors,
                f"{rel}.version_date",
                "must use YYYY-MM-DD format",
            )

        size = app.get("size")

        if (
            isinstance(size, bool)
            or not isinstance(size, int)
            or size <= 0
        ):
            add_error(
                errors,
                f"{rel}.size",
                (
                    "must be a positive integer "
                    "representing bytes"
                ),
            )

        if app.get("status") not in VALID_STATUSES:
            add_error(
                errors,
                f"{rel}.status",
                (
                    "must be one of: "
                    "Verified, Legacy, Archive"
                ),
            )

        # -------------------------------------------------
        # ICON
        #
        # The application icon is now optional in the
        # source JSON.
        #
        # If missing/empty, the generator will use the
        # icon belonging to the application's category.
        #
        # If an icon IS provided, it must be valid.
        # -------------------------------------------------

        icon = app.get("icon")

        if icon is not None and icon != "":
            validate_resource(
                icon,
                f"{rel}.icon",
                errors,
                expect_image=True,
                base_directory=path.parent,
            )

        else:
            # The category must provide a valid fallback.
            category_entry = categories.get(category_id)

            if category_entry is None:
                add_error(
                    errors,
                    f"{rel}.icon",
                    (
                        "application has no icon and "
                        "its category does not exist"
                    ),
                )

            else:
                category_path, category = category_entry
                category_icon = category.get("icon")

                if not isinstance(
                    category_icon,
                    str,
                ) or not category_icon.strip():
                    add_error(
                        errors,
                        f"{rel}.icon",
                        (
                            "application has no icon and "
                            "its category has no icon"
                        ),
                    )

                else:
                    validate_resource(
                        category_icon,
                        (
                            f"{rel}.icon "
                            f"(category fallback)"
                        ),
                        errors,
                        expect_image=True,
                        base_directory=category_path.parent,
                    )

        # -------------------------------------------------

        # SCREENSHOTS
        #
        # Empty/missing screenshots are allowed because
        # the final icon will be used as fallback.
        #
        # If screenshots are provided, they must contain
        # 1 to 5 valid images.
        # -------------------------------------------------

        screenshots = app.get("screenshots")

        if screenshots is None or screenshots == []:
            # Validated through the icon/category fallback
            # logic above.
            pass

        elif not isinstance(
            screenshots,
            list,
        ):
            add_error(
                errors,
                f"{rel}.screenshots",
                "must be an array",
            )

        elif not 1 <= len(screenshots) <= 5:
            add_error(
                errors,
                f"{rel}.screenshots",
                (
                    "must contain between "
                    "1 and 5 images"
                ),
            )

        else:
            for index, screenshot in enumerate(
                screenshots
            ):
                validate_resource(
                    screenshot,
                    (
                        f"{rel}.screenshots"
                        f"[{index}]"
                    ),
                    errors,
                    expect_image=True,
                    base_directory=path.parent,
                )

        validate_links(
            app.get("links"),
            f"{rel}.links",
            errors,
            require_name=True,
        )


def validate_authors(
    authors,
    errors,
):
    seen_ids = {}

    for stem, (path, author) in authors.items():
        rel = str(path.relative_to(ROOT))

        validate_required(
            rel,
            author,
            AUTHOR_REQUIRED,
            errors,
        )

        author_id = author.get("id")

        if (
            not isinstance(author_id, str)
            or not author_id
        ):
            add_error(
                errors,
                f"{rel}.id",
                "must be a non-empty string",
            )

        else:
            validate_id(
                author_id,
                "id",
                rel,
                errors,
            )

            if author_id != stem:
                add_error(
                    errors,
                    rel,
                    (
                        f"id '{author_id}' must match "
                        f"filename '{stem}.json'"
                    ),
                )

            if author_id in seen_ids:
                add_error(
                    errors,
                    rel,
                    (
                        f"duplicate id '{author_id}' "
                        f"(also in "
                        f"{seen_ids[author_id]})"
                    ),
                )

            else:
                seen_ids[author_id] = rel

        for field in (
            "name",
            "bio",
        ):
            if not isinstance(
                author.get(field),
                str,
            ):
                add_error(
                    errors,
                    f"{rel}.{field}",
                    "must be a string",
                )

        avatar = author.get("avatar")

        if avatar:
            validate_resource(
                avatar,
                f"{rel}.avatar",
                errors,
                expect_image=True,
                base_directory=path.parent,
            )

        validate_links(
            author.get("links"),
            f"{rel}.links",
            errors,
            require_name=False,
        )


def validate_categories(
    categories,
    errors,
):
    seen_ids = {}

    for stem, (path, category) in categories.items():
        rel = str(path.relative_to(ROOT))

        validate_required(
            rel,
            category,
            CATEGORY_REQUIRED,
            errors,
        )

        category_id = category.get("id")

        if (
            not isinstance(category_id, str)
            or not category_id
        ):
            add_error(
                errors,
                f"{rel}.id",
                "must be a non-empty string",
            )

        else:
            validate_id(
                category_id,
                "id",
                rel,
                errors,
            )

            if category_id != stem:
                add_error(
                    errors,
                    rel,
                    (
                        f"id '{category_id}' must match "
                        f"filename '{stem}.json'"
                    ),
                )

            if category_id in seen_ids:
                add_error(
                    errors,
                    rel,
                    (
                        f"duplicate id '{category_id}' "
                        f"(also in "
                        f"{seen_ids[category_id]})"
                    ),
                )

            else:
                seen_ids[category_id] = rel

        for field in (
            "name",
            "description",
        ):
            if not isinstance(
                category.get(field),
                str,
            ):
                add_error(
                    errors,
                    f"{rel}.{field}",
                    "must be a string",
                )

        order = category.get("order")

        if (
            isinstance(order, bool)
            or not isinstance(order, int)
        ):
            add_error(
                errors,
                f"{rel}.order",
                "must be an integer",
            )

        icon = category.get("icon")

        if not isinstance(
            icon,
            str,
        ) or not icon.strip():
            add_error(
                errors,
                f"{rel}.icon",
                "must be a non-empty string",
            )

        else:
            validate_resource(
                icon,
                f"{rel}.icon",
                errors,
                expect_image=True,
                base_directory=path.parent,
            )

        subcategories = category.get(
            "subcategories"
        )

        if not isinstance(
            subcategories,
            list,
        ):
            add_error(
                errors,
                f"{rel}.subcategories",
                "must be an array",
            )
            continue

        sub_ids = set()

        for index, subcategory in enumerate(
            subcategories
        ):
            item_path = (
                f"{rel}.subcategories"
                f"[{index}]"
            )

            if not isinstance(
                subcategory,
                dict,
            ):
                add_error(
                    errors,
                    item_path,
                    "must be an object",
                )
                continue

            sub_id = subcategory.get("id")

            if (
                not isinstance(
                    sub_id,
                    str,
                )
                or not sub_id
            ):
                add_error(
                    errors,
                    f"{item_path}.id",
                    (
                        "must be a non-empty "
                        "string"
                    ),
                )

            else:
                validate_id(
                    sub_id,
                    "id",
                    item_path,
                    errors,
                )

                if sub_id in sub_ids:
                    add_error(
                        errors,
                        item_path,
                        (
                            f"duplicate "
                            f"subcategory id "
                            f"'{sub_id}'"
                        ),
                    )

                else:
                    sub_ids.add(sub_id)

            if (
                not isinstance(
                    subcategory.get("name"),
                    str,
                )
                or not subcategory.get(
                    "name"
                ).strip()
            ):
                add_error(
                    errors,
                    f"{item_path}.name",
                    (
                        "must be a non-empty "
                        "string"
                    ),
                )

def main():
    errors = []

    apps = load_json_files(
        APP_DIR,
        errors,
    )

    authors = load_json_files(
        AUTHOR_DIR,
        errors,
    )

    categories = load_json_files(
        CATEGORY_DIR,
        errors,
    )

    validate_authors(
        authors,
        errors,
    )

    validate_categories(
        categories,
        errors,
    )

    validate_apps(
        apps,
        authors,
        categories,
        errors,
    )

    if errors:
        print()
        print("VitaHub validation failed:")
        print()

        for item in errors:
            print(f"- {item}")

        print()
        print(
            f"{len(errors)} error(s) found."
        )

        return 1

    print()
    print("VitaHub validation passed.")
    print()
    print(
        f"Applications: {len(apps)}"
    )
    print(
        f"Authors: {len(authors)}"
    )
    print(
        f"Categories: {len(categories)}"
    )

    return 0


if __name__ == "__main__":
    sys.exit(main())