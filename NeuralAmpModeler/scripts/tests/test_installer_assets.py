import importlib.util
import os
from pathlib import Path
import plistlib
import tempfile
import unittest
from unittest.mock import patch
import zipfile

spec = importlib.util.spec_from_file_location('installer_assets', Path(__file__).resolve().parents[1] / 'installer_assets.py')
assets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(assets)


class InstallerAssetsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.repo = self.root / 'repo'
        self.repo.mkdir()
        self.bundle = self.root / 'staged' / assets.BUNDLE
        self.bundle.mkdir(parents=True)
        self.override = patch.object(assets, 'REPO', self.repo)
        self.override.start()
        self.addCleanup(self.override.stop)
        for category, ext in [('Amp', 'nam'), ('FX', 'nam'), ('IR', 'wav'), ('Presets', 'fxp')]:
            p = self.repo / 'Models' / category / ('factory.' + ext)
            p.parent.mkdir(parents=True)
            p.write_bytes(b'portable Models/Amp/factory.nam')
        for name in assets.NOTICES:
            (self.repo / name).write_text(name)
        installer = self.repo / 'NeuralAmpModeler/installer'
        installer.mkdir(parents=True)
        (installer / 'ThirdPartyNotices.txt').write_text('third party notices')
        (installer.parent / 'config.h').write_text('#define PLUG_VERSION_STR "0.2.15"\n#define PLUG_VERSION_HEX 0x0000020F\n')
        assets.stage(self.bundle)

    def test_version_rejects_inconsistent_packed_version(self):
        self.assertEqual(assets.version(), '0.2.15')
        config = self.repo / 'NeuralAmpModeler/config.h'
        config.write_text(config.read_text().replace('020F', '020E'))
        with self.assertRaisesRegex(ValueError, 'disagree'):
            assets.version()

    def test_all_resources_are_required_and_hashed(self):
        for name in ['Models/Amp/factory.nam', 'Models/FX/factory.nam', 'Models/IR/factory.wav',
                     'Models/Presets/factory.fxp', *assets.NOTICES, 'ThirdPartyNotices.txt']:
            path = self.bundle / 'Contents/Resources' / name
            original = path.read_bytes()
            path.write_bytes(b'altered')
            with self.assertRaisesRegex(ValueError, 'Missing or altered'):
                assets.validate_resources(self.bundle)
            path.write_bytes(original)
        assets.validate_resources(self.bundle)

    def test_rejects_absolute_preset_paths(self):
        for absolute in [b'/Users/developer/Models/Amp/file.nam', b'C:\\dev\\Models\\Amp\\file.nam',
                         '/home/developer/Models/file.nam'.encode('utf-16-le')]:
            (self.repo / 'Models/Presets/factory.fxp').write_bytes(absolute)
            with self.assertRaisesRegex(ValueError, 'Absolute asset path'):
                assets.stage(self.bundle)

    def test_rejects_external_symlinks_and_credentials(self):
        link = self.bundle / 'outside'
        link.symlink_to(self.repo)
        with self.assertRaisesRegex(ValueError, 'external bundle symlink'):
            assets.validate_resources(self.bundle)
        link.unlink()
        (self.bundle / 'signing.pfx').write_bytes(b'private')
        with self.assertRaisesRegex(ValueError, 'credential'):
            assets.validate_resources(self.bundle)

    def test_archive_round_trip_includes_hidden_files_and_symlinks(self):
        binary = self.bundle / 'Contents/x86_64-win/Puke Amp.vst3'
        binary.parent.mkdir()
        binary.write_bytes(b'executable')
        binary.chmod(0o755)
        (binary.parent / '.hidden').write_text('hidden')
        (binary.parent / 'link').symlink_to(binary.name)
        archive = self.root / 'windows.zip'
        assets.pack_windows(self.bundle, archive)
        extracted = self.root / 'extracted'
        extracted.mkdir()
        assets.unpack_windows(archive, extracted)
        # Windows does not track POSIX modes. Simulate its inventory semantics
        # while testing its archive format and symlink extraction on Linux.
        with patch.object(assets.os, 'name', 'nt'):
            assets.check_archive(archive, self.bundle, extracted)
        with zipfile.ZipFile(archive) as z:
            self.assertTrue(all(n.startswith(assets.BUNDLE + '/') for n in z.namelist()))
        (extracted / assets.BUNDLE / 'Contents/x86_64-win/.hidden').write_text('tampered')
        with self.assertRaisesRegex(ValueError, 'round trip changed'):
            assets.check_archive(archive, self.bundle, extracted)

    def test_rejects_extra_zip_roots_and_traversal(self):
        for name in ['__MACOSX/info', 'installer.exe', assets.BUNDLE + '/../escape']:
            archive = self.root / 'bad.zip'
            with zipfile.ZipFile(archive, 'w') as z:
                z.writestr(name, b'unwanted')
            with self.assertRaises(ValueError):
                assets.check_archive(archive, self.bundle, self.root)
            with self.assertRaises(ValueError):
                assets.unpack_windows(archive, self.root / 'unpack')

    def test_mac_version_and_actual_architectures(self):
        plist = self.bundle / 'Contents/Info.plist'
        plist.write_bytes(plistlib.dumps(dict(CFBundleExecutable='Puke Amp', CFBundleVersion='0.2.15', CFBundleShortVersionString='0.2.15')))
        def tool(args, **kwargs):
            if args[0] == 'lipo':
                return 'arm64 x86_64\n'
            return ''
        with patch.object(assets.subprocess, 'check_output', side_effect=tool):
            self.assertEqual(assets.mac_architectures(self.bundle, 'universal'), ['arm64', 'x86_64'])
            with self.assertRaisesRegex(ValueError, 'executable contains'):
                assets.mac_architectures(self.bundle, 'arm64')
            plist.write_bytes(plistlib.dumps(dict(CFBundleVersion='0.0.1')))
            with self.assertRaisesRegex(ValueError, 'does not match'):
                assets.mac_architectures(self.bundle, 'universal')


if __name__ == '__main__':
    unittest.main()
