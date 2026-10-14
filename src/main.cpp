#include "HiveReader.h"
#include "BlueZInfo.h"
#include "LinkKey.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>

int main() {
    // --- config (hardcode for the first test) ---
    std::string systemHive = "/mnt/windows/Windows/System32/config/SYSTEM";
    std::string infoPath    = "/var/lib/bluetooth/68:C6:AC:65:42:A7/80:99:E7:AE:4B:86/info";
    std::string adapterMac  = "68C6AC6542A7";
    std::string deviceMac   = "8099e7ae4b86";

    // --- 1. read the key Windows has (raw bytes) ---
    HiveReader hive(systemHive);
    std::vector<std::uint8_t> winBytes = hive.readLinkKey(adapterMac, deviceMac);

    // --- 2. wrap in a LinkKey (need it as a fixed array) ---
    std::array<std::uint8_t, LinkKey::SIZE> arr{};
    std::copy(winBytes.begin(), winBytes.end(), arr.begin());
    LinkKey winKey(arr);

    // --- 3. read what BlueZ currently has ---
    BlueZInfo info(infoPath);
    auto currentHex = info.getLinkKey();

    // --- 4. print for the record ---
    std::printf("Windows key:        %s\n", winKey.toHex().c_str());
    std::printf("Windows reversed:   %s\n", winKey.reversed().toHex().c_str());
    std::printf("BlueZ current:      %s\n", currentHex ? currentHex->c_str() : "(none)");

    // --- 5. write it in ---
    // Stop bluetooth FIRST: a running bluetoothd flushes its cached key back to
    // the info file on shutdown, which would overwrite our edit. Stop, write, start.
    std::system("systemctl stop bluetooth");
    info.setLinkKey(winKey.toHex());   // reversed byte order
    info.save();
    std::system("systemctl start bluetooth");
    std::printf("Wrote reversed key to BlueZ (bluetooth stopped during write). Try connecting.\n");

    return 0;
}
