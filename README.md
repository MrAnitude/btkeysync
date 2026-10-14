# btkeysync

**Stop re-pairing your Bluetooth devices every time you switch between Linux and Windows.**

`btkeysync` is a small C++20 utility that reads the Bluetooth pairing keys Windows
generated for a device — straight out of the offline Windows registry hive, while
you are booted in Linux — and writes them into BlueZ's key store, so both
operating systems present the *same* keys to the device.

> **Status: work in progress.** The Bluetooth Classic (Link Key) path works
> end-to-end for a single device, but configuration is still hardcoded in
> `src/main.cpp`. See [Current status](#current-status) before you build.

---

## Table of contents

- [The problem](#the-problem)
- [How it works](#how-it-works)
- [Current status](#current-status)
- [Requirements](#requirements)
- [Building](#building)
- [Configuration](#configuration)
- [Usage](#usage)
- [Running at boot (systemd)](#running-at-boot-systemd)
- [Project layout](#project-layout)
- [Safety notes](#safety-notes)
- [Troubleshooting](#troubleshooting)
- [Roadmap](#roadmap)
- [References](#references)

---

## The problem

When you pair a Bluetooth device, the two sides run a one-time handshake that
produces a shared secret — a **Link Key** for Bluetooth Classic, or a **Long-Term
Key (LTK)** and friends for Bluetooth Low Energy. Both sides store that secret and
reuse it on every later connection, so the handshake never has to be repeated.

That key store lives in the *operating system*, not in the Bluetooth chip. On a
dual-boot machine, Linux and Windows therefore keep two completely separate
stores — but the **device** only keeps one key per computer, because it identifies
the computer by the adapter's hardware Bluetooth address (`BD_ADDR`), which is
identical under both OSes.

So the device ping-pongs:

1. Pair under Windows → works in Windows.
2. Reboot to Linux, pair again → the device overwrites its saved key. Linux works,
   **Windows is now broken**.
3. Reboot to Windows, pair again → Windows works, **Linux is now broken**.

Forever.

**The fix, in one sentence:** pair the device once under each OS, then copy the
keys so that both operating systems hand the device the *same* key. The device
sees one consistent computer regardless of which OS booted, and never needs to
re-pair.

`btkeysync` performs and maintains that copy automatically.

## How it works

The two halves of a Bluetooth system matter here:

- **The Controller** — the physical chip. It owns the radio and a fixed hardware
  `BD_ADDR`. That address is the same no matter which OS booted, which is the
  whole reason this trick works.
- **The Host** — the OS software stack. It runs pairing, generates keys, and
  *stores* them. This is the part that differs per OS, and the only part
  `btkeysync` touches.

Each host stack keeps its keys somewhere different:

| OS | Stack | Key location | Format |
| --- | --- | --- | --- |
| Linux | BlueZ | `/var/lib/bluetooth/<ADAPTER_MAC>/<DEVICE_MAC>/info` | INI-style plain text |
| Windows | Microsoft BT stack | `HKLM\SYSTEM\CurrentControlSet\Services\BTHPORT\Parameters\Keys\<ADAPTER_MAC>\<DEVICE_MAC>` | registry, `SYSTEM`-only |

Because BlueZ's store is plain text and trivially rewritable, `btkeysync` copies
in **one direction only: Windows → Linux**. Windows is never modified, so it can
never be broken by a bad run. Linux is made to conform.

Crucially, Windows is read **offline** — you do not boot it. The `SYSTEM`
registry hive is just a file on the Windows partition
(`Windows/System32/config/SYSTEM`), and [`hivex`](https://github.com/libguestfs/hivex)
parses it directly. No code ever has to run inside Windows, no second boot hook,
no cross-OS drift.

The pipeline is four stages:

```
 Windows SYSTEM hive                BlueZ info file
 (read-only, offline)               (/var/lib/bluetooth/.../info)
         │                                    ▲
         │  1. read                           │ 3. write
         ▼                                    │
    HiveReader  ──── 2. translate ───►   BlueZInfo
                      (LinkKey:
                   hex + byte order)
```

1. **`HiveReader`** opens the hive and walks
   `ControlSet001 → Services → BTHPORT → Parameters → Keys → <adapter> → <device>`,
   returning the raw key bytes.
2. **`LinkKey`** wraps those 16 bytes and handles the representation work: hex
   encode/decode and byte-order reversal. *This is where almost every failure in
   this kind of tool lives* — see [Troubleshooting](#troubleshooting).
3. **`BlueZInfo`** loads the device's `info` file, replaces the `Key=` value in
   the `[LinkKey]` section, backs the original up to `info.bak`, and writes it
   back.
4. A **systemd oneshot unit** runs the whole thing at boot so the setup is
   self-healing: whatever Windows currently holds, Linux matches on the next boot.

### Why bluetooth is stopped during the write

A running `bluetoothd` caches bonds in memory and flushes them back to the `info`
file on shutdown — which would silently clobber the edit. So `main.cpp` does
`systemctl stop bluetooth`, writes, then `systemctl start bluetooth`.

This is also why the systemd unit is ordered `After=bluetooth.service` and
**not** `Before=`: the program manages the daemon itself, and a `Before=`
ordering would deadlock (bluetooth waiting on us, us waiting on bluetooth).

## Current status

What works today:

- ✅ Reading a Bluetooth **Classic Link Key** from an offline Windows `SYSTEM` hive
- ✅ Hex encoding/decoding and byte-order reversal (`LinkKey`)
- ✅ Reading and rewriting the `[LinkKey]` section of a BlueZ `info` file, with
  automatic `.bak` backup
- ✅ Stop/write/start of `bluetooth.service` around the edit
- ✅ A systemd oneshot unit for running at boot

Not implemented yet:

- ❌ **Configuration** — the hive path, `info` path, adapter MAC and device MAC are
  hardcoded in `src/main.cpp` and must be edited for your machine
- ❌ **Bluetooth LE** — `[LongTermKey]`, `[IdentityResolvingKey]`, `EDiv`/`Rand`.
  LE devices (most modern mice, keyboards, controllers) are not handled
- ❌ **Multiple devices** — one device per run; no loop over the adapter's bonds
- ❌ **Idempotency** — it writes unconditionally instead of skipping when the keys
  already match
- ❌ **Auto-mounting** the Windows partition; it must already be mounted
- ❌ **Logging** beyond `printf`, and proper exit codes on failure

See [Roadmap](#roadmap).

## Requirements

- Linux with **BlueZ** and systemd
- A **C++20** compiler (GCC 10+ / Clang 10+)
- **CMake** ≥ 3.16
- **libhivex** development headers
- The Windows partition mounted and readable (NTFS), with **Fast Startup and
  hibernation disabled** in Windows — a hibernated Windows leaves NTFS locked and
  the hive stale
- `root` — the tool reads `/mnt/...`, writes `/var/lib/bluetooth`, and controls
  `bluetooth.service`

Install the dependencies:

```bash
# Debian / Ubuntu / Pop!_OS
sudo apt install build-essential cmake libhivex-dev

# Fedora
sudo dnf install gcc-c++ cmake hivex-devel

# Arch
sudo pacman -S base-devel cmake hivex
```

## Building

```bash
git clone https://github.com/MrAnitude/btkeysync.git
cd btkeysync
cmake -S . -B build
cmake --build build
```

The binary lands at `build/btkeysync`.

`CMakeLists.txt` locates hivex by hand with `find_path`/`find_library`, because
hivex ships no CMake config package. If it can't be found you'll get a clear
`hivex not found — install libhivex-dev` at configure time.

To install it where the systemd unit expects it:

```bash
sudo install -m 755 build/btkeysync /usr/local/bin/btkeysync
```

## Configuration

Until a config file lands ([Roadmap](#roadmap)), edit the four values at the top
of [`src/main.cpp`](src/main.cpp):

```cpp
std::string systemHive = "/mnt/windows/Windows/System32/config/SYSTEM";
std::string infoPath   = "/var/lib/bluetooth/68:C6:AC:65:42:A7/80:99:E7:AE:4B:86/info";
std::string adapterMac = "68C6AC6542A7";
std::string deviceMac   = "8099e7ae4b86";
```

Finding your values:

**1. Adapter and device MACs, and the `info` path (Linux side).**

```bash
bluetoothctl list              # your adapter's MAC
bluetoothctl devices           # your paired devices' MACs
sudo ls /var/lib/bluetooth/    # adapter folder, colon-separated
sudo cat /var/lib/bluetooth/<ADAPTER>/<DEVICE>/info
```

The `info` file tells you which kind of device you have: a `[LinkKey]` section
means Bluetooth Classic (supported), while `[LongTermKey]` /
`[IdentityResolvingKey]` means LE (not yet supported).

**2. The hive path.** Mount the Windows partition read-only and locate the hive:

```bash
lsblk -f                                        # find the Windows NTFS partition
sudo mkdir -p /mnt/windows
sudo mount -o ro /dev/nvme0n1p3 /mnt/windows    # substitute your partition
ls /mnt/windows/Windows/System32/config/SYSTEM
```

**3. MAC formatting.** Note the three different spellings in play, and match each
one exactly:

| Where | Format | Example |
| --- | --- | --- |
| BlueZ directory path | colon-separated, uppercase | `68:C6:AC:65:42:A7` |
| Hive adapter subkey | no separators | `68C6AC6542A7` |
| Hive device value name | no separators, lowercase | `8099e7ae4b86` |

If `HiveReader` throws `could not find device MAC`, the casing or separators are
almost certainly the culprit. You can inspect the hive's actual subkey names with
`hivexsh` (from the `libhivex-bin` / `hivex` package):

```bash
hivexsh /mnt/windows/Windows/System32/config/SYSTEM
> cd \ControlSet001\Services\BTHPORT\Parameters\Keys
> ls
> cd <adapter>
> lsval
```

## Usage

```bash
sudo /usr/local/bin/btkeysync
```

Output looks like:

```
Windows key:        A73F1C...
Windows reversed:   ...1C3FA7
BlueZ current:      8821B4...
Wrote reversed key to BlueZ (bluetooth stopped during write). Try connecting.
```

Then connect the device:

```bash
bluetoothctl connect <DEVICE_MAC>
```

It should connect with no pairing prompt. Reboot into Windows and confirm it
still works there too — it must, since Windows was never written to.

## Running at boot (systemd)

[`systemd/btkeysync.service`](systemd/btkeysync.service) is a `oneshot` unit that
runs the sync once per boot:

```bash
sudo install -m 644 systemd/btkeysync.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable btkeysync.service
```

Check it after a reboot:

```bash
systemctl status btkeysync.service
journalctl -u btkeysync.service -b
```

Because the unit is ordered `After=local-fs.target bluetooth.service`, the
Windows partition must be mounted by `local-fs.target` — i.e. it needs an
`/etc/fstab` entry. Add one, read-only:

```fstab
UUID=<windows-partition-uuid>  /mnt/windows  ntfs3  ro,noauto,x-systemd.automount  0  0
```

Get the UUID with `lsblk -f` or `blkid`. If you'd rather not touch `fstab`, add
the mount yourself in the unit with an `ExecStartPre=` line instead.

## Project layout

```
btkeysync/
├── CMakeLists.txt            # build; locates libhivex manually
├── includes/
│   ├── HiveReader.h          # offline Windows registry hive reader
│   ├── LinkKey.h             # 16-byte key: hex ⇄ bytes, byte reversal
│   └── BlueZInfo.h           # read/modify/save a BlueZ info file
├── src/
│   ├── main.cpp              # wiring + hardcoded config (for now)
│   ├── HiveReader.cpp
│   ├── LinkKey.cpp
│   └── BlueZInfo.cpp
├── systemd/
│   └── btkeysync.service     # oneshot boot unit
├── third-party/
│   └── SimpleIni.h           # placeholder for proper INI parsing (empty)
└── guidelines.md             # the long-form design/background write-up
```

[`guidelines.md`](guidelines.md) is the deep-dive: the full Bluetooth key model,
every host stack's storage format, and the step-by-step reasoning this
implementation follows. Read it if you want the *why* behind any of the above.

### The pieces

**`HiveReader`** — RAII wrapper over `hivex_open`/`hivex_close` (non-copyable).
`readLinkKey(adapterMac, deviceMac)` walks the fixed `BTHPORT` path and returns
the raw value bytes, throwing `std::runtime_error` with the offending path
segment on a miss.

**`LinkKey`** — a value type over `std::array<uint8_t, 16>`. `fromHex` parses a
32-character hex string with `std::from_chars`; `toHex` emits uppercase;
`reversed()` returns a byte-flipped copy. Keeping the ordering concern in one
place is deliberate — it is the single most error-prone part of the job.

**`BlueZInfo`** — slurps the `info` file into a string on construction,
`getLinkKey()`/`setLinkKey()` patch the `Key=` value inside the `[LinkKey]`
section by string position, and `save()` copies the original to `<path>.bak`
before truncating and rewriting. (The string-position approach is why
`SimpleIni.h` is vendored but unused — a real INI parser is the intended
replacement.)

## Safety notes

- **Windows is read-only, always.** The tool only ever reads the hive. Mount the
  Windows partition `ro` so an accident is impossible.
- **Every write is backed up.** `BlueZInfo::save()` copies `info` to `info.bak`
  first. To revert: `sudo cp info.bak info && sudo systemctl restart bluetooth`.
- **Disable Windows Fast Startup and hibernation.** Otherwise NTFS stays locked
  and the hive you read is stale — you'll sync old keys and wonder why nothing
  connects.
- **Never commit key material.** The `.gitignore` excludes `*.bak`, `*.hive`,
  `SYSTEM`, `*.key` and similar for exactly this reason. Link keys and LTKs are
  secrets: anyone holding one can impersonate your computer to that device.
- **Scope.** This only reads Windows and writes Linux, on one machine, for
  devices *you* already bonded yourself. It never touches the controller or the
  device firmware. It is an automation of the manual procedure documented on the
  Arch Wiki.

## Troubleshooting

**The device still won't connect after a sync.**
Byte order, nine times out of ten. Windows and BlueZ do not always store the key
in the same direction. `main.cpp` currently writes `winKey.toHex()` — the hive's
own order. Swap it for `winKey.reversed().toHex()`, rebuild, and retry. The
program prints both forms, so you can see exactly what each one is before and
after.

**`could not open <hive path>`.**
The Windows partition isn't mounted, or the path is wrong. Check `lsblk -f`,
remount, and confirm the file exists at
`<mountpoint>/Windows/System32/config/SYSTEM`.

**`hive path segment not found: ControlSet001`.**
Your system's current control set isn't `001`. Windows keeps `ControlSet001`,
`ControlSet002` and a `Current` pointer; read
`HKLM\SYSTEM\Select\Current` (via `hivexsh`) to see which is live, and adjust the
path in `HiveReader::readLinkKey`.

**`could not find device MAC: ...`.**
MAC formatting — see the table in [Configuration](#configuration). List the
actual value names with `hivexsh` and `lsval`.

**`could not open file Path: /var/lib/bluetooth/...`.**
Either you're not root, or the device isn't paired under Linux yet. Pair it
normally with `bluetoothctl` first; `btkeysync` edits an existing bond, it does
not create one.

**It writes nothing and reports success.**
`BlueZInfo::setLinkKey()` returns silently when there is no `[LinkKey]` section —
which is the case for an LE-only device. Check the `info` file: if you see
`[LongTermKey]` instead, LE support isn't implemented yet.

**It worked, then stopped after a Windows update or a re-pair.**
Expected, and exactly what the boot service is for: Windows regenerated the key,
so the next Linux boot re-syncs it. If you ran manually, just run again.

## Roadmap

In rough priority order:

- [ ] Config file (`/etc/btkeysync.conf`) for hive path, mount point and
      per-device overrides — replacing the hardcoded values in `main.cpp`
- [ ] Auto-discover the adapter and enumerate *all* bonded devices, syncing each
- [ ] Bluetooth LE support: `[LongTermKey]`, `[SlaveLongTermKey]`,
      `[IdentityResolvingKey]`, plus `EDiv`/`Rand` (and their endianness)
- [ ] Resolve the live `ControlSet` from `HKLM\SYSTEM\Select\Current` instead of
      assuming `ControlSet001`
- [ ] Idempotency: compare before writing, and leave the file untouched when the
      keys already match
- [ ] Replace positional string editing with a real INI parser (the vendored
      `SimpleIni.h` is a placeholder)
- [ ] Proper logging to the journal and meaningful exit codes; never leave
      `bluetooth.service` stopped if the write throws
- [ ] Mount the Windows partition read-only itself when it isn't already
- [ ] CLI flags: `--dry-run`, `--device`, `--verbose`
- [ ] Unit tests for `LinkKey` and `BlueZInfo` parsing

## References

- [`guidelines.md`](guidelines.md) — the full design write-up in this repo
- [Arch Wiki — Bluetooth: Dual boot pairing](https://wiki.archlinux.org/title/Bluetooth#Dual_boot_pairing)
- [hivex](https://github.com/libguestfs/hivex) — the offline registry hive library
- [BlueZ](https://www.bluez.org/)
- [Bluetooth Core Specification](https://www.bluetooth.com/specifications/specs/core-specification/) — pairing and key derivation
