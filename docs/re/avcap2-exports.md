# libSceAvcap2 — Exports, IDR-Anforderung, Audio, Bitrate (FW 13.00)

Statische Analyse (kein Zugriff auf die Konsole). Eingaben: entschlüsselte ELFs `libSceAvcap2`, `SceRemotePlay`,
`SceAvCapture` (= `SceAvCaptureManager`, der IPMI-Server hinter Avcap2) und `SceGameLiveStreaming`, Build J03851548.
Alle Offsets sind modulrelative Offsets (= vaddr) dieser Firmware. Es ist kein Sony-Code im Repository.

Konfidenz: **sicher** = direkt aus Code/Hash belegt, **wahrscheinlich** = mehrere konsistente Indizien,
**Vermutung** = plausibel, aber nicht belegt (braucht Live-Test).

## 1. Methodik und Reproduzierbarkeit

Skripte in `tools/re/` (Python, lesen nur `.cache/re/*.elf`):

| Skript | Zweck |
|---|---|
| `elfdump.py` | ELF/Dyn-Parser, NID-Funktion `nid(name)`, Funktionsstarts aus `.eh_frame_hdr` |
| `disasm.py`, `funcdump.py`, `callctx.py` | Disassembly (capstone) mit PLT/GOT-Auflösung auf `NID#lib#mod` bzw. bekannte Namen |
| `ipcstubs.py` | wertet die 47 IPMI-Stubs von libSceAvcap2 aus (Methoden-Id, Ein-/Ausgabepuffer) |
| `export_table.py` | gibt die Export-Tabelle (Abschnitt 3) aus |
| `consumers.py` | Imports + Aufrufstellen der Consumer-ELFs |
| `bruteforce_names.py`, `bruteforce2.py`, `bruteforce3.py` | NID-Namenssuche (Wortlisten × Verben) |
| `avcap2_names.txt`, `common_names.txt`, `consumer_names.txt` | aufgelöste bzw. Kandidaten-Namen |

NID-Formel (verifiziert gegen `prospero-nid` aus `prosperosunshine-sdk:v0.45`, u. a. `sceAvcap2Initialize` →
`svzPXluOz8U`, `sceAvcap2RequestKeyFrame` → `YaXDckFygvw`): `sha1(name + key)[:8]` **byte-umgekehrt** (little-endian
u64) → Base64, 11 Zeichen, `/` → `-`. Ohne die Umkehrung stimmt kein Wert.

Import-Namen in den Consumern lauten `NID#lib#mod` mit Base64-Ids (A=0, B=1, …). Für libSceAvcap2 ist das in
`SceRemotePlay` `#h#d` (lib 33, mod 29) und in `SceGameLiveStreaming` `#P#P`; ID-Zuordnung über die SCE-Dyn-Tags
`0x61000045` (Modul) / `0x61000049` (Bibliothek).

Namenssuche: ~224 k + 16,4 M + 238 M Kandidaten (`sceAvcap2` + Verb + 1–3 Wörter). Bei 64-Bit-Hash sind Treffer
praktisch kollisionsfrei, daher gelten **alle 39 aufgelösten Namen als sicher**. 20 von 59 NIDs sind unbekannt.

## 2. Architektur (sicher, aus Disassembly)

* libSceAvcap2 ist ein dünner **IPMI-Client** (`SceAvcap2Ipc`, Client-Klasse `SceAvcap2IpcClient`) zum Prozess
  `SceAvCapture`. Jede Export-Funktion nimmt einen globalen Rwlock (`SceAvcap2LibRes_Rwlk`; Wrapper `0x32E0` bei
  Initialize/Terminate/Open*/Close, `0x3120` bei den übrigen), holt den Singleton (`0x2C10`) und ruft einen von 47 Stubs (`0x43E0`…`0x5B50`), die
  `client->vtbl[+0x58](methodId, in[], nIn, &rc, out[], nOut)` aufrufen. Methoden-Ids: `0xA70000 + 0 … 0x2E`.
* **Handle** = Zeiger auf ein 0xA8-Byte-Objekt: `[+0]` u8 = 1 (gültig), `[+4]` u32 = Kanal-Id, `[+8]` eingebettetes Reader-Objekt (Zeiger auf den gemappten
  Shared-Memory-Ring bei `[+0x10]`, Event-Flag `/evf_ucap_%06x`, Leseposition `[+0x20]`). `Read*` lesen **direkt aus dem Ring**
  (kein IPC); `Open*/Start/Stop/Set*` sind IPC.
* **Server**: Dispatch in `SceAvCapture` `0xF990` (Sprungtabelle `0x26614`, Index = Methode − 0xA70000) → dünne
  Handler `0xFE50…0x10910` → Manager-Methoden `0xA70…0x2140` → virtuelle Methoden des Kanal-/Codec-Objekts. Der
  Besitz des Kanals wird über eine Bitmaske des Clients geprüft (sonst `0x81950001`).
* Videopfad des Servers: `sceVencCreateEncoder/StartEncode/StopEncode/SetConfig/GetConfig/SetRateControlConfig/
  SetPictureConfig/SetReferenceFrameInvalidationConfig/QueryHeader/…` (`libSceVenc`), Klassen `AvcapVideoScanIn`,
  `AvcapVideoCodecAvc` (Vtable `0x283D0`), `AvcapVideoCodecHevc`.
* Audiopfad des Servers: Geräte `AudioNormal/AudioAout/AudioSecond/PadSpk/Haptic`, Encoder `libSceOpusCeltEnc` und
  `libSceM4aacEnc`, Rings `shm_acap.{full,share,restrict,aux,…}`.

### IPC-Methode → Export → Serverwirkung (Auswahl, sicher)

| IPC | Export | Server-Vtable (Codec) | Venc-Aufruf |
|---|---|---|---|
| 0xA7000D | `RequestKeyFrame(h)` | +0x88 → `0x3050` | `sceVencSetPictureConfig{0x10, type=1}` |
| 0xA70018 | `RequestSkipFrame(h)` | +0xB0 → `0x33D0` | `sceVencSetPictureConfig{0x10, type=7}` |
| 0xA70015 | `B+6003nL0AI(h, 0\|1)` | +0xA0 → `0x3230` | `sceVencSetPictureConfig{0x10, type=0, wert=1\|2}` |
| 0xA7001A | `SetInvalidFrame(h, u32)` | +0xC0 → `0x3890` | `sceVencSetReferenceFrameInvalidationConfig{0x10, u8@8=1, u32@0xC=wert}` |
| 0xA7001B | `SetVideoBitrate(h, blk)` | +0xC8 → `0x39E0` | `sceVencSetRateControlConfig` |
| 0xA70019 | `SetVideoEncodeConfig(h, blk)` | +0xB8 → `0x3520` | `sceVencSetConfig` |

## 3. Export-Tabelle (59 Exports, 39 Namen aufgelöst)

`h` = `sceAvcap2Handle*` (Zeiger), `h**` = Out-Parameter. Spalte „IPC“ = IPMI-Methoden-Id (`-` = kein IPC).
Signaturen aus den Wrappern abgeleitet; Argumente, die nur als 4/8-Byte-Puffer sichtbar sind, sind mit `?` markiert.

| NID | Offset | Name | IPC-Methode | Signatur / Bemerkung |
|---|---|---|---|---|
| `svzPXluOz8U` | 0xD0 | `sceAvcap2Initialize` | - | `()` globale Init, exklusiver Lock; kein IPC |
| `gojqghDU+1Y` | 0x130 | `sceAvcap2Terminate` | - | `()` globale Terminierung |
| `3nHpE7Dp5SE` | 0x190 | `sceAvcap2OpenAudio` | 0xa70000 | `(h**, cfg*0x38, u8)` Audio-Kanal oeffnen; IPC-Nutzlast `{u8, cfg[0x38]}` |
| `4jwUgirLFrA` | 0x360 | `sceAvcap2OpenVideo` | 0xa70001 | `(h**, cfg*0x2E0, u8)`; IPC `{u64 (Default 0x5000000000), u8 flag}` + cfg[0x2E0]; Antwort: Kanal-Id, u8, u32 |
| `Tzk7-W5mp0A` | 0x730 | `sceAvcap2OpenHaptic` | 0xa70002 | `(h**, cfg*0x18, u8)` Haptik (Controller) |
| `ijHDdN-xBf4` | 0x8C0 | `sceAvcap2OpenPadSpk` | 0xa70003 | `(h**, cfg*0x18, u8)` Controller-Lautsprecher |
| `tR7gPe1i8hw` | 0xA50 | `sceAvcap2Close` | 0xa70004 | `(h)` schliesst Kanal, gibt Handle frei |
| `8CGNCwBsItI` | 0xB40 | `sceAvcap2Start` | 0xa70005 | `(h)` |
| `Z5dKK0xnQ8g` | 0xBD0 | `sceAvcap2Stop` | 0xa70006 | `(h)` |
| `WhsHggqdwcg` | 0xC60 | `sceAvcap2ReadAudio` | - | `(h, dst, cap, hdr*(>=0x10, hdr[0]=0x10), flags)` Ring-Reader (shm), flags&1 = nonblocking; Rueckgabe = Bytes |
| `pN0hIQZsMzc` | 0xDD0 | `sceAvcap2ReadVideo` | - | `(h, dst, cap, desc*0x70, flags)` AVC-only, kopiert Meta nach desc |
| `jkv9MlN7gvs` | 0xF00 | `sceAvcap2ReadVideo2` | - | 11-Byte-Thunk: `ReadVideo3(h,dst,cap,desc,NULL,flags)` |
| `TyUnv8GHF0o` | 0xF10 | `sceAvcap2ReadVideo3` | - | `(h, dst, cap, desc*, extra*0x2DC oder NULL, flags)` neuere Variante, Desc >= 0x88 |
| `1Xp85wUaVTE` | 0x10E0 | `sceAvcap2ReadHaptic` | - | `(h, buffer, hdr*, flags)`, Paket ab `buffer+8`, höchstens 0x800, Header 0x18 (`haptics.md`) |
| `8j-pBJmjL7M` | 0x1170 | `sceAvcap2ReadPadSpk` | - | `(h, dst, cap, hdr*, flags)`, Header 0x20 |
| `ysFbdZrSCMM` | 0x1200 | `sceAvcap2Select` | - | `(n, h*[n], u64* readyMask)` wartet (Event-Flag) bis mind. ein Kanal Daten hat; kein IPC |
| `S4nvYDibKBw` | 0x1340 | unbekannt | - | `(h, u64*)` Event-Flag-Poll (kein IPC, sceKernelWaitEventFlag Timeout 0) |
| `BjHu5q5-6NI` | 0x13D0 | unbekannt | - | `(h)` setzt Event-Flag Bits 3 (weckt Warter); kein IPC |
| `NeKlrUv+lbY` | 0x1450 | `sceAvcap2GetNonVclNalUnitStream` | 0xa70007 | `(h, out*0x1A8)` SPS/PPS/(VPS) eines offenen Kanals |
| `VAFgNc0Pfp8` | 0x14F0 | `sceAvcap2GetNonVclNalUnitStream2` | 0xa70008 | `(in*0x28, out*0x1A8)` SPS/PPS ohne offenen Kanal, nur aus Parametern (Aufloesung/FPS/Codec) |
| `8HjqtZXLvek` | 0x1580 | `sceAvcap2GetNonVclNalUnitStream3` | 0xa70009 | `(in*0x178, out*0x1A8)` wie oben, groessere Eingabe |
| `aAbMwQwWHyM` | 0x1610 | unbekannt | 0xa7000a | `(h?, arr*, n)` Array aus n x 16 B (Bildbereiche?); genutzt von RemotePlay und GLS |
| `Cdf6VsJgvaY` | 0x16B0 | `sceAvcap2SetImagePaste` | 0xa7000b | `(h, {16 B})` Image-Paste Konfiguration (-> Venc ImagePaste) |
| `RW88AiwmL+Y` | 0x1750 | unbekannt | 0xa7000c | `(out*0x20)` Statusstruktur, RemotePlay ruft sie vor OpenVideo auf |
| `YaXDckFygvw` | 0x17D0 | `sceAvcap2RequestKeyFrame` | 0xa7000d | `(h)` **IDR/Keyframe anfordern** |
| `DHFDHdKAr9U` | 0x1860 | `sceAvcap2GetVideoOutMode` | 0xa7000e | `(u32* flags)` Ausgabemodus (0x13C beobachtet) |
| `HAeczEM-m7c` | 0x18E0 | unbekannt | 0xa7000f | `(h?, u32)` unbekannt |
| `88YEF+XHfRY` | 0x1970 | unbekannt | 0xa70010 | `(h, out*0x10)` unbekannt |
| `Dht3tcbRoEY` | 0x1A10 | unbekannt | 0xa70011 | `(in*0x90, out u64 + ?)` unbekannt |
| `CYgd06bO5q0` | 0x1AB0 | `sceAvcap2AllocateVideoEncoderMemory` | 0xa70012 | `(u64, ...,out*0x20)` Venc-Speicher reservieren (nur Memory-Mode 1/2) |
| `yN0w4tWivqs` | 0x1B90 | unbekannt | 0xa70013 | `(in*0x90, out*0x20, out*0x20)` reserviert den Encoder-Speicher vor `OpenVideo` (Memory Mode 1), nötig für HEVC (`docs/re/hevc.md`) |
| `96UQhNnBlxc` | 0x1C60 | `sceAvcap2FreeEncoderVideoMemory` | 0xa70014 | `(buf*0x20, buf*0x20)` Venc-Speicher freigeben (Teardown vor Terminate) |
| `B+6003nL0AI` | 0x1D30 | unbekannt | 0xa70015 | `(h, u32 0 oder 1)` Server: sceVencSetPictureConfig{type 0, Wert 1 oder 2} |
| `j7ClJpPRVQI` | 0x1DC0 | `sceAvcap2GetImagePasteMemory` | - | `(h, void** addr, size_t* len)` mapped Bereich, kein IPC |
| `NEY5WpUI0Io` | 0x1E70 | `sceAvcap2ChangeScanInResolution` | 0xa70016 | `(h, u32)` Server-Vtable +0xA8 |
| `117ggZLcGc0` | 0x1F00 | `sceAvcap2GetVideoEncoderCapability` | 0xa70017 | `(u32* out)` Encoder-Faehigkeiten |
| `Ez6n05jUI1U` | 0x1F80 | `sceAvcap2RequestSkipFrame` | 0xa70018 | `(h)` Server: PictureConfig type 7 |
| `-NotZDC2pQc` | 0x2010 | `sceAvcap2SetVideoEncodeConfig` | 0xa70019 | `(h, blk*0xE0)` -> sceVencSetConfig; Block = Config+0xB8 |
| `fLzv9I4mHF0` | 0x20B0 | `sceAvcap2SetInvalidFrame` | 0xa7001a | `(h, u32 frameIdx)` -> sceVencSetReferenceFrameInvalidationConfig |
| `iy6ZcsdeoMk` | 0x2140 | `sceAvcap2SetVideoBitrate` | 0xa7001b | `(h, {u64 0x10, u32 avg, u32 peak}*)` -> sceVencSetRateControlConfig |
| `mYgFfxPwEjY` | 0x21E0 | `sceAvcap2GetAudioOutVolume` | 0xa7001c | `(h?, u32* out)` |
| `4S4mGtzLmyI` | 0x2280 | `sceAvcap2SetAudioOutVolume` | 0xa7001d | `(h?, u32)` |
| `41+5ixaiTj0` | 0x2310 | `sceAvcap2GetMicVolume` | 0xa7001e | `(h?, u32* out)` |
| `BPgql5PsMKo` | 0x23B0 | `sceAvcap2SetMicVolume` | 0xa7001f | `(h?, u32)` |
| `32gujAUoIMc` | 0x2440 | unbekannt | 0xa7002c | `(u32 in, u32* out)` unbekannt |
| `k7gz608olKM` | 0x24D0 | unbekannt | 0xa7002d | `(u32)` unbekannt |
| `7KBaem5GyBQ` | 0x2540 | unbekannt | 0xa7002e | `({0x18}, arr n x16)` unbekannt |
| `X8c-f8LgzHo` | 0x25F0 | `sceAvcap2MapMemory` | 0xa70020 | `(h, ...)` Speicher in Kanal mappen (ext. Frame-Area) |
| `2GC2UnBJHIA` | 0x26A0 | unbekannt | 0xa70021 | `(u32 in, u32* out)` unbekannt |
| `0W6GB-ecnK0` | 0x2740 | unbekannt | 0xa70022 | `(u32)` unbekannt |
| `2Lv-4HVkqO4` | 0x27C0 | unbekannt | 0xa70023 | `(u32)` unbekannt |
| `ImOtOnPjgio` | 0x2840 | `sceAvcap2SetPrivacyImage` | 0xa70024 | `(img*, n)` Privacy-Bild setzen |
| `4hkuDvkGu7U` | 0x28E0 | `sceAvcap2DisableScanInEncode` | 0xa70025 | `()` globaler Schalter |
| `HrGLkn9o4MY` | 0x2950 | `sceAvcap2EnableScanInEncode` | 0xa70026 | `()` globaler Schalter |
| `VNRuuZQx7gE` | 0x29C0 | unbekannt | 0xa70027 | `(u32* out)` unbekannt |
| `pEm2jVo44dE` | 0x2A40 | unbekannt | 0xa70028 | `(u8* out)` unbekannt |
| `r9BQzY0IvxA` | 0x2AC0 | unbekannt | 0xa70029 | `(u32)` unbekannt |
| `GBx9f5BmDvU` | 0x2B30 | unbekannt | 0xa7002a | `(u32)` unbekannt |
| `+OspdnZ-8mc` | 0x2BA0 | unbekannt | 0xa7002b | `(u8)` unbekannt |

Zusätzliche sichere Fakten zur Tabelle:

* Auflösungs-Enum (`cfg+0x18`, aus der Eingabe-Konvertierung von `SceRemotePlay` bei `0x70E60`):
  1 = 640×360, 2 = 960×540, 3 = 1280×720, **4 = 1920×1080**, 5 = 2560×1440, 6 = 3840×2160 (Validator: ≤ 6).
* Timing-Enum (`cfg+0x1C`): **0 = 30 fps, 1 = 60 fps** (RemotePlay setzt `fps != 30`; die Konvertierung bildet
  `0x1E → 0`, `0x3C → 1` ab). Validator erlaubt 0…2 (2 vermutlich höhere Rate, unbelegt).
* `cfg+0x10` Kanalart ≤ 2 (Server-Factory: 0 = `AvcapVideoScanIn`, Objektgröße 0x828; 1 = 0x718; 2 = 0x720),
  `cfg+0x14` Purpose < 8 (0 VideoRecording, 1 GameLiveStreaming, 2 Remote Play, 3 SocialScreen, 4 Shareplay, 5 ShareScreen, 6 VideoChat, 7 PS Now;
  Wirkung auf HEVC: `docs/re/hevc.md`), `cfg+0x24` Codec 0…2, `cfg+0x20` ≤ 1 (Vermutung: HDR/10 Bit).
* `GetNonVclNalUnitStream2(in*0x28, out*0x1A8)` erzeugt SPS/PPS **ohne offenen Kanal** aus Parametern (RemotePlay
  nutzt nur diese Variante, nie die Handle-Variante). Die 0x28-Eingabe entspricht dem Kopfblock `cfg+0x08…0x30`
  (`{0x28, _, kind, purpose, res, timing, flag, codec, …}`).
* `ReadVideo2/3` (neuer): Descriptor ≥ 0x88 Byte, optional 0x2DC Byte Zusatzdaten; `ReadVideo` (alt) kopiert
  `intern+0x10…0x80` ins 0x70-Descriptor und legt die Größe in `desc+0x5C` ab (nur AVC; sonst `0x8195000A`).
* Fehlercodes (aus den Wrappern): `0x81950001` ungültiges Argument/Handle, `0x81950002` kein Frame (nonblocking),
  `0x81950003` Ring nicht gemappt, `0x81950004` Overrun, `0x81950008` Server-Objekt fehlt/geschlossen,
  `0x81950009` Stream beendet/entzogen, `0x8195000A/D/E` Codec-/Größen-/Flag-Fehler.

## 4. R1 — IDR/Keyframe auf Anforderung

**Antwort: Ja. `sceAvcap2RequestKeyFrame` (NID `YaXDckFygvw`, Offset `0x17D0`).** Existenz, Name, Signatur:
**sicher**. Wirkung („nächster Frame ist ein IDR“): **wahrscheinlich** (Name + Serverpfad + Consumer-Nutzung, aber noch nicht live
beobachtet).

Belege:

1. Name per NID-Hash eindeutig (`sceAvcap2RequestKeyFrame` → `YaXDckFygvw`, mit `prospero-nid` gegengeprüft).
2. Export `0x17D0`: Lock `0x3120`; `h != NULL && h[0] == 1`, sonst `0x81950001`; dann Stub `0x4B10`
   (IPC `0xA7000D`, Nutzlast nur `u32 Kanal-Id = h[+4]`, keine Ausgabepuffer). Rückgabe = IPC-Status (≥ 0 OK).
   Aufruf: `int rc = sceAvcap2RequestKeyFrame(handle);`
3. Server: Handler → Manager `0x1520` → `channel->vtbl[+0x88]` (`0x3050`) → `sceVencSetPictureConfig({size 0x10,
   type 1})` unter dem Venc-Event-Flag-Lock. Parallel `RequestSkipFrame` (type 7).
4. Consumer `SceRemotePlay`: Wrapper `0x71140` (Mutex `this+0x28`, `RequestKeyFrame([this+0x38])`, Ergebnis `rc ≥ 0`).
   Er wird nur aus der Video-Pump `0x6CBF0` (Aufruf `0x6CEEF`) gerufen, wenn `this[0x158] == 1` (IDR angefordert,
   gesetzt von `0x6D800`, bzw. `0x101` von `0x6DFD0`) und entweder `this[0x159] != 0` (sofort) oder seit der
   Anforderung > 1000 ms vergangen sind (Zeitfunktion `0x6EE40`, Konstante 1000.0 bei `0x5246A0`). Das ist der
   Verlust-/Wiederherstellungspfad der Remote-Play-Session.
5. **Zweiter Pfad (RFI)**, ebenfalls in der Pump: `this[0x150] == 1` → `0x711B0` → **`sceAvcap2SetInvalidFrame(h, u32)`**
   (NID `fLzv9I4mHF0`, `0x20B0`, IPC `0xA7001A`). `0x6D890(first_lost, current)` merkt den kleinsten verlorenen
   Frame-Index (`this[0x154]`) und begrenzt ihn auf den Index des letzten IDR (`this[0x1E0]`). Server:
   `sceVencSetReferenceFrameInvalidationConfig({0x10, u8@8 = 1, u32@0xC = frameIdx})`. Der Index ist der
   encoder-seitige Frame-Zähler aus dem ReadVideo-Descriptor (`desc+0x20`; `desc+0x1C == 1` markiert IDR).
   Konfidenz **wahrscheinlich**: in der Pump gilt `this[0x1E0] = desc+0x20` nur, wenn `desc+0x1C == 1`.
6. Es gibt **kein** OpenVideo-Feld, das IDRs erzwingt; das Intervall ist Encoder-Default (siehe R3).

Vorschlag Live-Test R1 (im vorhandenen Capture-Loop, Symbol per `dlsym(av, "sceAvcap2RequestKeyFrame")` wie die übrigen):

* Nach ≥ 100 Frames `rc = RequestKeyFrame(handle)` (höchstens 1×/s); erwartet `rc == 0`.
* Beobachten: Anzahl Frames bis zum nächsten Annex-B-NAL-Typ 5 (IDR) bzw. `desc+0x1C == 1` und `desc+0x20`.
  Erfolg: IDR innerhalb 1–3 Frames, deutlich vor dem natürlichen ~55-Frame-Raster. Gegenprobe: gleicher Lauf ohne Aufruf.
* Optional: `SetInvalidFrame(handle, desc+0x20 eines kürzlich gelesenen Frames)` → `rc == 0`, Folgeframe bleibt P (kein
  IDR); `RequestSkipFrame` → Lücke im `desc+0x20`-Zähler. Nur zur Einordnung, nicht für den Produktivpfad.

## 5. R2 — Audio

**Antwort: Ja. Avcap2 hat einen vollständigen Audiopfad (sicher), und `SceRemotePlay` holt das Audio darüber.**

Imports von `SceRemotePlay` (22 Avcap2-Funktionen): `OpenAudio` (`0x72CA8`), `ReadAudio` (`0x72EEB`),
`Select` (`0x72E41`), `Start`, `Close`; zusätzlich `OpenHaptic/ReadHaptic` (Controller-Haptik).
Daneben importiert RemotePlay `libSceAudioIn/AudioOut/OpusCeltEnc/OpusCeltDec` — Vermutung: Mikrofon/Party-Chat,
nicht das Spiel-Audio (nicht weiter verfolgt). `SceGameLiveStreaming` nutzt dieselben Funktionen
(`OpenAudio(h, cfg, 1)`, `ReadAudio`, `Select`, `SetAudioOutVolume`, `SetMicVolume`).

### API-Form (sicher)

```
int sceAvcap2OpenAudio(h **out, const void *cfg /*0x38 B*/, uint8_t flag /*Remoteplay 0, GLS 1*/);
int sceAvcap2Start(h);   int sceAvcap2Stop(h);   int sceAvcap2Close(h);
int sceAvcap2ReadAudio(h, void *dst, size_t cap, void *hdr /*>=0x10 B, hdr[0] = 0x10 (u64)*/, uint32_t flags);
int sceAvcap2Select(uint32_t n, h *handles[n], uint64_t *readyMask);   /* optional, blockierend */
```

`ReadAudio` ist derselbe Ring-Reader wie `ReadVideo` (Header-Größe 0x10): `flags & 1` = nicht blockierend
(`0x81950002` = noch keine Daten), sonst blockiert er auf dem Event-Flag. Rückgabe > 0 = Bytes **eines Pakets** im Ring
(`cap` muss ≥ Paketgröße sein; RemotePlay nutzt 0xC000). Header: RemotePlay setzt `hdr[0] = 0x10`, `hdr[8] = 0`; der
Server legt 16 Byte Meta pro Paket ab (vermutlich Zeitstempel/Zähler — nicht belegt). `0x81950008/9` signalisieren
Ende/Entzug.

### Konfig-Struktur `cfg` (0x38 Byte)

| Offset | Wert | Bedeutung | Konfidenz |
|---|---|---|---|
| 0x00 | u64 `0x38` | Größe | sicher |
| 0x08 | u64 `0x18` | Größe des Kopfblocks | sicher |
| 0x10 | u32 0 | Kanalart (≤ 2; RemotePlay: 0) | sicher |
| 0x14 | u32 | Purpose (< 8: 0 VideoRecording, 1 GameLiveStreaming, 2 Remoteplay, 3 SocialScreen, 4 Shareplay, 5 ShareScreen, 6 VideoChat, 7 PS Now). Der Server wählt daraus den Audio-Ring: Remoteplay (2) und PS Now (7) → `full`, GLS (1) → `aux`, Share/VideoChat → `share`, Recording/Social/Shareplay → `restrict` | wahrscheinlich |
| 0x18 | u32 | **Encoder-Typ**: 0 = rohes PCM, 1 = AAC (`libSceM4aacEnc`), **2 = Opus-CELT (`libSceOpusCeltEnc`)**, 3 = PCM 12 Kanal (nur Purpose 2; gemessen 13.00: 1024 Abtastwerte × 12 Kanäle × Float32 je Paket, `surround_probe`), 4 = Sonderfall (nur Purpose 8). Typ 2 verlangt `cfg+0x30 < 3` | sicher (Server-Factory `0x11720`, Validator `0x10AA0`) |
| 0x1C | u32 | **ResourceArbitrator-Id (muss aktiv sein, z. B. 180)** — siehe „R2-Nachtrag“ | sicher |
| 0x20 | u64/u32 | Größe des Codec-Blocks: 0x10 (AAC), 0x18 (Opus) | sicher |
| 0x28 | u32 | **Bitrate in bit/s** (RemotePlay: Opus 64000 = `0xFA00`, AAC 128000 = `0x1F400`); geht bei Opus direkt in `OPUS_SET_BITRATE (4002)` | sicher |
| 0x2C | u8 | nur Opus: Flag, das `OPUS_SET_VBR(0)` (= CBR) auslöst | wahrscheinlich |
| 0x30 | u32 | nur Opus: Frame-Größen-Index 0 → 960, 1 → 480, 2 → 960 Samples (Tabelle `0x26748`) | wahrscheinlich |
| 0x34 | u8 | nur Opus: weiteres Flag (setzt einen Opus-CTL, 4046) | Vermutung |

Die 12 Kanäle von Typ 3: Bei Stereo-Ausgabe der Konsole tragen nur Kanal 0 und 1 Ton (13.00, `STATUS.md` „Für 0.3.1“). Auf
13.60 blieb es laut PR 16 (BuffMcBigHuge) auch mit echter 5.1-Quelle und erzwungenem 5.1-LPCM auf HDMI bei Kanal 0 und 1,
solange der Fernseher im EDID nur 2-Kanal-LPCM meldet; die Systemmischung folgt wohl dem EDID. `cfg+0x10 = 2` (AudioSecond)
ist dort ebenfalls stereo, `cfg+0x10 = 1` (AudioAout) liefert keine Pakete. Welcher Kanal welcher Lautsprecher ist, ist
ungemessen; der Host nimmt die 8-Kanal-Reihenfolge von `sceAudioOut` an (`src/audio/channel_map.hpp`).

Aus dem Server (`0x11C90`): Opus-Encoder mit **48 000 Hz, 2 Kanäle**, `OPUS_SET_SIGNAL(MUSIC)`, Bitrate aus `cfg+0x28`.
Avcap2 liefert also direkt Opus-Pakete (`SceOpusCeltEnc` = CELT-only), Frame 10 ms (Index 1) oder 20 ms (Index 0/2) —
**kein 5-ms-Frame**. Erwartet Sunshine/Moonlight 5 ms (Standard-`packetDuration`), muss man entweder die Dauer aushandeln
(prüfen!) oder Typ 0 (PCM) lesen und selbst enkodieren. Das PCM-Format (Typ 0) ist nicht bestimmt (vermutlich 48 kHz
Stereo, 16 Bit oder Float): per Live-Test über die Paketgröße ermitteln.

Ressourcen: Das RA-Profil 180 vergibt laut Konsolenlog bereits `acp[270]` (Audio-Capture) — OpenAudio sollte also ohne
weiteren Resource-Request gehen (Vermutung; sonst `0x81950008`, RemotePlay wiederholt dann alle ~40 ms).

Vorschlag Live-Test R2 (nach Video-Start, gleiche RA-Grant):

1. `cfg` (0x38, genullt) mit `{0x38, 0x18, kind 0, purpose 2, type 2, +0x1C 0, +0x20 0x18, +0x28 64000, +0x30 1}`;
   `OpenAudio(&ha, cfg, 0)` → erwartet `rc == 0`; bei `0x81950001` Felder variieren (+0x1C, purpose) unter Beachtung der
   Validator-Regeln oben.
2. `Start(ha)`; Schleife `ReadAudio(ha, buf, 0xC000, hdr{0x10, 0}, flags = 1)`; protokollieren: `rc` (Bytes), die 16 Byte `hdr`,
   erste Bytes des Pakets (Opus-TOC-Byte), Rate (100 Pakete/s ⇒ 10 ms, 50/s ⇒ 20 ms).
3. Pakete in eine Datei schreiben, auf dem PC mit libopus (`opus_decode`, 48 kHz, 2 ch) dekodieren und anhören.
4. Zweiter Lauf mit `type 0` (PCM): Paketgröße = Samples·Kanäle·Bytes → Format ableiten.
5. Parallel zu Video: kein Konflikt erwartet (RemotePlay macht beides).

## 6. R3 — Bitrate, Framerate, GOP

### `cfg+0xC8` (Struktur sicher, Einheit wahrscheinlich)

`cfg+0xB8…0x198` ist ein eingebetteter **0xE0-Byte-Block** („Encoder-Parameter“). Genau dieser Block wird zur Laufzeit von
`SetVideoEncodeConfig(h, blk)` übertragen und im Server auf `sceVencSetConfig` abgebildet; `cfg+0xC0…0xE0` ist der
Rate-Control-Teilblock (Server `+0x4F0`, Größe 0x20), `cfg+0xC8` = dessen Feld `+8`, `cfg+0xCC` = Feld `+0xC`.
Berechnung in `SceRemotePlay` (`0x70130`, Eingaben `kbps`, `fps`):

| Feld | Wert | Bedeutung |
|---|---|---|
| `cfg+0xCC` | `kbps·1000` | Spitzen-/Maximalbitrate (bit/s) |
| `cfg+0xC8` | `cfg[0xCC] / 1.12` (AVC) bzw. `/ 1.10` (HEVC) (Floats bei `0x5243F8`) | **Ziel-/Durchschnittsbitrate (bit/s)** — unser Wert 6 000 000 sind also 6 Mbit/s |
| `cfg+0xD0` | `kbps·1000·1001 / (fps·1000)` | Bit-Budget pro Frame (NTSC-Faktor) |
| `cfg+0x10C` | (falls `cfg[0x108] == 1`) `kbps·1001000 / (fps·1000) / 8` | Byte-Budget pro Frame |

Bestätigt durch `SceGameLiveStreaming` (`0x4D0D0`): `SetVideoBitrate(h, {0x10, edx, ecx})` mit zwei frei gewählten Werten,
und Server `0x39E0`: Feld `+8` → Rate-Control `+8` (= `cfg+0xC8`), Feld `+0xC` → `+0xC` (= `cfg+0xCC`), jeweils nur wenn ≠ 0.

### Laufzeit-Änderung

* **Bitrate:** `sceAvcap2SetVideoBitrate(h, &{u64 0x10; u32 avg; u32 peak})` (NID `iy6ZcsdeoMk`, `0x2140`, IPC `0xA7001B`):
  kleinste Lösung, 0 = Wert unverändert lassen. Pfad **sicher**, Einheit bit/s **wahrscheinlich**.
  Alternativ `SetVideoEncodeConfig(h, cfg+0xB8)` mit dem 0xE0-Block (RemotePlay-Weg über `0x71090` und `0x71B30`).
* **Framerate:** Es gibt **keinen** dedizierten Aufruf. RemotePlay ändert nur das Frame-Bit-Budget (`cfg+0xD0`) und das Feld
  `cfg+0x130` (nur wenn `cfg[0x128] == 1`). Die eigentliche Rate hängt am Open-Parameter `cfg+0x1C` (0 = 30, 1 = 60 fps).
  → Ein Framerate-Wechsel erfordert wahrscheinlich Close/Reopen (**Vermutung**).

### `cfg+0x94` (belegt: IDR-Abstand in Bildern, Messung in `docs/re/hevc.md`)

Der folgende Text ist der Stand vor der Messung.

`SceRemotePlay` setzt `cfg+0x94 = N · fps` (`N = this[0x2003D8]`, Setter `0x718A0`, gespeist über `0x6DB80/0x6DC00`) und
`cfg+0x98 = !cfg[0xE8]`. Das passt zu „IDR-/GOP-Intervall in Frames (N Sekunden)“, ist aber **nicht belegt**: Im Server wird
`cfg+0x94` an keiner Stelle einzeln gelesen (kein `[x+0x9C]`-Zugriff in `SceAvCapture`); der Block `cfg+0x88…0xB8` geht nur als
Ganzes in Venc-Strukturen ein (`0x18390`: `cfg+0x98` → Venc-Wert `max(x,1)`, `cfg+0xA8/0xAC` → weiterer Wert). Der
beobachtete IDR-Abstand ≈ 55 Frames bei `0x94 = 30` passt nicht zu einem einfachen „30 Frames“. Wirkung daher unklar, Live-Test nötig.

Vorschlag Live-Test R3:

1. **Bitrate live:** laufender Stream, `SetVideoBitrate(h, {0x10, 3000000, 3300000})` → `rc == 0`; Bytes/s in 5-s-Fenstern
   vor/nach messen (6 → ~3 Mbit/s erwartet, 1–2 s Einschwingzeit). Danach `{0x10, 0, 8000000}` und `{0x10, 8000000, 0}`
   zum Verständnis von avg/peak.
2. **Encode-Config:** `blk = cfg+0xB8` (0xE0, `blk[0] = 0xE0`) mit geändertem `cfg+0xC8/0xCC/0xD0` übertragen →
   `rc == 0`, gleiche Wirkung wie (1).
3. **GOP-Hypothese:** drei Läufe mit `cfg+0x94 = 0 / 30 / 120` (optional zusätzlich `cfg+0x98`); IDR-Abstand (NAL 5) über
   60 s auswerten. Keine Änderung ⇒ Feld irrelevant (auf Default belassen).
4. **Timing:** `cfg+0x1C = 0 / 1 / 2` → tatsächliche Framerate messen; `2` kann `rc != 0` liefern.
5. `GetVideoEncoderCapability(&u32)` (NID `117ggZLcGc0`, `0x1F00`) einmal aufrufen und den Wert loggen.

## 7. Offene Punkte

* 20 NIDs ohne Namen. Potenziell nützlich: `B+6003nL0AI` (PictureConfig type 0 mit Wert 1/2 — evtl. Pacing/Low-Latency-Modus),
  `NEY5WpUI0Io` = `ChangeScanInResolution` (Auflösungswechsel zur Laufzeit), `RW88AiwmL+Y` (Status-Struktur 0x20 B).
* Die Felder des `ReadVideo`-Descriptors (`+0x1C` IDR-Flag, `+0x20` Frame-Index, `+0x28/+0x30` Sekunden/Nanosekunden,
  `+0x54` Größen-/QP-Indikator) stammen aus der RemotePlay-Pump → vor Nutzung per Hexdump gegenprüfen.
* PCM-Format, ReadAudio-Header-Felder, Wirkung von `cfg+0x1C` (Audio) und `cfg+0x94` (Video) sind ungeklärt (siehe Tests).
* Die Analyse gilt für Build J03851548 / FW 13.00. Offsets nicht auf andere Firmware übertragen; Namen und NIDs sind stabil.

## 8. R2-Nachtrag — `0x81950008` bei OpenAudio und `0x80C78000` bei zu hoher Bitrate

### 8.1 Ursache von `0x81950008` bei `OpenAudio` (sicher, statisch)

`cfg+0x1C` ist **keine** User-Id, sondern die **ResourceArbitrator-Resource-Id** (Pendant zu `cfg+0x28` bei OpenVideo).
Der Server prüft sie beim Anlegen eines neuen Audio-Kanals und lehnt mit `0x81950008` ab, wenn die Id nicht in der Liste der
aktuell aktiven RA-Ressourcen steht. Aufrufkette in `SceAvCapture`:

1. IPC `0xA70000` → Manager `0xA70` → Validator `0x10AA0` (Fehler dort wäre `0x81950001`) → Objekt anlegen `0x109B0` → Init `0x24F0`.
2. `0x24F0` ruft `channel->vtbl[+0x20](this, 1)` = `0x110A0`. Dort: `id = *(cfg_copy+0x24)` (= `cfg+0x1C`, Getter `0x10DB0`),
   dann `ok = this->vtbl[+0x100](id)`; bei `ok == 0` wird `0x81950008` zurückgegeben (`0x110D0`).
3. `vtbl[+0x100]` = `0x27B0`: ruft RA-Export `fBX+DBOwNis` (die Funktion „32 aktive Resource-Ids in 128-Byte-Puffer lesen“, genau die, die der
   Payload schon nutzt) und vergleicht `id` per AVX2 gegen alle 32 Einträge. Kein Treffer, oder RA-Aufruf schlägt fehl ⇒ `false`.
4. `0x81950008` wird als Server-Status unverändert vom Client (`0x43E0`) durchgereicht — der Client erzeugt ihn nicht selbst.

Alle getesteten Werte (0, 1, User-Id `0x1EA2F4D9`) sind keine aktive RA-Id ⇒ immer `0x81950008`, unabhängig von Encoder, Purpose und Flag.
Der Pfad `esi == 0` (Wiederverwendung eines schon offenen Kanals desselben Typs) überspringt die Prüfung; für einen neuen Kanal gilt sie immer.

Wie die Systemprozesse dorthin kommen: `SceRemotePlay` wählt je nach Betriebsart die RA-Id 0xB4 (180), 0xB1, 0xB9, 0xAF oder 0xB6
(`0x6B150`, Tabelle `0x528528`), speichert sie in `this+0x2C`, registriert (`kJvjeet+f9I`, Aufruf `0x6B1EC`) und fordert `RUNNING` an
(`CYjfezkWcaE`). Die Session übergibt dieselbe Id später an die Audio-Konfiguration (`this+0xC050`, Setter `0x72FB0`, → `cfg+0x1C`) und
an die Video-Konfiguration (`cfg+0x28`). `SceGameLiveStreaming` macht dasselbe mit eigenen Ids (`0x4E0CF`…`0x4E53B`). Davor ruft
keiner ein Audio-spezifisches Enable/Select auf; es gibt **keinen** separaten Audio-RA-Request: die gleiche Profil-Grant (bei 180:
`vcn[20] scanin[2K] acp[270]`) deckt Audio ab, solange die Id in der aktiven Liste steht.

Die Profile im RA-Log auf 13.00 (`src/uhd_spike.cpp`): 180, 185 und 175 sind `vcn[20] scanin[2K] acp[270] prio[5]`, 177 und 182 sind
`vcn[70] scanin[4K] acp[270] prio[1] Confirm[forced OK]`. H.264 in Modus 5 und 6 öffnet nur mit 177 und 182, auf den 2K-Profilen kommt
`0x8195000a` oder bei laufendem Spiel `0x81950008`. Den Wunsch nach `scanin[4K]` kann der RA auch mit laufendem Spiel erfüllen (GTA III), er lehnt
aber in manchen Lagen ab: Bei Mario 64 meldet er `[ScanIn]Is4KRunnable: 4K NG(4K120)` und schickt `result[false]` an den Callback, obwohl der
Aufruf `RUNNING` 0 zurückgibt. Das Spiel läuft dabei weiter.

### 8.2 Konkreter nächster Live-Test

Reihenfolge (alles nach erfolgreichem Video-Start; RA-Profil 180 bleibt aktiv):

0. Optional Vorabprüfung: RA-Liste (`fBX+DBOwNis`, 32×u32) lesen und prüfen, dass 180 enthalten ist.
1. `cfg` (0x38 Byte, little-endian, alles andere 0) — erster Versuch exakt wie RemotePlay (Opus, Frame-Index 0):

```
off 0x00  u64 0x0000000000000038
off 0x08  u64 0x0000000000000018
off 0x10  u32 0            kind
off 0x14  u32 2            purpose Remoteplay
off 0x18  u32 2            Encoder Opus-CELT
off 0x1C  u32 180 (0xB4)   RA-Id  <-- die Korrektur
off 0x20  u64 0x18         Codec-Block-Größe (Opus)
off 0x28  u32 64000        Bitrate
off 0x2C  u32 0
off 0x30  u32 0            Frame-Index 0 = 960 Samples (20 ms); danach 1 (10 ms) testen
off 0x34  u32 0
```

2. `rc = sceAvcap2OpenAudio(&ha, cfg, 0)`. Erwartung `0`. Fehlerdeutung: `0x81950001` = Validator (Feldwert prüfen, v. a. Typ 2 braucht `cfg+0x30 < 3`),
   `0x81950008` = Id nicht aktiv (RA-Grant weg/falsche Id), `0x81950003` = Server-Allokation.
3. `sceAvcap2Start(ha)`, dann `ReadAudio(ha, buf, 0xC000, hdr{0x10, 0}, 1)` in Schleife (nonblocking, 5 ms Pause bei `0x81950002`).
4. Loggen: Paketgröße, `hdr`-16-Byte, Pakete/s (50/s = 20 ms, 100/s = 10 ms), erste 16 Byte; Pakete in Datei, auf dem PC mit libopus (48 kHz, 2 ch) dekodieren.
5. Danach `Stop/Close(ha)`; zweiter Lauf mit Encoder 0 (PCM; `cfg+0x20 = 0x10`, `cfg+0x28 = 0`) zur Formatbestimmung.

Hinweis: Gelingt es, steht die Id 180 erst durch das gewährte `RUNNING` in der Liste — die Wiederholungsschleife (10×40 ms) im Test ist also
bei fehlender Grant sinnlos; sie hilft nur, wenn die Grant-Callback-Antwort noch aussteht.

### 8.3 `0x80C78000` nach `SetVideoBitrate({0x10, 20000000, 22000000})`

* Der Wert steht in **keinem** der vier ELFs (`libSceAvcap2`, `SceAvCapture`, `SceRemotePlay`, `SceGameLiveStreaming`); er stammt aus `libSceVenc`
  (Fehlerfamilie `0x80C7xxxx`, im Server taucht dort `0x80C70101` = „später nochmal“ und `0x80C71001` auf).
* Weg in den Client: Encoder-Thread des Servers (`0x4210…0x45E4`) holt das Encode-Ergebnis (`libSceVenc`-Import `2FTI1YJYNBw`). Negative Ergebnisse außer
  `0x80C70101` (Retry) und `0x80C71001` (→ `0x8195000B`) werden **unverändert** zurückgegeben (`0x4593…0x45C3`), landen als Fehlerrecord im Shm-Ring und
  kommen als Rückgabewert von `ReadVideo` an. Die Fehlermeldung `[AvControl] ** stop scanin` stammt aus der Scan-In-Steuerung (`AvcapScanInControl`), die den
  Kanal nach dem Encoder-Fehler abbaut — der Kanal ist danach tot (Close + neu Open nötig).
* **Es gibt keinen Bereichs-Check in Avcap2.** Weder `SetVideoBitrate` im Client (`0x2140`, nur Handle-Prüfung) noch der Server-Handler (`0x39E0`: nur „≠ 0 → übernehmen“,
  dann `sceVencSetRateControlConfig`) prüft Werte, und auch `SetVideoEncodeConfig` (`0x17B80` prüft nur die Array-Längen) nicht. Der Aufruf selbst meldet daher `0`; die Ablehnung
  kommt asynchron beim nächsten Encode aus `libSceVenc`/dem Hardware-Encoder. Das obere Limit ist statisch aus den vorhandenen Binaries nicht bestimmbar (`libSceVenc` fehlt).
* Beobachtung aus den Consumern: SceRemotePlay/GLS klemmen nirgends; sie berechnen `avg = peak/1.12` (AVC) und übergeben nur Werte aus ihren Qualitätsstufen. Ihr Verhältnis
  ist 1,12; der Test hatte 1,10.
* Hypothesen (Vermutung): (a) absolutes HW-/Level-Limit des Encoders bei 1080p60, (b) Encoder-/VBV-Speicher wird beim Open aus den Anfangswerten berechnet
  (`sceVencQueryMemorySize`/`QueryPreset`, `0x4E20`), sodass nur Werte ≤ Anfangsbitrate sicher sind — passt dazu, dass 0,3 → 6 Mbit/s zurück funktionierte;
  (c) Verhältnis avg/peak.
* Empfohlene Tests: (1) Treppe 8 / 10 / 12 / 15 Mbit/s (avg, peak = avg·1,12), 10 s pro Stufe, vor jedem Schritt Kanal frisch öffnen, bei Fehler Stufe notieren;
  (2) Kanal mit hohem Anfangswert öffnen (`cfg+0xC8/0xCC` = 20 000 000 / 22 400 000) und prüfen, ob das Open selbst scheitert oder der Stream läuft;
  (3) Wiederholung der 20 M-Stufe mit Ratio 1,12 und `peak = 0` (nur avg). Produktiv: Obergrenze fest klemmen (z. B. ≤ Anfangsbitrate des Opens) und bei `ReadVideo`-Fehlern
  `0x80Cxxxxx` den Kanal schließen/neu öffnen, statt weiterzulesen.

**Nachtrag (HEVC-Spike, `docs/re/hevc.md`):** Hypothese (b) trifft für HEVC zu: Ein Kanal, der mit der Bitrate in `cfg+0xC8/0xCC` geöffnet wird, läuft bis 100 Mbit/s fehlerfrei, und nur `SetVideoBitrate` über den Anfangswert hinaus führt zu `0x80C78000`. Für H.264 ist das nicht getestet. `libSceVenc` liegt inzwischen unter `.cache/re/`.
