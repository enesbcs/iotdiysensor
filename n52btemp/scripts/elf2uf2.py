from SCons.Script import Import
import os

Import("env")

# nice!nano / SuperMini Adafruit nRF52 bootloader 0.6.0 (Jun 2021)
# accepts CFG_UF2_FAMILY_APP_ID 0xADA52840 or board ID 0x239A00B3.
# The modern 0x6B846188 family ID is NOT recognized by this bootloader,
# which would cause every block to be rejected (switch default: return -1).
FAMILY_NRF52840 = 0xADA52840
UF2_BLOCK = 512
UF2_MAGIC0 = 0x0A324655
UF2_MAGIC1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30


def parse_ihex(path):
    start = None
    chunks = []
    base_hi = 0
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line[0] != ":":
                continue
            rec = bytes.fromhex(line[1:])
            count = rec[0]
            addr = int.from_bytes(rec[1:3], "big")
            rtype = rec[3]
            data = rec[4:4 + count]
            if rtype == 0x04:
                base_hi = int.from_bytes(data, "big") << 16
            elif rtype == 0x02:
                base_hi = int.from_bytes(data, "big") << 4
            elif rtype == 0x00:
                base = base_hi + addr
                if start is None:
                    start = base
                chunks.append((base, data))
    if start is None:
        raise RuntimeError("no data in hex file")
    chunks.sort()
    image = b""
    cur = start
    for base, data in chunks:
        if base > cur:
            image += b"\xff" * (base - cur)
        if base <= cur:
            data = data[cur - base:]
        image += data
        cur = base + len(data)
    return start, image


def hex_to_uf2(hex_path, uf2_path, family_id):
    base, image = parse_ihex(hex_path)
    if base & 0xFF:
        raise RuntimeError("flash base not page aligned: 0x%08x" % base)
    pad = (-len(image)) % 256
    image += b"\xff" * pad
    num_blocks = len(image) // 256
    with open(uf2_path, "wb") as out:
        for i in range(num_blocks):
            payload = image[i * 256:(i + 1) * 256]
            block = bytearray(UF2_BLOCK)
            block[0:4] = UF2_MAGIC0.to_bytes(4, "little")
            block[4:8] = UF2_MAGIC1.to_bytes(4, "little")
            block[8:12] = (0x2000).to_bytes(4, "little")  # familyID present
            block[12:16] = (base + i * 256).to_bytes(4, "little")
            block[16:20] = len(payload).to_bytes(4, "little")
            block[20:24] = i.to_bytes(4, "little")
            block[24:28] = num_blocks.to_bytes(4, "little")
            block[28:32] = family_id.to_bytes(4, "little")
            block[32:288] = payload
            block[508:512] = UF2_MAGIC_END.to_bytes(4, "little")
            out.write(block)
    return num_blocks


def build_uf2(target, source, env):
    elf = str(target[0])
    if os.path.basename(elf) != "firmware.elf":
        return
    build_dir = os.path.dirname(elf)
    cc = env.get("CC", "arm-none-eabi-gcc")
    objcopy = os.path.join(os.path.dirname(cc), "arm-none-eabi-objcopy")
    hex_path = os.path.join(build_dir, "firmware.hex")
    uf2_path = os.path.join(build_dir, "firmware.uf2")
    os.system('"%s" -O ihex "%s" "%s"' % (objcopy, elf, hex_path))
    total = hex_to_uf2(hex_path, uf2_path, FAMILY_NRF52840)
    print("Built %s (%.0f KiB app, %d UF2 blocks)"
          % (uf2_path, os.path.getsize(uf2_path) / 1024.0, total))


env.AddPostAction("$BUILD_DIR/firmware.elf", build_uf2)