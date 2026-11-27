"""Build a ReiPan-compatible data folder from the local SLPS_255.44 CHS ISO."""
from collections import defaultdict
from pathlib import Path
import argparse
import hashlib
import os
import re
import struct
import tempfile
import zlib


SECTOR = 2048
MISSING = 0xFFFFFFFF
ARCHIVE_PREFIX = 2711576576
ARCHIVE_SIZE = 2731259904
TABLE_SIZE = 276590
TABLE_CRC = 0xD9294C49
ELF_SIZE = 2576344
ELF_SHA256 = "207f6dce9689888f09e5b735732f124285870708dd880f460e6c54f5f3535b36"
EU_COUNT = 7724
JP_COUNT = 7324
CATALOG = Path(__file__).resolve().parents[1] / "src/system/eeiop/cd_file_dat.h"


def read_jp_table(data):
    if zlib.crc32(data) != TABLE_CRC:
        raise ValueError("Unexpected SLPS_255.44 file table")
    header = struct.unpack_from("<12I", data)
    assert header[:4] == (0x46484400, 100, len(data), JP_COUNT)
    _, _, _, count, sizes, labels, stored, names, sectors, _, _, _ = header
    def words(offset):
        return struct.unpack_from(f"<{count}I", data, offset)
    def string(offset):
        return data[offset:data.index(0, offset)].decode("ascii")
    return [
        (string(folder), string(name), size, label, cmp, sector)
        for (folder, name), size, label, cmp, sector in zip(
            struct.iter_unpack("<2I", data[names:names + count * 8]),
            words(sizes), data[labels:labels + count], words(stored), words(sectors)
        )
    ]


def read_iso(stream, offset, size):
    stream.seek(offset)
    data = stream.read(size)
    if len(data) != size:
        raise ValueError(f"Truncated ISO at offset {offset}")
    return data


def iso_root(stream):
    pvd = read_iso(stream, 16 * SECTOR, SECTOR)
    if pvd[:7] != b"\x01CD001\x01":
        raise ValueError("Expected a 2048-byte-sector ISO")
    root, length = struct.unpack_from("<I4xI", pvd, 158)
    if not 0 < length <= 1024 * 1024:
        raise ValueError("Invalid ISO root directory size")
    data = read_iso(stream, root * SECTOR, length)
    entries = {}
    offset = 0
    while offset < len(data):
        size = data[offset]
        if size == 0:
            offset = (offset // SECTOR + 1) * SECTOR
            continue
        record = data[offset:offset + size]
        if len(record) < 34 or 33 + record[32] > len(record):
            raise ValueError("Invalid ISO root directory entry")
        name = record[33:33 + record[32]].split(b";", 1)[0]
        if not (record[25] & 2):
            entries[name.upper()] = struct.unpack_from("<I4xI", record, 2)
        offset += size
    return entries


def iso_member_offset(root, name, expected_size):
    lba, size = root.get(name, (0, 0))
    if lba == 0 or size != expected_size:
        raise ValueError(f"Unexpected ISO entry for {name.decode('ascii')}")
    return lba * SECTOR


def restore_archive(disc, root, dest, entries):
    base = iso_member_offset(root, b"IMG_BD.BIN", ARCHIVE_PREFIX)
    if base + ARCHIVE_SIZE > os.fstat(disc.fileno()).st_size:
        raise ValueError("ISO is missing the archive's appended resources")

    recovered = 0
    magic = {".tm2": b"TIM2", ".phf": b"phf\0", ".pk4": b"PK4\0"}
    for _, name, raw_size, _, encoded, sector in entries:
        offset = sector * SECTOR
        if raw_size == MISSING or offset < ARCHIVE_PREFIX:
            continue
        expected = magic.get(Path(name).suffix.lower())
        if (raw_size != encoded >> 1 or encoded & 1 or offset + raw_size > ARCHIVE_SIZE
                or expected is None or read_iso(disc, base + offset, 4) != expected):
            raise ValueError(f"Invalid appended resource {name}")
        recovered += 1
    if recovered != 95:
        raise ValueError(f"Expected 95 appended resources, found {recovered}")

    # The ISO directory gives the prefix length. The original catalog also
    # references data after that extent on this pressing.
    prefix_hash = hashlib.sha256()
    with dest.open("xb") as target:
        for offset in range(0, ARCHIVE_PREFIX, 8 * 1024 * 1024):
            length = min(8 * 1024 * 1024, ARCHIVE_PREFIX - offset)
            chunk = read_iso(disc, base + offset, length)
            prefix_hash.update(chunk)
            target.write(chunk)
        tail = read_iso(disc, base + ARCHIVE_PREFIX, ARCHIVE_SIZE - ARCHIVE_PREFIX)
        target.write(tail)
    print(f"IMG_BD.BIN: {ARCHIVE_SIZE} bytes, {recovered} restored entries")
    print(f"Original archive SHA-256: {prefix_hash.hexdigest()}")
    print(f"Appended archive SHA-256: {hashlib.sha256(tail).hexdigest()}")


def enum_key(name):
    return re.sub(r"[^A-Z0-9_]", "_", name.upper())


def to_reipan_order(entries, header, caption_size, message_size):
    names = defaultdict(list)
    for row in entries:
        names[enum_key(row[1])].append(row)

    rows = []
    matched = 0
    unavailable = []
    for key, file_no, props in re.findall(
        r"^\s+(\w+) =\s+0x([0-9a-f]+),  // ([^\r\n]+)", header.read_text(encoding="utf-8"), re.M
    ):
        assert int(file_no, 16) == len(rows)
        folder = props.split()[0]
        matches = names[key]
        if len(matches) != 1:
            matches = [
                row for row in (matches or names[re.sub(r"_\d+$", "", key)])
                if row[0].removeprefix("../bin/data/") == folder
            ]
        assert len(matches) < 2, key
        if re.fullmatch(r"MSG(?:_[FGSI])?_OBJ", key):
            if matches:
                matched += 1
            folder, _, _, label, _, sector = names["MSG_OBJ"][0]
            rows.append((folder, "reipan_msg.obj", message_size, label, message_size << 1, sector))
            continue
        if matches:
            matched += 1
            rows.append(matches[0])
            continue

        if re.sub(r"^[FGSI]_", "", key) == "CAPTION_PHF":
            # Loose standalone B2d in the compatibility folder.
            rows.append(("bin/data/2ddata/b2d/", "reipan_caption.b2d",
                         caption_size, 4, caption_size << 1, 0))
            continue

        # Locale selections refer back to the matching Japanese asset.
        alias = {
            "E_CMN_TEX_PK4": "CMN_TEX_PK4",
            "E_SETTINGS_PHF": "SETTINGS_PHF",
            "TITLEBG_PHF": "OUTGAME_TTLBG_PHF",
            "MEMOCA_TTL_PHF": "YN_WDW_PHF",
            "MEMOCA_SAVELOAD_PHF": "YN_WDW_PHF",
        }.get(key, re.sub(r"^[FGSI]_", "", key))
        if len(names[alias]) == 1:
            rows.append(names[alias][0])
        else:
            if props != "label" and int(props.split()[-1]) != MISSING:
                unavailable.append(key)
            rows.append(("", key, MISSING, 0, 0xFFFFFFFE, MISSING))

    assert len(rows) == EU_COUNT and matched == 7320, (len(rows), matched)
    return rows, unavailable


def caption_layout():
    # The Japanese captions ship as PK4 textures. The port expects a B2d
    # layout with named animation entry points. Register an empty group and
    # supply those entry points so transitions can complete.
    strings = bytearray()
    commands = []
    for name in ["_RegistSprite_", *"ABCDEFGHIJKLMNOPQRTUVWXYZ"]:
        commands.append((1, 3, len(strings), 0, 0))
        strings.extend(name.encode("ascii") + bytes(1))
        if name == "_RegistSprite_":
            commands.append((7, 8, 1, 0, 0, 0, -1, 0, 0, -1))
        commands.append((2, 1, 0))
    strings.extend(bytes(-len(strings) % 4))
    data = bytearray(struct.pack("<4s3I", b"b2d\0", 16 + len(strings), 16, len(commands)))
    data.extend(strings)
    for cmd in commands:
        data.extend(struct.pack(f"<{len(cmd)}i", *cmd))
    data.extend(bytes(-len(data) % 16))
    return data


def message_table(archive, entries):
    _, name, size, _, stored, sector = entries[2646]
    assert name == "msg.obj" and stored == size << 1
    with archive.open("rb") as image:
        image.seek(sector * SECTOR)
        data = bytearray(image.read(size))
    assert len(data) == size
    group_count = struct.unpack_from("<I", data)[0] // 4
    assert group_count == 0x84
    jp_card_group = struct.unpack_from("<I", data, 4 * 0x82)[0]
    first_message = struct.unpack_from("<I", data, jp_card_group)[0]
    assert (first_message - jp_card_group) // 4 == 62
    struct.pack_into("<I", data, 4 * 0x83, jp_card_group)
    data.extend(bytes(-len(data) % 16))
    return data


def subtitle_table(archive, entries, rei_rows):
    original_counts = (10, 40, 9, 7, 99)
    reipan_counts = (45, 40, 12, 7, 108)
    by_path = {(folder, name): i for i, (folder, name, *_) in enumerate(rei_rows)}
    assert entries[3730][1] == "subtitle.obj"
    _, _, size, _, stored, sector = entries[3730]
    assert stored == size << 1
    with archive.open("rb") as image:
        image.seek(sector * SECTOR)
        source = image.read(size)
    assert len(source) == size

    tables = struct.unpack_from("<5I", source)
    data = bytearray(20)
    converted = 0
    for kind, (table, actual, expected) in enumerate(zip(tables, original_counts, reipan_counts)):
        offsets = struct.unpack_from(f"<{actual}I", source, table)
        assert offsets[0] == table + 4 * actual
        group_end = tables[kind + 1] if kind + 1 < len(tables) else len(source)
        result_table = len(data)
        struct.pack_into("<I", data, 4 * kind, result_table)
        data.extend(bytes(4 * expected))

        for row, (start, end) in enumerate(zip(offsets, (*offsets[1:], group_end))):
            assert start + 8 <= end and (end - start) % 4 == 0
            struct.pack_into("<I", data, result_table + 4 * row, len(data))
            entry = bytearray(source[start:end])
            native_id = struct.unpack_from("<h", entry)[0]
            if native_id >= 0:
                folder, name, audio_size, *_ = entries[native_id]
                header_folder, header_name, header_size, *_ = entries[native_id + 1]
                assert name.lower().endswith(".str")
                assert header_name.lower() == name[:-4].lower() + ".hxd"
                assert header_folder == folder
                if audio_size == MISSING:
                    assert header_size == MISSING
                    file_id = -1
                else:
                    assert 0 < header_size <= 0x800
                    file_id = by_path[folder, name]
                    assert rei_rows[file_id + 1][:2] == (header_folder, header_name)
                struct.pack_into("<h", entry, 0, file_id)
                converted += 1
            data.extend(entry)

        if expected > actual:
            blank = len(data)
            data.extend(struct.pack("<hhhh", -1, -1, -3, 0))
            for row in range(actual, expected):
                struct.pack_into("<I", data, result_table + 4 * row, blank)
    print(f"subtitle.obj: translated {converted} stream file IDs")
    data.extend(bytes(-len(data) % 16))
    return data


def write_file_table(path, rows):
    data = bytearray(48)
    offsets = []

    def add_column(values, width):
        offsets.append(len(data))
        for value in values:
            data.extend(value.to_bytes(width, "little"))
        data.extend(bytes(-len(data) % 4))

    add_column((row[2] for row in rows), 4)
    add_column((row[3] for row in rows), 1)
    add_column((row[4] for row in rows), 4)
    offsets.append(len(data))
    names_at = len(data)
    data.extend(bytes(8 * len(rows)))
    add_column((row[5] for row in rows), 4)

    strings = {}
    for i, (folder, name, size, label, stored, sector) in enumerate(rows):
        if size != MISSING:
            assert sector * SECTOR + (stored >> 1) <= ARCHIVE_SIZE
        for j, value in enumerate((folder, name)):
            if value not in strings:
                strings[value] = len(data)
                data.extend(value.encode("ascii") + bytes(1))
            struct.pack_into("<I", data, names_at + i * 8 + j * 4, strings[value])

    struct.pack_into(
        "<12I", data, 0, 0x46484400, 100, len(data), len(rows), *offsets, 1, 0x53484352, 0
    )
    with path.open("xb") as out:
        out.write(data)
    print(f"{path} ({len(data)} bytes, SHA-256 {hashlib.sha256(data).hexdigest()})")


def font_widths(elf):
    if hashlib.sha256(elf).hexdigest() != ELF_SHA256:
        raise ValueError("Unexpected SLPS_255.44 executable")
    elf_bias = 0xFF000
    banks = struct.unpack_from("<10I", elf, 0x324C58 - elf_bias)
    widths = bytes(value for bank in banks
                   for value in struct.unpack_from("<210I", elf, bank - elf_bias))
    assert len(widths) == 2100 and max(widths) <= 24
    return widths


def prepare(iso, output):
    with iso.open("rb") as disc:
        root = iso_root(disc)
        table_offset = iso_member_offset(root, b"ZERO3.FHD", TABLE_SIZE)
        entries = read_jp_table(read_iso(disc, table_offset, TABLE_SIZE))
        elf_offset = iso_member_offset(root, b"SLPS_255.44", ELF_SIZE)
        widths = font_widths(read_iso(disc, elf_offset, ELF_SIZE))
        archive = output / "IMG_BD.BIN"
        restore_archive(disc, root, archive, entries)
    caption = caption_layout()
    messages = message_table(archive, entries)
    rows, unavailable = to_reipan_order(entries, CATALOG, len(caption), len(messages))
    subtitles = subtitle_table(archive, entries, rows)
    folder, name, _, label, _, lba = rows[0x0F11]
    assert name == "subtitle.obj"
    rows[0x0F11] = (folder, "reipan_subtitle.obj", len(subtitles), label, len(subtitles) << 1, lba)
    for relative, content in {
        "bin/data/2ddata/b2d/reipan_caption.b2d": caption,
        "bin/data/msg/reipan_msg.obj": messages,
        "bin/data/subtitle/reipan_subtitle.obj": subtitles,
        "font_widths.bin": widths,
    }.items():
        path = output / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
    write_file_table(output / "ZERO3.FHD", rows)
    print("Unavailable in this release:", ", ".join(unavailable))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path, help="Supported SLPS_255.44 ISO")
    parser.add_argument("output", type=Path, help="New directory for the adapted data")
    args = parser.parse_args()
    if not __debug__:
        parser.error("Run without Python's -O option to retain format checks")
    output = args.output.absolute()
    if output.exists():
        parser.error(f"Output already exists: {output}")
    try:
        output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix=".reipan-data-", dir=output.parent) as work:
            staged = Path(work) / "data"
            staged.mkdir()
            prepare(args.iso, staged)
            if output.exists():
                raise FileExistsError(output)
            staged.rename(output)
    except (OSError, ValueError, AssertionError, struct.error) as error:
        parser.exit(1, f"Data preparation failed: {error or 'unexpected catalog layout'}\n")
    print(f"Prepared game data: {output}")


if __name__ == "__main__":
    main()
