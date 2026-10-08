# SPDX-License-Identifier: GPL-3.0-or-later
import contextlib, importlib.util, io, json, os, subprocess, tempfile, unittest
from pathlib import Path
TESTS = Path(os.environ.get('POCKET_BOOTSTRAP_TESTS', str(Path(__file__).resolve().parent)))
spec = importlib.util.spec_from_file_location('existing', TESTS / 'test_bootstrap_sources.py')
existing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(existing)
bootstrap = existing.bootstrap
NAMES = ('openfpgaSDK', 'openfpgaCore', 'SpinalHDL')

class ExternalTests(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='pocket-v2-external-fixture-')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.package, self.manifest = existing.BootstrapTests().fixture(self.root)
        self.repo = self.root / 'fixture'
        self.dest = self.root / 'workspace'
        self.external = {name: self.repo for name in NAMES}
        for name in NAMES:
            self.manifest['repositories'][name] = {'external': True, 'commit': self.manifest['repositories'][name]['commit'], 'repository': 'https://example.invalid/never-fetched.git'}
        self.save()

    def save(self):
        (self.package / 'handoff-manifest.json').write_text(json.dumps(self.manifest))

    def verify_result(self):
        for name in NAMES:
            target = self.dest / 'deps' / name
            self.assertEqual(subprocess.check_output(['git', '-C', str(target), 'rev-parse', 'HEAD'], text=True).strip(), self.manifest['repositories'][name]['commit'])
            self.assertEqual(subprocess.check_output(['git', '-C', str(target), 'status', '--porcelain'], text=True), '')
        self.assertEqual((self.dest / 'mister-pal-pocket/base.txt').read_text(), 'edited source\n')

    def test_external_materialization_success(self):
        bootstrap.materialize(self.package, self.dest, self.external)
        self.verify_result()

    def test_cli_sdk_core_spinal_success(self):
        cmd = ['python3', str(existing.SCRIPT), '--package', str(self.package), '--workspace', str(self.dest), '--sdk', str(self.repo), '--core', str(self.repo), '--spinal', str(self.repo)]
        run = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.verify_result()

    def test_verify_only_without_external_paths(self):
        self.assertEqual(bootstrap.verify(self.package)['schema'], 1)
        self.assertFalse(self.dest.exists())

def case(name, kind):

    def test(self):
        if kind == 'missing_option':
            self.external.pop(name)
        elif kind == 'missing_path':
            self.external[name] = self.root / 'does-not-exist'
        elif kind == 'wrong_pin':
            self.manifest['repositories'][name]['commit'] = '0' * 40
            self.save()
        elif kind == 'tracked_dirty':
            (self.repo / 'base.txt').write_text('user edit')
        elif kind == 'untracked_dirty':
            (self.repo / 'user-new.txt').write_text('user new file')
        elif kind == 'not_git':
            path = self.root / 'ordinary-directory'
            path.mkdir()
            self.external[name] = path
        if kind in ('tracked_dirty', 'untracked_dirty'):
            self.external = {key: self.repo for key in NAMES}
            for other in NAMES:
                if other != name:
                    self.manifest['repositories'][other] = {'bundle': 'dependencies/base.bundle', 'commit': self.manifest['repositories'][other]['commit']}
            self.save()
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises((ValueError, subprocess.CalledProcessError)):
                bootstrap.materialize(self.package, self.dest, self.external)
        self.assertFalse(self.dest.exists(), 'Rejected dependency must not create workspace')
    return test
for name in NAMES:
    for kind in ('missing_option', 'missing_path', 'wrong_pin', 'tracked_dirty', 'untracked_dirty', 'not_git'):
        setattr(ExternalTests, 'test_' + name + '_' + kind + '_rejected_before_output', case(name, kind))
if __name__ == '__main__':
    unittest.main(verbosity=2)
