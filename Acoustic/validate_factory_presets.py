"""Check Acoustic factory preset chunks and their IR references before signing."""

import argparse
import math
from pathlib import Path
import struct


SOURCE = Path(__file__).resolve().parents[1] / "Models" / "Instruments"
FXP_HEADER = struct.Struct(">4sI4sI4sII28sI")
MAX_CHUNK_SIZE = 1024 * 1024


def files_with_suffix(directory, suffix):
    return {
        path.relative_to(directory).as_posix(): path
        for path in directory.rglob("*")
        if path.is_file() and path.suffix.lower() == suffix
    }


def check_preset(path, captures):
    data = path.read_bytes()
    magic, size, kind, fxp_version, plugin, _, count, _, chunk_size = FXP_HEADER.unpack_from(data)
    if (magic, kind, fxp_version, plugin) != (b"CcnK", b"FPCh", 1, b"PkAc"):
        raise ValueError("expected a Puke Amp: Acoustic chunk preset")
    if size != len(data) - 8 or chunk_size != len(data) - FXP_HEADER.size or not 0 < chunk_size <= MAX_CHUNK_SIZE:
        raise ValueError("inconsistent FXP size")

    chunk = data[FXP_HEADER.size:]
    offset = 8 if chunk.startswith(b"tffp") else 0  # Optional iPlug version header.
    version, = struct.unpack_from("<i", chunk, offset)
    if version not in (1, 2) or count != {1: 6, 2: 7}[version]:
        raise ValueError("unsupported Acoustic state version or parameter count")
    offset += 4
    for _ in range(2):
        length, = struct.unpack_from("<i", chunk, offset)
        offset += 4
        if not 0 <= length <= len(chunk) - offset:
            raise ValueError("invalid IR reference length")
        reference = chunk[offset:offset + length].decode("utf-8")
        offset += length
        if reference and reference not in captures:
            raise ValueError(f"IR reference does not match a bundled WAV: {reference!r}")
    if not all(map(math.isfinite, struct.unpack(f"<{count}d", chunk[offset:]))):
        raise ValueError("non-finite mix parameter")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", nargs="*", type=Path, help="optional staged Acoustic bundles")
    args = parser.parse_args()
    source_presets = files_with_suffix(SOURCE / "Presets", ".fxp")
    if not source_presets:
        parser.error(f"no factory presets under {SOURCE / 'Presets'}")

    source_captures = files_with_suffix(SOURCE, ".wav")
    roots = [bundle / "Contents" / "Resources" / "Models" / "Instruments" for bundle in args.bundle]
    for root in roots or [SOURCE]:
        presets = files_with_suffix(root / "Presets", ".fxp")
        if presets.keys() != source_presets.keys():
            parser.error(f"{root}: preset inventory differs from source")
        captures = files_with_suffix(root, ".wav").keys() & source_captures.keys()
        for name, path in sorted(presets.items()):
            try:
                if root != SOURCE and path.read_bytes() != source_presets[name].read_bytes():
                    raise ValueError("preset content differs from source")
                check_preset(path, captures)
            except (OSError, ValueError, struct.error) as error:
                parser.error(f"{path}: {error}")
        print(f"Validated {len(presets)} factory preset(s) in {root}")


if __name__ == "__main__":
    main()
