#!/usr/bin/env python3
"""Explicit manual test of the three Discord webhooks.

Run only from the test-webhooks job of discord-notifications.yml.
Does not load or modify the catalog or notification state.
"""
from .discord_notifications import send, webhook_url

CHANNELS = (
    ("release", "#announcements"),
    ("new", "#new-homebrew"),
    ("updated", "#homebrew-updates"),
)


def run_test():
    # Resolve/validate all three secrets *before* sending anything.
    endpoints = [(channel, webhook_url(kind)) for kind, channel in CHANNELS]
    for channel, endpoint in endpoints:
        send(endpoint, {
            "embeds": [{
                "title": "🧪 TEST — PSVitaAlive Discord Notifications",
                "description": (
                    f"Manual webhook connectivity test for **{channel}**.\n\n"
                    "This is **not** an application update or a new release. "
                    "No catalog content, versions, or notification history were changed."
                ),
                "color": 3931904,
                "footer": {"text": "PSVitaAlive • Test message • No action required"},
            }]
        })
        print(f"Discord webhook test delivered to {channel}.")


if __name__ == "__main__":
    run_test()
