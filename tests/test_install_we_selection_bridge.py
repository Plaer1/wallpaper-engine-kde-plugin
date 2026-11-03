"""Source-only reversible install/remove checks using temporary UI trees."""
import importlib.util
from pathlib import Path
import tempfile
import unittest


REPOSITORY = Path(__file__).resolve().parents[1]
INSTALLER_PATH = REPOSITORY / "tools" / "install-we-selection-bridge.py"
SPEC = importlib.util.spec_from_file_location("selection_bridge_installer", INSTALLER_PATH)
INSTALLER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(INSTALLER)


class InstallerLifecycleTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.dist = Path(self.temporary.name) / "ui" / "dist"
        self.scripts = self.dist / "scripts"
        self.scripts.mkdir(parents=True)
        self.index = self.dist / "index.html"
        self.index_bytes = b"<html><script src=\"scripts/scripts.js\"></script></html>\n"
        self.index.write_bytes(self.index_bytes)
        self.index.chmod(0o640)
        self.target = self.scripts / "we-kde-selection-bridge.js"
        self.state = self.dist / INSTALLER.STATE_NAME
        self.backup = self.scripts / INSTALLER.BACKUP_NAME

    def tearDown(self):
        self.temporary.cleanup()

    def test_fresh_install_remove_restores_index_and_removes_new_bridge(self):
        source = (REPOSITORY / "tools" / "we-kde-selection-bridge.js").read_bytes()
        INSTALLER.install(self.dist, source)
        self.assertIn(INSTALLER.TAG.encode(), self.index.read_bytes())
        self.assertEqual(self.target.read_bytes(), source)
        self.assertIn(b"version: 2", source)
        self.assertEqual(self.target.stat().st_mode & 0o777, 0o644)
        self.assertTrue(self.state.is_file())
        self.assertFalse(self.backup.exists())
        self.assertTrue(list(self.dist.glob("index.html.wekde-*.bak")))

        INSTALLER.remove(self.dist)
        self.assertEqual(self.index.read_bytes(), self.index_bytes)
        self.assertFalse(self.target.exists())
        self.assertFalse(self.state.exists())
        self.assertFalse(self.backup.exists())

    def test_upgrade_and_repeated_update_restore_original_v1_bridge(self):
        original = b"/* bridge v1 */\n"
        self.target.write_bytes(original)
        self.target.chmod(0o600)
        tagged = self.index_bytes.replace(
            INSTALLER.ANCHOR.encode(),
            (INSTALLER.ANCHOR + INSTALLER.TAG).encode())
        original_index = tagged + b"<!-- unrelated existing page content -->\n"
        self.index.write_bytes(original_index)

        INSTALLER.install(self.dist, b"/* bridge v2 */\n")
        self.assertEqual(self.target.read_bytes(), b"/* bridge v2 */\n")
        INSTALLER.install(self.dist, b"/* bridge v2 update */\n")
        self.assertEqual(self.backup.read_bytes(), original)
        INSTALLER.remove(self.dist)

        self.assertEqual(self.target.read_bytes(), original)
        self.assertEqual(self.target.stat().st_mode & 0o777, 0o600)
        self.assertEqual(self.index.read_bytes(), original_index)
        self.assertIn(INSTALLER.TAG.encode(), self.index.read_bytes())
        self.assertIn(INSTALLER.ANCHOR.encode(), self.index.read_bytes())
        self.assertFalse(self.state.exists())

    def test_removed_original_tag_is_reinstated_without_losing_later_html(self):
        original = b"/* bridge v1 */\n"
        self.target.write_bytes(original)
        tagged = self.index_bytes.replace(
            INSTALLER.ANCHOR.encode(),
            (INSTALLER.ANCHOR + INSTALLER.TAG).encode())
        self.index.write_bytes(tagged)
        INSTALLER.install(self.dist, b"/* bridge v2 */\n")

        later_edit = tagged.replace(INSTALLER.TAG.encode(), b"") + b"<!-- added later -->\n"
        self.index.write_bytes(later_edit)
        INSTALLER.remove(self.dist)
        self.assertEqual(self.target.read_bytes(), original)
        self.assertIn(INSTALLER.TAG.encode(), self.index.read_bytes())
        self.assertIn(b"<!-- added later -->", self.index.read_bytes())

    def test_preexisting_untagged_target_is_restored(self):
        original = b"/* unrelated existing file */\n"
        self.target.write_bytes(original)
        self.target.chmod(0o604)
        INSTALLER.install(self.dist, b"/* bridge v2 */\n")
        self.assertIn(INSTALLER.TAG.encode(), self.index.read_bytes())
        INSTALLER.remove(self.dist)
        self.assertEqual(self.target.read_bytes(), original)
        self.assertEqual(self.target.stat().st_mode & 0o777, 0o604)
        self.assertEqual(self.index.read_bytes(), self.index_bytes)

    def test_unsupported_layout_changes_nothing(self):
        self.index.write_bytes(b"<html>unknown</html>\n")
        before = self.index.read_bytes()
        with self.assertRaises(SystemExit):
            INSTALLER.install(self.dist, b"/* bridge v2 */\n")
        self.assertEqual(self.index.read_bytes(), before)
        self.assertFalse(self.target.exists())
        self.assertFalse(self.state.exists())

    def test_missing_original_backup_blocks_removal_without_mutation(self):
        original = b"/* bridge v1 */\n"
        self.target.write_bytes(original)
        INSTALLER.install(self.dist, b"/* bridge v2 */\n")
        self.backup.unlink()
        before_index = self.index.read_bytes()
        before_target = self.target.read_bytes()
        with self.assertRaises(SystemExit):
            INSTALLER.remove(self.dist)
        self.assertEqual(self.index.read_bytes(), before_index)
        self.assertEqual(self.target.read_bytes(), before_target)
        self.assertTrue(self.state.exists())


if __name__ == "__main__":
    unittest.main()
