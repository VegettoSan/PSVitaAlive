#!/usr/bin/env python3
"""Standalone Discord notifier. Never modifies generated catalogs."""
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

SITE = "https://vegettosan.github.io/PSVitaAlive/app.html?title_id="
WEBHOOKS = {
    "new": "DISCORD_NEW_HOMEBREW_WEBHOOK_URL",
    "updated": "DISCORD_HOMEBREW_UPDATES_WEBHOOK_URL",
    "release": "DISCORD_RELEASE_WEBHOOK_URL",
}


def read_json(path, fallback=None):
    file = Path(path)
    return json.loads(file.read_text(encoding="utf-8")) if file.exists() else fallback


def save_json(path, value):
    file = Path(path)
    file.parent.mkdir(parents=True, exist_ok=True)
    file.write_text(json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def state_from_file(path):
    data = read_json(path, {})
    if not isinstance(data, dict) or data.get("schema", 1) != 1:
        raise ValueError("Invalid Discord notification state")
    if not isinstance(data.get("apps", {}), dict) or not isinstance(data.get("releases", []), list):
        raise ValueError("Invalid Discord notification state fields")
    return {"schema": 1, "catalog_initialized": bool(data.get("catalog_initialized", False)),
            "apps": data.get("apps", {}), "releases": data.get("releases", [])}


def snapshot(catalog):
    if not isinstance(catalog, list) or not catalog:
        raise ValueError("Catalog must be a non-empty array (refusing to overwrite state)")
    result = {}
    for app in catalog:
        if not isinstance(app, dict):
            raise ValueError("Invalid catalog entry")
        title_id = app.get("title_id")
        if not isinstance(title_id, str) or not title_id.strip() or title_id in result:
            raise ValueError("Missing or duplicate title_id")
        result[title_id] = {"version": str(app.get("version") or "").strip()}
    return result


def changes(old, new, apps):
    additions, updates = [], []
    for app in apps:
        title_id = app["title_id"]
        if title_id not in old:
            additions.append(app)
        elif old[title_id].get("version", "") != new[title_id]["version"]:
            updates.append((app, old[title_id].get("version", "")))
    return additions, updates


def limited(value, maximum=250):
    value = str(value or "").replace("@", "＠").replace(chr(96), "'")
    return value[:maximum - 1] + "…" if len(value) > maximum else value


def app_embed(app, old_version=None):
    title_id = app["title_id"]
    name = limited(app.get("name") or title_id, 180)
    version = limited(app.get("version") or "Unknown", 70)
    description = limited(app.get("description"), 250)
    url = SITE + urllib.parse.quote(title_id, safe="")
    if old_version is None:
        title, line = f"New Homebrew: {name}", f"Version: **{version}**"
    else:
        title = f"Homebrew Updated: {name}"
        line = f"Version: **{limited(old_version or 'Unknown', 70)} → {version}**"
    content = f"{line}\nTitle ID: {limited(title_id, 80)}"
    if app.get("version_date"):
        content += f"\nRelease date: {limited(app['version_date'], 35)}"
    if description:
        content += f"\n\n{description}"
    return {"title": title, "description": content, "url": url, "color": 3931904}


def webhook_url(kind):
    url = os.getenv(WEBHOOKS[kind], "").strip()
    parsed = urllib.parse.urlparse(url)
    if parsed.scheme != "https" or parsed.hostname not in {"discord.com", "discordapp.com"} or not parsed.path.startswith("/api/webhooks/"):
        raise ValueError(f"Missing/invalid GitHub Actions secret: {WEBHOOKS[kind]}")
    return url + ("&" if "?" in url else "?") + "wait=true"


def send(url, payload):
    body = json.dumps({"username": "PSVitaAlive Updates", "allowed_mentions": {"parse": []}, **payload}, ensure_ascii=False).encode("utf-8")
    for attempt in range(5):
        request = urllib.request.Request(url, data=body, headers={"Content-Type": "application/json", "User-Agent": "PSVitaAlive-Discord/1.0"}, method="POST")
        try:
            with urllib.request.urlopen(request, timeout=25) as response:
                if response.status not in (200, 204):
                    raise RuntimeError(f"Discord returned HTTP {response.status}")
                return
        except urllib.error.HTTPError as exc:
            if exc.code == 429 and attempt < 4:
                try:
                    wait = float(json.loads(exc.read().decode("utf-8")).get("retry_after", 2))
                except (ValueError, TypeError):
                    wait = 2
                time.sleep(min(max(wait, 1), 20))
                continue
            if 500 <= exc.code < 600 and attempt < 4:
                time.sleep(2 ** attempt)
                continue
            raise RuntimeError(f"Discord webhook HTTP {exc.code}") from None
        except (urllib.error.URLError, TimeoutError) as exc:
            if attempt < 4:
                time.sleep(2 ** attempt)
                continue
            raise RuntimeError(f"Discord network error: {type(exc).__name__}") from None
    raise RuntimeError("Discord webhook exhausted retries")


def notify_catalog(catalog_path, state_path):
    apps = read_json(catalog_path)
    current = snapshot(apps)
    state = state_from_file(state_path)
    if not state["catalog_initialized"]:
        state["apps"] = current
        state["catalog_initialized"] = True
        save_json(state_path, state)
        print(f"Initialized Discord catalog baseline: {len(current)} Homebrew (no announcements).")
        return
    added, updated = changes(state["apps"], current, apps)
    if not added and not updated:
        print("No new Homebrew or version changes; nothing to announce.")
        return
    urls = {kind: webhook_url(kind) for kind, items in (("new", added), ("updated", updated)) if items}
    for kind, items in (("new", added), ("updated", updated)):
        for start in range(0, len(items), 10):
            batch = items[start:start + 10]
            embeds = ([app_embed(app) for app in batch] if kind == "new"
                      else [app_embed(app, prior) for app, prior in batch])
            send(urls[kind], {"embeds": embeds})
            print(f"Sent {kind} notification batch ({len(batch)} apps).")
    state["apps"] = current
    save_json(state_path, state)
    print(f"Catalog notifications completed: {len(added)} new, {len(updated)} updated.")


def notify_release(event_path, state_path):
    event = read_json(event_path)
    release = event.get("release", {}) if isinstance(event, dict) else {}
    if not release or release.get("draft") or release.get("prerelease"):
        print("Skipping draft/prerelease or missing release event.")
        return
    tag = release.get("tag_name")
    url = release.get("html_url")
    if not isinstance(tag, str) or not tag or not isinstance(url, str) or not url.startswith("https://github.com/VegettoSan/PSVitaAlive/releases/"):
        raise ValueError("Invalid release event")
    state = state_from_file(state_path)
    if tag in state["releases"]:
        print("Release already announced.")
        return
    endpoint = webhook_url("release")
    description = limited(release.get("body") or "A new version of PSVitaAlive is available.", 700)
    send(endpoint, {"embeds": [{"title": "PSVitaAlive Release: " + limited(tag, 180), "url": url, "description": description, "color": 3931904}]})
    state["releases"].append(tag)
    save_json(state_path, state)
    print(f"Announced official release: {tag}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=("catalog", "release"), required=True)
    parser.add_argument("--state", required=True)
    parser.add_argument("--catalog", default="catalog.json")
    parser.add_argument("--event", default=os.getenv("GITHUB_EVENT_PATH", ""))
    args = parser.parse_args()
    if args.mode == "catalog":
        notify_catalog(args.catalog, args.state)
    else:
        notify_release(args.event, args.state)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, RuntimeError, OSError) as exc:
        print(f"Discord notification failed: {exc}", file=sys.stderr)
        sys.exit(1)
