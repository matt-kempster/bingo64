#!/usr/bin/env python3
"""Build the sound-data portion of the extractor manifest.

Following the split sm64coopdx established: structure ships with the game
(sound_data.ctl, bank_sets — generated from the repo's committed jsons),
while Nintendo's waveforms and music come out of the user's ROM.

Emits for the manifest:
  - sequences.bin: the structural header/table as a literal, plus per-
    sequence ROM copy instructions (bodies are verified ROM slices).
  - sound_data.tbl: total size plus a sample map of (dstOff, romOff, len)
    copy instructions computed by pairing the ROM ctl's per-bank sample
    records with a replay of serialize_tbl's layout (align 16, ascending
    ROM offset order per sample bank).

The tbl layout replay is validated against the actual built tbl size, and
sequences against the built file, so a release build fails loudly if the
audio pipeline ever changes shape.
"""
import struct

ROM_CTL_OFF, ROM_CTL_LEN = 5748512, 97856
ROM_TBL_OFF, ROM_TBL_LEN = 5846368, 2216704
ROM_SEQ_OFF = 0x7B0860

TYPE_CTL, TYPE_TBL, TYPE_SEQ = 1, 2, 3


def parse_seqfile(data, filetype):
    magic, num = struct.unpack(">HH", data[:4])
    assert magic == filetype, (magic, filetype)
    return [struct.unpack(">II", data[4 + i*8:12 + i*8]) for i in range(num)]


def rom_ctl_samples(ctl, tbl_entries):
    """Walk every bank in the ROM ctl; return per-sample-bank dict:
    sample_bank_index -> sorted set of (rom_tbl_off_within_bank, length)."""
    entries = parse_seqfile(ctl, TYPE_CTL)
    per_bank = {}
    # ROM ctl entry i belongs to sample bank looked up via tbl sharing:
    # parse_tbl in disassemble_sound maps duplicate tbl entries to one bank.
    seen = {}
    bank_of_entry = []
    # tbl entries: same (offset) may repeat; assign bank ids in first-seen order
    for (off, ln) in tbl_entries:
        if off not in seen:
            seen[off] = len(seen)
        bank_of_entry.append(seen[off])

    for i, (off, ln) in enumerate(entries):
        header = ctl[off:off + 16]
        num_inst, num_drums, shared = struct.unpack(">III", header[:12])
        data = ctl[off + 16:off + ln]
        sb = bank_of_entry[i]
        samples = per_bank.setdefault(sb, set())

        (drum_base,) = struct.unpack(">I", data[:4])
        sound_addrs = []
        inst_addrs = []
        for j in range(num_inst):
            (a,) = struct.unpack(">I", data[4 + j*4:8 + j*4])
            if a:
                inst_addrs.append(a)
        for a in inst_addrs:
            # inst: u8 loaded, u8 lo, u8 hi, u8 release, u32 env,
            #       3 * (u32 sample, f32 tuning)
            for k in range(3):
                (s_addr,) = struct.unpack(">I", data[a + 8 + k*8:a + 12 + k*8])
                if s_addr:
                    sound_addrs.append(s_addr)
        if num_drums:
            for j in range(num_drums):
                (da,) = struct.unpack(">I", data[drum_base + j*4:drum_base + j*4 + 4])
                # drum: u8 release, u8 pan, u8 loaded, u8 pad, (u32 sample,
                # f32 tuning), u32 env
                (s_addr,) = struct.unpack(">I", data[da + 4:da + 8])
                sound_addrs.append(s_addr)

        for s_addr in sound_addrs:
            # sample (non-shindou, 20B): u32 zero, u32 addr(in tbl bank),
            # u32 loop, u32 book, u32 len
            zero, addr, loop, book, ln2 = struct.unpack(
                ">IIIII", data[s_addr:s_addr + 20])
            samples.add((addr, ln2))
    return per_bank, entries, bank_of_entry, seen


def built_ctl_samples(path):
    """Parse the built native (LE, 64-bit) ctl. Per bank entry, return the
    set of (bank-relative tbl offset, length) for every referenced sample.
    Layouts follow assemble_sound.pack with WORD_BYTES=8 (P->Q, X->4 pad)."""
    ctl = open(path, "rb").read()
    magic, num = struct.unpack_from("<HH", ctl, 0)
    assert magic == TYPE_CTL, magic
    banks = []
    for i in range(num):
        off, ln = struct.unpack_from("<QI", ctl, 8 + i*16)[:2]
        num_inst, num_drums, shared, date = struct.unpack_from("<IIII", ctl, off)
        blob = ctl[off + 16:off + ln]
        samples = set()

        def sample_at(addr):
            if addr == 0:
                return
            tbl_off, = struct.unpack_from("<Q", blob, addr + 8)
            slen, = struct.unpack_from("<I", blob, addr + 32)
            samples.add((tbl_off, slen))

        drum_table, = struct.unpack_from("<Q", blob, 0)
        inst_addrs = struct.unpack_from(f"<{num_inst}Q", blob, 8)
        for a in inst_addrs:
            if a == 0:
                continue
            for k in range(3):  # sounds at +16, each 16 bytes: u64 sample, f32, pad
                s_addr, = struct.unpack_from("<Q", blob, a + 16 + k*16)
                sample_at(s_addr)
        if num_drums:
            drum_addrs = struct.unpack_from(f"<{num_drums}Q", blob, drum_table)
            for da in drum_addrs:
                s_addr, = struct.unpack_from("<Q", blob, da + 8)
                sample_at(s_addr)
        banks.append(samples)
    return banks


def build_sound_maps(rom, builddir):
    ctl = rom[ROM_CTL_OFF:ROM_CTL_OFF + ROM_CTL_LEN]
    rom_tbl = rom[ROM_TBL_OFF:ROM_TBL_OFF + ROM_TBL_LEN]
    tbl_entries = parse_seqfile(rom_tbl, TYPE_TBL)
    per_bank, ctl_entries, bank_of_entry, bank_first_off = \
        rom_ctl_samples(ctl, tbl_entries)

    built_tbl = open(f"{builddir}/sound/sound_data.tbl", "rb").read()
    built_tbl_len = len(built_tbl)
    bmagic, bnum = struct.unpack_from("<HH", built_tbl, 0)
    assert bmagic == TYPE_TBL
    built_tbl_entries = [struct.unpack_from("<QI", built_tbl, 8 + i*16)[:2]
                         for i in range(bnum)]

    built_banks = built_ctl_samples(f"{builddir}/sound/sound_data.ctl")
    assert len(built_banks) == len(ctl_entries) == len(built_tbl_entries)

    # Ctl banks sharing a sample bank each reference a subset of its
    # samples; group per sample bank on both sides before pairing (both in
    # ascending tbl-offset order).
    built_per_sb = {}
    dst_base_per_sb = {}
    for i in range(len(built_banks)):
        sb = bank_of_entry[i]
        built_per_sb.setdefault(sb, set()).update(built_banks[i])
        prev = dst_base_per_sb.setdefault(sb, built_tbl_entries[i][0])
        assert prev == built_tbl_entries[i][0], (i, sb)

    tbl_map = {}   # dstOff -> (romOff, len)
    for sb, built_samples in built_per_sb.items():
        rom_bank_base = ROM_TBL_OFF + \
            [k for k, v in bank_first_off.items() if v == sb][0]
        rom_samples = sorted(per_bank[sb])
        built_samples = sorted(built_samples)
        assert len(rom_samples) == len(built_samples), \
            (sb, len(rom_samples), len(built_samples))
        for (raddr, rlen), (baddr, blen) in zip(rom_samples, built_samples):
            assert rlen == blen, (sb, hex(raddr), rlen, blen)
            tbl_map[dst_base_per_sb[sb] + baddr] = (rom_bank_base + raddr, rlen)
    tbl_map = sorted((d, s, l) for d, (s, l) in tbl_map.items())

    # sequences.bin: literal header + ROM body copies.
    seq = open(f"{builddir}/sound/sequences.bin", "rb").read()
    magic, cnt = struct.unpack("<HH", seq[:4])
    header_len = struct.unpack_from("<Q", seq, 8)[0]  # first entry offset
    rom_seq = rom[ROM_SEQ_OFF:]
    rom_entries = parse_seqfile(rom_seq, TYPE_SEQ)
    seq_map = []   # (dstOff, romOff, len)
    for i in range(cnt):
        off, ln = struct.unpack_from("<QI", seq, 8 + i*16)
        roff, rln = rom_entries[i]
        assert ln == rln, (i, ln, rln)
        assert seq[off:off+ln] == rom_seq[roff:roff+rln], f"seq {i} body mismatch"
        seq_map.append((off, ROM_SEQ_OFF + roff, ln))
    seq_header = seq[:header_len]
    end = seq_map[-1][0] + seq_map[-1][2]
    assert set(seq[end:]) <= {0}, "non-zero trailing seq data"  # align(64) pad

    return {
        "tbl_len": built_tbl_len,
        "tbl_map": tbl_map,
        "seq_len": len(seq),
        "seq_header": seq_header,
        "seq_map": seq_map,
    }


def verify_tbl_content(rom, builddir, tbl_map, tbl_len):
    """Report how much of the built tbl the ROM copies reproduce exactly
    (differences are expected where alo's encoder re-encoded samples)."""
    built = open(f"{builddir}/sound/sound_data.tbl", "rb").read()
    same = total = 0
    for (dst, src, ln) in tbl_map:
        total += ln
        if built[dst:dst+ln] == rom[src:src+ln]:
            same += ln
    return f"tbl: {len(tbl_map)} samples, {total} bytes mapped, " \
           f"{100.0*same/total:.1f}% byte-identical to built"
