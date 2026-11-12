#!/usr/bin/env python3
"""Keep passive scanner storage scoped to its session and clean up on Home."""
from pathlib import Path
import sys


def replace(text, old, new):
    assert text.count(old) == 1, f"Scanner patch anchor mismatch: {old[:80]!r}"
    return text.replace(old, new, 1)


root = Path(sys.argv[1])
scanner = root / "src/root/ui/scanner_list.cpp"
s = scanner.read_text()
s = replace(s, "#include <cstring>\n", "#include <cstring>\n#include <new>\n")
s = replace(s, "ScannerAdvRaw g_advRing[kAdvRing];", "ScannerAdvRaw *g_advRing = nullptr;")
s = replace(s, "void captureAdv(const NimBLEAdvertisedDevice *d) {", """void releaseAdvRing() {
    portENTER_CRITICAL(&g_advMux);
    auto *ring = g_advRing;
    g_advRing = nullptr;
    g_advHead = g_advTail = 0;
    portEXIT_CRITICAL(&g_advMux);
    delete[] ring;
}

void captureAdv(const NimBLEAdvertisedDevice *d) {""")
s = replace(s, "    const uint8_t *a = d->getAddress().getVal();",
            "    const auto address = d->getAddress();\n    const uint8_t *a = address.getVal();")
s = replace(s, """    g_advCount.fetch_add(1, std::memory_order_relaxed);
    portENTER_CRITICAL(&g_advMux);
    uint16_t next""", """    portENTER_CRITICAL(&g_advMux);
    if (!g_advRing) {
        portEXIT_CRITICAL(&g_advMux);
        return;
    }
    g_advCount.fetch_add(1, std::memory_order_relaxed);
    uint16_t next""")
s = replace(s, """    portENTER_CRITICAL(&g_advMux);
    g_advHead = 0;
    g_advTail = 0;
    portEXIT_CRITICAL(&g_advMux);
    g_advCount.store(0, std::memory_order_relaxed);

    if (WiFi""", """    scannerBleTeardown(true);
    g_advCount.store(0, std::memory_order_relaxed);

    if (WiFi""")
s = replace(s, """    NimBLEDevice::init("");
    delay(150);

    NimBLEScan *scan = NimBLEDevice::getScan();
    if (!scan) {
        uiRamLeaveHeavy();
        return nullptr;
    }""", """    auto *ring = new (std::nothrow) ScannerAdvRaw[kAdvRing];
    if (!ring) {
        uiRamLeaveHeavy();
        return nullptr;
    }
    portENTER_CRITICAL(&g_advMux);
    g_advRing = ring;
    portEXIT_CRITICAL(&g_advMux);

    if (!NimBLEDevice::init("")) {
        scannerBleTeardown(true);
        return nullptr;
    }
    delay(150);

    NimBLEScan *scan = NimBLEDevice::getScan();
    if (!scan) {
        scannerBleTeardown(true);
        return nullptr;
    }""")
s = replace(s, """    if (!(ok || scan->isScanning())) {
        NimBLEDevice::deinit(true);
        uiRamLeaveHeavy();
        return nullptr;
    }""", """    if (!(ok || scan->isScanning())) {
        scannerBleTeardown(true);
        return nullptr;
    }""")
s = replace(s, """    ScannerAdvRaw tmp[kAdvRing];
    size_t n = 0;
    portENTER_CRITICAL(&g_advMux);
    while (g_advTail != g_advHead && n < kAdvRing) {
        tmp[n++] = g_advRing[g_advTail];
        g_advTail = (uint16_t)((g_advTail + 1) % kAdvRing);
    }
    portEXIT_CRITICAL(&g_advMux);

    std::vector<ScannerAdvSnap> out;
    out.reserve(n);
    for (size_t i = 0; i < n; i++) out.push_back(snapFromRaw(tmp[i]));""", """    portENTER_CRITICAL(&g_advMux);
    const size_t pending = (g_advHead + kAdvRing - g_advTail) % kAdvRing;
    portEXIT_CRITICAL(&g_advMux);

    std::vector<ScannerAdvSnap> out;
    out.reserve(pending);
    for (size_t i = 0; i < pending; ++i) {
        ScannerAdvRaw raw;
        portENTER_CRITICAL(&g_advMux);
        const bool available = g_advRing && g_advTail != g_advHead;
        if (available) {
            raw = g_advRing[g_advTail];
            g_advTail = (g_advTail + 1) % kAdvRing;
        }
        portEXIT_CRITICAL(&g_advMux);
        if (!available) break;
        out.push_back(snapFromRaw(raw));
    }""")
s = replace(s, """    portENTER_CRITICAL(&g_advMux);
    g_advHead = 0;
    g_advTail = 0;
    portEXIT_CRITICAL(&g_advMux);
    NimBLEScan *scan""", """    if (!g_advRing) return; // No scanner session or UI lease to release.
    releaseAdvRing();
    NimBLEScan *scan""")
scanner.write_text(s)

suite = root / "src/menu/ble/BLE_Suite.cpp"
s = suite.read_text()
start = s.index("void BleSuiteMenu() {")
end = s.index("// Attack Execution with Target Selection", start)
menu = s[start:end]
i = menu.index("        if (check(EscPress)) {")
j = menu.index("        if (check(PrevPress))", i)
menu = menu[:i] + "        if (check(EscPress)) break;\n" + menu[j:]
menu = replace(menu, "    while (true) {", "    while (!forceHome) {")
menu = replace(menu, "        delay(50);\n    }\n}", """        delay(50);
    }

    BLEStateManager::deinitBLE(true);
    scannerData.clear();
    g_selectedDevice.address = "";
    g_selectedDevice.name = "";
}""")
suite.write_text(s[:start] + menu + s[end:])
