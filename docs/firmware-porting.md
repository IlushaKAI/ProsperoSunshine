# Firmware-Porting

Stand: 1. November 2026. **Verifiziert sind Firmware 13.00 und 10.20** (10.20 bei 1080p; Schritt 6 entfällt dort, weil der Encoder auf 10.x die Bitrate nicht aus der Konfiguration übernimmt). Dieses Dokument beschreibt,
was am Host firmwareabhängig ist, wie die ResourceArbitrator-Funktionen zur Laufzeit gefunden werden und
was vor einer Freigabe weiterer Versionen auf einer echten Konsole geprüft werden muss. Es ist keine
Zusage, dass andere Versionen funktionieren.

## Was firmwareabhängig ist

| Bereich | Datei | Annahme aus 13.00 | Wie es gefunden wird |
| --- | --- | --- | --- |
| ResourceArbitrator: Register, Unregister, Request, List | `src/platform/ps5/ra_resolve.c`, `ra_nids.h` | Modulrelative Adressen `0xdb0`, `0xee0`, `0x10b0`, `0x11f0` | Laufzeit-Suche per NID, Abgleich mit Tabelle (siehe unten) |
| Avcap2-Funktionen | `capture.c` | Exportiert unter bekannten Namen | `kernel_dynlib_dlsym` per Name, unkritisch |
| Avcap2-Video-Konfiguration | `capture.c` `configure()` | 0x2E0 Bytes (10.x und 13.60 abweichend, `record_shift`, siehe „Berichte zum Video-Datensatz anderer Firmwares“), Unterblock-Größen, `0x2A4 = 2` (Bildschirm inkl. Home-Menü), RA-Profil 180 bei `0x28` (auch unter 1080p), 182 für 1440p und 4K (auf 13.00 freigeschaltet, `ps_capture_large_modes_verified`, sonst nur mit dem experimentellen Schalter), Zweck `0x14` = 2, für HEVC über 1080p 6 (`docs/re/hevc.md`) | Fest, auf 13.00 gemessen (`docs/re/avcap2-exports.md`) |
| Avcap2-Audio-Konfiguration | `capture.c` | 0x38 Bytes, `cfg+0x14 = 2` (Remote-Play-Mix), `cfg+0x1C = 180` | Fest, auf 13.00 gemessen |
| Frame-Descriptor | `video_ps5.cpp`, `docs/avcap2-descriptor.md` | `ReadVideo`: 0x70 Bytes, TSC-Zeitstempel in Wort 9. `ReadVideo2` (HEVC): 0x88 Bytes, Bildtyp `+0x2c`, TSC `+0x58`, Bytes des Bildes `+0x6c` (`docs/re/hevc.md`) | Fest, auf 13.00 gemessen |
| Bitrate | `capture.c` `configure()`, Host-Konfiguration | Die Bitrate steht beim Öffnen in `cfg+0xc8/0xcc/0xd0` (HEVC zusätzlich `+0x10c`); der Host klemmt auf 150 Mbit/s auf 13.00 und sonst auf 50 (`ps_capture_high_bitrate_verified`), außer mit dem experimentellen Schalter. `SetVideoBitrate` allein stoppte den Encoder ab 16 Mbit/s und bei HEVC über dem Wert beim Öffnen | Auf 13.00 gemessen (`docs/re/hevc.md`) |
| HEVC | `capture.c` `ps_capture_open`, `configure()` | `cfg+0x24 = 1`; Encoder-Speicher (Memory Mode 1: `cfg+0x198 = 0x50`, `cfg+0x1a0 = 1`, Import per NID `yN0w4tWivqs`, Freigabe `96UQhNnBlxc`); die Remote-Play-Felder (nicht auf ein Minimum reduziert); Lesen nur mit `ReadVideo2`; Parameter-Sets aus `GetNonVclNalUnitStream` als SPS, PPS, VPS, der Host sortiert das VPS nach vorn | Fest, auf 13.00 gemessen. Auf 10.20 reicht `cfg+0x24 = 1`, auch für 10 Bit; Speicher und Remote-Play-Felder entfallen auf 10.x. Der Host bietet HEVC auf 13.00 und 10.20 an (`ps_capture_hevc_verified`), sonst H.264, außer mit dem experimentellen Schalter |
| Virtuelles Pad | `pad.c`, `pad.h` | ioctls auf `/dev/hid`: add `0xC018482A`, insert `0x8018482C`, delete `0x80104850`; Blöcke 0xA8 Bytes, Layout im Kopfkommentar von `pad.c` | Fest, auf 13.00 gemessen; unverändert |
| Kernel-Zugriffe des SDK | PS5 Payload SDK v0.45 | `kernel_dynlib_*`, `kernel_get_fw_version` nutzen SDK-interne Kernel-Offsets je Firmware | Vom SDK abgedeckt, sofern es die Version kennt |

## Berichte zum Video-Datensatz anderer Firmwares

Außer für 10.20 nicht selbst gemessen. Der Block bei `0xb8` wächst mit der Firmware, alles ab `0x198` verschiebt sich mit ihm. Der Host setzt die Verschiebung um (`record_shift` in `capture.c`). Für 9.60 sind es −`0x20`, wieder ohne innere Köpfe und Rate-Felder, mit dem QP 0x18 tiefer als auf 10.x. Für 10.x sind es −`0x18`, außerdem lässt er dort die inneren Köpfe und die Rate-Felder von 13.00 weg; den Datensatz hat ein Nutzer auf 10.20 gemessen, und die Firmware-Sonde fand ihn auf 10.40 genauso (`docs/firmware-probe.md`); andere 10.x-Versionen sind ungeprüft. Für 13.60 sind es +8, hier ungetestet, weil keine Konsole mit 13.60 vorliegt; Nutzer streamen damit (Issue #1). 13.40 bekommt seit Issue #14 dasselbe, ebenso jede Version zwischen 13.40 und 13.60. Wo zwischen 13.00 und 13.40 der Block wächst, ist offen:

| Firmware | Gesamt | Block `0xb8` | Ab `0x198` | Quelle |
| --- | --- | --- | --- | --- |
| 8.40 | vermutlich kürzer als 13.00 | nicht genannt | −`0x20`, ungetestet | Bericht in Issue 23 (Host 0.2.4): mit dem Datensatz von 13.00 lehnt `OpenVideo` mit `0x81950008` ab, obwohl Ressource 180 aktiv ist, wie 10.40 vor dem Datensatz von 10.x (Issue 2). Seitdem bekommt alles unter 10 den von 9.60, der nächsten gemessenen Firmware; 9.60 antwortet auf den von 13.00 allerdings mit `0x81950001`, 8.40 kann also abweichen |
| 9.60 | `0x2c0` | `0xc0` | −`0x20`, ohne die inneren Köpfe und Rate-Felder von 13.00 | `firmware_probe.elf` auf einer PS5 (nicht Pro), 2026-11-06, Host 0.3.4 (`0x9600004`, `known=0`): von −`0x40` bis +`0x20` öffnet nur `0x2c0`; `0x2c8`, `0x2d0`, `0x2e0` und `0x2e8` lehnt der Validator mit `0x81950001` ab. H.264 und HEVC 8 und 10 Bit (nur `cfg+0x24`) liefern Bilder von 360p bis 1440p; 2160p scheitert mit `0x81950008` bei `video out mode` `0x34` (kein 4K-Bit). Der QP sitzt in `cfg+0xac`/`+0xb0` (`ps_qp_offset`); `cfg+0xc4`/`+0xc8` ändern hier nichts. 34 Slices bei H.264 1080p, 17 bei HEVC. Validator schützt `0x80`, `0x168`, `0x1d8`, `0x2a8`. ResourceArbitrator per NID an denselben Offsets wie 10.20. Gestreamt mit H.264 1080p60 und Ton (`STATUS.md`, „Firmware-Sonde auf 9.60“) |
| 10.20 (Pro) | `0x2c8` | `0xc8` | −`0x18`, außerdem ohne die inneren Köpfe und Rate-Felder von 13.00 | Bericht in Issue 2 (Host 0.1.4 mit diesem Datensatz, danach 0.1.6 mit `record_shift`): `OpenVideo` und `Start` liefern 0, Moonlight streamt auf Android und iOS. H.264 hat 34 Slices je Bild, FFmpeg nimmt mit D3D11VA höchstens 32, also braucht Moonlight unter Windows dafür Software-Decoding; HEVC (nur `cfg+0x24 = 1`, auch 10 Bit) hat 17 und läuft dort mit Hardware-Decoding, gemessen mit Moonlight unter Windows (`STATUS.md`, `docs/re/hevc.md`). Der Ton mit etwa 0,4-facher Geschwindigkeit lag nicht an 10.x, sondern an `min_log_level = debug`: Jede Zeile geht synchron ins Kernel-Log, gemessen 5,2 ms je Zeile. Die Slice-Felder für H.264 (seit 0.3.1 gesetzt: `cfg+0x40 = 1`, `+0x44 = 4`, `+0x48 = 1`) nimmt 10.20 an, schneidet aber weiter 34 Slices (Host 0.3.1, PS5 Pro, `first frame has 34 slices`). Offen: Rate (der Encoder bleibt bei seiner Standardrate); welches Feld auf 10.x die Slice-Zahl setzt |
| 10.40 | `0x2c8` | `0xc8` | −`0x18`, wie 10.20 | `firmware_probe.elf` auf einer PS5 (nicht Pro), 2026-11-05: von −`0x40` bis +`0x20` öffnet nur `0x2c8`, der Baukasten der Sonde ergibt `configure()`; H.264, HEVC 8 und 10 Bit (nur `cfg+0x24`), PCM und Opus mit dem Datensatz des Hosts; QP in `cfg+0xc4`/`+0xc8`; dieselben vom Validator geschützten Wörter; alle zwölf Modi von 360p bis 2160p liefern Bilder; 34 Slices bei H.264 1080p, 17 bei HEVC; ResourceArbitrator per NID an denselben Offsets wie 10.20 (`STATUS.md`, „Firmware-Sonde auf 10.20 und 10.40“). Derselbe Mitwirkende streamte dort mit Moonlight über das Internet (Tailscale): H.264 lief rund zwei Stunden ohne Abbruch, HEVC HDR blieb nach einer Weile hängen, Ursache offen; laut Issue 2 streamt HEVC 8 und 10 Bit für einen anderen Nutzer ohne Probleme |
| 12.40 | `0x2e0` | nicht genannt | nicht genannt | Log in PBS-Public-Issues-Tracker, Issue 1 |
| 12.70 | `0x2e0` | nicht genannt | 0 | Bericht in Issue 17 (Host 0.2.3): mit dem Datensatz von 13.00 liefert `OpenVideo` 0, HEVC (Variante `minimal`) streamt; der Tester nennt es „fast fehlerfrei“ |
| 13.00 | `0x2e0` | `0xe0` | – | gemessen, `configure()` |
| 13.40 | vermutlich `0x2e8` | nicht genannt | +8 | Bericht in Issue 14 (Host 0.2.3): mit dem Datensatz von 13.00 lehnt `OpenVideo` mit `0x80c70010` ab, wie 13.60 vor 0.1.5 (Issue 1). Mit dem von 13.60 (Testbuild `0.3.1-fw1340-1`, PS5 Slim Digital): `OpenVideo` und `Start` liefern 0, Moonlight auf Android zeigt H.264 |
| 13.60 | `0x2e8` | `0xe8` | +8 | Quelltext von PlayBridge und ps5-stream8; laut seregonwar beruhen beide auf der Analyse des PBS-Payloads |

`0x81950008` hat laut ps5-web-stream mehrere Zweige (Bildausgabe passt nicht, Größen im Datensatz fehlen, Ressource nicht aktiv). Laut PlayBridge erlaubt in `GetVideoOutMode` Bit `0x20` 1440p und Bit `0x08` 4K. Der Host prüft das nicht vorab, sondern fällt beim Scheitern zurück: Die Berichte passen dazu nicht vollständig (`0x13c` wird für 1080p und für 4K genannt). `host_diag.log` nennt den Wert seit diesem Stand als `video out mode`.

## ResourceArbitrator: Suche per NID

`libSceResourceArbitrator.sprx` exportiert die vier Funktionen, aber nur unter NIDs, deren
Klartextnamen nicht bekannt sind. Deshalb findet `kernel_dynlib_dlsym` (Name → NID) sie nicht. Früher
standen feste Adressen relativ zur Modulbasis im Code, abgesichert durch eine harte 13.00-Prüfung.

Jetzt löst `ps_ra_resolve()` die vier NIDs direkt mit `kernel_dynlib_resolve()` auf:

| Funktion | NID | 13.00 | 10.20 |
| --- | --- | --- | --- |
| register `(id, callback, user)` | `kJvjeet+f9I` | `0xdb0` | `0xde0` |
| unregister `(id)` | `LqmYZGB-UEQ` | `0xee0` | `0xfe0` |
| request `(id, state, data, pid)` | `CYjfezkWcaE` | `0x10b0` | `0x1220` |
| list `(int32_t active[32])` | `fBX+DBOwNis` | `0x11f0` | `0x1360` |

NIDs sind Hashes der Symbolnamen und bleiben gleich, solange Sony die Funktion nicht umbenennt; die
Adressen dagegen verschieben sich mit jedem Build des Moduls. Die Entscheidungsregel steht portabel in
`ps_ra_choose()` (`ra_nids.h`, Test `tests/ra_choose_test.cpp`):

- **Bekannte Firmware** (Eintrag in `ps_ra_known_firmware`): Das NID-Ergebnis muss der Tabelle
  entsprechen, sonst wird abgelehnt. Fehlt ein NID, gilt der Tabellenwert als Rückfall.
- **Unbekannte Firmware:** Alle vier NIDs müssen auflösen (innerhalb von 1 MiB hinter der Modulbasis),
  sonst lehnt der Host mit `PS_CAPTURE_FIRMWARE` ab. Gelingt es, meldet klog ausdrücklich, dass die
  Firmware unverifiziert ist.

Die Suche läuft einmal pro Host-Prozess beim ersten `ps_capture_open` und ruft nichts im Modul auf.
klog zeigt eine Zeile pro Funktion, z. B. auf 13.00:

```text
ProsperoSunshine: ra firmware=0x13000040 known=1
ProsperoSunshine: ra register   nid=kJvjeet+f9I -> 0xdb0 table=0xdb0 use=0xdb0 (nid)
```

Byte-Signaturen über den Modulcode wurden erwogen und verworfen: Der Code ist nur über den Kernel
lesbar (direktes Lesen beendet den Prozess, Ausführen-only-Speicher), die Muster wären Auszüge aus
Sony-Code, und die NIDs sind robuster.

### Prüf-Payload

`out/ra_nid_probe.elf` lädt nur `libSceIpmi` und `libSceResourceArbitrator`, löst die NIDs auf und
schreibt das Ergebnis nach klog. Es registriert sich nicht beim ResourceArbitrator, öffnet kein Avcap2
und fasst das Pad nicht an; es kann also neben einem laufenden Host gestartet werden.
`result=PASS` bedeutet: alle vier per NID gefunden und, bei bekannter Firmware, gleich der Tabelle.

Ergebnis auf 13.00 (29. Oktober 2026):

```text
ProsperoSunshine: ra_nid result=PASS rc=0 firmware=0x13000040 known=1 register=0xdb0 unregister=0xee0 request=0x10b0 list=0x11f0
```

## Checkliste für eine neue Firmware

Nur auf einer Konsole mit dieser Firmware, Schritt für Schritt, mit klog:

1. **SDK:** Läuft ein einfaches Payload (`runtime_probe.elf`)? Kennt das SDK die Kernel-Offsets der
   Version? Ohne das scheitern `kernel_dynlib_*` und damit alles Weitere.
2. **ResourceArbitrator:** `ra_nid_probe.elf` senden. Erwartet: vier aufgelöste Offsets, `rc=0`. Die
   Offsets in `ps_ra_known_firmware` eintragen (Schlüssel `Version & 0xffff0000`).
3. **Avcap2-Video:** `capture_probe.elf` bzw. `avcap_spike.elf`: `OpenVideo` muss mit der 0x2E0-Konfiguration
   0 liefern, das Bild muss decodierbar sein (`ffprobe`). Liefert `OpenVideo` einen Fehler, hat sich die
   Konfigurationsstruktur geändert: mit dem entschlüsselten `libSceAvcap2` neu vermessen
   (`docs/re/avcap2-exports.md`, Werkzeuge in `tools/re/`).
4. **Descriptor:** TSC-Zeitstempel in Wort 9 und IDR-Kennung prüfen (`capture_soak.elf`,
   `docs/avcap2-descriptor.md`).
5. **Audio:** `audio_spike.elf`: `OpenAudio` mit `cfg+0x1C = 180` muss PCM liefern.
6. **Bitrate:** Das Öffnen mit der Bitrate in `cfg+0xc8/0xcc/0xd0` muss auch über 15 Mbit/s laufen (13.00: 20, 30 und 50 Mbit/s
   ohne Fehler, `docs/re/hevc.md`). Hält der Encoder nur weniger, die Klemme des Hosts (50 Mbit/s außerhalb von 13.00) senken.
7. **HEVC (optional):** `hevc_spike.elf`: `OpenVideo` mit `cfg+0x24 = 1`, Speicher-Reservierung (`yN0w4tWivqs`) und
   `sceAvcap2Start` müssen 0 liefern, `ReadVideo2` Bilder; mit `ffmpeg -xerror` decodieren, das VPS muss vorn stehen. Bis das
   belegt ist, bietet der Host HEVC auf dieser Version nur mit dem experimentellen Schalter an (`ps_capture_hevc_verified` ist nur auf 13.00 und 10.20 wahr, siehe „Experimentelle Schalter“); die Prüfung
   in `app/src/psapp/experimental_list.h` muss dann für die Version erweitert werden.
8. **Pad:** `pad_probe.elf` (fügt ein neutrales virtuelles Pad hinzu und entfernt es wieder; vorher
   klären, dass niemand die Konsole gerade bedient). Prüfen: ioctl-Nummern, 0xA8-Blockgröße,
   Feldlagen von Tasten, Sticks und Triggern, Zuordnungsdialog. Das Pad hat keinen eigenen
   Firmware-Schalter; sein Verhalten auf anderer Firmware ist ungeprüft.
9. **Ende-zu-Ende:** Moonlight-Stream mit Bild, Ton, Eingabe; `tools/moonlight/` Smoke-Test, bei belegtem Schritt 7 auch mit `--hevc`.
10. Erst danach: Version in `README.md` und `STATUS.md` als geprüft eintragen.

Solange Schritte 3 bis 6, 8 und 9 für eine Version nicht belegt sind (Schritt 7 nur für HEVC), gilt sie als nicht unterstützt, auch wenn
die NID-Suche gelingt. Ausnahme 10.20: Schritt 6 ist dort nicht erfüllbar, der Encoder behält seine Standardrate (`STATUS.md`).

## `OpenVideo` meldet `0x81950008`

Auf 13.00 heißt das: Der Dienst führt die Ressource aus `cfg+0x28` nicht als aktiv (`docs/re/avcap2-exports.md`, Abschnitt 8.1).
Der Host wiederholt den Aufruf rund 1 s lang und schreibt danach die Liste der aktiven Ressourcen in `host_diag.log`.
`out/open_probe.elf` geht weiter: Es probiert die Ressourcen 180, 177, 185, 175 und 182 sowie 180 zusätzlich an `cfg+0x20` und `cfg+0x2c` und
schreibt ebenfalls nach `host_diag.log`, sodass der Bericht der App das Ergebnis zeigt. Nicht während eines Streams starten.

Ergebnis auf 13.00: Alle fünf Ressourcen öffnen und liefern in 2 s rund 120 Frames. 180 an `cfg+0x20` ergibt `0x81950001`, an `cfg+0x2c` öffnet es
normal. Der Host lief danach weiter.

## Berichte von anderen Firmwares

Ohne Konsole mit dieser Firmware ist der Bericht der App der erste Anhaltspunkt: Seite „Diagnostics“, im Netz unter
`http://<console>:47996/`, solange die Seite offen ist und der Host läuft, sonst `/data/prosperosunshine/diagnostics.txt`. Er nennt Firmware,
Rechte der Dateien und aus `host_diag.log` den Schritt von `ps_capture_open`, an dem die Capture scheiterte. Adressen, Gerätenamen
und IDs sind entfernt. Der Bericht ersetzt keine der Prüfungen oben.

## Experimentelle Schalter

Damit Tester auf ungeprüfter Firmware gesperrte Funktionen selbst einschalten und uns das Ergebnis schicken können, gibt es in den Einstellungen der App die Sektion „Experimental“. Bisher ist das auf keiner Konsole gelaufen, auch nicht auf 13.00; geprüft sind nur die Tests unten.

Die Regel: Was auf anderer Firmware nur gesperrt ist, aber funktionieren könnte, wird ein Schalter mit Diagnose und Fehlerlog. Alle stehen in einer Liste, `app/src/psapp/experimental_list.h`: Schlüssel in `sunshine.conf`, Name im Log, Texte der App und die Prüfung, wo die Funktion verifiziert ist. Daraus entstehen die Kacheln der App (ihre Breite folgt der Anzahl), das Lesen der Konfiguration im Host, die Log-Zeilen und der Bericht. Eine neue Sperre beginnt mit einem Eintrag dort. Was die Funktion im Versuch tut und wann sie als unbewiesen gilt, kommt dazu nach `experimental.h` und in den Versuchspfad von `capture.c`; das lässt sich nicht aus einer Tabelle ableiten.

Bewusst kein Schalter:

- **`ps_ra_resolve` lehnt ab**, wenn die ResourceArbitrator-Aufrufe nicht aufgelöst werden: Dann gibt es nichts, was man aufrufen könnte. Der Bericht sagt das in der Zeile „Capture“ und dass kein Schalter daran etwas ändert.
- **Die Rate-Felder auf 10.x** bleiben weg: Der Datensatz dort hat sie nicht, und der Encoder nimmt keine Bitrate an. Deshalb wirkt dort auch der Bitrate-Schalter nicht. Stattdessen setzt der Host die Quantisierer `cfg+0xc4` (IDR) und `cfg+0xc8` (übrige Bilder) nach der Bitrate des Clients (`encoder_qp.h`); ohne sie sendet der Encoder in einem bewegten Spiel 0,3 bis 0,4 Mbit/s.

Die übrigen Firmware-Abfragen im Code sind keine Sperren: `record_shift` legt das Layout fest, `firmware_verified` der App ändert nur Text (Suche nach `_verified`, `kernel_get_fw_version` und Firmware-Vergleichen).

| Schalter in der App | Schlüssel in `sunshine.conf` | Zählt nicht auf |
| --- | --- | --- |
| HEVC (8 Bit) | `experimental_hevc = 1` | 13.00, 10.20 |
| HDR (HEVC 10 Bit) | `experimental_hdr = 1` | 13.00, 10.20 |
| 1440p und 4K | `experimental_large_modes = 1` | 13.00 |
| Bis 150 Mbit/s statt 50 | `experimental_high_bitrate = 1` | 13.00; wirkungslos auf allen 10.x |
| Unter 1080p (720p, 540p, 360p) | `experimental_small_modes = 1` | 10.20 und 13.00 (auf 13.00 mit eigenem HEVC-Budget und Bitrate-Deckel je Modus, `STATUS.md`) |

Der Host liest die Schlüssel beim Start selbst (`main_ps5.cpp`), Sunshine kennt sie nicht. Die Regeln stehen in `src/platform/ps5/experimental.h`, `ps_capture_hevc_verified`, `ps_capture_large_modes_verified`, `ps_capture_high_bitrate_verified` und `ps_capture_small_modes_verified` bleiben unverändert das, was gemessen ist; der Host fragt jetzt `ps_capture_hevc_allowed`, `ps_capture_hdr_allowed`, `ps_capture_large_modes_allowed`, `ps_capture_high_bitrate_allowed` und `ps_capture_small_modes_allowed`. HDR bietet HEVC mit an, weil Moonlight Main 10 nur zusammen mit HEVC bekommt. `hevc_mode = 1` schlägt die Schalter.

Auf 10.x hat der Bitrate-Schalter keine Wirkung: Der Datensatz dort hat keine Rate-Felder (`configure()`, `shift < 0`), der Encoder nimmt keine Bitrate an, und `SetVideoBitrate` ruft der Host nicht auf (auf 10.20 gibt es 0 zurück, danach scheitert das nächste Lesen mit `0x80c78000`). Der Host ignoriert den Schalter deshalb auf allen 10.x und darunter, und die App zeigt „No effect here“, statt eine Wirkung vorzutäuschen.

**Varianten.** Auf 10.20 startet `cfg+0x24 = 1` allein HEVC (`docs/re/hevc.md`). Der Host probiert deshalb je Bittiefe erst `minimal` (nur `cfg+0x24`, Rate-Felder wie bei H.264, kein Encoder-Speicher), dann `full` (Felder und Speicher von 13.00). Was ein Bild geliefert hat, bleibt bis zum Neustart des Hosts. Auf 10.x gibt es nur `minimal`, weil `configure()` die inneren Felder dort weglässt. Mit `full` verschiebt `configure()` auf 13.60 die Felder ab `0x198` und den Encoder-Speicher um 8 mit dem Rest des Datensatzes; die Felder darunter bleiben an den Stellen von 13.00, weil nicht bekannt ist, wo der Block `0xb8` seine zusätzlichen 8 Byte hat. Ohne Schalter nimmt der Host die Variante `standard`, also den Datensatz wie zuvor, auch bei Feldern, die auf 13.60 nicht passen.

**Ablauf** (`ps_capture_open_or_fall_back`):

1. Solange eine Funktion unbewiesen ist, gehören Header und das erste Bild zum Öffnen: Ohne Bild nach 3 s (`rc=0`) oder mit Lesefehler gilt der Versuch als gescheitert. Das Bild geht verloren; der Stream fordert zu Beginn ohnehin ein IDR an.
2. Scheitert die Variante an einem Schritt ab dem Encoder (`encoder memory`, `OpenVideo`, `Start`, `headers`, `first frame`), kommt die nächste.
3. Sind alle gescheitert und liegt die Bitrate mit Schalter über 50 Mbit/s, öffnet der Host im selben Stream mit 50 Mbit/s neu (Spitze im selben Verhältnis) und beginnt die Varianten neu. Läuft das, bleibt es bis zum Neustart des Hosts bei 50. Scheitert auch 50, lag es nicht an der Rate, und der Schalter bleibt.
4. Sind alle gescheitert und ist der Modus über 1080p, fällt der Host wie bisher im selben Stream auf 1080p zurück und beginnt die Varianten neu. Läuft 1080p, ist 1440p/4K bis zum Neustart aus. Ein Modus unter 1080p fällt ebenso auf 1080p zurück (`ps_exp_mode_may_fall_back`): Der Client bekommt dann, was jeder Client vor diesen Modi bekam, statt keinen Stream. War sein Schalter noch unbewiesen, also vor seinem ersten Bild, und läuft dann 1080p, sind 720p, 540p und 360p bis zum Neustart aus. Wo sie verifiziert sind oder schon liefen, gilt der Rückfall nur für diesen Stream (ein Titel, der startet oder endet, kann den Modus kurz ablehnen), und der nächste Stream fragt wieder nach dem kleinen Modus. Der Schalter der jeweils anderen Seite bleibt.
5. Scheitert auch 1080p, ist die Funktion bis zum Neustart des Hosts aus: HEVC, oder bei 10 Bit nur HDR. Serverinfo und RTSP lesen `active_hevc_mode` je Stream, also sieht der nächste Stream nur noch H.264 und 1080p.

**Entscheidung nach einem Fehlschlag.** Der Stream, in dem HEVC oder HDR scheitert, endet. Der Client hat seinen Decoder schon für den ausgehandelten Codec aufgesetzt und hat keinen Weg zurück; H.264 in diesem Stream zu schicken, ergäbe nur ein schwarzes Bild (`STATUS.md`, iPhone). Moonlight kommt beim nächsten Start auf H.264. Aussperren kann das nicht, weil der Zustand nur im Speicher liegt und nichts in `sunshine.conf` ändert; auch das Ausschalten bleibt beim Tester. Ein größerer Modus bleibt ein Fall für den Rückfall im selben Stream, das kann der Client schon heute; ein kleinerer auch, weil er nur vor dem ersten Bild zurückfällt und der Client dann 1080p bekommt wie ohne diese Modi. Die Bitrate auch: Der Client bekommt nur weniger Bits und merkt sonst nichts.

Nicht der Funktion zugerechnet werden: Capture belegt, PS VR, verlorene Ressource, abgelehnte Firmware und die Ablehnung des Modus durch den Arbitrator (einzelne Titel verweigern 4K, `docs/re/hevc.md`). Eine Funktion, die schon einmal lief, geht durch spätere Fehlschläge nicht mehr aus, ein Titel kann beim Start kurz alles scheitern lassen. Ein solcher Fehlschlag vor dem ersten Erfolg, etwa `0x81950008` während ein Titel startet, schaltet die Funktion dagegen bis zum Neustart aus, denn von einer dauerhaften Ablehnung ist er nicht zu unterscheiden.

**Absturz.** Geht der Host innerhalb eines Versuchs unter, gäbe es keine Ergebniszeile, und der nächste Stream nähme denselben Weg. Deshalb nennt die Datei `experimental_attempt` im Datenordner den laufenden Versuch. Findet der nächste Start sie, schreibt er die Ergebniszeile mit `step=host went down` nach und lässt die Funktion für diesen Lauf aus. Der übernächste Start versucht es wieder. Einen hängenden Aufruf fängt das nicht ab.

**`host_diag.log`** (bei Schalter an, sonst keine Zeile mehr als zuvor; der Parser `parse_host_log` in `app/src/psapp/diag.cpp` liest genau diese Felder, `step` bis zum Zeilenende):

```text
ProsperoSunshine: experimental switches=hevc set=hevc firmware=0x13600000
ProsperoSunshine: experimental attempt switches=hevc firmware=0x13600000 trial=hevc codec=hevc bits=8 mode=1920x1080 fps=60 kbit=20000 variant=minimal
ProsperoSunshine: experimental result=failed switches=hevc firmware=0x13600000 trial=hevc codec=hevc bits=8 mode=1920x1080 fps=60 kbit=20000 variant=minimal error=-1 rc=0x80c78000 step=Start
ProsperoSunshine: experimental result=ok switches=hevc firmware=0x13600000 trial=hevc codec=hevc bits=8 mode=1920x1080 fps=60 kbit=20000 variant=full error=0 rc=0 step=none
ProsperoSunshine: experimental disabled feature=hevc
```

`switches` sind die Schalter, die auf dieser Firmware zählen, `set` die in der Datei, `trial` was dieser Versuch zeigen muss (`hevc`, `hdr`, `large`, `bitrate`; `none` bei einem gewöhnlichen Stream), `kbit` das Mittel, mit dem er öffnet. `feature=bitrate` heißt: wieder 50 Mbit/s bis zum Neustart. Der Bericht der App führt daraus den Abschnitt „Experimental switches“ mit Ergebnis und `rc` je benutzter Funktion, im Erfolg wie im Fehlschlag, und mit jedem Versuch, ohne Adressen, Gerätenamen und IDs. Die Home-Seite der App bittet nach einem Fehlschlag und nach einem Erfolg darum, den Bericht auf GitHub zu posten.

**Was ein Bericht hergibt:** Läuft `minimal` oder `full` auf einer Firmware und sehen Tester ein richtiges Bild, gehört die Version nach den Schritten der Checkliste in die Prüfung ihres Eintrags in `experimental_list.h`. Ein Bericht, der auf `Start` oder `OpenVideo` scheitert, nennt den Code: `0x81950001` ist der Validator, also ein falsches Layout (`record_shift`). Ein Erfolg sagt nur, dass Bilder kamen; ob sie sich dekodieren lassen, sagt der Tester.

**Welcher Build:** Die Kopfzeile `host version=... commit=...` nennt den Commit. `scripts/build.sh` nimmt `PROSPERO_COMMIT` aus der Umgebung, sonst `git rev-parse`, sonst `unknown` mit einer Warnung. `scripts/build.ps1` und CI reichen den Wert hinein. In einem Worktree zeigt `.git` auf einen Ordner außerhalb des Containers, dort findet git nichts: Ohne `-e PROSPERO_COMMIT=<hash>` steht dann `unknown` da. Der Hash ist der Stand von `HEAD`; nicht eingecheckte Änderungen zeigt er nicht.

**Warten auf die Ressource:** Endet das Warten nach `RUNNING` ohne Freigabe, steht in `host_diag.log` zum Beispiel `capture RUNNING outcome=refused value=3 waited_ms=120 list_rc=0 active=175,177`. `outcome` ist `timeout` (keine Antwort in 6 s, bis 0.2.2 in 2 s; eine Zuteilung nach 500 ms oder mehr steht als `capture RUNNING granted after N ms` im Log), `refused` (`value` ist `data[0]` der Antwort, sonst `-1`) oder `revoked`; `active` ist die Liste aus `list()`. Der Bericht führt das in der Zeile „Capture“ aus. Am Datensatz ändert das nichts.

**Prüfung ohne Konsole.** `tests/capture_records_test.c` übersetzt `capture.c` nativ gegen `tests/fake_avcap.h` und vergleicht 240 Datensätze (12.40, 13.00, 13.60, 10.20, 10.40; H.264 und HEVC; 1080p bis 4K; 30 und 60 fps; 8 und 10 Bit; 20 und 150 Mbit/s) aus `configure()` und wie `OpenVideo` sie bekommt mit `tests/capture_records.golden`, erzeugt aus dem Code vor den Schaltern. `tests/capture_open_test.c` führt die Abläufe oben durch den echten Pfad von `ps_capture_open`.
