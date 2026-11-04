#!/usr/bin/env python3
"""Patch a pinned upstream KVXputer checkout with the native Nexus Neon chat app.

Never flashes hardware or writes to the user's upstream repository.
A shared server token may be injected in the transient build tree from an
environment variable, but is never stored in Git and never stored in NVS.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil

UPSTREAM_COMMIT = "c2ff56f154c35eb13fcfab845fb30dcecf372449"
EXPECTED_TOKEN_SHA256 = "a6ac994135dd6fea42ede78e658d6385b9b15b40d879b57e454666da265d920f"
ROOT = Path(__file__).resolve().parents[1]
OVERLAY = ROOT / "overlay" / "src" / "menu" / "nexus_neon"

def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    if new in text:
        return
    assert text.count(old) == 1, f"Unexpected upstream layout: {path} (anchor {old!r})"
    path.write_text(text.replace(old, new, 1), encoding="utf-8")

def install(kvx: Path, nexus: Path, token: str) -> None:
    kvx = kvx.resolve()
    nexus = nexus.resolve()
    assert (kvx / "src/root/ui/main_menu.h").is_file(), "KVXputer source not found"
    assert (kvx / "src/root/ui/menu_registry.cpp").is_file(), "KVXputer menu registry not found"
    assert (nexus / "nexus-adv/firmware/include/ca_roots.h").is_file(), "Nexus TLS CA roots not found"
    if token:
        assert hashlib.sha256(token.encode()).hexdigest() == EXPECTED_TOKEN_SHA256, (
            "Unexpected token value: refusing to compile with wrong/shared credentials"
        )

    dest = kvx / "src/menu/nexus_neon"
    dest.mkdir(parents=True, exist_ok=True)
    for source in OVERLAY.glob("*"):
        if source.suffix not in {".h", ".cpp"}:
            continue
        shutil.copyfile(source, dest / source.name)
    shutil.copyfile(nexus / "nexus-adv/firmware/include/ca_roots.h", dest / "ca_roots.h")

    # MQTT V0.2.3 (user version V5) is native, not a second setup()/loop().
    mqtt_source = ROOT / "overlay" / "src" / "menu" / "kvx_mqtt"
    mqtt_dest = kvx / "src" / "menu" / "kvx_mqtt"
    mqtt_dest.mkdir(parents=True, exist_ok=True)
    for source in mqtt_source.glob("*"):
        if source.suffix in {".h", ".cpp"}:
            shutil.copyfile(source, mqtt_dest / source.name)
    # Credentials are optional: /mqtt/config.ini on microSD is the normal
    # configuration source. Otherwise CI can inject them only at build time.
    mqtt_env = {
        "KVX_MQTT_HOST": os.environ.get("KVX_MQTT_HOST", ""),
        "KVX_MQTT_USER": os.environ.get("KVX_MQTT_USER", ""),
        "KVX_MQTT_PASSWORD": os.environ.get("KVX_MQTT_PASSWORD", ""),
        "KVX_MQTT_DEVICE": os.environ.get("KVX_MQTT_DEVICE", "a"),
        "KVX_MQTT_PEER": os.environ.get("KVX_MQTT_PEER", "b"),
    }
    mqtt_port = int(os.environ.get("KVX_MQTT_PORT", "8883"))
    assert 1 <= mqtt_port <= 65535, "Invalid MQTT TLS port"
    if any(mqtt_env[k] for k in ("KVX_MQTT_HOST", "KVX_MQTT_USER", "KVX_MQTT_PASSWORD")):
        assert all(mqtt_env[k] for k in ("KVX_MQTT_HOST", "KVX_MQTT_USER", "KVX_MQTT_PASSWORD")), "Incomplete MQTT build configuration"
    mqtt_secrets = (
        "#pragma once\n"
        "// Transient build configuration. Do not commit this generated file.\n"
        + "".join("#define " + k + " " + json.dumps(v) + "\n" for k, v in mqtt_env.items())
        + "#define KVX_MQTT_PORT " + str(mqtt_port) + "\n"
    )
    (mqtt_dest / "kvx_mqtt_private.h").write_text(mqtt_secrets, encoding="utf-8")
    # KVXputer retains its existing platformio.ini and all historical modules.
    # Add the single MQTT dependency without changing source filters or partitions.
    platformio_ini = kvx / "platformio.ini"
    pio_text = platformio_ini.read_text(encoding="utf-8")
    if "256dpi/MQTT@2.5.2" not in pio_text:
        dep_anchor = "\tmikalhart/TinyGPSPlus\n"
        assert pio_text.count(dep_anchor) == 1, "Unexpected upstream lib_deps"
        platformio_ini.write_text(pio_text.replace(dep_anchor,
             dep_anchor + "\t256dpi/MQTT@2.5.2\n", 1), encoding="utf-8")


    # A blank token permits typechecking/compiling, but network operations
    # intentionally fail closed. A real token is injected only in a transient
    # builder workspace, without committing or displaying it in CI logs.
    secret = (
        "#pragma once\n"
        "// Ephemeral build config; NEVER commit generated credential headers.\n"
        '#define NEXUS_NEON_SERVER_URL "https://nexus-messagerie.ilyaane-patel1.workers.dev"\n'
        "#define NEXUS_NEON_SHARED_TOKEN " + json.dumps(token, ensure_ascii=True) + "\n"
    )
    (dest / "nexus_neon_secrets.h").write_text(secret, encoding="utf-8")

    menu = kvx / "src/root/ui/main_menu.h"
    replace_once(menu,
        '#include "menu/discovery/discovery_menu.h"',
        '#include "menu/discovery/discovery_menu.h"\n#include "menu/nexus_neon/nexus_neon_menu.h"')
    replace_once(menu,
        '    DiscoveryMenu discoveryMenu;',
        '    DiscoveryMenu discoveryMenu;\n    NexusNeonMenu nexusNeonMenu;')
    replace_once(menu,
        '#include "menu/nexus_neon/nexus_neon_menu.h"',
        '#include "menu/nexus_neon/nexus_neon_menu.h"\n#include "menu/kvx_mqtt/kvx_mqtt_menu.h"')
    replace_once(menu,
        '    NexusNeonMenu nexusNeonMenu;',
        '    NexusNeonMenu nexusNeonMenu;\n    KvxMqttMenu kvxMqttMenu;')

    reg = kvx / "src/root/ui/menu_registry.cpp"
    old = '    {"config",'
    text = reg.read_text(encoding="utf-8")
    assert text.count(old) == 1, "Unexpected KVX menu layout"
    # Append both modules after Config, without removing any existing tile.
    line_end = text.index("\n", text.index(old)) + 1
    entry = (
        '#if defined(HAS_KEYBOARD)\n'
        '    {"nexus-neon", "Nexus Neon", &mainMenu.nexusNeonMenu, MENU_FLAG_NONE},\n'
        '#endif\n'
    )
    mqtt_entry = (
        '#if defined(HAS_KEYBOARD)\n'
        '    {"kvx-mqtt", "MQTT", &mainMenu.kvxMqttMenu, MENU_FLAG_NONE},\n'
        '#endif\n'
    )
    # On the 240x135 display the last KVX page has three columns/two rows:
    # Nexus is position 21, MQTT position 22 (row directly below, left side).
    if entry not in text:
        reg.write_text(text[:line_end] + entry + mqtt_entry + text[line_end:], encoding="utf-8")
    elif mqtt_entry not in text:
        at = text.index(entry) + len(entry)
        reg.write_text(text[:at] + mqtt_entry + text[at:], encoding="utf-8")

    # It is one native app in KVXputer. Never include the separate
    # Nexus .bin, a secondary bootloader or a second setup()/loop().
    print("Installed: native Nexus V10 text-only + MQTT V5; 2 menu tiles")
    print("Compiled token injected:", "yes" if token else "no (fails closed)")

if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--kvx", type=Path, required=True)
    p.add_argument("--nexus", type=Path, required=True)
    args = p.parse_args()
    install(args.kvx, args.nexus, os.environ.get("NEXUS_SHARED_TOKEN", ""))
