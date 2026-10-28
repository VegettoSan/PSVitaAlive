import unittest
from unittest.mock import patch

from scripts import discord_webhook_test as test_module


class DiscordWebhookSmokeTests(unittest.TestCase):
    def test_three_channels_without_catalog_state_access(self):
        with patch.object(test_module, "webhook_url", side_effect=lambda kind: "https://example.test/" + kind) as lookup, patch.object(test_module, "send") as send:
            test_module.run_test()
        self.assertEqual([call.args[0] for call in lookup.call_args_list], ["release", "new", "updated"])
        self.assertEqual([call.args[0] for call in send.call_args_list], [
            "https://example.test/release",
            "https://example.test/new",
            "https://example.test/updated",
        ])
        for call in send.call_args_list:
            payload = call.args[1]
            self.assertEqual(len(payload["embeds"]), 1)
            self.assertIn("TEST", payload["embeds"][0]["title"])
            self.assertIn("No catalog content", payload["embeds"][0]["description"])


if __name__ == "__main__":
    unittest.main()
