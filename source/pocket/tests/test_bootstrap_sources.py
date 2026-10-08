import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "tools/bootstrap_sources.py"
spec = importlib.util.spec_from_file_location("bootstrap_sources", SCRIPT)
bootstrap = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bootstrap)


class BootstrapTests(unittest.TestCase):
    def fixture(self, root):
        repo = root / "fixture"
        repo.mkdir()
        run = lambda *a: subprocess.run(a, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        run("git", "init", str(repo))
        (repo / "base.txt").write_text("baseline\n")
        run("git", "-C", str(repo), "add", "base.txt")
        run("git", "-C", str(repo), "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid", "commit", "-m", "fixture")
        commit = subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip()
        package = root / "relocated package"
        (package / "dependencies").mkdir(parents=True)
        (package / "source").mkdir()
        (package / "source/base.txt").write_text("edited source\n")
        (package / "source/new.txt").write_text("new platform\n")
        run("git", "-C", str(repo), "bundle", "create", str(package / "dependencies/base.bundle"), "HEAD")
        (package / "dependencies/musl-1.2.5.tar.gz").write_bytes(b"fixture only")
        manifest = {"schema": 1, "files": {}, "repositories": {}}
        for name in bootstrap.DEPENDENCIES:
            manifest["repositories"][name] = {"bundle": "dependencies/base.bundle", "commit": commit}
        for p in package.rglob("*"):
            if p.is_file():
                manifest["files"][p.relative_to(package).as_posix()] = hashlib.sha256(p.read_bytes()).hexdigest()
        (package / "handoff-manifest.json").write_text(json.dumps(manifest))
        return package, manifest

    def test_relocated_offline_materialization_preserves_source_and_dependency_pins(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            package, manifest = self.fixture(root)
            dest = bootstrap.materialize(package, root / "user workspace")
            self.assertEqual((dest / "base.txt").read_text(), "edited source\n")
            self.assertTrue((dest / "new.txt").is_file())
            self.assertFalse((dest / ".git").exists())
            self.assertTrue((root / "user workspace/logs/bootstrap.log").is_file())
            for name in bootstrap.DEPENDENCIES:
                repo = root / "user workspace/deps" / name
                self.assertTrue((repo / ".git").is_dir())
                head = subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip()
                self.assertEqual(head, manifest["repositories"][name]["commit"])
            with self.assertRaisesRegex(ValueError, "must not exist"):
                bootstrap.materialize(package, root / "user workspace")

    def test_corruption_fails_before_destination_creation(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            package, _ = self.fixture(root)
            (package / "source/base.txt").write_text("corrupted")
            with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                bootstrap.materialize(package, root / "output")
            self.assertFalse((root / "output").exists())

    def test_shallow_history_can_be_cloned_again_after_restore(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            package, manifest = self.fixture(root)
            run = lambda *a: subprocess.run(a, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            repo = root / "fixture"
            (repo / "base.txt").write_text("second revision\n")
            run("git", "-C", str(repo), "add", "base.txt")
            run("git", "-C", str(repo), "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid", "commit", "-m", "second")
            shallow = root / "shallow"
            run("git", "clone", "--depth=1", repo.as_uri(), str(shallow))
            run("git", "-C", str(shallow), "bundle", "create", str(package / "dependencies/base.bundle"), "HEAD")
            boundary = (shallow / ".git/shallow").read_bytes()
            (package / "dependencies/base.shallow").write_bytes(boundary)
            for entry in manifest["repositories"].values():
                entry["commit"] = boundary.decode().strip()
                entry["shallow"] = "dependencies/base.shallow"
            for name in ("dependencies/base.bundle", "dependencies/base.shallow"):
                manifest["files"][name] = hashlib.sha256((package / name).read_bytes()).hexdigest()
            (package / "handoff-manifest.json").write_text(json.dumps(manifest))
            bootstrap.materialize(package, root / "restored")
            run("git", "clone", "--no-local", str(root / "restored/deps/openfpgaCore"), str(root / "clone-again"))
            run("git", "-C", str(root / "clone-again"), "fsck", "--connectivity-only", "--no-dangling")

    def test_manifest_escape_and_missing_bundle_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            package, manifest = self.fixture(root)
            manifest["files"]["../outside"] = "0" * 64
            (package / "handoff-manifest.json").write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, "Unsafe archive path"):
                bootstrap.verify(package)
            del manifest["files"]["../outside"]
            manifest["repositories"]["openfpgaSDK"]["bundle"] = "dependencies/missing.bundle"
            (package / "handoff-manifest.json").write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, "Unverified bundle"):
                bootstrap.verify(package)


if __name__ == "__main__":
    unittest.main()
