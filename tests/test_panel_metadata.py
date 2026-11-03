import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class PanelMetadataTest(unittest.TestCase):
    def test_applet_is_discoverable_in_system_tray(self):
        panel = json.loads((ROOT / "panel/metadata.json").read_text(encoding="utf-8"))
        plugin = json.loads((ROOT / "plugin/metadata.json").read_text(encoding="utf-8"))

        self.assertEqual(panel["KPackageStructure"], "Plasma/Applet")
        self.assertEqual(panel["X-Plasma-API-Minimum-Version"], "6.0")
        self.assertEqual(panel["X-Plasma-NotificationArea"], "true")
        self.assertEqual(panel["X-Plasma-NotificationAreaCategory"], "SystemServices")
        self.assertEqual(panel["KPlugin"]["License"], plugin["KPlugin"]["License"])
        self.assertEqual(panel["KPlugin"]["License"], "GPLv2+")


if __name__ == "__main__":
    unittest.main()
