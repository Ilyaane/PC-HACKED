#!/usr/bin/env python3
"""Prepare, collect and verify the three Nexus Neon feature profiles."""
import hashlib
import json
import os
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "firmware" / ".pio" / "build" / "cardputer-adv"
OUT = ROOT / "dist"
SERVER = "https://nexus-messagerie.ilyaane-patel1.workers.dev"
EXPECTED_TOKEN_SHA256 = "a6ac994135dd6fea42ede78e658d6385b9b15b40d879b57e454666da265d920f"
PROFILES = {
    "morse-voice": (1, 1, "Nexus_Néon.bin", "Nexus_Néon_Full.bin"),
    "voice": (0, 1, "Nexus_Néon_No_Morse.bin", "Nexus_Néon_Full_No_Morse.bin"),
    "text": (0, 0, "Nexus_Néon_No_Morse_No_Micro.bin",
                    "Nexus_Néon_No_Morse_No_Micro_Full.bin"),
}
FULL_NAME = "Nexus_Néon_6_Firmwares.zip"

def sha(data):
    return hashlib.sha256(data).hexdigest()

def prepare(profile):
    morse, voice, _, _ = PROFILES[profile]
    token = os.getenv("NEXUS_SHARED_TOKEN", "")
    if not token:
        raise SystemExit("NEXUS_SHARED_TOKEN secret missing: refusing unconfigured firmware")
    if sha(token.encode("utf-8")) != EXPECTED_TOKEN_SHA256:
        raise SystemExit("NEXUS_SHARED_TOKEN does not match the requested shared code; no binary generated")
    hdr = (ROOT / "firmware" / "include" / "nexus_neon_config.h")
    hdr.write_text(
        "#pragma once\n"
        f"#define NEXUS_NEON_MORSE {morse}\n"
        f"#define NEXUS_NEON_VOICE {voice}\n"
        f"#define NEXUS_FIXED_SERVER {json.dumps(SERVER)}\n"
        f"#define NEXUS_FIXED_TOKEN {json.dumps(token)}\n",
        encoding="utf-8",
    )
    print("Generated profile", profile, "Morse", morse, "Voice", voice,
          "with verified shared code (not printed)")

def collect(profile):
    morse, voice, app_name, full_name = PROFILES[profile]
    OUT.mkdir(exist_ok=True)
    inputs = {
        app_name: BUILD / "firmware.bin",
        full_name: BUILD / "NEXUS_ADV_full_flash_0x0.bin",
    }
    for name, src in inputs.items():
        data = src.read_bytes()
        assert len(data) > 100_000, (name, len(data))
        assert len(data) <= (8 * 1024 * 1024 if "Full" in name else 3342336)
        assert data[0] == 0xE9, f"Invalid ESP32-S3 image: {name}"
        (OUT / name).write_bytes(data)
        assert (OUT / name).read_bytes() == data
        print(name, len(data), sha(data))

def package():
    entries=[]
    for profile, (morse, voice, app, full) in PROFILES.items():
        for name, offset in [(app,"0x10000"), (full,"0x0")]:
            p = OUT / name
            data = p.read_bytes()
            assert data[0] == 0xE9
            entries.append(dict(name=name, profile=profile, morse=bool(morse),
                                voice=bool(voice), flash_offset=offset,
                                size_bytes=len(data), sha256=sha(data)))
    assert len(entries)==6 and len({e["name"] for e in entries})==6
    mains = [e for e in entries if e["flash_offset"]=="0x10000"]
    assert len({e["sha256"] for e in mains})==3, "Feature profiles compiled identically"
    by = {x["profile"]:x["size_bytes"] for x in mains}
    if not (by["voice"] < by["morse-voice"] and by["text"] < by["voice"]):
        print("WARNING: non-monotonic firmware size; inspect feature stripping", by)
    readme = """NEXUS NEON: SIX BINAIRES / TROIS PROFILS
Pour Cardputer ADV (ESP32-S3), pas Cardputer classique / M5Stick.
Version Launcher : application 0x10000, pour M5Launcher.
Version Full : image complete 0x0, a installer uniquement avec flasheur compatible.
NE PAS installer un fichier Full comme une application dans M5Launcher.

Serveur HTTPS et code partagé integres a la compilation.
Dans le menu Systeme > Parametres, configurer le correspondant :
- Cardputer Alice -> destinataire bob
- Cardputer Bob -> destinataire alice
Wi-Fi et correspondant restent sauvegardes en NVS.
Morse/Vocaux uniquement dans les versions correspondantes.
MicroSD requise pour enregistrer et conserver les vocaux.

ATTENTION : code partagé extractible d'un firmware ; ne pas publier les .bin.
Tests CI : API du serveur, compilation PlatformIO et structure des images.
Non testes : microphone ES8311 reel, transfert vocal, affichage/Touch sur
materiel, Wifi reel, M5Launcher et echanges bout-en-bout entre deux appareils.
La version Full peut remplacer le lanceur et effacer des donnees NVS.
"""
    manifest=json.dumps({"project":"Nexus Neon","server":SERVER,
                         "target":"M5Stack Cardputer ADV","images":entries},
                        ensure_ascii=False,indent=2)+"\n"
    zip_path = OUT / FULL_NAME
    with zipfile.ZipFile(zip_path,"w",zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for e in entries: z.write(OUT/e["name"],e["name"])
        z.writestr("LIRE_AVANT_FLASH.txt",readme)
        z.writestr("MANIFEST_SHA256.json",manifest)
    with zipfile.ZipFile(zip_path) as z:
        assert z.testzip() is None
        assert len(z.namelist())==8
        for e in entries:
            assert sha(z.read(e["name"]))==e["sha256"]
    print("FINAL ZIP",zip_path.name,zip_path.stat().st_size,
          "bytes; 6 complete images checked")
    print("IMAGE SIZES",by)

if __name__=="__main__":
    if len(sys.argv)<2: raise SystemExit("usage: neon_build.py prepare PROFILE|collect PROFILE|package")
    cmd = sys.argv[1]
    if cmd == "prepare": prepare(sys.argv[2])
    elif cmd == "collect": collect(sys.argv[2])
    elif cmd == "package": package()
    else: raise SystemExit("Unknown command")
