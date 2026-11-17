#!/usr/bin/env python3
"""Shared, fail-closed staging and validation for the native export commands."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import stat
import subprocess
import zipfile

REPO = Path(__file__).resolve().parents[2]
BUNDLE = 'Puke Amp.vst3'
NOTICES = ('LICENSE', 'LicenseText.rtf', 'ASSETS_LICENSE.md', 'THIRD_PARTY.md')


def inside(path, root):
    try:
        path.resolve().relative_to(root.resolve())
        return True
    except ValueError:
        return False


def version():
    config = (REPO / 'NeuralAmpModeler/config.h').read_text()
    match = re.search(r'^#define PLUG_VERSION_STR "(\d+\.\d+\.\d+)"$', config, re.M)
    if not match:
        raise ValueError('Missing numeric PLUG_VERSION_STR in config.h')
    value = match[1]
    major, minor, patch = map(int, value.split('.'))
    packed = re.search(r'^#define PLUG_VERSION_HEX (0x[0-9a-fA-F]+)$', config, re.M)
    if not packed or int(packed[1], 16) != (major << 16 | minor << 8 | patch):
        raise ValueError('PLUG_VERSION_STR and PLUG_VERSION_HEX disagree')
    return value


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def stage(bundle):
    resources = bundle / 'Contents/Resources'
    resources.mkdir(parents=True, exist_ok=True)
    models = resources / 'Models'
    if models.exists():
        shutil.rmtree(models)
    shutil.copytree(REPO / 'Models', models, symlinks=True)
    for name in NOTICES:
        shutil.copy2(REPO / name, resources / name)
    shutil.copy2(REPO / 'NeuralAmpModeler/installer/ThirdPartyNotices.txt', resources)
    validate_resources(bundle)


def validate_resources(bundle):
    resources = bundle / 'Contents/Resources'
    for category, extension in [('Amp', '.nam'), ('FX', '.nam'), ('IR', '.wav'), ('Presets', '.fxp')]:
        source = REPO / 'Models' / category
        if not any(source.rglob('*' + extension)):
            raise ValueError(f'Missing source assets: {category}')
    sources = [(p, resources / 'Models' / p.relative_to(REPO / 'Models'))
               for p in (REPO / 'Models').rglob('*') if p.is_file()]
    sources += [(REPO / name, resources / name) for name in NOTICES]
    sources += [(REPO / 'NeuralAmpModeler/installer/ThirdPartyNotices.txt', resources / 'ThirdPartyNotices.txt')]
    for source, target in sources:
        if not target.is_file() or digest(source) != digest(target):
            raise ValueError(f'Missing or altered resource: {target}')
    for p in bundle.rglob('*'):
        if p.is_symlink():
            if not p.exists() or not inside(p, bundle):
                raise ValueError(f'Broken or external bundle symlink: {p}')
        if p.suffix.lower() in {'.pdb', '.obj', '.ilk', '.pfx', '.p12', '.p8', '.pem', '.pkg', '.dmg'}:
            raise ValueError(f'Unexpected build artifact or credential: {p}')
    for preset in (resources / 'Models/Presets').rglob('*.fxp'):
        # Factory state must use portable asset references, including UTF-16 state.
        raw = preset.read_bytes().replace(b'\x00', b'')
        if re.search(rb'/(?:Users|home|workspaces)/|[A-Za-z]:[\\/]', raw):
            raise ValueError(f'Absolute asset path in factory preset: {preset}')


def mac_architectures(bundle, requested):
    plist = plistlib.loads((bundle / 'Contents/Info.plist').read_bytes())
    for field in ('CFBundleShortVersionString', 'CFBundleVersion'):
        if plist.get(field) != version():
            raise ValueError(f'Built {field} does not match config.h')
    executable = bundle / 'Contents/MacOS' / plist['CFBundleExecutable']
    actual = sorted(subprocess.check_output(['lipo', '-archs', str(executable)], text=True).split())
    expected = sorted(['arm64', 'x86_64'] if requested == 'universal' else [requested])
    if actual != expected:
        raise ValueError(f'Expected {expected}, executable contains {actual}')

    def rpaths(path):
        commands = subprocess.check_output(['otool', '-l', str(path)], text=True)
        return re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.+?) \(offset', commands)

    def expand(value, loader):
        return value.replace('@loader_path', str(loader.parent)).replace('@executable_path', str(executable.parent))

    executable_rpaths = rpaths(executable)
    # Every bundled Mach-O dependency must also support the requested slices.
    for path in bundle.rglob('*'):
        if not path.is_file() or path.is_symlink():
            continue
        with path.open('rb') as stream:
            magic = stream.read(4)
        if magic not in (b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf', b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca'):
            continue
        slices = subprocess.check_output(['lipo', '-archs', str(path)], text=True).split()
        if not set(actual).issubset(slices):
            raise ValueError(f'Missing dependency architectures: {path}')
        deps = subprocess.check_output(['otool', '-L', str(path)], text=True)
        ids = subprocess.check_output(['otool', '-D', str(path)], text=True).splitlines()[1:]
        ids = {line.strip() for line in ids}
        search_paths = [expand(r, path) for r in rpaths(path)]
        search_paths += [expand(r, executable) for r in executable_rpaths]
        for dep in re.findall(r'^\s+(.+?) \(', deps, re.M):
            if dep in ids or dep.startswith(('/usr/lib/', '/System/Library/')):
                continue
            candidates = ([Path(r) / dep[len('@rpath/'):] for r in search_paths]
                          if dep.startswith('@rpath/') else [Path(expand(dep, path))])
            if not any(p.is_file() and inside(p, bundle) for p in candidates):
                raise ValueError(f'Unbundled or nonportable dependency: {dep} in {path}')
    return actual


def inventory(bundle):
    result = {}
    for p in [bundle, *bundle.rglob('*')]:
        rel = p.relative_to(bundle.parent).as_posix()
        mode = stat.S_IMODE(p.lstat().st_mode) if os.name != 'nt' else None
        if p.is_symlink():
            result[rel] = ('link', os.readlink(p), mode)
        elif p.is_file():
            result[rel] = ('file', digest(p), mode)
        else:
            result[rel] = ('directory', mode)
    return result


def check_archive(archive, original, extracted):
    with zipfile.ZipFile(archive) as z:
        for entry in z.infolist():
            parts = entry.filename.rstrip('/').split('/')
            if parts[0] != BUNDLE or '..' in parts or '\\' in entry.filename:
                raise ValueError(f'Unexpected ZIP entry: {entry.filename}')
    if sorted(p.name for p in extracted.iterdir()) != [BUNDLE]:
        raise ValueError('ZIP must extract to exactly one Puke Amp.vst3 bundle')
    if inventory(original) != inventory(extracted / BUNDLE):
        raise ValueError('Archive round trip changed bundle contents, permissions, or symlinks')
    validate_resources(extracted / BUNDLE)


def pack_windows(bundle, archive):
    # zipfile includes hidden files, unlike Compress-Archive. Explicit Unix
    # attributes preserve internal symlinks and permissions for installer tools.
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
        for path in [bundle, *sorted(bundle.rglob('*'))]:
            name = path.relative_to(bundle.parent).as_posix()
            if path.is_symlink():
                info = zipfile.ZipInfo(name)
                info.create_system = 3
                info.external_attr = (stat.S_IFLNK | 0o777) << 16
                z.writestr(info, os.readlink(path).encode())
            else:
                z.write(path, name)


def unpack_windows(archive, destination):
    links = []
    with zipfile.ZipFile(archive) as z:
        for info in z.infolist():
            parts = info.filename.rstrip('/').split('/')
            if parts[0] != BUNDLE or '..' in parts or '\\' in info.filename:
                raise ValueError(f'Unsafe archive entry: {info.filename}')
            target = destination.joinpath(*parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            if stat.S_ISLNK(info.external_attr >> 16):
                link = z.read(info).decode()
                if not inside(target.parent / link, destination / BUNDLE):
                    raise ValueError(f'External symlink in archive: {info.filename}')
                links.append((target, link))
            elif info.is_dir():
                target.mkdir(exist_ok=True)
            else:
                target.write_bytes(z.read(info))
                os.chmod(target, stat.S_IMODE(info.external_attr >> 16))
    # Targets may sort after their links. Materialize links after all files and
    # directories, so Windows can distinguish file and directory symlinks.
    for target, link in links:
        target.symlink_to(link, target_is_directory=(target.parent / link).is_dir())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['version', 'stage', 'validate', 'mac-check', 'archive-check', 'manifest', 'windows-pack', 'windows-unpack', 'copy'])
    parser.add_argument('--bundle', type=Path)
    parser.add_argument('--arch', choices=['universal', 'arm64', 'x86_64'])
    parser.add_argument('--archive', type=Path)
    parser.add_argument('--extracted', type=Path)
    parser.add_argument('--platform', choices=['macos', 'windows-x64'])
    parser.add_argument('--source', type=Path)
    args = parser.parse_args()
    if args.command == 'version':
        print(version())
    elif args.command == 'stage':
        stage(args.bundle)
    elif args.command == 'validate':
        validate_resources(args.bundle)
    elif args.command == 'mac-check':
        validate_resources(args.bundle)
        mac_architectures(args.bundle, args.arch)
    elif args.command == 'archive-check':
        check_archive(args.archive, args.bundle, args.extracted)
    elif args.command == 'copy':
        shutil.copytree(args.source, args.bundle, symlinks=True)
    elif args.command == 'windows-pack':
        pack_windows(args.bundle, args.archive)
    elif args.command == 'windows-unpack':
        unpack_windows(args.archive, args.extracted)
    elif args.command == 'manifest':
        # Native callers invoke this only after signature/ticket round-trip validation.
        architectures = mac_architectures(args.bundle, args.arch) if args.platform == 'macos' else ['x86_64']
        result = dict(schema_version=1, product='Puke Amp', version=version(), platform=args.platform,
                      architectures=architectures, archive=args.archive.name, sha256=digest(args.archive),
                      bundle_name=BUNDLE, signed=True, notarized=args.platform == 'macos')
        (args.archive.parent / 'manifest.json').write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError, zipfile.BadZipFile) as error:
        raise SystemExit(f'error: {error}')
