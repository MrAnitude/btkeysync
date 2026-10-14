# Dual-Boot Bluetooth LTK Sync: A Build Guide

Sep 26, 2026 · @anikau

## The problem: why dual-boot forces re-pairing

When you pair a Bluetooth device (mouse, keyboard, headphones, controller) with a computer, the two sides run a one-time handshake that produces a set of secret keys. Both sides store those keys and reuse them on every later connection so they never have to redo the handshake.

The key store lives inside the operating system, not in the Bluetooth chip. So when you dual-boot one machine, Linux and Windows each keep their own separate store. If you pair under Linux, then reboot into Windows and pair again, the device is asked to overwrite the keys it saved. The device only keeps one key per computer (it identifies the computer by the adapter's Bluetooth address, which is the same in both OSes). Whichever OS paired most recently wins, and the other OS's bond silently breaks.

The result you have seen: pair under Windows, everything works; reboot to Linux, the device won't connect until you remove and re-pair it; reboot back to Windows, now Windows is broken. The device ping-pongs.

**The fix, in one sentence:** pair the device once under each OS, then copy the keys so that both operating systems present the *same* keys to the device. The device then sees one consistent computer no matter which OS booted, and never needs to re-pair. This guide explains the mechanism first, then how to build a small program that performs and maintains that copy automatically at boot.

## How Bluetooth works (the parts that matter here)

**The radio.** Bluetooth is a short-range radio protocol in the 2.4 GHz band. Everything else is layers of software agreeing on how to use that radio to find each other, connect, and stay secure.

**Two flavours: Classic and Low Energy.** Since Bluetooth 4.0 there are two mostly-separate stacks sharing the same radio:

- **Bluetooth Classic (BR/EDR)** — the older one, used by audio headsets (A2DP), older mice/keyboards, file transfer. Its security produces a single secret called the **Link Key**.
- **Bluetooth Low Energy (BLE / LE)** — the newer, power-sipping one, used by modern peripherals, wearables, most current mice/keyboards, controllers. Its security produces a **set** of keys (described in the next section), the most important being the **LTK — Long-Term Key**.

A device can be Classic-only, LE-only, or **dual-mode** (supports both). This matters because *where the keys live and what they are called differs between the two*, and a dual-mode device may have both a Link Key and an LTK that both need syncing.

**Controller vs. Host — the split that makes all this possible.** A Bluetooth system is built in two halves:

1. **The Controller** — the physical chip (in your USB dongle, laptop card, or built into the SoC). It owns the radio and the lowest-level timing. Crucially, it has its own fixed **Bluetooth Device Address (BD\_ADDR)** — a MAC-like identifier. This address is the *same regardless of which OS booted*, because it lives in the hardware. That is the whole reason the trick works: the device recognises the computer by this hardware address, so both OSes are the "same computer" to it — they just need to prove it with matching keys.
2. **The Host** — the software stack running in the OS. It handles pairing logic, key generation, key *storage*, device names, and the profiles (audio, HID, etc.). This is the part that differs between Linux, Windows, and macOS, and this is where the keys are kept.

**HCI — the wire between them.** The Host and Controller talk over a standard protocol called **HCI (Host Controller Interface)**. When pairing happens, the Host runs the key-exchange logic, and the resulting keys are handed down to the Controller and also written to the Host's own on-disk database. Understanding this split tells you exactly what you can and cannot change: you cannot alter the controller's hardware address, but you *can* rewrite the Host's stored keys, because that is just data in the OS.

## Pairing and keys: what actually gets stored

When a device and computer pair, they run a key exchange and each side saves the outcome. These saved secrets are what you will be copying between operating systems. Here is what each one is.

**Bluetooth Classic — the Link Key.** A single 128-bit secret shared by the two sides. Both the computer and the device store it, indexed by each other's address. That is the whole story for Classic.

**Bluetooth LE — a small bundle of keys.** LE pairing produces several keys, and each direction of the link has its own set. The ones that matter:

| Key | What it is | Why it matters for sync |
| --- | --- | --- |
| **LTK (Long-Term Key)** | The main encryption key that secures the link on every future reconnection | The central item to sync. Without a matching LTK, encryption fails and the device is dropped. |
| **EDIV + Rand** | A short identifier and random value that let the device look up the right LTK | Older LE (legacy pairing) needs these alongside the LTK; they must match too. |
| **IRK (Identity Resolving Key)** | Lets one side recognise the other even when it rotates its address for privacy | Needed so a device using a random ("resolvable") address is still recognised across OSes. |
| **CSRK (Connection Signature Resolving Key)** | Signs unencrypted data for integrity | Rarely the sticking point, but part of a complete copy. |

**Central vs. peripheral keys.** In LE, the two roles are *central* (usually your computer) and *peripheral* (usually the device). Depending on how pairing was configured, the LTK you need to copy may be the one the peripheral holds or the one the computer holds. Modern secure pairing (LE Secure Connections) uses a single shared LTK for both directions, which is simpler; older legacy pairing has a per-direction LTK plus EDIV/Rand. When you inspect your stored keys you will see which case you are in.

**Bonding.** "Bonding" is the term for the act of *saving* these keys for reuse. A device that is "paired but not bonded" forgets its keys on disconnect; a "bonded" device keeps them. Everything in this guide assumes bonded devices — that is the normal case for anything you use repeatedly.

**The core insight for the build:** all of these are just bytes sitting in a file or registry entry that the Host stack owns. Copying them from one OS's store into the other's, in that OS's expected format, is the entire task.

## Bluetooth host stacks and where each keeps its keys

A "host stack" is the OS's Bluetooth software — the Host half from earlier. Each OS ships a different one, stores keys in a different place and format, and that is what you have to bridge.

**Linux — BlueZ.** The standard Linux stack. It stores bonds as plain text files on disk, which makes Linux the easiest side to read and write. The layout is:

```
/var/lib/bluetooth/<ADAPTER_MAC>/<DEVICE_MAC>/info
```

That `info` file is an INI-style text file with sections like `[LinkKey]` (Classic) and `[LongTermKey]`, `[IdentityResolvingKey]`, `[SlaveLongTermKey]` (LE), each listing the key bytes as hex, plus `EDiv` and `Rand` values. Because it is text, you can read exactly what BlueZ holds and write new values into it. After editing you must restart the `bluetooth` service so BlueZ reloads.

**Windows — the Microsoft Bluetooth stack.** Keys live in the registry, not files, under a key that only the `SYSTEM` account can read:

```
HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\BTHPORT\Parameters\Keys\<ADAPTER_MAC>\<DEVICE_MAC>
```

For Classic devices the value is the Link Key directly. For LE devices, the entry holds multiple named values (LTK, ERand, EDIV, IRK, and so on) — note Windows sometimes stores them with different byte ordering than BlueZ, which is the single most common source of mistakes. Reading this key requires elevating to `SYSTEM` (e.g. via PsExec or the Registry editor run as SYSTEM); a normal Administrator is not enough.

**macOS — the Apple stack.** Keys are in a system property list at `/private/var/root/Library/Preferences/com.apple.bluetoothd.plist` (path and format vary by version), readable only as root. macOS is the least commonly synced and the most version-fragile; most dual-boot setups are Linux + Windows.

**The endianness and format trap.** The same LTK is a fixed sequence of bytes, but different stacks may print or store those bytes in reverse order, or split them differently. The device does not care about your files — it cares that the final key bytes match what it holds. So the real work is not copying strings; it is understanding each store's format and byte order and translating correctly between them. Almost every failure comes from a byte-order or format mismatch, not from a wrong key.

**Which direction to copy.** Because BlueZ is plain text and easy to rewrite, the usual strategy is: pair under Windows first, read the keys Windows generated (as SYSTEM), then write those same keys into BlueZ's `info` file. That way Windows keeps working untouched and you only ever edit the Linux side. You can also go the other way, but rewriting the Windows registry on every boot is more fragile, so most builds make Linux conform to Windows' keys.

## Architecture of the sync program

Before any code, be clear about what the program *is*. It is a small tool that runs on the Linux side, reads the keys Windows generated for each bonded device, and makes BlueZ hold the identical keys. Conceptually it has four parts:

1. **A reader for the Windows store.** Something that can reach the Windows registry hive while you are booted in Linux. You do not boot Windows to do this — you mount the Windows system partition from Linux and read its registry hive file offline (the `SYSTEM` hive is a file on that partition). A registry-hive parsing library reads the `BTHPORT\...\Keys` subtree from that file.
2. **A translator.** Logic that takes each device's raw key values from the Windows hive and converts them into BlueZ's expected format — correct field names, correct hex representation, and above all correct byte order. This is where the real care goes.
3. **A writer for the BlueZ store.** Logic that finds or creates the matching `/var/lib/bluetooth/<adapter>/<device>/info` file and writes the translated keys into the right INI sections, then triggers a reload of the bluetooth service.
4. **A boot hook.** A mechanism that runs steps 1–3 automatically early in the Linux boot, before or just as Bluetooth comes up, so the synced keys are already in place by the time you use the device.

**Why offline registry reading, not "run on both OSes."** A design that needs code running inside Windows too is far more fragile (two boot hooks, two languages, drift between them). Reading the Windows hive as a file from Linux means the whole program lives in one place, in one language, and Windows never has to be touched after the initial pairing. This is the standard, robust design.

**One-time vs. every-boot.** If neither OS ever re-pairs the device, the keys are stable and you only need to sync once. In practice, though, an OS may occasionally regenerate keys (a re-pair, an update, a battery-out reset on the device). Running the sync at every boot is cheap and makes the setup self-healing: whatever Windows currently holds, Linux is made to match on the next Linux boot. That is why the boot hook matters.

**Scope boundary.** This program only ever reads Windows and writes Linux (in the recommended direction). It never changes the controller, never touches the device firmware, and only manipulates key material for devices *you* have already bonded on *your own* machine. It is a convenience tool for your own hardware, in the same spirit as the widely documented manual procedure it automates.

## Step-by-step build guidelines

Work through these in order. Each step is described as *what to do and why*, so you learn the tooling as you go. No implementation code — these are the guidelines you build from.

**Step 1 — Pair the device on both OSes.** Boot Windows, pair the device normally. Boot Linux, pair the same device normally. At this point everything ping-pongs — that is expected. You now have a bond entry for the device in *both* stores, which is what the sync will reconcile.

**Step 2 — Learn to read the BlueZ side.** In Linux, look at `/var/lib/bluetooth/`. Identify your adapter's MAC folder and, inside it, your device's MAC folder, and open the `info` file. Read it with `bluetoothctl info <device>` too, to correlate. Note which sections exist (`[LinkKey]` for Classic, `[LongTermKey]`/`[IdentityResolvingKey]` for LE). This teaches you the exact target format you must produce.

**Step 3 — Get read access to the Windows hive from Linux.** Mount the Windows partition (it is usually NTFS; ensure it was fully shut down, not hibernated, or the filesystem will be read-only/locked — disable Windows Fast Startup). Locate the registry hive file at `Windows/System32/config/SYSTEM` on that partition. You will parse this file directly; you are not booting Windows.

**Step 4 — Parse the keys out of the hive.** Use a registry-hive reading library (for example, a Python library that opens offline hive files, or the `reged`/`chntpw` family of tools). Navigate to `ControlSet001\Services\BTHPORT\Parameters\Keys\<adapter>\<device>`. Extract the values. For a Classic device you will find the Link Key. For an LE device you will find named values for LTK, and typically `ERand`, `EDIV`, `IRK`, and address-type fields. Write down exactly what you find as raw hex — do not reformat yet.

**Step 5 — Work out the byte-order mapping.** This is the crux. Compare a known key: pair a device, read what BlueZ recorded and what Windows recorded for the *same* bond, and line them up. You will see whether the LTK bytes are in the same order or reversed, and the same for `Rand`/`ERand` and `IRK`. Establish the exact transformation rule for each field once, by observation, rather than assuming. Common patterns: the LTK and IRK often need reversing; EDIV is a small integer that may differ in endianness. Verify against a bond you can test.

**Step 6 — Write the translated keys into BlueZ.** For each device, open (or create) its BlueZ `info` file and set the key fields to the translated Windows values, in the correct INI sections and hex casing BlueZ expects. Keep a backup of the original file first. If the device only exists in the Windows store (paired there but not Linux), you may need to create the directory and a minimal `info` file with the right addresses and attributes.

**Step 7 — Reload and test.** Restart the bluetooth service (`systemctl restart bluetooth`) so BlueZ re-reads the files. Connect the device under Linux — it should connect without re-pairing. Then reboot into Windows and confirm it *still* works there (it must, since you never changed Windows). If Linux fails, re-check the byte order for that key; if Windows broke, you accidentally edited the wrong side.

**Step 8 — Generalise to all devices.** Loop the read/translate/write over every device found under the Windows adapter, so one run syncs your whole set (mouse, keyboard, headset) rather than one device at a time. Handle Classic and LE devices with separate field logic.

**Step 9 — Package it as a single script/program.** Wrap steps 3–7 into one program that takes no manual input: it finds the Windows partition, mounts it read-only if needed, parses the hive, translates, writes BlueZ, and restarts the service. Make it idempotent — running it when keys already match should change nothing. Log what it did so you can debug a bad boot.

## Running on boot, safety, and testing

**Making it run at boot (systemd).** The clean way on modern Linux is a **systemd service unit** that runs your program once, early, then exits. The concepts to learn:

- A *oneshot* service (`Type=oneshot`) runs a command to completion at boot rather than staying resident.
- Ordering directives (`Before=bluetooth.service`, or `After=` the mount of the Windows partition) tell systemd to run your sync *before* Bluetooth starts using its keys, and only after the partition it reads is available.
- `WantedBy=multi-user.target` (via `systemctl enable`) is what actually schedules it at every boot.

So the boot hook is: write a small unit file that points at your program, declare its ordering against `bluetooth.service` and the Windows mount, and enable it. That is the whole "runs on boot" requirement.

**Permissions.** The program touches `/var/lib/bluetooth` and mounts a partition — both require root. A boot-time systemd service already runs as root, so this is natural; if you test by hand, use `sudo`.

**Safety practices — do not skip these:**

- **Back up before writing.** Copy each BlueZ `info` file before editing so a bad translation is one restore away.
- **Mount Windows read-only.** You only ever *read* the Windows hive. Mounting it read-only makes it impossible to corrupt Windows by accident.
- **Disable Windows Fast Startup / hibernation.** A hibernated Windows leaves NTFS in a locked state and the hive can be stale. Fully shutting Windows down keeps the hive current and the partition mountable.
- **Make it idempotent and fail-safe.** If the Windows partition is missing or the hive can't be parsed, the program should log and exit cleanly, leaving BlueZ untouched — never write half-translated keys.
- **Handle the LE Secure Connections vs. legacy difference.** Check which pairing your device used; the set of fields to copy differs, and copying the wrong set produces a bond that looks present but fails to encrypt.

**Testing checklist:**

1. Run the program by hand under `sudo`, read its log, confirm it reports the devices and the keys it wrote.
2. Restart bluetooth, connect each device under Linux — no re-pair prompt.
3. Reboot into Windows — each device still connects (proves you didn't disturb Windows).
4. Reboot back to Linux without running anything manually — the boot service should have already synced; device connects immediately.
5. Deliberately re-pair one device under Windows, reboot to Linux — confirm the next boot re-syncs it (self-healing works).

**Common pitfalls, ranked by how often they bite:**

1. **Byte order** — far and away number one. If a device won't connect after sync, reverse the LTK/IRK bytes and retry before suspecting anything else.
2. **Hibernated Windows** — stale or unreadable hive; the sync silently uses old keys.
3. **Wrong ControlSet** — Windows has `ControlSet001`, `ControlSet002`, and a `CurrentControlSet` link; read the one that is actually current.
4. **LE vs. Classic confusion** — writing a Link Key into an LE `info` file, or vice versa; match the device type.
5. **Service ordering** — if the sync runs after Bluetooth has already loaded old keys, it appears to do nothing until the next reboot; fix the `Before=`/`After=` ordering.

**Reference.** This whole procedure is the automated form of the manual "dual boot pairing" process documented on the Arch Wiki's Bluetooth page and in many community write-ups — worth reading alongside this guide to see concrete field names for your specific device and stack versions.

