# PSVitaAlive — Discord forum routing for manual feedback (2026-10-28)

## Goal

User-written feedback from the PS Vita client's **Report** footer button is now posted directly into the official Discord community's public forums:

| Form type | Target Discord forum | Result |
| --- | --- | --- |
| Problem | `#bug-reports` | New individual thread/post named `🐛 <title>` |
| Suggestion | `#feature-requests` | New individual thread/post named `💡 <title>` |

Existing automated diagnostics (download, installation, ZIP extraction, plugin problems, catalog failures, etc.) **still use the existing diagnostic webhook**. Their correlated logs are not posted in the public forums by this change.

## Files

- `Client PSVitaAlive/source/network/error_reporter.cpp`: directs `sendManualFeedback()` to the problem/suggestion webhook according to `req.suggestion`, and sends `thread_name` in the Discord JSON body. The automatic `sendErrorReport()` destination is untouched.
- `Client PSVitaAlive/include/network/feedback_forum.hpp`: sanitizes control characters from forum post titles, preserves UTF-8, and prefixes the correct category emoji.
- `tests/test_feedback_forum.cpp`: host C++ tests for both types, controls/whitespace, Unicode titles, and fallback.
- `.github/workflows/client-build.yml`: runs the host test before the normal VitaSDK VPK build.

No new web service, bot, application permissions, database, or GitHub Actions notification state was introduced. The homebrew source/catalog architecture is not changed.

## Discord requirements

Both webhooks must target Discord **Forum** channels. Executing a webhook in a forum requires the JSON property `thread_name`, which creates an individual forum thread. Discord accepts `content` and/or `embeds` as the first post in that thread.

Keep **Require Tag** disabled for these two forum channels until their tag snowflake IDs have been deliberately configured in the client. Existing forum tags may remain available for manual moderation.

## UI and privacy

The existing localized UI remains unchanged:

- Type: Problem / Suggestion.
- Title: required, 64 characters maximum.
- Description: required, 700 characters maximum.
- Existing network transport, cooldown (45 seconds), success/error messages, and rate-limit behavior.
- Embeds retain client version, user-reported date, description, and `allowed_mentions.parse = []`.
- Manual feedback does **not** collect or attach diagnostic logs, Title IDs, selected app context, or device identifiers.

## Operational caveat

As with the older diagnostic webhook, the forum webhook URLs are compiled into the PS Vita client and can be extracted from public VPKs or repository sources. This risk was explicitly accepted for a direct-webhook-only implementation. If abuse occurs, revoke/rotate the affected webhook and rebuild the client; previous VPKs using the old URL will stop sending feedback.

## Validation

1. Host C++ unit test compiles with `-Wall -Wextra -Werror` and passes locally.
2. The GitHub Actions PS Vita build is triggered by the source/header/workflow changes. Check the `Build PS Vita Client` workflow for full VitaSDK compilation before distributing the new VPK.
3. Install the new VPK on a PS Vita and send a harmless test **Problem**. Verify it creates exactly one thread in `#bug-reports`.
4. After cooldown, send a harmless test **Suggestion**. Verify a separate thread in `#feature-requests`.
5. Verify the title, description, version, date, and original language. Confirm no logs in forum messages.
6. Trigger a reportable installation error via the normal report mechanism and verify the existing diagnostic destination still receives it.
7. Verify a failed network send preserves the form draft for retry.
8. Check that neither forum requires tags if posts fail with Discord HTTP 400.

No generated catalogs are modified by this feature.
