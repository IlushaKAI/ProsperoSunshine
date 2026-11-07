# Arbeitsstand

Stand: 31. Oktober 2026. Plan und Reihenfolge: `ROADMAP.md`. Ziel und Regeln: `PLAN.md`. Architektur: `docs/adr/001-sunshine-port.md`.

## Kurzfassung

**Der Host streamt Ende-zu-Ende.** Ein Client auf Basis von moonlight-common-c (derselben Protokollbibliothek, die Moonlight verwendet) paart sich mit der PS5, startet die App „PS5 Screen“ und empfängt den PS5-Bildschirm als H.264 oder HEVC in 1080p, 1440p oder 4K mit 59,94 fps, den Spielton als Opus und schickt Controller-Eingaben an ein virtuelles DualSense. Pairing übersteht einen Host-Neustart.

Eine 30-Minuten-Sitzung und zehn Wiederverbindungen liefen fehlerfrei.

Echte Moonlight-Clients (Moonlight Web, Moonlight PC, Moonlight auf dem iPhone im WLAN) sind gepairt und streamen; Spiele (GTA III, Mario 64) erscheinen im Stream, und Spielstart/-ende überbrückt der Host automatisch.

**PS5-App (Phase 8):** Die App „ProsperoSunshine“ (`PPSA47989`) liegt unter `app/`. Live per Controller auf der Konsole geprüft: Host-Status, PIN-Eingabe für eine echte Pairing-Anfrage (Client meldet `PairStatus=1`), Autoload über den Payload Manager an und wieder aus (Eintrag `local/prospero-sunshine` in `/data/pldmgr/autoload.txt`, andere Einträge unverändert), „Restart host“ (Host fährt sauber herunter und startet neu). Einstellungen speichern und auf Standard zurücksetzen live geprüft (`wan_encryption_mode = 0` geschrieben, danach Datei wieder leer). Wunsch für die nächste Runde: Optik auf das Niveau von ProsperoStore/ProsperoEden/Playden heben. Autoload steht aktuell auf **aus**.

**Bild-/Ton-Synchronisation (P5.2, erste Messung; die Bildwerte sind überholt, siehe „Bild und Ton beim Tastendruck“ unter „Stand 30. Oktober 2026“):** Navigationsklick im Home-Menü als gemeinsames Ereignis, 60 s, gemessen am Client (`tools/moonlight/av_sync.py`). Tastendruck → Bildänderung 23–25 ms, Tastendruck → Klickton 28–49 ms; Ton liegt 5–23 ms hinter dem Bild (burst-korrigiert Median ≈ 0 ms), also innerhalb der üblichen ±45-ms-Toleranz. Nur die Rechts-Drücke sind auswertbar (bei Links-Drücken erkennt der Bild-Detektor die Änderung nicht zuverlässig); der Drift über 60 s (−4,3 ms/min) ist zu kurz gemessen, ein 10-Minuten-Lauf steht aus.

**Noch nicht geprüft:** Moonlight auf Android/TV, andere Firmwares als 13.00 und 10.20.

## Nachweise nach Roadmap

| Paket | Ergebnis | Beleg |
| --- | --- | --- |
| P0.1 | Alle Quellen mit SHA256 in `deps.lock`, Patches unter `patches/<name>/`, modulare `scripts/deps.sh` + `scripts/deps/*.sh` mit Stempeln | — |
| P0.2/0.3 | Boost 1.92.0 (Sunshines Pin), Simple-Web-Server, nlohmann_json gebaut; Live: Asio-TLS 1.3, Boost.Log, property_tree | `src/boost_probe.cpp` |
| P0.4 | FFmpeg 9.0.2 minimal (avcodec/avutil/swscale, CBS, kein Codec); Sunshines `cbs.cpp` läuft unverändert auf der Konsole | `src/cbs_probe.cpp` |
| P0.5 | Ein CMake-Projekt baut Host, Probes und Bibliotheken | `CMakeLists.txt`, `scripts/build.sh` |
| P0.6 | `out/prospero_sunshine.elf` (12,4 MB) startet, beantwortet `/serverinfo`, fährt per `QUIT` sauber herunter | — |
| P0.7 | ADR-001 festgeschrieben | `docs/adr/001-sunshine-port.md` |
| R1 | `sceAvcap2RequestKeyFrame`: IDR nach 2–3 Frames (8/8); im Stream nach ~50 ms | `src/avcap_spike.cpp` |
| R2 | Spielton über `sceAvcap2OpenAudio` (cfg+0x1C = RA-Id 180): PCM float32 Stereo 48 kHz, 1024 Frames/Paket; auch Opus 10/20 ms möglich | `src/audio_spike.cpp`, `docs/re/avcap2-exports.md` |
| R3 | `sceAvcap2SetVideoBitrate` wirkt live; bis 15 Mbit/s (Spitze ×1,12) stabil, ab 16 Mbit/s stoppt der Encoder. Überholt: Seit dem HEVC-Einbau öffnet der Host mit der Bitrate in der Konfiguration und klemmt auf 50 Mbit/s (siehe „HEVC im Host“) | `src/bitrate_spike.cpp` |
| R4 | Host läuft als Payload-Daemon, Daten unter `/data/prosperosunshine/`, Neustart per Steuer-Socket + ELF-Loader | — |
| R5 | Natives virtuelles Pad ohne Mono; neues Pad löst „Wer benutzt diesen Controller?“ aus (im Stream sichtbar und beantwortbar) | `src/pad_probe.cpp` |
| R6 | Ports 47984/47989/48010 TCP und 47998–48000 UDP frei; Discovery (mDNS) nicht untersucht | — |
| P1.1–P1.4 | `CaptureSession` mit Tests; 10 min Home-Menü: 35 966 Frames, 59,94 fps, 600 IDR, fehlerfrei decodierbar; 5 saubere Zyklen; RSS konstant 12,8 MB; Descriptor-Felder entschlüsselt (u. a. TSC-Zeitstempel) | `src/capture_soak.cpp`, `docs/avcap2-descriptor.md` |
| P2.1–P2.4 | Identität bleibt über Neustart; Pairing per PIN über Steuer-Socket; ungepairter Client wird abgewiesen (401); Launch/Resume/Cancel; zweite Sitzung abgelehnt | `tools/moonlight/` |
| P3.1–P3.3 | Video über unveränderten `stream.cpp`-Pfad inkl. FEC; 0 Frame-Lücken; IDR-Anforderung des Clients bedient | `tools/moonlight/run_smoke.sh` |
| P3.4 | Nicht nötig: der PS5-Encoder schreibt bereits VUI mit `max_num_reorder_frames=0`, `max_dec_frame_buffering=1` | `cbs_probe` |
| P4.1/4.2 | Gamepad-Ereignisse kommen an, Pad wird angelegt; Tastenabbildung als getestete Tabelle | `src/input/gamepad_map.hpp`, `tests/gamepad_map_test.cpp` |
| P5.1 | Echter Spielton im Stream (RMS ≈ −40 dBFS im Home-Menü), Sunshines Opus-Encoder mit 5-ms-Paketen | — |
| P1.4 / P6.2 | Spiele als Bildquelle: GTA III und Mario 64 erscheinen im Stream. Spielstart und -ende entziehen dem Host die Capture-Ressource (`0x81950009`); der Host öffnet Video und Ton automatisch neu (1,6–2,1 s Pause, Frame-Nummern laufen weiter). 120-s-Stream über zwei Spielstarts/-enden: 6989 Frames, 0 Frame-Lücken, fehlerfrei decodierbar, 0 Audiofehler | `out/game_tiles.png` (lokal), `src/sunshine/video_ps5.cpp` |
| Echte Clients | Moonlight Web und Moonlight (PC) gepairt (PIN über Steuer-Socket) und gestreamt; Moonlight Web braucht ein App-Cover (jetzt eingebettet) | — |
| iPhone | Moonlight auf dem iPhone (WLAN, PS5 per Kabel) gepairt, Bild und Ton laufen. Vorher kam nur Ton: Der Host meldete im RTSP-DESCRIBE HEVC (`active_hevc_mode = 0` heißt in Sunshine „unbekannt“, nicht „aus“), das iPhone wählte HEVC (`bitStreamFormat:1`) und fand in unserem H.264 nie einen Keyframe (`ML_ERROR_NO_VIDEO_FRAME`). Jetzt `1` = aus für HEVC und AV1 | `src/sunshine/video_ps5.cpp`, Debug-Log der Sitzung |
| Steuer-Socket | `STATUS` meldet während eines Streams `address=<ip> client=<name>` (live: `address=… client=handy`), sonst leer; Sunshine-Patch 0005 merkt sich den Client des letzten Launch/Resume | `src/control/control_server.cpp`, `patches/sunshine/0005-last-launch-client.patch` |
| P4.3 | Client mit gehaltenem Steuerkreuz + R2 hart beendet: Host erkennt den Abbruch nach ≈ 5,2 s (ENet-Timeout) und setzt das Pad 0,4 s später neutral (`Resetting Input`). Keine dauerhaft hängenden Eingaben, aber ≈ 5,6 s Nachlauf | `tools/moonlight/hold_kill_test.sh`, `tools/klog_tail.py` |
| P6.2 (Teil) | 10 % verworfene Videopakete beim Client: Stream läuft durch, 18 von 1792 Frames fehlen (4 Lücken), 6 IDR-Anforderungen jeweils nach 50–70 ms bedient, längster Aussetzer 100 ms | `tools/moonlight/loss_test.sh` |
| P6 (Teil) | 30-Minuten-Sitzung bei 20 Mbit/s angefordert: 107 885 Frames, 59,94 fps, 0 Frame-Lücken, 0 FEC-Fehler, max. Abstand 22 ms, komplett decodierbar; 359 787 Audiopakete ohne Fehler. 10/10 Wiederverbindungen. Host-Speicher 18,8–26,0 MB je nach Sitzungszustand, ohne Anstieg über die Sitzungen | `out/moonlight/soak30.log`, `tools/moonlight/reconnect_test.sh` |

### Ende-zu-Ende-Messung (Smoke-Test, 20 s, 20 Mbit/s angefordert)

1196 Frames, 59,94 fps, 0 fehlende Frame-Nummern, 0 FEC-Fehler, max. Frame-Abstand 19 ms, SPS/PPS vor jedem IDR, Datei mit `ffmpeg -xerror` fehlerfrei decodierbar; Audio ≈ 190 Opus-Pakete/s ohne Decodierfehler; Host setzte 14 988 kbit/s.

## Offen und bekannte Grenzen

### Stand 30. Oktober 2026 (alles lokal in `main`, nichts gepusht)

Live auf der Konsole geprüft mit dem aktuellen Host (Testclient vom PC):

- **mDNS im echten Host:** `tools/mdns_query.py` sieht `PS5._nvstream._tcp.local` mit PTR, SRV (Port 47989), TXT und A. Ob Moonlight die PS5 von selbst listet, ist noch nicht gesehen.
- **NID-Auflösung im Host:** Bildaufnahme öffnet sich auf 13.00, Stream 15 s: 895 Frames, 0 Lücken, 0 Audiofehler, fehlerfrei decodierbar.
- **Client-Abbruch (behebt P9.8):** Client hart beendet ohne `/cancel`: Ping-Timeout, 0,2 s später `Session app stopped` und `virtual pad 0 detached (0)`. Danach `currentgame = 0`, ein neuer Client startet direkt, und die App bekommt sofort wieder Eingaben (RemoteEye). Vorher blieb das Pad angemeldet und nahm der App die Eingaben (Sunshine-Patch 0006).
- **Bildrate:** 720p30 angefordert: 298 Frames in 10 s, geliefert als 1080p; 1080p30: 297 Frames; 1080p60: 598 Frames. Jeweils 0 Lücken, 0 Audiofehler, fehlerfrei decodierbar.
- **Verbindungsaufbau:** Der auf 2 s gesenkte `ping_timeout` galt in Sunshine auch für die Zeit von `/launch` bis RTSP; Clients, die länger brauchten, scheiterten mit „RTSP OPTIONS -1“ (reproduziert mit dem Docker-Testclient, ≈ 3 s). Jetzt 10 s für den Aufbau, 2 s bleiben für die Abbruch-Erkennung (Patch 0007). Kommt kein Client, endet auch „PS5 Screen“.
- **App mit aktuellem Host:** App neu gebaut und installiert (`01.000.001`), sie kopiert ihren Host beim Start; „Restart host“ startet ihn neu („The host restarted with the saved settings“).
- **Moonlight auf dem iPhone (Nutzerbericht, 2026-10-31):** Die PS5 erscheint ohne IP-Eingabe zum Hinzufügen (P9.2), das Pairing mit PIN-Eingabe in der App klappt, die Steuerung funktioniert.
- **Release-Archiv:** `scripts/package.sh` lokal aus dem aktuellen Build ausgeführt: 23 Dateien, `sha256sum -c` ohne Fehler, keine lokalen Pfade oder Konsolenadresse im ELF (per `grep -a`).
- **Release 0.1.0:** Inhalt von `PPSA47989.zip` auf die Konsole kopiert, App gestartet, „Restart host“ ausgelöst, danach Stream 15 s: 896 Frames, fehlerfrei decodierbar. GitHub-CI baut von null und ist grün; das fand einen Fehler in der Baureihenfolge (`nlohmann-json` vor `libdisplaydevice`), den der lokale Cache verdeckt hatte. Die SHA-256 der Release-Datei stimmt mit dem Katalog-Eintrag überein, der Katalog-Bot hat den Eintrag nach seinen Prüfungen gemergt.
- **Bild und Ton beim Tastendruck (P9.4):** Gemessen am Client mit `tools/moonlight/av_sync.py`, 44 Drücke im Abstand von 1537 ms, nur das Stream-Pad verbunden. Die Zeiten laufen vom Absenden des Drucks bis zur Ankunft am Client.

  | Vordergrund | Taste → Bild | Taste → Ton | Ton minus Bild (Median) |
  | --- | --- | --- | --- |
  | ProsperoSunshine-App | 49–85 ms, Median 57 (44/44) | 31–71 ms, Median 47 (44/44) | −10 ms |
  | Home-Bildschirm | 87–113 ms, Median 95 (44/44) | 80–122 ms, Median 105 (29/44) | +10 ms |

  Die ganze Kette samt Encoder und Netz bleibt in der App also im Median unter 60 ms; die rund 40 ms mehr auf dem Home-Bildschirm entstehen in der Oberfläche der Konsole. Der Eingabeweg im Host hat keinen eigenen Puffer oder Takt. Bild und Ton liegen in beiden Fällen innerhalb der ±45-ms-Toleranz. Die Drift bleibt klein: −0,01 ms/min über 10 Minuten, +0,00 ms/min im Lauf oben.
- **Korrektur früherer Bildwerte:** Der Bild-Detektor sprach auf die Schlüsselbilder an, die der Encoder einmal pro Sekunde sendet (32 von 37 Treffern fielen genau darauf). Die 23–25 ms der ersten Messung waren daher keine Reaktion: Links-Drücke lagen dort exakt 500 ms später (524 ms), das 1-Hz-Muster im 1,5-s-Raster. `av_sync.py` überspringt Schlüsselbilder jetzt und drückt im Abstand von 1537 ms. Im Kontrolllauf ohne Klickton (0/30, 342 ms) hatte die Startsequenz von `stream_test` zudem die App gestartet; für den 10-Minuten-Lauf (0/380, 265 ms) ist das dieselbe Sequenz und damit die wahrscheinliche Ursache, belegt ist es dort nicht. `av_sync.py` nutzt jetzt immer `--no-script`. Die 28–49 ms beim Ton aus der ersten Messung (Home-Bildschirm) lassen sich nicht mehr nachstellen; dass der Tonweg nicht langsamer geworden ist, zeigt der App-Wert oben.

**PS-Taste über Select (2026-10-31):** Moonlights Bildschirm-Controller am Handy hat keine PS-Taste. Select halten sendet sie jetzt nach 500 ms (`back_button_timeout`), ein kurzer Druck sendet Create beim Loslassen. Gemessen mit `stream_test --tick-kind back` auf dem Home-Bildschirm, je zwei Drücke im Abstand von 6 s, ausgewertet an Bildern aus dem empfangenen Stream: 700 ms halten öffnet das Kontrollzentrum ohne Create-Menü, der zweite Druck schließt es. 120 ms öffnet das Create-Menü, der zweite Druck schließt es. In Moonlight auf dem iPhone öffnet Select halten das Kontrollzentrum (Nutzertest).

**Release 0.1.1:** App aus dem Release-Build installiert (`01.000.002`, in der App angezeigt), „Restart host“ ausgelöst, danach Stream 27 s: 1614 Frames, fehlerfrei decodierbar; 700 ms Select öffnet das Kontrollzentrum über der App, der zweite Druck schließt es. Das Archiv prüft `sha256sum -c` ohne Fehler, im ELF stehen keine lokalen Pfade oder Konsolenadresse (per `grep -a`).

**Rumble (P9.10, 2026-10-31):** Der Host liest die Motorwerte des virtuellen Pads wie Remote Play und schickt Änderungen an den Client (`docs/re/pad-output.md`). Gemessen mit Homebrew UI Lab im Vordergrund und `stream_test --tick-seq left,...`, nur das Stream-Pad verbunden: 8 von 8 Abweisungen kamen als `rumble low=0 high=16191` (Stärke 0,25 auf dem kleinen Motor) und danach als Stopp an. Der Start lag 6–22 ms nach dem Absenden des Drucks, der Stopp 30–50 ms nach dem Start; alles am Client-Log abgelesen, 19 s Stream fehlerfrei decodierbar. Vor Patch 0008 kam von denselben Pulsen nur der Stopp an, also vibrierte nichts.

**Lichtleiste (P9.10, 2026-10-31):** Der Host schickt Farbänderungen des virtuellen Pads an den Client. Gemessen mit Homebrew UI Lab im Vordergrund und `stream_test`: 2 s nach Stream-Start meldet das Client-Log `led pad=0 rgb=0,0,255` (Systemblau), 50 ms später `127,240,216` (die Farbe des Titels). Rumble lief im selben Stream weiter (6 von 6 Pulsen), 0 Frame-Lücken. Ob ein echter Moonlight-Client die Farbe am Controller zeigt, ist nicht geprüft.

**HEVC-Spike (P9.7, 2026-10-31):** Avcap2 liefert HEVC (`docs/re/hevc.md`). Gemessen mit `src/hevc_spike.cpp` auf dem Home-Bildschirm, je 180 Frames in 1080p60, frischer Kanal je Stufe: Bei 6, 15, 30, 50, 80 und 100 Mbit/s kein Fehler. `ffprobe` meldet HEVC Main, Level 5.0, yuv420p, BT.709; die Streams zu 6, 50 und 100 Mbit/s decodiert `ffmpeg` mit 180 Frames und ohne Fehlermeldung, sobald das VPS vor SPS und PPS steht. Das decodierte Bild zeigt den Home-Bildschirm korrekt. 30 → 10 → 30 Mbit/s im laufenden Kanal geht, 20 → 40 Mbit/s bricht nach dem Wechsel mit `0x80c78000` ab. 1440p und 4K scheitern bei der Speicher-Reservierung (`0x81950003`). Der Code nach dem Aufräumen ergab dieselben Ergebnisse, seine Streams wurden nicht erneut decodiert. Nicht gemessen: Spielszenen mit viel Bewegung, längere Läufe, echte Moonlight-Clients.

**Schlüsselbilder alle 10 s statt jede Sekunde (2026-10-31, noch in keinem Release):** Der HEVC-Encoder ließ nach jedem Schlüsselbild Bilder aus (bei 15 und 20 Mbit/s eines, bei 5 Mbit/s fünf, also 100 ms Pause je Sekunde und 54 fps), die Bildnummern verrieten das nicht. Der Host setzt den Abstand jetzt auf 10 s (`docs/re/hevc.md`). Alle Werte mit dem Windows-Client (`NATIVE=1`):

- **Home-Bildschirm, 25 s:** HEVC 5 Mbit/s 59,62 fps (vorher rund 54), 15 Mbit/s 59,90 (vorher 58,94), 31 Mbit/s 59,94; H.264 bei 5, 15, 31 Mbit/s je 59,94. Jeweils 0 Lücken, `ffmpeg -xerror` fehlerfrei.
- **Bewegung (Demo von Mario 64, je 20 s, Stufen 5, 15, 31 und 50 Mbit/s, beide Codecs):** 0 Lücken, fehlerfrei decodierbar. H.264 59,93–59,96 fps. HEVC 59,63–59,90 fps, bei 5 Mbit/s 58,64 mit 21 Sprüngen über 25 ms und längstem Abstand 86 ms: Dort lässt der Encoder auch ohne Schlüsselbild Bilder aus. Ein Vergleich derselben Szene mit dem alten Abstand wurde nicht gemessen, die Bildqualität auch nicht.
- **Verlust (`loss_test.sh`, Docker-Client, Home-Bildschirm, 30 s):** 10 %: 1416 Frames, 96 Schlüsselbilder auf Anforderung, längster Aussetzer 170 ms. 3 %: drei Läufe, zwei mit 85 und 88 ms; der erste, wenige Sekunden nach dem Schließen eines Spiels begonnen, mit einem Aussetzer von 1,9 s, der sich nicht wiederholte. Ein Lauf mit dem Windows-Client (H.264, 20 s, 3 s nach dem Schließen eines Spiels) zeigte keine Pause, längster Abstand 19 ms; die Ursache bleibt offen, dieser Lauf lief im Docker-Client.
- **Starts:** 50 Stream-Starts ohne Fehlschlag (unten).
- **Wiederverbinden (`reconnect_test.sh`, je 6 s, ohne Pause):** Windows-Client 40 von 40. Docker-Client einmal 9 von 10 und einmal 29 von 30. Der Fehlschlag von 9/10 lag beim Host, siehe unten. Der von 29/30 war ein Timeout der TCP-Verbindung zu RTSP und trat mit dem Windows-Client nicht auf.
- **Belegt beim Öffnen (`step=resource list error=-2`, Log):** Direkt nach dem Schließen der eigenen Sitzung führte der ResourceArbitrator die Ressource noch, das erneute Öffnen scheiterte als „belegt“ und der Client bekam kein Bild. Der Host wartet jetzt bis 0,8 s darauf. In den 70 Öffnungen mit der Korrektur kam es nicht wieder vor, die Wartezeit selbst wurde dabei also nicht ausgelöst und ist nicht live geprüft.
- **Spiel im Stream (HEVC, 70 s, Mario 64 gestartet und geschlossen):** 3866 Frames, 0 Lücken, fehlerfrei decodierbar, 0 Audiofehler. Pausen 0,42 s und 1,88 s beim Start, 2,83 s beim Schließen; das erste Bild nach jeder Pause ist ein Schlüsselbild.
- **30 fps (Home-Bildschirm, 15 Mbit/s, 25 s):** H.264 29,97 fps, HEVC 29,85 fps, 0 Lücken, fehlerfrei decodierbar, periodisch ein Schlüsselbild alle 300 Frames.
- **Moonlight 6.2.0 am PC (per Kommandozeile gestartet, Home-Bildschirm, kein Spiel, keine Eingabe, je rund 45 s, 15 Mbit/s eingestellt, Moonlights Statistik im Log):** HEVC 59,93 fps empfangen und decodiert, 59,84 fps angezeigt, 0 % Netzverlust, 0,12 % durch Jitter. H.264 59,94 fps, 0 % Verlust. Der Host setzte dabei 9452 kbit/s; das ist die Rechnung von Sunshine für FEC und Ton (`rtsp.cpp`), keine Klemme des Hosts.
- **Nicht geprüft:** Moonlight in einem Spiel und auf anderen Geräten mit diesem Stand; Dauertest über mehrere Stunden (ein 2-Stunden-Lauf wurde auf Wunsch des Nutzers früh abgebrochen und nicht ausgewertet).

**Bericht von Firmware 10.40 (Issue 2, 2026-10-31, nicht selbst gemessen):** Host, Pairing und „Start at boot“ laufen laut Bericht, `OpenVideo` scheitert mit `0x81950008`. Der Host wiederholt den Aufruf jetzt und schreibt die aktiven Ressourcen in den Bericht; `open_probe.elf` probiert Varianten durch (`docs/firmware-porting.md`). Auf 13.00 geprüft: Der neue Host öffnet wie bisher und streamt H.264 und HEVC, der Probe läuft durch. Auf 10.40 ist beides ungeprüft.

**Testclient als Windows-Programm (2026-10-31):** `tools/moonlight/build_windows.sh` baut `stream_test.exe`; `NATIVE=1` nutzt es.

- **Docker im Vergleich:** Gleiche Szene (Home-Bildschirm), 20 s, Bitrate-Anforderung 80 Mbit/s (der Host klemmt auf 50). Windows: H.264 1195 und 1196 Frames, HEVC 1180 Frames, jeweils 0 Lücken. Docker: 300 und 224 Frames (H.264), 365 (HEVC), längster Aussetzer bis 3,7 s. Aussagen zu Verlust und hohen Bitraten aus dem Docker-Client gelten also für dessen Netz, nicht für den Host.
- **Fehlstarts:** Mit dem alten Schlüsselbild-Abstand 100 Starts in Folge (je 5 s, abwechselnd H.264 und HEVC, Home-Bildschirm, kein Fehlschlag, 0 Lücken, H.264 295–299 Frames, HEVC 266–269), mit dem neuen 50 Starts (kein Fehlschlag, 0 Lücken, H.264 295–297 Frames, HEVC 267–287). Zusammen mit den Messungen unten: Im Docker-Client 1 Fehlschlag in 86 Starts, im Windows-Client 0 in 150. Das spricht gegen den Host als Ursache, belegt es aber nicht; die Stichprobe könnte bei dieser Rate auch zufällig fehlerfrei sein (rund 17 %). HEVC beginnt rund 0,4 s später als H.264. Mit 0.2.4 (`f6d55610`, 2026-11-04) 150 weitere Starts im Windows-Client (`tools/moonlight/start_series.sh`, je 5 s, abwechselnd H.264 und HEVC, Home-Bildschirm): kein Fehlschlag, 0 Lücken, H.264 295–299 Bilder, HEVC 292–295. Im Windows-Client damit 0 Fehlschläge in 300 Starts.

**HEVC im Host (P9.11, 2026-10-31):** Der Host bietet auf Firmware 13.00 HEVC an (`ServerCodecModeSupport` 257) und öffnet H.264 und HEVC jetzt mit der Bitrate des Clients in der Konfiguration. Grenze 50 Mbit/s statt 15. Alles gemessen mit `stream_test` (`--hevc`), Bildquelle meist die Demo von Mario 64. Echter Client siehe „Nutzertest“.

- **Nutzertest (2026-10-31):** Moonlight am PC, GTA III: „lief gut“. Laut Host-Log HEVC, 1080p60, 15 Mbit/s, über 8 Minuten ohne Capture-Ausfall; dann zwei Entzüge (`0x81950009`) kurz hintereinander, nach 0,6 und 0,35 s wieder da. Ob sie vom Spielstart oder -ende kamen, ist nicht festgehalten. Moonlight auf dem iPhone: „ging auch gut“. Laut Host-Log wählte es von selbst HEVC, bat um 720p60, bekam 1080p60 bei 7,3 Mbit/s, rund 2 Minuten; kurz vor dem Ende dieselben zwei Entzüge kurz hintereinander, nach 0,6 und 0,4 s wieder da. Moonlight am PC mit GTA III bei hoher Bitrate: „hat gut geklappt“. Laut Host-Log HEVC, 1080p60, 49,5 Mbit/s, rund 5,5 Minuten ohne Capture-Ausfall; rund 9 s vor dem Ende wieder die zwei Entzüge, nach 0,6 und 0,3 s wieder da. Dieses Paar trat in allen drei Sitzungen auf, passt also vermutlich zum Schließen des Spiels. Die tatsächlich erzeugte Datenrate und die Bildqualität sind nicht gemessen, das Urteil ist der Eindruck des Nutzers.

- **Stream:** HEVC und H.264 bei 2, 15 und 31 Mbit/s (HEVC-Werte 2 und 31, dazu 15 bei 30 fps; H.264 alle drei und 15 bei 30 fps), je 10 s: 0 Frame-Lücken, `ffmpeg -xerror` fehlerfrei. Ein Lauf (HEVC 2 Mbit/s) begann rund 2 s später als die anderen, die Ursache ist nicht geklärt.
- **Dauer:** HEVC 31 Mbit/s über 120 s: 7082 Frames decodiert, 5 Lücken mit zusammen 20 fehlenden Frames (je 68–72 Pakete angekommen, passend zur Docker-Grenze unten; der Client holte sich jeweils ein IDR). H.264 31 Mbit/s über 60 s: 3595 Frames, 0 Lücken.
- **Spiel schließen und neu starten im Stream:** HEVC läuft 1,7 s nach dem Entzug weiter (`0x81950009`, reopened), 2425 Frames in 45 s, 0 Lücken, Ton ohne Fehler.
- **Codec-Wechsel:** H.264 → HEVC → HEVC → H.264 in aufeinanderfolgenden Sitzungen fehlerfrei. Öffnet der Ton den Kanal zuerst, öffnet ihn das Bild mit dem richtigen Modus neu (`ps_capture_mode_current`).
- **Encoder ohne Netz (`hevc_spike`, Mario 64, 3 s je Stufe):** HEVC 6–100 Mbit/s und H.264 15–50 Mbit/s ohne Fehler; bei 100 Mbit/s entstanden 33 Mbit/s, größter Frame 239 KB. Eine Szene, die 50 Mbit/s voll ausschöpft, wurde nicht gemessen.
- **Netz (`udp_burst.py`, je 180 Bursts zu 1024 Byte, 60 pro Sekunde):** Windows nativ empfängt 60, 100, 150 und 250 Pakete je Burst vollständig (bis 122 Mbit/s). Im Docker-Container kommen 60 vollständig an, von größeren Bursts nur 67–93 Pakete. Bei 55 Mbit/s angefordert und viel Bewegung bricht der Testclient daher ab (6 von 600 Frames), obwohl der Host fehlerfrei sendet. Über 31 Mbit/s liegt daher nur der Nutzertest vor.
- **Nicht geprüft:** das Verhalten auf anderer Firmware (nur im Code).

**Release 0.1.2:** App aus dem Release-Build installiert (`01.000.003`, in der App angezeigt), „Restart host“ ausgelöst, das Log meldet „H.264 and HEVC“. Danach mit Homebrew UI Lab je 15 s Stream über `stream_test`: HEVC 879 und H.264 897 Frames, fehlerfrei decodierbar, 0 Frame-Lücken, je 6 von 6 Rumble-Pulsen, Lichtleiste `0,0,255` und `127,240,216`. Das Archiv prüft `sha256sum -c` ohne Fehler, im ELF stehen keine lokalen Pfade oder Konsolenadresse (per `grep -a`). Veröffentlicht am 2026-10-31: Die Store-API (`/api/v1/apps/PPSA47989.json`) meldet `0.1.2`, `01.000.003` und die sha256 des Release-Zips. Die Installation dieser Version aus dem Store ist nicht geprüft.

**Diagnose und Nachprüfungen (2026-10-31, nach 0.1.2, noch in keinem Release):**

- **Diagnosebericht:** Die Seite „Diagnostics“ zeigt den Bericht auf der Konsole; per `curl http://<console>:47996/` kam er mit HTTP 200 an, während die Seite offen war. `grep` auf Konsolenadresse und Gerätenamen: 0 Treffer. Er nennt Firmware `13.00 (0x13000040), verified`.
- **„Start at boot“ bei fremder Datei (Issue 2):** `autoload.txt` per FTP als uid 0, Modus 0600 nachgestellt, wie sie der Payload Manager laut Quelle anlegt. Mit laufendem Host liest die App den Stand über den Host, der Schalter klappt, die Datei hat danach Modus 0666 und den Eintrag. Ohne Host meldet die App `errno 13` und bittet, den Host zu starten. Das ist die Bedingung der gemeldeten Meldung; dass auf 10.40 dieselbe Ursache vorliegt, ist nicht belegt. Der Moonlight-Fehler aus Issue 2 bleibt ungeklärt.
- **Hinweise zur Capture:** `avcap_spike.elf` hielt die Capture, ein Stream endete mit `stream_test` Exit 4; der Bericht zeigte `open failed`, Fehler -2 und den Hinweis auf eine belegte Capture. Nach einem normalen Stream steht dort `last open ok`. Die Zeile auf der Startseite ist für diesen Fall nicht fotografiert (Screenshot in dem Zustand verweigert). Die Fälle „failed“ und „abgewiesene Firmware“ wurden mit einer gefälschten `host_diag.log` angezeigt, HDR nur im Unit-Test.
- **`hevc_mode = 1`:** `ServerCodecModeSupport` ist 1 (sonst 257), ein H.264-Stream läuft, eine HEVC-Anfrage liefert keine Daten.
- **`back_button_timeout = -1`:** Mit dem Standard öffnet Select 800 ms halten das Kontrollzentrum (Frame 600 des Streams), ein zweiter Halt schließt es. Mit `-1` öffnet es sich nicht, oben rechts erscheint das Capture-Symbol, der kurze Druck „Create“ wirkt also weiter.
- **Bildreaktion und Takt (`av_sync.py`, 600 s, Home-Bildschirm):** Bild 380/380, Median 96,0 ms (86,2–135,9, sd 6,3). Ton 221/380, Median 105,2 ms, also 8,6 ms nach dem Bild, Trend +0,00 ms/min. Bild und Ton driften beide +2,39 ms/min gegen die Uhr des PCs, gegeneinander +0,00. Die 265 ms vom 2026-10-29 kamen mit `--no-script` nicht wieder. Das passt zur Startsequenz als Ursache (Korrektur oben), direkt belegt ist sie weiterhin nicht.
- **Fehlstart:** Der ENet-Fehler nach dem Schließen des ProsperoStore kam in 6 von 6 Versuchen nicht wieder. In einer Reihe von 20 Starts scheiterte Lauf 7 anders: RTSP `SETUP streamid=video` lief nach dem erfolgreichen Audio-SETUP in den Fehler 110, der Host beendete „PS5 Screen“ nach seinen 10 s ohne Sitzung (Patch 0007). 60 weitere Starts mit Debug-Log liefen fehlerfrei. Zusammen 1 Fehlschlag in 86 Starts; ob Host oder Testclient in Docker schuld war, ist offen.
- **Adaptive Trigger (P9.10, Host-Log auf Stufe `debug`, Nutzertest mit Moonlight am PC und DualSense):** Der Host reicht das Trigger-Feld des Pads an den Client weiter. Ruhewert `030500000000000000000000050000000000000000000000`. Teardown setzt im Auto beide Blöcke auf `21 fe03 …`: Laut Nutzer werden L2 und R2 spürbar härter, das Log passt dazu. Mit dem Werkzeug setzt es nur den zweiten Block (`21 fe03 48922409 …`), laut Nutzer wird dann nur R2 hart; die Zuordnung stimmt also. GTA III sendete in zwei Sitzungen nichts, die Szene ist nicht festgehalten. Änderungen kommen im Abstand von Sekunden. **Grenze:** Nach dem Beenden des Streams bleibt der Trigger hart, beim nächsten Verbinden wird er weich (beides Nutzertest); der Host schickt beim Anlegen des Pads „aus“, am Testclient als `flags=0xc types=5/5` gemessen. Ursache laut Quelle: Moonlight PC v6.2.0 setzt die Effekte beim Beenden nicht zurück (`docs/re/adaptive-triggers.md`).
- **Eingabe:** Solange das RemoteEye-Testpad neben dem Host-Pad verbunden war, meldete der Nutzer in Teardown keine Eingabe; nach `ps5_detach` ging sie. Mit zwei Client-Controllern hängen zwei Host-Pads, Teardown nutzte Pad 0.
- **Ruhemodus, erster Test, Host ohne Erholung (mit Freigabe des Nutzers, `ps5_rest`, nach rund 90 s `ps5_wake`): Der Host überlebte ihn nicht brauchbar.** Im Ruhemodus antwortet die Konsole auf Ping, nicht auf Port 47989. Das Aufwecken meldete nach 11 s „on“, die Konsole stand dann auf „Press the PS button“ und der Nutzerauswahl. Der Host-Prozess läuft danach noch (gleiche PID), aber die Ports 47989, 47984, 48010 und 47996 bleiben zu, auch über 4 Minuten nach dem Einloggen. Sein Log endet mit `Couldn't accept incoming connections: error_code=0x000000a3` aus dem RTSP-Listener. ELF-Loader, FTP und klogsrv antworten dagegen weiter. Danach hingen auch die Werkzeuge: `ps5_launch` und Screenshot liefen in den Timeout, das RemoteEye-Pad meldete den Port als belegt. Auch der ELF-Loader nahm einen neu gesendeten Host an, ohne dass der hochkam, und FTP antwortete rund 15 Minuten nach dem Aufwecken nur noch mit Timeouts, klogsrv und 9021 waren zeitweise zu. Ob das am Ruhemodus selbst, an der Homebrew-Umgebung oder am alten Host-Prozess lag, ist offen. Am Fernseher stand laut Nutzer nur noch das Hintergrundbild der App, die Konsole musste hart ausgeschaltet werden. Nach dem Neustart und dem Laden der Homebrew-Umgebung lief der Host wieder über den Autostart.
- **Neustart der Dienste ohne Ruhemodus (`relisten_probe.elf`, Befehl `RELISTEN`):** Der Host startet seine Dienste im selben Prozess neu, das Log zeigt die neue Steuer-Socket-Zeile 0,25 s nach „Starting the services anew“. Danach `serverinfo` frei, Stream 10 s: 594 Frames H.264, Exit 0. Mitten im Stream: Sitzung vom Host beendet (`terminated_by_host`, Exit 0), Pad getrennt, danach HEVC 10 s: 598 Frames, Exit 0. „Restart host“ in der App läuft danach normal. Im normalen Betrieb gab es keine Fehlauslösung (30 s Stream: 1794 Frames). Im Log steht je Neustart einmal `Couldn't accept incoming connections: Operation canceled`, das ist das Beenden des alten Listeners. 
- **Ruhemodus mit 0.1.3 (zwei Tests mit Freigabe des Nutzers, Home-Bildschirm, kein Spiel offen, kein Stream, je rund 90 s, Aufwecken über das Netz):** Nach dem Aufwecken antwortet `serverinfo` beim ersten Abruf (`SUNSHINE_SERVER_FREE`), FTP sofort. Im zweiten Test, mit dem endgültigen Stand, steht im Log `Starting the services anew`, das Log vor dem Ruhemodus liegt als `sunshine.log.1`. Nach dem Einloggen HEVC 15 s: 895 Frames, H.264 15 s: 896 Frames, beide Exit 0; die App startet normal. Beobachtung danach alle 30 s `serverinfo` und FTP: erster Test 20 Minuten, zweiter 10 Minuten, jeweils 0 Fehler; danach noch ein Stream mit 597 Frames. Der Hänger des ersten Tests kam nicht wieder, seine Ursache bleibt offen. **Nicht geprüft:** Aufwecken per Controller, längerer Ruhemodus, Stream vor dem Einloggen.
- **Ruhemodus mit offenem Spiel und laufendem Stream (2026-11-01, vom Besitzer ausgelöst, rund 2 Minuten, Aufwecken mit `ps5_wake` nach 11 s):** Konsole und Spiel kommen zurück, der Host nicht. Digimon war laut Kernel-Log währenddessen im Zustand `SUSPENDING` und steht nach dem Einloggen mit dem Controller wieder vorn (`ps5_running`, Besitzer). `sunshine.log.1`: Stream 4K HEVC verbunden, 8 s später `capture lost (4)`, nach 513 ms wieder offen, dann `Couldn't accept incoming connections`. Danach `Starting the services anew`, aber in `host_diag.log` folgt kein `host ready`, und ein `CLIENT DISCONNECTED` fehlt. 47989, 47984 und 48010 weisen Verbindungen noch 2,5 Minuten nach dem Aufwachen ab; in den zwei Tests ohne Stream antwortete `serverinfo` beim ersten Abruf. Ob der Neustart beim Beenden der Sitzung hängt oder nur die Logs wieder stehen, ist nicht belegt.
- **Release 0.1.3 (2026-10-31):** Vor dem Release auf der Konsole installiert (`01.000.004`, in der App angezeigt), HEVC und H.264 je 15 s fehlerfrei, `sha256sum -c` ohne Fehler, CI grün. Die Digest-Angabe des GitHub-Assets `PPSA47989.zip` stimmt mit dem lokalen Archiv überein. Der Katalog-PR ist gemergt; die Store-API meldet `0.1.3` mit derselben SHA-256 wie das Release-Archiv. Die Installation aus dem Store ist nicht geprüft.
- **Tests:** `scripts/test.sh` und `app/tests/run.sh` bestanden. Letzter Stand der App auf der Konsole, 8 s Stream: 473 Frames, `stream_test` Exit 0.
- **Release 0.1.4 (2026-10-31):** Vor dem Release auf der Konsole installiert (`01.000.005`, in der App angezeigt), Host meldet 0.1.4; Home-Bildschirm je 15 s: H.264 1080p, HEVC 1080p, 1440p, 4K, 893–894 Frames, 0 Lücken, ffmpeg fehlerfrei. `scripts/test.sh` und `app/tests/run.sh` bestanden, CI grün. Das Asset `PPSA47989.zip` vom Release hat dieselbe SHA-256 wie das lokale Archiv. Der Katalog-PR ist gemergt; die Store-API meldet `0.1.4` mit derselben SHA-256. Die Installation aus dem Store ist nicht geprüft.
- **Release 0.1.5 (2026-11-01):** Vor dem Release auf der Konsole installiert (`01.000.006`, in der App angezeigt), Host meldet 0.1.5; Home-Bildschirm je 15 s: H.264 1080p und 4K, HEVC 1080p, 1440p und 4K, 883–894 Frames, 0 Lücken, ffmpeg fehlerfrei. `scripts/test.sh` und `app/tests/run.sh` bestanden, CI grün. Die Assets vom Release haben dieselbe SHA-256 wie die lokalen Dateien. Der Katalog-PR ist gemergt; die Store-API meldet `0.1.5` mit derselben SHA-256. Die Installation aus dem Store ist nicht geprüft. Der 13.60-Versuch ist auf 13.60 nicht geprüft.
- **Release 0.1.6 (2026-11-01):** Vor dem Release auf der Konsole installiert (`01.000.007`, in der App angezeigt), Host meldet 0.1.6; Home-Bildschirm je 12 s, 1080p: H.264 713, HEVC 714, HDR 711 Bilder, längste Lücke je 19 ms, ffmpeg fehlerfrei. `scripts/test.sh` und `app/tests/run.sh` bestanden, CI grün. Die Assets vom Release haben dieselbe SHA-256 wie die lokalen Dateien. Der Katalog-PR ist gemergt; die Katalogdatei nennt `0.1.6` mit derselben SHA-256. Store-API und die Installation aus dem Store sind nicht geprüft.
- **Release 0.1.7 (2026-11-01):** Vor dem Release auf der Konsole installiert und neu registriert (`01.000.008`, in der App angezeigt), Host meldet 0.1.7 mit Commit `ddd51c45`; Home-Bildschirm je 12 s: H.264, HEVC und HDR 1080p 714, 713 und 711 Bilder, HEVC 4K (Deckel 150 Mbit/s im Log) 703, HDR 4K 693, keine fehlenden Bilder, längste Lücke höchstens 24 ms, ffmpeg fehlerfrei. `scripts/test.sh` und `app/tests/run.sh` bestanden, CI grün. Die öffentlich geladene `PPSA47989.zip` hat dieselbe SHA-256 wie die lokale, die Digest-Angabe des Tar-Archivs stimmt ebenfalls. Der Katalog-PR ist gemergt, die Katalogdatei nennt `0.1.7` mit derselben SHA-256. Die Installation aus dem Store ist nicht geprüft.
- **PRs für 10.x, Ton und Spielstart, gegengeprüft auf 13.00 (2026-11-01, Testclient, zusammengeführt, Host mit App installiert):** Home-Bildschirm 1080p je 12 s: H.264 715, HEVC 713, HDR 711 Bilder, längste Lücke 22, 50 und 27 ms; HDR 4K mit 40 Mbit/s angefordert: 693 Bilder, Main 10, längste Lücke 28 ms; ffmpeg jeweils fehlerfrei, keine fehlenden Bilder. Mario 64 aus dem Stream gestartet, 25 s: 1499 Bilder, Ton mit −20 dBFS. GTA III gewählt, während Mario vorn läuft: 409. GTA III gewählt, während Mario hinter der App offen ist: Der Host schließt die App und lehnt nach 3 s mit 409 ab (`PPSA99310 is still open`); danach mit `ps5_close` geschlossen und ohne ShadowMountPlus-Sperre wieder gestartet. Auf dem Home-Bildschirm stehen in `/mnt/sandbox` nur `NPXS…`-Ordner. Mario im laufenden Stream gestartet und geschlossen: Aufnahme 1,5 und 1,8 s weg, danach `PS5 audio: channel reopened`, keine fehlenden Bilder. Nicht vorgekommen sind die Wiederholung bei `0x80940033` und das Warten des Tons auf eine beim Streamstart fehlende Aufnahme. Kein echter Moonlight-Client. Die 10.20-Angaben stammen von einem Mitwirkenden auf dessen Konsole.
- **Bitrate über 50 Mbit/s (2026-11-01, 13.00, Testclient am Kabel, kein echtes Moonlight, kein Spiel):** 4K, Rauschseite im Browser, je 15 s, Encoder-Vorgabe → geliefert. Volles Rauschen: HEVC 51 → 73, 102 → 102, 154 → 151 Mbit/s; HDR 102 → 99; H.264 51 → 135, 102 → 142. Rauschen auf einem Viertel: HEVC 51 → 45, 81 → 57, 102 → 63, 154 → 67; HDR 102 → 36; H.264 102 → 75. Jeweils 870–893 Bilder, keine fehlenden, längste Lücke höchstens 34 ms, ffmpeg fehlerfrei. Dauerlauf HEVC 154, volles Rauschen, 120 s: 7177 Bilder, längste Lücke 32 ms, ffmpeg fehlerfrei; rund 150 Mbit/s, aus der Paketzahl geschätzt. Danach Deckel auf 150 Mbit/s nur für 13.00 (`ps_capture_high_bitrate_verified`). Home-Bildschirm je 8 s, Log `bitrate 150000 kbit/s`: HEVC 4K (angefordert 239 Mbit/s), 1440p, 1080p, HDR 1080p, H.264 1080p und 4K: 461–478 Bilder, keine fehlenden, längste Lücke höchstens 54 ms (HEVC 1440p), sonst höchstens 26 ms, ffmpeg fehlerfrei.
- **Experimental-Schalter, nur auf 13.00 geprüft (2026-11-01, Testclient):** Die App zeigt alle vier als „Not needed: verified here“, der Bericht und `host_diag.log` nennen den Commit. Mit Schaltern aus und mit allen vier in `sunshine.conf` an (je 10 s, H.264 und HEVC 1080p, HEVC und HDR 4K, Mario 64 15 s): 571–595 Bilder (Mario 869), keine fehlenden, ffmpeg fehlerfrei, längste Lücke höchstens 85 ms (einmal, H.264 als erster Lauf nach dem Host-Start), sonst höchstens 54 ms. Log gleich: 13.00 öffnet wie vorher. Auf anderer Firmware ist nichts davon gelaufen: Die Varianten, der Rückfall und die Schalter-Kacheln sind dort nicht geprüft, nur per Unit-Test mit nachgebautem Avcap2.
- **13.60, nur Berichte von Nutzern (2026-11-01, Issue 1):** Zwei Nutzer melden, dass der Stream dort läuft, ohne Versionsangabe, Messung oder Angaben zu Controller und Ton. Einer nennt dabei H.265, obwohl der Host dort nur H.264 anbietet; das ist nicht geklärt.
- **1440p und 4K, H.264 (2026-10-31):** Die RA-Profile 177 und 182 (`vcn[70] scanin[4K] prio[1]`) öffnen Modus 5 und 6 (`src/uhd_spike.cpp`); der Host nimmt 182, wenn der Client mehr als 1080p verlangt. Gemessen mit dem lokalen Client (ohne Fenster), ffmpeg decodiert jeweils fehlerfrei in der verlangten Größe:
  - Home-Bildschirm, je 15 s: 1080p 893, 1440p 895, 4K 892 Frames, jeweils 59,94 fps und 0 Lücken.
  - GTA III (ruhige Szene, rund 4 Mbit/s), je 20 s: 4K 1166, 1440p 1177 Frames, 59,94 fps, 0 Lücken, 0 Audiofehler. Das Spiel lief danach weiter.
  - Moonlight 6.2 für PC, Home-Bildschirm: 1440p 60,3 und 60,5 fps, 4K 60,2 fps, 0 % Netzverlust. Ein erster 1440p-Lauf zeigte kein Bild: Im Log von Moonlight fehlen `Recreating renderer`, `Test decode successful` und `Renderer ... chosen`, dafür steht dort eine Qt-Warnung zum Pixelverhältnis des Fensters. Pakete und angeforderte IDRs kamen an, nur der Decoder wurde nie eingerichtet (Warteschlange lief dreimal je Sekunde über). Das spricht für eine Ursache im Client. Nach einem Neustart von PC und Konsole trat es in zwei Wiederholungen nicht mehr auf; die genaue Ursache ist nicht geklärt.
  - Bewegung (Seite im Browser der Konsole, Karte über dem Home-Bildschirm, kein Spiel): Rauschen auf einem Viertel der Fläche, lokaler Client, 4K, je 12 s, angefordert 5, 15, 30, 60 Mbit/s: geliefert 7,0, 12,1, 22,4, 43,7 Mbit/s, jeweils 59,94 fps, 0 Lücken. 1440p bei 15 Mbit/s: 11,0. 1080p bei 5: 3,6. Moonlight, 4K, 40 Mbit/s, 30 s: 60,2 fps, 0 % Netzverlust. Die Bitrate wird also auch in 4K geregelt. Dauerlauf, lokaler Client, 4K, 40 Mbit/s, 10 min auf derselben Seite: 35 958 Frames, 59,94 fps, 29,7 Mbit/s, 60 IDR, 0 Lücken, längster Abstand 20 ms, 0 Audiofehler.
  - Volles Rauschen hält der Encoder nicht ein: 4K 131–136 Mbit/s bei jeder Anforderung von 5 bis 80, 1440p 66–70, 1080p 35 bei 15, 39 bei 40. Trotzdem 59,95 fps und 0 Lücken am LAN (lokaler Client). `cfg+0xd4` und `cfg+0xd8` ändern daran nichts, `cfg+0xd4 = 51` verkleinert nur die IDR-Bilder (einmaliger Spike, 5 s je Fall). Ob ein echtes Spiel in diesen Bereich kommt, ist offen.
  - Mario 64: Der RA verweigert 182 (`Is4KRunnable: 4K NG(4K120)`), bei GTA III nicht. Das passt zu einer Ausgabe mit 120 Hz; die Ausgabe selbst wurde nicht gemessen. Das Spiel läuft weiter. Teardown landete ebenfalls auf 1080p (Grund dort nicht mitgelesen), Digimon Story Time Stranger bekam auch auf dem ersten Bildschirm 4K. Der Host fällt dann auf 1080p zurück: lokaler Client 1193 Frames in 20 s, 59,94 fps, 0 Lücken; Moonlight mit 4K-Anforderung 59,95 fps und Spielton.
  - Mario im laufenden 4K-Stream gestartet und geschlossen (Moonlight, 75 s): Aufnahme nach 1,6 s bzw. 2,9 s wieder da, Stream blieb verbunden, 56,15 fps im Schnitt. Er bleibt danach bis zum nächsten Stream bei 1080p.
- **1440p und 4K, HEVC (2026-11-01):** Der Host öffnet HEVC über 1080p mit Zweck 6 statt 2 und RA-Profil 182, Herleitung und Vergleich der Zwecke in `docs/re/hevc.md`. Gemessen mit dem lokalen Client (ohne Fenster), noch unveröffentlichter Stand:
  - Home-Bildschirm, je 15 s: 4K 882, 1440p 891, 1080p 894 Frames, jeweils 59,94 fps, 0 Lücken, 0 Audiofehler, ffmpeg fehlerfrei in der verlangten Größe. H.264 4K zur Gegenprobe: 893 Frames.
  - GTA III im laufenden Stream gestartet (4K, 70 s): 4170 Frames, 59,83 fps, 0 Lücken, längster Abstand 37 ms, ffmpeg fehlerfrei. Im Host-Log kein Verlust der Aufnahme.
  - Mario 64 lief schon, 4K und 1440p verlangt, je 20 s: Der Host meldet die Verweigerung und liefert 1080p, 1194 und 1189 Frames, rund 59,8 fps, 0 Lücken, ffmpeg fehlerfrei.
  - Dauerlauf mit Rauschen auf einem Viertel der Fläche (Browser-Karte), 4K, 40 Mbit/s verlangt, 10 min: 35 889 Frames, 59,84 fps, 29,6 Mbit/s, 60 IDR, 0 Lücken, längster Abstand 34 ms, 0 Audiofehler. Die 0,1 fps Abstand zu H.264 passen zu einem ausgelassenen Bild je IDR. ffmpeg dekodiert die 2,2 GB ohne Fehlermeldung (Exit 0), HEVC Main 3840x2160 Level 6.0.
  - 30 fps, Home-Bildschirm, je 15 s: 4K 439 Frames, 29,91 fps, 1440p 446 Frames, 29,97 fps, 0 Lücken, längster Abstand 66 bzw. 51 ms, ffmpeg fehlerfrei.
  - Frame-Budget `cfg+0x10c` auf die Höchstwerte gesetzt (`docs/re/hevc.md`). Viertel-Rauschen, je 15 s, verlangt und geliefert: 4K 5→3,7, 15→10,6, 40→29,9, 60→43,3 Mbit/s; 1440p 15→9,6; 1080p 5→3,1, 15→9,5, 40→20,0. Überall 59,94 fps, 0 Lücken, längster Abstand 18–20 ms statt vorher 33–37 ms, auch über ein periodisches IDR hinweg. Mario 64 je 20 s, 1080p: 5→2,3, 15→6,1, 40→15,7 Mbit/s, 4K-Wunsch fällt auf 1080p zurück; 59,94 fps, längster Abstand 21 ms. H.264 1080p unverändert 59,94 fps. Alle ffmpeg fehlerfrei.
  - Rückfall, wenn `Start` einen großen Modus ablehnt: Modus 6 auf Profil 180 (Probe, Home): erstes Öffnen scheitert mit `0x80c78000`, 1080p läuft danach; H.264 295 ms bis zum Ende des zweiten Öffnens, 120 Frames in 2 s; HEVC 534 ms, 119 Frames. Eine echte Bildausgabe kleiner als der Modus ist nicht getestet.
  - Nutzertest mit Moonlight am PC, 4K, 60 fps, HEVC, zwei Sitzungen (54 s und 5:42 min, dazwischen 16 s): nach Rückmeldung ohne Probleme. Im Host-Log 3840x2160, auf 50 Mbit/s begrenzt (verlangt 101), 8 Tonkanäle, kein Verlust der Aufnahme. Nach dem Test lag Digimon Story Time Stranger vorn. Tatsächliche Bitrate und Bildrate am Client sind dabei nicht gemessen.
  - Nicht gemessen: Wiederverbinden nach Paketverlust. Der Nutzertest lief vor dem geänderten Frame-Budget.
  - Die Meldung „Gameplay recording paused because you entered a blocked scene“ stand im Vorspann von GTA III auf dem jeweils 200. Bild nach dem Öffnen: HEVC Zweck 6 und H.264 Zweck 2, beide in 4K, nicht aber HEVC Zweck 2 und 6 in 1080p. Der Stream lief weiter.
- **Titelstarts blockiert (2026-10-31, einmal gesehen):** Alle Titel scheiterten mit `0x80940033`, im Log von ShadowMountPlus `launch mount unavailable … status=16 (Device busy)`. Davor lief die App weiter (keine Exit-Meldung für ihren Prozess), während ein Spiel gestartet wurde, und wurde später erneut gestartet (`failed to reset switch mount`); danach fehlte `runtime release start`. Nach dem Quelltext von ShadowMountPlus 1.7 (auf der Konsole läuft v1.7beta7) sperrt ein Titel, dessen Freigabe aussteht, jeden weiteren Start. Das ist aus Log und Quelltext erschlossen, nicht gezielt nachgestellt. Nur ein Neustart der Konsole half, das Schließen und Scannen nicht.

Noch offen:

1. **Auflösung (P9.3, Meilenstein 0.4):** Unter 1080p liefert der Host auf 13.00 und 10.20 den nächstgelegenen von 720p, 540p und 360p (unten, „Modi unter 1080p (2026-11-02, 13.00)“ und „Modi unter 1080p auf Firmware 10.20“), freie Größen gibt es nicht, der Client skaliert den Rest. Darüber gehen H.264 und HEVC (siehe „1440p und 4K“ oben). Offen: 4K im aktiven Spielbetrieb (nicht ohne Eingaben und Spielstände messbar), 4K mit anderen Clients als Moonlight für PC.
2. **Mehrere Controller am selben Nutzer:** Die Oberfläche der Konsole nimmt Eingaben von allen an, die App aber nur von einem. Beobachtet mit dem RemoteEye-Test-Pad: War es zuerst verbunden, ignorierte die App das Stream-Pad (44 Drücke, keine Bildänderung); nach `ps5_detach` reagierte sie auf alle 44. Das passt zu P9.8 in umgekehrter Reihenfolge. Mit einem echten DualSense verhielt es sich anders (Nutzertest, iPhone, 2026-10-31): Er trennte sich beim Verbinden, die Moonlight-Steuerung ging sofort, auch in Mario 64. Wieder eingeschaltet während des Streams reagierte nur Moonlight, nach Stream-Ende arbeitete der DualSense normal. Im Klog meldet die Konsole beim Anmelden des Stream-Pads `DEVICE_REMOVED` für den DualSense; im Host-Code gibt es dafür keinen Aufruf, der Grund liegt also auf Konsolenseite und ist unbekannt. Für den Stream ist das kein Hindernis, daher bleibt es bei der Beschreibung in der README.
3. **Payload-Manager-Fenster:** Ein offenes Fenster des Payload Managers liegt über Apps und nimmt Eingaben an sich, auch wenn es nicht reagiert. In dem Zustand lässt es sich nur am echten Controller schließen. Beim Fernsteuern vorher prüfen.
4. **Eigene Werkzeuge vom PC:** `PIN`/`QUIT` und `pair.py status` über den Steuer-Socket gehen vom PC aus nur noch mit der leeren Datei `/data/prosperosunshine/control_lan` (danach Host neu starten). Sonst geht der Host-Neustart über die App.
5. **Store-Installation (P8.9):** Die Installation aus dem ProsperoStore klappt (Nutzerbericht, 2026-10-31).
6. **Rumble je Spiel:** Im Nutzertest (2026-10-31) vibrierte der Controller in Moonlight beim Streamen. Welche Spiele den klassischen Modus nutzen und welche nur die Haptik, ist offen.
7. **Seltene Fehlstarts:** Ein Stream direkt nach dem Schließen des ProsperoStore scheiterte (`stream_test` Exit 3: `Failed to establish ENet connection on UDP port 47999`, Fehler 11; im Host-Log `Ping Timeout` und `Initial Ping Timeout`). Der Host blieb betriebsbereit, die Wiederholung lief fehlerfrei. Nachgeprüft: 1 anderer Fehlschlag in 86 Starts (oben). Die Ursachen sind nicht geklärt; im Windows-Client kam in 300 Starts kein Fehlstart vor (oben).
8. **Weiter offen wie bisher:** Ruhemodus in den oben genannten Fällen (Tests nur mit Rückfrage), Android/TV-Clients, andere Firmwares (Checkliste in `docs/firmware-porting.md`), Wake-on-LAN (unten).

- **MAC-Adresse und Wake-on-LAN:** Der Host meldet die MAC gepaarten Clients: `/serverinfo` über HTTPS mit Client-Zertifikat liefert die Adresse, die auch die ARP-Tabelle des PCs zeigt (`pair.py info`). Die frühere Angabe „MAC ist 0“ war ein Messfehler: Über HTTP gibt Sunshine für alle Clients absichtlich einen Platzhalter aus Nullen zurück (`nvhttp.cpp`). Ein Einmal-Probe auf der Konsole bestätigte die Quellen: `sceNetGetMacAddress(…, 0)`, `sceNetCtlGetInfo` und `SIOCGIFCONF` liefern dieselbe Adresse. **Aufwecken aus Moonlight am PC (2026-11-01, ein Versuch des Besitzers, im selben Netz):** Die Konsole blieb im Ruhemodus (Ping antwortet, Host-Ports zu); `ps5_wake` mit dem `WAKEUP` von Remote Play weckte sie danach. Ob das Magic Packet die Konsole erreichte, ist nicht mitgeschnitten.
- **Boost.Filesystem `create_directories`** scheitert bei neuen verschachtelten Pfaden, wenn `errno` vorher belegt ist (ein Libc-Aufruf setzt kein `errno`). Der Host nutzt nur `std::filesystem` und ist nicht betroffen.
- **Zweite RTSP-Verbindung** wird ohne erklärende Fehlermeldung geschlossen (Upstream-Verhalten).
- **Eingaben nach hartem Verbindungsabbruch** wirkten mit Sunshines Standard (`ping_timeout` 10 s) ≈ 5,6 s nach. Mit dem neuen Host-Standard von 2 s (Moonlight pingt alle 100 ms) erkennt der Host den Abbruch nach 1,8 s und setzt das Pad dann neutral (gemessen mit `tools/moonlight/hold_kill_test.sh`).
- **Entzug der Capture-Ressource** passiert bei Spielstart/-ende (immer) und sporadisch wenige Sekunden nach Anmeldung des virtuellen Pads (2 von 7 Sitzungsstarts; in vier gezielten Wiederholungen mit vollem Kernel-Log nicht reproduziert). Der Host stellt Video und Ton jeweils nach ≈ 2 s wieder her; der Client sieht eine kurze Pause.
- **Nur eine Sitzung gleichzeitig:** Eine zweite Streaming-Sitzung (z. B. Moonlight Web, das während einer laufenden Sitzung erneut verbindet) wird beendet, weil sonst zwei Leser denselben Avcap2-Kanal leeren.
- **Avcap2-Symbolauflösung war nicht threadsicher:** `kernel_dynlib_dlsym` aus mehreren Threads gleichzeitig ließ Ton und Keyframe-Anforderung scheitern; alle Funktionen werden jetzt einmal beim Öffnen aufgelöst.
- **Log-Stufe:** `min_log_level` in `/data/prosperosunshine/sunshine.conf` gilt auch für das Kernel-Log (z. B. `min_log_level = debug` für Diagnose).
- **Steuer-Socket** ist im LAN offen und nur für Entwicklung gedacht; die Host-API für die PS5-App (P2.5) ist noch nicht abgestimmt.
- **Keine Maus/Tastatur** (v1.0-Umfang).
- **Rumble nur klassisch:** Die Haptik des DualSense kommt nicht an (`docs/re/pad-output.md`). Adaptive Trigger gehen nur mit Moonlight am PC und bleiben nach dem Stream hart, bis zum nächsten Verbinden (`docs/re/adaptive-triggers.md`).

### Spiele in Moonlight (2026-11-01, nicht veröffentlicht)

- **Liste und Cover:** `pair.py info` zeigt „PS5 Screen“ und 34 Titel, `pair.py cover` liefert für die ersten drei PNGs. Moonlight für Android (TV) zeigt alle mit Cover. Es sortiert selbst nach Namen; mit führendem Leerzeichen im Namen steht „PS5 Screen“ dort vorn (vom Besitzer am TV bestätigt).
- **Start aus dem Home-Menü:** `run_smoke.sh --appid` mit Mario 64, 1080p H.264: Titel startet, 25 s Stream mit 59,94 fps, längste Lücke 23 ms, Ton läuft, Host läuft danach weiter. Moonlight am TV: Digimon direkt gestartet, laut Host-Log 4K60 HEVC mit 50 Mbit/s (angefordert 111 Mbit/s), rund 7,5 Minuten ohne Warnung im Log, der Besitzer meldet „lief sehr gut“. Beim Schließen des Titels über das Konsolenmenü ging die Aufnahme zweimal verloren und kam nach 629 und 506 ms in 4K wieder; der Stream lief auf dem Startbildschirm weiter. Der TV zeigte beim Start seine eigene HDR-Meldung (laut Besitzer vom TV, nicht von der Konsole). Der Host sandte da 8 Bit: Ein 4K-HEVC-Stream vom Startbildschirm ist laut `ffprobe` `Main`, `yuv420p`, BT.709.
- **Titel läuft schon, oder ein anderer läuft** (Testclient, je 8 s, 59,9 fps): derselbe wird nicht neu gestartet, ein anderer startet nicht, der Stream zeigt den laufenden. Log: „already in front“, „Not starting“. Seit dem Fix für diesen Fall schlägt der Start eines anderen Titels mit 409 fehl, statt den laufenden unter dem neuen Namen zu zeigen. Gemessen auf Firmware 10.20 mit Moonlight, GTA III vorn, Wolverine gewählt: „Failed to start Marvel's Wolverine (error 409)“, kein Stream.
- **Spiel hinter der Einstellungs-App** (Firmware 10.20, ShadowMountPlus 1.7beta11, Moonlight): Resident Evil Requiem lief, die App wurde darüber geöffnet (das Spiel bleibt dahinter offen), dann GTA III gewählt. Der Host schloss die App und lehnte 2,5 s nach der Anfrage ab: „Not starting PPSA03527: PPSA31246 is still open …“, ShadowMountPlus protokollierte keinen Start. Danach über die Optionen seiner Kachel geschlossen, wurde das Spiel freigegeben (`runtime release start`) und startete wieder normal.
- **Einfrieren beim Start, behoben:** Mit zur Laufzeit geladenem `libSceSystemService` und `libSceUserService` blieb der Host nach dem Titelstart stehen (Prozesszustand 4, auch SIGKILL half nicht, erst ein Neustart der Konsole). Eine Sonde auf diesem Weg blieb schon vor ihrer ersten Log-Zeile stehen, das Kernel-Log zeigt `mDBG: Sending signal(pid, tid, signo: 0xa0020101)`. Eine Sonde mit gelinkten Bibliotheken bekam die App-ID zurück und lief weiter, ebenso nun der Host. Warum das Laden zur Laufzeit den Prozess anhält, ist nicht geklärt.
- **Kein Bild direkt nach dem Start, behoben:** Während ein Titel hochfährt, meldet `GetVideoOutMode` 0 (gemessen bis 500 ms), und `OpenVideo` antwortet mit dem veralteten Wert `0x81950008`. Die Aufnahme wartet nun auf den Wert und liest ihn bei Wiederholungen neu.
- **`0x80960003`** heißt „bereits initialisiert“ (`kAlreadyInitialized` in ps5-homebrew-ui).
- **Start bei offener App** (Testclient, Mario 64, 20 s H.264): Die Einstellungs-App schließt sich, der Titel startet, 59,9 fps, längste Lücke 23 ms. Der nächste Start danach ging ohne die SMP-Sperre (`0x80940033`).
- **4K-Rückfall nach einem Start aus Moonlight** (Testclient, Mario 64, HEVC, 4K verlangt): `capture open failed step=RUNNING`, dann „larger mode refused“, 1080p mit 59,9 fps, keine fehlenden Bilder, längste Lücke 22 ms.
- **Offen:** weitere Titel.

### Touch und Bewegung im Pad (2026-11-01, nicht veröffentlicht)

Gemessen mit `stream_test --touch-motion`, Stream „PS5 Screen“ mit offener App. Die App liest beim Stream mit `scePadReadState` zehnmal pro Sekunde mit (Schalterdatei `pad_log`, `tools/moonlight/pad_log.py`). Das ist also die Sicht eines Titels.

- **Touch:** Gesendet in 0..1, gelesen: 0,5/0,5 → 960,540. 0,25/0,75 → 480,809. Zweiter Finger 0,75/0,25 → 1439,270. Beide gleichzeitig, jeder mit eigener id; Abheben senkt die Anzahl auf 1 und 0.
- **Drehrate:** 10, 20, 30 Grad/s kommen als 0,175, 0,349, 0,524 rad/s an, am Ende 0.
- **Beschleunigung und Ruhewert:** 9,80665 m/s² auf y kommt als 1,0 an, gleich dem Ruhewert `0,1,0`. Dass die Werte aus dem Block stammen, zeigt ein Pad von RemoteEye, dessen Block null bleibt: Da liest die App 0,0,0 und eine Null-Quaternion.
- **Tasten daneben:** Steuerkreuz rechts und links während der Bewegung (`0x20`, `0x80`). Die Drehrate bleibt dabei stehen; die App reagiert normal.
- **Create-Menü:** Solange es offen ist, meldet `scePadReadState` Bit 31 in den Tasten und keine Finger, die Drehrate kommt aber durch. Es stand bei der ersten Messung schon offen, das Vorschaubild zeigte die App beim Streamen. Wer es geöffnet hat, ist nicht geklärt. In drei Läufen danach ging es nicht wieder auf (Bilder aus dem empfangenen Stream).
- **`access()`** antwortet im Titel mit EPERM, auch für eine vorhandene Datei. Für die Schalterdatei also `stat`.
- **Offen:** ein Titel, der Touch oder Bewegung wirklich nutzt, das Vorzeichen der Achsen, echte Moonlight-Clients. Die Orientierung bleibt beim Ruhewert: Moonlight liefert keine, also sieht ein Titel, der nur sie liest, keine Bewegung.

### HDR (2026-11-01, nicht veröffentlicht)

Moonlight bekommt nun HEVC Main 10 angeboten (`ServerCodecModeSupport` 769), `hevc_mode = 2` schaltet das ab. Gemessen mit `stream_test --hevc10`, Bildausgabe der Konsole wie vorgefunden (`video out mode 0x13c`, welche HDR-Einstellung die Konsole hatte, ist nicht geprüft). Dekodieren mit `ffmpeg -xerror` lief in allen Läufen fehlerfrei.

| Lauf | Client meldet | `ffmpeg` | Längste Lücke |
| --- | --- | --- | --- |
| Home, 1080p, 12 s | `hdr mode: 1` | Main 10, `yuv420p10le`, bt2020nc/bt2020/smpte2084 | 19 ms |
| Home, 4K, 12 s | `hdr mode: 1` | wie oben, 3840x2160 | 19 ms |
| Mario 64 aus Moonlight gestartet, 1080p, 25 s | `hdr mode: 1` | wie oben | 22 ms |
| Titel mitten im Stream geschlossen, 22 s | `hdr mode: 1` | wie oben, auch nach dem Wiederöffnen | 1785 ms (Wiederöffnen nach 1750 ms) |
| 4K, Rauschen auf einem Teil der Fläche (Browser-Karte), 40 Mbit/s verlangt, 20 s | `hdr mode: 1` | wie oben, 3840x2160; rund 20,7 Mbit/s, 1171 Bilder, keine fehlend | 19 ms |
| dasselbe in 8 Bit als Vergleich | `hdr mode: 0` | Main, `yuv420p`, bt709; rund 29,9 Mbit/s, 1183 Bilder, keine fehlend | 19 ms |
| danach HEVC 8 Bit, 8 s | `hdr mode: 0` | Main, `yuv420p`, bt709 | 19 ms |
| danach H.264, 8 s | `hdr mode: 0` | h264 | 19 ms |

- **Echtes PQ, nicht nur umetikettiert:** Die helle Oberfläche des Startbildschirms hat in 8 Bit Luma 251, in 10 Bit 638 von 64..940, also rund 0,66 PQ. Ein Bild aus Mario 64 sieht nach PQ-Tone-Mapping (`zscale`, `tonemap`) richtig aus. Der Scan-In meldet dabei `dynamicRange(2)` statt `(1)`.
- **Sonde:** `out/hdr_spike.elf`, 8 und 10 Bit je 299 Bilder in 5 s, Rückgaben 0. Keine der Ausweich-Varianten war nötig. Die Felder von `docs/re/hdr.md` stimmen also.
- **Metadaten:** Avcap2 liefert keine, der Host schickt Nullen (unbekannt).
- **Echter Client auf einem HDR-Fernseher:** Der Besitzer startete Digimon Story Time Stranger aus Moonlight am TV mit eingeschaltetem HDR. `sunshine.log`: Spiel gestartet, `requested 3840x2160@60, delivering 3840x2160@60`, `HEVC HDR bitrate 50000 kbit/s (requested 110988)`, Ton und Pad verbunden, keine Warnung bis zum Lesen während des laufenden Streams. Das Urteil zum Bild kommt vom Besitzer: Es passt, trotz der Metadaten-Nullen. Gemessen ist am Client nichts, die Dauer des Streams auch nicht.
- **Offen:** andere Clients und Bildschirme, wie sie mit den Metadaten-Nullen umgehen. Ob Digimon selbst HDR ausgibt. Die Matrix aus Bildausgabe der Konsole und Stream (`docs/re/hdr.md`, Schritt 5); die Einstellung ändert der Besitzer. Der Rückfall auf 1080p in 10 Bit ist nicht eigens gemessen.

### HEVC auf Firmware 10.20 (2026-11-01, nicht veröffentlicht)

Gemessen auf einer PS5 Pro mit 10.20 (Datensatz von 10.x, `record_shift`). Eine Sonde zeigte: `cfg+0x24 = 1` allein startet HEVC,
8 und 10 Bit, 30 und 60 fps; Speicher-Reservierung und Remote-Play-Felder ändern nichts (`docs/re/hevc.md`). Jedes Bild hat
17 Slice-Segmente, H.264 dort 34.

| Lauf | Ergebnis |
| --- | --- |
| Sonde, je 2 s, Startbildschirm, Bilder auf einem Windows-PC mit FFmpeg (`err_detect=explode`) decodiert | HEVC 8 und 10 Bit: D3D11VA, DXVA2 und D3D12VA je 120 von 120 Bildern. H.264 derselben Konsole: 0 von 180 per Hardware, 180 von 180 in Software |
| 10 Bit | Main 10, `p010` über D3D11VA, BT.2020, PQ, begrenzter Bereich |
| Testhost mit dieser Änderung, dazu #4, #5 und ein Test-Quantisierer für 10.x (`cfg+0xc4`/`+0xc8` je nach Bitrate 24/28, 27/31 oder 30/34; nicht in diesem Stand), Moonlight unter Windows, Decoder und Codec auf „Automatisch“, „PS5 Screen“, 36 s | `HEVC`, 1920x1080, 60,3 fps, 0 % verlorene Bilder, Decodierzeit 0,38 ms. Log: `PS5 capture HEVC bitrate …`, keine Warnung |
| dasselbe mit HDR | `HEVC 10-bit HDR`, 60,05 fps, 0 % verlorene Bilder, Decodierzeit 0,40 ms. Log: `PS5 capture HEVC HDR bitrate …` |

- **Clients:** Der Besitzer hat am selben Tag auf dieser Konsole Moonlight auf Android (2448x1080), iOS (2796x1290), Windows x64
  und Nintendo Switch (1280x720) genutzt, alle ohne Probleme. Windows mit dem Testhost oben (HEVC und HEVC HDR), die anderen vorher mit
  H.264 auf dem Datensatz von 10.x.
- **10.20 gilt als geprüft:** Die App zeigt dort keinen Hinweis „untested“ mehr, und `ra_nids.h` kennt die Adressen der
  ResourceArbitrator-Aufrufe (`0xde0`, `0xfe0`, `0x1220`, `0x1360`, auf 10.20 per NID gefunden). `ra_nid_probe.elf` aus diesem Stand meldet dort `result=PASS … known=1`, NID und Tabelle stimmen für alle vier überein.
- **HEVC auf Android, iOS und Switch (nachgetragen 2026-11-05):** inzwischen auf 10.20 gelaufen, siehe „Auf 0.1.9 mit weiteren
  Clients“ unter „Modi unter 1080p auf Firmware 10.20“ und „Quantisierer auf 10.x folgt der Bitrate“. Dazu Play Together auf 0.2.2
  (2026-11-04, #11): Switch im LAN, iPhone über Mobilfunk und Android über WLAN in einem anderen Netz, alle mit HEVC.
- **Offen:** andere 10.x-Versionen (bekommen nur H.264); Schritt 6
  der Checkliste (Bitrate beim Öffnen) ist auf 10.x nicht erfüllbar; Modi über 1080p bleiben auf 10.x aus (`ps_capture_large_modes_verified`; die darunter laufen auf 10.20, siehe „Modi unter 1080p auf Firmware 10.20“); die Bitrate,
  die der Encoder auf 10.x wie bei H.264 nicht aus der Konfiguration übernimmt.

### Modi unter 1080p auf Firmware 10.20 (2026-11-02, nicht veröffentlicht)

Gemessen auf der PS5 Pro mit 10.20, Ausgabe der Konsole in 1080p (`video out mode 0x13c`), Startbildschirm, kein Titel vorn,
Host ohne Stream. Sonde `src/small_mode_probe.cpp`, je Modus ein neuer Kanal mit dem Datensatz des Hosts, 3 s, alle 500 ms ein
IDR angefordert, 16 Mbit/s verlangt. Auf 10.x setzt der Host dabei den Quantisierer 24/28 (`encoder_qp.h`).

- **Alle 24 Kombinationen laufen:** 640x360, 960x540, 1280x720 und 1920x1080, je H.264, HEVC 8 und HEVC 10 Bit, je 30 und 60
  fps. `OpenVideo` und `Start` geben 0 zurück, Profil 180, Zweck 2, 90 bzw. 179–181 Bilder. Die gespeicherten Streams decodiert
  PyAV (FFmpeg) auf dem PC ohne Fehler in genau diesen Größen, HEVC 10 Bit als `yuv420p10le`. Ein zweiter, unabhängiger Lauf
  am selben Tag (eine eigene Sonde, die alle Modi nacheinander öffnet) sah dieselben Modi Bilder liefern, mit denselben Slices.
  Auf 13.00 scheiterte im alten `mode_spike` 720p60 H.264; der neue Spike und der Host öffnen dort inzwischen alle drei
  (unten, „Modi unter 1080p (2026-11-02, 13.00)“).
- **Bildgrößen bei 60 fps** (IDR im Mittel, andere Bilder im Mittel, Slices je Bild; der Startbildschirm bewegt sich kaum, die
  Werte vergleichen die Modi untereinander, nicht mit einem Spiel):

| Modus | H.264 | HEVC 8 Bit | HEVC 10 Bit |
| --- | --- | --- | --- |
| 1920x1080 | 65,1 KB, 580 B, 34 Slices | 68,4 KB, 257 B, 17 | 55,7 KB, 263 B, 17 |
| 1280x720 | 43,1 KB, 659 B, 45 | 43,2 KB, 181 B, 12 | 36,8 KB, 182 B, 12 |
| 960x540 | 27,1 KB, 473 B, 34 | 24,7 KB, 153 B, 9 | 20,3 KB, 156 B, 9 |
| 640x360 | 14,8 KB, 333 B, 23 | 13,3 KB, 95 B, 6 | 11,1 KB, 95 B, 6 |

  Ein IDR in 720p kostet also rund zwei Drittel von 1080p, in 540p rund 40 %, in 360p rund 20 %. Bei 30 fps sind die Bilder
  gleich groß.
- **Slices:** H.264 kommt in 720p mit 45 Slices, in 540p wie 1080p mit 34, beides mehr als die Hardware-Decoder von FFmpeg
  nehmen (32); erst 360p bleibt mit 23 darunter. HEVC bleibt überall darunter.
- **Host:** Mit diesem Stand wählt der Host auf 10.20 für eine Anfrage unter 1080p den Modus, der ihr in der Höhe am nächsten
  liegt, bei Gleichstand den größeren: 960x544 (PS Vita) gibt 960x540, 854x480 960x540, 1280x800 1280x720, 1600x900 bleibt bei
  1080p. Lehnt das System einen dieser Modi ab, fällt der Stream wie bei 1440p und 4K auf 1080p, also auf das, was jeder
  Client vor diesen Modi bekam. Auf 10.20 und bei einem Modus, der schon lief, nur für diesen Stream; auf anderer Firmware
  ist der Schalter danach bis zum Neustart aus, wenn er noch unbewiesen war.
- **Moonlight auf dem iPhone über das Internet (2026-11-02, 13:45–14:03):** Testhost aus diesem Stand mit der
  Firmware-Sonde, GTA III, Verbindung verschlüsselt (Client nicht im LAN). Jede Anfrage bekam, was oben steht:
  640x360@60 gab 640x360 (HEVC HDR), 960x544@60 960x540 (HEVC und H.264), 1280x720@60 1280x720 (H.264). Keine Warnung,
  kein Verlust der Aufnahme, kein Rückfall. Über Mobilfunk (4G) liefen die ersten Läufe in 360p und 540p laut Nutzer
  flüssig; 720p in H.264 ruckelte leicht, zwei spätere 540p-Läufe deutlich. Derselbe 540p-Lauf in H.264 über das WLAN eines
  anderen Anbieters lief ohne Ruckeln, im Client 19 ms mittlere Latenz. Der Host lieferte in jedem Lauf alle Bilder, das
  Ruckeln lag also auf der Strecke.
- **Datenrate in 540p H.264** (der Fall der PS Vita: H.264 960x540@60, verlangt 3428 kbit/s, QP 30/34 aus `encoder_qp.h`), mit
  einem Messzähler im Testhost (Bytes jedes gelesenen Bildes, alle 5 s): in GTA III mit Fahrt durch die Stadt, drei Läufe zu je
  1–2,5 min, 0,24 bis 1,9 Mbit/s im Mittel je 5 s, in Fahrt meist 0,9 bis 1,3; die dichteste Sekunde einmal 2,7 Mbit/s, in
  Fahrt sonst 1,0 bis 1,8; größtes Bild 27 KB (das erste IDR); 300 Bilder je 5 s ohne Ausnahme.
- **Ab 1080p unverändert, live (2026-11-02, 15:04–15:11):** derselbe Host, GTA III. Für 1920x1080@60 bekamen Moonlight auf dem
  iPhone im WLAN (HEVC) und Moonlight unter Linux am Kabel (HEVC HDR) 1920x1080@60, beide mit 14 988 kbit/s und QP 24/28.
  Linux bekam außerdem 2560x1440@60 für 2560x1440@60 (HEVC HDR, 30 988 kbit/s, QP 21/25; der Schalter für 1440p/4K prüfte
  wie bisher das erste Bild, nach 10 ms) und 3840x2160@60 für 3840x2160@60 (HEVC HDR, 50 000 kbit/s bei verlangten 62 988). Das sind dieselben Antworten
  wie bei einem Host ohne diese Änderung am selben Tag (13:12 und 13:13: 2560x1440 und 3840x2160 wie verlangt, 30 988 und
  50 000 kbit/s). Keine Warnung, kein Rückfall.
- **Auf 0.1.9 mit weiteren Clients (2026-11-02, 18:57–19:24, Heimnetz, GTA III):**
  - iPhone 15: 1280x720@60 verlangt und bekommen (HEVC, 7308 kbit/s).
  - Android-Telefon mit 165 Hz: 854x480@165 verlangt, 960x540@60 bekommen (HEVC HDR, 4908 kbit/s).
  - Nintendo Switch: 1280x720@60 verlangt und bekommen (HEVC, 16 024 kbit/s).
  - Keine Warnung der Aufnahme, kein Rückfall.
- **PS Vita (2026-11-02, 19:52–19:58):** Moonlight auf einer echten PS Vita im WLAN in einer anderen Stadt, also über das
  Internet, GTA III, 0.1.9 mit dieser Änderung.
  - 960x544@60 verlangt, 960x540@60 bekommen, H.264 mit 34 Slices je Bild, 7308 kbit/s verlangt (QP 30/34).
  - 5 min 40 s ohne Verlust der Aufnahme und ohne Rückfall.
  - Laut Nutzer flüssig, mit wenigen sehr kurzen Rucklern, insgesamt sehr gut. Der Decoder der Vita verträgt also die
    34 Slices.
  - Die gesendete Bitrate zählte dieser Host nicht. Gemessen ist sie mit dem iPhone in derselben Einstellung (oben).
  - Der Stream wurde auf der Vita geschlossen, ohne ihn zu beenden; der Host beendete die Sitzung beim Ping-Timeout.

### Titelwechsel im Stream unter 1080p (2026-11-03, 10.20, Release 0.2.3)

Gemessen auf der PS5 Pro mit 10.20, Ausgabe der Konsole in 1080p (`video out mode 0x13c`). Moonlight für Windows streamte
„PS5 Screen“ vom Startbildschirm; während des Streams wurde Black Myth: Wukong von außen gestartet und nach etwa 30 s
beendet (Prozessende; die Ausgabe wechselt dabei wie beim Schließen aus dem Menü). Wukong stellt die Ausgabe auf einen
eigenen Modus (`0x44`). GTA III und Resident Evil 4 bleiben beim Modus des Startbildschirms und nehmen die Aufnahme nicht
weg.

- **Ausgabe ohne Modus:** Beim Start und beim Ende des Titels geht die Aufnahme verloren (`ReadVideo` `0x81950009`), und
  `sceAvcap2GetVideoOutMode` meldet 1,6 bis 2,1 s lang 0 (eine Sonde las den Modus während des Streams alle 20 ms). So
  lange lehnt `OpenVideo` jeden Modus mit `0x81950008` ab.
- **0.2.1 und 0.2.2:** Die Wiederöffnung wartet 1 s auf den Modus und versucht unter 1080p fünfmal, danach fällt der Stream
  auf 1080p und bleibt dort bis zum Ende. 960x544 verlangt (H.264): nach dem Start in 1965 ms, nach dem Ende in 2058 ms
  wieder offen, beide Male in 1920x1080. Eine PS Vita zeigt dann kein Bild mehr; der Stream steht, die Eingaben gehen
  weiter.
- **Mit dieser Änderung:** Eine Wiederöffnung im laufenden Stream fällt unter 1080p nicht zurück, solange die Ausgabe
  keinen Modus hat oder ihn während der Versuche wechselt (`output not settled` im `host_diag.log`); die nächste
  Wiederöffnung 250 ms später verlangt denselben Modus, höchstens 6 s lang. Jede Wiederöffnung verlangt wieder den Modus
  des Clients. 960x544: nach dem Start 1991 ms, nach dem Ende 2035 ms, beide Male 960x540.
- **PS Vita im Heimnetz** (Testhost auf 0.2.1 mit derselben Logik, aber für alle Auflösungen und mit Sonde): Wukong
  zweimal gestartet und zweimal beendet, jedes Mal nach 1,9 bis 2,3 s wieder 960x540; laut Nutzer blieb das Bild.
- **Ab 1080p unverändert:** 1920x1080 HEVC: 1959 und 2059 ms, 1080p (0.2.1: 1973 und 2111 ms). 3840x2160 HEVC: nach dem
  Start 1867 ms und 1080p, weil das System 4K bei Wukongs Ausgabe ablehnt, auch wenn sie schon steht; nach dem Ende bleibt
  1080p bis zum Schluss, wie mit 0.2.1 (1985 ms, 1080p). Die neue Prüfung griff bei diesen Läufen nicht.

### Terminate App schließt das Spiel (2026-11-01, nicht veröffentlicht)

Gemessen auf der PS5 Pro mit 10.20 und ShadowMountPlus 1.7beta11.

- **Schließen:** `sceSystemServiceKillApp(app, -1, 0, 0)` schließt den Titel im Vordergrund wie „Spiel schließen“ im
  Startbildschirm. Sonde `close_probe.elf`: `rc=0`, der Aufruf kehrt nach 1,0 bis 1,2 s zurück, dann sind der Titel aus dem
  Vordergrund und sein Ordner in `/mnt/sandbox` weg (GTA III dreimal, Wolverine einmal).
- **ShadowMountPlus danach:** Es gibt den Titel frei, sobald kein Eintrag `/mnt/sandbox/<Titel>_*` mehr da ist, nach einer
  Sekunde Karenz, sonst nach 10 s. Ein Neustart desselben Titels vor dieser Freigabe läuft, aber ShadowMountPlus bindet den neuen
  Prozess nicht („runtime preparation timed out“) und sieht später auch sein Ende nicht; zurück bleibt ein leerer Ordner
  `<Titel>_000`. Das löste sich, als ein anderer Titel startete. Mit Warten bis zur Freigabe lief der Neustart sauber
  (`GAME started`); bei Wolverine war das 2,8 s nach dem Schließen.
- **Host mit `close_game_on_quit = enabled`, Moonlight unter Windows:** Ctrl+Alt+Shift+E im Stream von GTA III ergab „Closing
  PPSA03527 for Terminate App“, nach 1073 und 1137 ms geschlossen, ShadowMountPlus gab ihn frei. Ctrl+Alt+Shift+Q (mit
  „Quit app on host PC after ending stream“ aus, Vorgabe von Moonlight) beendet nur den Stream, das Spiel läuft weiter. GTA III
  danach erneut gewählt: startete normal.
- **Beenden aus der App-Liste von Moonlight, ohne das Spiel weiter als laufend zu melden:** kam nicht beim Host an. Der Host
  meldete nach dem Stream kein laufendes Spiel (Patch 0006), Moonlight übersprang `/cancel` und blieb unter Windows bei
  „Quitting…“ stehen (zweimal gesehen, mit It Takes Two und GTA III). Ohne `close_game_on_quit` gilt das weiter.
- **Mit `close_game_on_quit` meldet der Host das Spiel nach dem Stream weiter als laufend** (`serverinfo`, `/resume`), solange es
  vorn ist. Moonlight unter Windows, nach einem Neustart der Konsole: It Takes Two aus Moonlight gestartet, mit Ctrl+Alt+Shift+Q
  verlassen, in der Liste als laufend gezeigt, fortgesetzt („Session app [It Takes Two] resumed“), wieder verlassen. Dann GTA III
  gewählt und das Beenden von It Takes Two bestätigt: „Closed PPSA02343 in 1069 ms“, ShadowMountPlus gab es 1 s später frei, GTA III
  startete und wurde gebunden (`GAME started`). Stopp-Knopf auf GTA III in der Liste: „Closed PPSA03527 in 962 ms“, freigegeben,
  danach keine Ordner mehr in `/mnt/sandbox`.
- **Moonlight unter iOS und Android, gleicher Host:** iOS: It Takes Two gestartet, verlassen, „Quit App“: „Closed PPSA02343 in
  1049 ms“. Sofort wieder gestartet: Der Host wartete auf die Freigabe („Starting PPSA02343 4411 ms after Terminate App closed
  it“), ShadowMountPlus band ihn. „Resume App“ setzte fort, „Quit Running App and Start“ auf GTA III schloss It Takes Two
  (1137 ms) und startete GTA III. Android: „Quit Current Game and Start“ zweimal, GTA III → It Takes Two (1059 ms, während iOS
  GTA III noch streamte; dessen Stream endete, denn `cancel()` beendet alle Sitzungen und prüft nicht, welcher Client die App
  gestartet hat) und It Takes Two → GTA III (835 ms); „Resume Session“ setzte GTA III fort. Während Android streamte, kam iOS
  nicht dazu („PS5 capture busy with another session“, wie vorher). Ein letztes Beenden schloss GTA III mitten im Stream von
  Android (1167 ms). ShadowMountPlus band jeden Start und gab jeden geschlossenen Titel frei, ohne „runtime preparation timed
  out“ oder „Device busy“; danach keine Spielordner in `/mnt/sandbox`. „Quit Session“ unter Android nicht einzeln geprüft; es
  ruft dasselbe `NvHTTP.quitApp()` auf wie „Quit Current Game and Start“.
- **Firmware 13.00, Testclient `stream_test` statt Moonlight, Mario 64 und GTA III (2026-11-02):** `/cancel` schloss das Spiel nach
  dem Stream und mitten im Stream: Mario in 2325 bis 3080 ms, GTA III in 848 und 885 ms, danach kein Spielordner in `/mnt/sandbox`.
  Sofort wieder gestartet: Der Host wartete 1861 und 1871 ms ab dem Schließen, der Start klappte. Wechsel durch `/cancel` und direkt
  `/launch` von GTA III klappte. Stream-Ende ohne `/cancel`: Mario lief weiter, `serverinfo` meldete BUSY, `/resume` streamte
  wieder. Mario hinter dem Startbildschirm: weiter BUSY, ein `/cancel` in dem Zustand lief nicht. Settings-App über Mario: FREE, `/cancel`
  ließ Mario offen, ebenso „Restart host“. „PS5 Screen“ mit Mario vorn, dann `/cancel`: nichts geschlossen. Eine andere App bei
  laufendem Spiel: 409, das Spiel blieb als laufend gemeldet und `/cancel` schloss es danach. Alle Titel starteten danach weiter.
- **Einstellung aus, gleiche Konsole:** H.264 und HEVC 4K wie vorher (längste Bildlücke 19 bis 23 ms), `/cancel` ließ Mario laufen.
  Ein selbst angelegter leerer Ordner `/mnt/sandbox/PPSA00000_000` hielt den Start von Mario nicht mehr auf; ein echter Rest nach
  einem verpassten Ende von ShadowMountPlus war es nicht. Mario hinter der App blockierte GTA III weiter mit 409.
- **Offen:** Moonlight auf 13.00 (Resume und Quit im Menü) und der Fall, in dem der Host bis zur Obergrenze von 11 s wartet (ein leerer
  Ordner `<Titel>_000` ist noch da).

### Quantisierer auf 10.x folgt der Bitrate (2026-11-01, nicht veröffentlicht)

Gemessen auf der PS5 Pro mit 10.20, GTA III bei schneller Fahrt mit bewegter Kamera, HEVC 10 bit HDR 1920x1080 bei 60 fps,
Moonlight unter Windows mit 16 364 kbit/s angefordert. Messbuild: der Host zählte die Bytes jedes gelesenen Bildes (`ReadVideo2`) und
schrieb alle 5 s Mittelwert, stärkste Sekunde und größtes Bild nach `host_diag.log`; der Quantisierer kam je Verbindung aus einer Datei.

| `cfg+0xc4`/`+0xc8` | Mittel (Mbit/s) | stärkste Sekunde | größtes Bild |
|---|---|---|---|
| nicht gesetzt (Stand 0.1.7) | 0,26–0,41 | 0,7 | IDR 30 KB, sonst ~2 KB |
| 30/34 | 1,4–2,4 | 3,1 | IDR 69 KB |
| 27/31 | 1,9–2,9 | 3,7 | IDR 43 KB |
| 24/28 | 2,4–3,8 | 5,1 | IDR 110 KB |
| 21/25 | 4,2–6,7 | 10,0 | IDR 169 KB, sonst bis 70 KB |

- Ohne Quantisierer ist das Bild in Blöcken zerlegt (Kanten, Schrift, die Sonne der App); mit 27/31 ist dieselbe Ansicht bei derselben
  Anforderung scharf (Bildschirmfotos im PR).
- **Mit dieser Tabelle, GTA III:** Moonlight unter Windows (16 364 kbit/s → 24/28, HEVC mit und ohne HDR), Android (34 988 → 21/25,
  HEVC mit und ohne HDR), iOS (46 988 → 21/25, HEVC HDR) und Switch (1280x720, 16 024 → 24/28, HEVC): scharf, keine Abbrüche,
  höchstens 6,2 Mbit/s in einer Sekunde. Unter Windows wirkte HDR auf der hellen Karte von GTA III ausgewaschen, ohne HDR und
  unter Android mit HDR nicht; der Quantisierer ändert keine Farben, das ist offen und nicht Teil dieser Änderung.
- `SetVideoBitrate` mitten im Stream (20 000 kbit/s, ohne Quantisierer) gab 0 zurück, danach scheiterte `ReadVideo2` mit
  `0x80c78000`; der Host öffnete die Aufnahme nach 332 ms neu. Eine Ratensteuerung gibt es auf 10.x damit nicht.
- Eine Verbindung mit 21/25 endete nach 24 s mit „Ping Timeout“ (der Client schickte keine Steuerpakete mehr, das Video lief bis
  zuletzt); die Wiederholung lief 90 s ohne Abbruch.
- Die Tabelle in `encoder_qp.h`: 30/34 unter 8 Mbit/s, 27/31 ab 8, 24/28 ab 12, 21/25 ab 20, jeweils etwa ab dem Doppelten der
  stärksten Sekunde. Feiner als 21/25 ist nicht gemessen. Andere Spiele können mehr brauchen; der Encoder hält die Bitrate nicht ein.
- **Offen:** H.264 auf 10.x mit diesen Werten (gleicher Quantisierer, mehr Bits erwartet; alle Clients wählten HEVC) und andere
  10.x-Versionen.

### Schalter und Bericht, Stand 0.1.8 (2026-11-02)

[v0.1.8](https://github.com/Darkatek7/ProsperoSunshine/releases/tag/v0.1.8) auf `6ee70fa5`, CI grün. Katalog-PR gemergt und die Katalogdatei nennt
0.1.8; die Anzeige im Store und eine Installation daraus sind nicht geprüft. Gemessen auf der eigenen Konsole mit 13.00 und dem Testclient `stream_test`, Build `6ee70fa5` bzw. der Stand direkt davor.

- **Schalter „Quit closes the game“ in den Settings:** Einschalten mit „Save and restart host“ schrieb `close_game_on_quit = 1`,
  der Host meldete `close_game_on_quit=1`. Mario 64 aus dem Stream, danach `/cancel`: „Closed PPSA99310 in 2814 ms“, kein Spielordner
  mehr in `/mnt/sandbox`. Ausschalten entfernte den Schlüssel, die Datei war wieder leer. Ein von Hand gesetztes `enabled` zeigt die App
  als an; das ist nur im Unit-Test geprüft.
- **Bericht:** Nach dieser Sitzung enthielt `diagnostics.txt` weder den Namen noch die Title-ID von Mario (`Session app [<title>]`,
  `Closed <title> in 2814 ms`) noch die lokale Benutzer-ID; „PS5 Screen“ blieb lesbar.
- **Abschluss mit `6ee70fa5`, Einstellung aus:** H.264 1080p in Mario 10 s: 554 Bilder, längste Lücke 20 ms, Ton −19,5 dBFS. HEVC und HDR in
  Mario bei 4K-Anfrage: das System lehnte den großen Modus ab, 1080p wie bei manchen Titeln bekannt, je längste Lücke 20 ms. HDR 4K ohne
  Spiel 6 s: 332 Bilder bei 3840x2160, längste Lücke 19 ms. Keine fehlenden Bilder, alles fehlerfrei dekodiert.
- **Offen:** der Schalter mit Moonlight selbst statt dem Testclient. Der Quantisierer aus PR 8 läuft auf dieser Konsole nicht, weil 13.00
  ihn nicht verwendet.

### Modi unter 1080p (2026-11-02, 13.00)

Die Messungen „Im Host“ bis „Mit Deckel“ liefen auf einer eigenen Variante vor dem Merge von PR 9 (#9, Modi unter 1080p auf 10.20): Sie wählte den größten Modus, der die Anfrage nicht übersteigt, prüfte unter 1080p immer das erste Bild und brauchte keinen Schalter. HEVC-Budgets, Bitrate-Deckel und die fünf Wiederholungen von `OpenVideo` unter 1080p sind dieselben wie im gemergten Stand, der den nächstgelegenen Modus wählt und 13.00 mit 10.20 als verifiziert führt.

Remote Play ordnet 640x360, 960x540 und 1280x720 die Modi 1, 2 und 3 zu (`SceRemotePlay 0x6f8d5`). Gemessen mit `src/mode_spike.cpp` auf dem Home-Bildschirm ohne HDMI-Kabel (`video out mode 0x13c`), RA-Profil 180, 60 fps, 3 Mbit/s, je 3 s: Alle drei Modi starten mit H.264 und HEVC, 179 oder 180 Bilder, `ffprobe` nennt die verlangte Größe, ffmpeg dekodierte die geprüften Aufnahmen fehlerfrei (H.264 alle drei, HEVC je Modus den höchsten startenden Wert, bei 360p auch `0x4000`).

Die frühere Aussage „unter 1080p startet Avcap2 nicht“ gilt damit auf 13.00 nicht mehr. Warum der alte Spike scheiterte, ist nicht geklärt; der Datensatz ist seitdem gewachsen.

HEVC braucht einen kleineren Wert in `cfg+0x10c`: Mit dem Wert für 1080p, `0x60000`, meldet `Start` in allen drei Modi `0x80c78000`.

| Modus | geprüft, startet | geprüft, startet nicht |
| --- | --- | --- |
| 3 (720p) | `0x10000` bis `0x40000` | `0x60000` |
| 2 (540p) | `0x8000` bis `0x20000` | `0x30000`, `0x60000` |
| 1 (360p) | `0x4000` bis `0x20000` | `0x60000` |

**Im Host (Testbuild vor 0.2.0, `stream_test`, `ffprobe` und ffmpeg auf der Aufnahme).** Der Host nimmt den größten Modus, der die Anfrage nicht übersteigt, und setzt je Modus den höchsten startenden Wert von `cfg+0x10c`.

Home-Bildschirm ohne HDMI-Kabel (`0x13c`), je 10 s, 19 Läufe: H.264 und HEVC 8 Bit in 1280x720, 960x540 und 640x360, HEVC 10 Bit (HDR) in denselben drei, 30 fps mit H.264 540p und HEVC 360p, dazu 1080p und `2556x1178` zum Vergleich (beide 1080p). Alle 59,9 oder 29,97 fps, keine fehlenden Bilder, längste Lücke 20 ms (35 ms bei 30 fps), fehlerfrei dekodiert, `ffprobe` nennt die erwartete Größe. `960x544` lieferte 960x540, `854x480` lieferte 640x360. H.264 720p, HEVC 720p und HEVC 360p starteten auch mit 39 Mbit/s; dieser Bildschirm ist dafür zu ruhig, tatsächlich flossen höchstens 3,4 Mbit/s.

Mario-64-Demo, gestartet aus dem Stream, ohne HDMI-Kabel, je 35 s:

| Verlangt | Geliefert | Bitrate Host | gemessen | Bilder |
| --- | --- | --- | --- | --- |
| H.264 1280x720 | 1280x720 | 7,3 Mbit/s | 3,7 Mbit/s | 2096 |
| H.264 640x360 | 640x360 | 2,0 Mbit/s | 1,3 Mbit/s | 2096 |
| H.264 960x544 | 960x540 | 23 Mbit/s | 7,4 Mbit/s | 2096 |
| HEVC 1280x720 | 1280x720 | 23 Mbit/s | 6,1 Mbit/s | 2094 |
| HEVC 960x540 | 960x540 | 3,4 Mbit/s | 3,0 Mbit/s | 2093 |
| HDR 960x540 | 960x540 | 7,3 Mbit/s | 4,3 Mbit/s | 2094 |

Alle 59,94 fps, keine fehlenden Bilder, längste Lücke 23 ms, fehlerfrei dekodiert. Je ein Bild aus H.264 360p, HEVC 540p und HEVC 720p angesehen: vollständig und scharf.

**Rückfall:** Testbuild mit dem abgelehnten Wert `0x60000` bei 720p, mit HDMI-Kabel, HEVC, 720p verlangt: Warnung `mode refused by the system, delivering 1920x1080` 0,2 s nach der Anfrage, derselbe Stream lief mit 1920x1080 weiter (707 Bilder in 12 s, keine fehlenden, fehlerfrei dekodiert). Die Streams danach mit H.264 720p und HEVC 540p bekamen wieder ihren Modus. Ein offenes Spiel, das einen kleinen Modus ablehnt, oder ein Modus, der startet und kein Bild liefert, wurde nur im Unit-Test geprüft (`capture_open_test`).

**Release-Build 0.2.0 (`c5dd1fae`), HDMI-Kabel wieder eingesteckt, Fernseher als Ausgabe (`0x13c`, in Mario `0x144`):** Auf dem Home-Bildschirm H.264 720p, HEVC `960x544` (geliefert 960x540), H.264 und HEVC 1080p und HEVC 4K je 10 s, alle mit 59,9 fps, ohne fehlende Bilder, längste Lücke 19 ms, fehlerfrei dekodiert. Also gehen auch 1080p und 4K weiter wie bisher.

Zwei Ausreißer, beide nicht wiederholbar:

- **Ein Fehlstart von 13 Spielstarts.** H.264 360p, Mario aus dem Stream gestartet: `PS5 capture failed to start: -1`, der Client beendete mit Code 4. `GetVideoOutMode` meldete mindestens 3,3 s lang 0 (sonst 580 bis 820 ms) und `OpenVideo` `0x81950008`, auch beim Versuch mit 1080p im selben Öffnen. Ein Stream, der gleich 1080p verlangt hätte, wäre also ebenso gescheitert; am kleinen Modus lag es nicht. Warum die Ausgabe dieses Mal länger brauchte, ist nicht geklärt. Ohne HDMI-Kabel kam das in sechs Spielstarts nicht vor, das ist aber zu wenig für einen Zusammenhang. Der direkte Vergleich danach, abwechselnd je fünfmal 1080p und 360p in Mario für je 6 s, lief zehnmal ohne fehlende Bilder.
- **Ein fehlendes Bild.** HDR 360p, Home-Bildschirm: Bild 18 fehlte, längste Lücke 494 ms, ein IDR mehr, sonst fehlerfrei. Vier Wiederholungen mit HDR 360p liefen ohne fehlendes Bild. Ob das Netz oder die Konsole schuld war, zeigt die Messung nicht.

**Echtes Moonlight (Besitzer, Build `c5dd1fae`, vor dem Bitrate-Deckel):** `640x360@120` verlangt, 60 fps geliefert, 90 s HEVC HDR mit 2 Mbit/s in 360p. Drei Entzüge (`0x81950009`, nach 1,6 s, 2,5 s und 0,4 s wieder da), der Besitzer hatte dabei ein Spiel gestartet und beendet. Ein Lauf von 120 s mit dem Testclient ohne Spiel blieb frei davon: 7189 Bilder, keine fehlenden, längste Lücke 19 ms. Mit 150 Mbit/s am Client (beim Host 119) meldete `Start` `0x80c78000`, der Stream lief in 1080p weiter.

**Bitrate-Grenze (Testclient, Home-Bildschirm, je 4 s, Größe per `ffprobe`).** Zu viele Bits je Bild, und `Start` scheitert. Gefunden über den Rückfall auf 1080p:

| Modus | fps | läuft bis | scheitert ab |
| --- | --- | --- | --- |
| H.264 360p | 60 | 29 Mbit/s | 39 Mbit/s |
| H.264 360p | 30 | 15 Mbit/s | 20 Mbit/s |
| HEVC 360p | 60 | 63 Mbit/s | 79 Mbit/s |
| HEVC 360p | 30 | 31 Mbit/s | 39 Mbit/s |
| HEVC HDR 360p | 60 | 63 Mbit/s | 119 Mbit/s (dazwischen nicht geprüft) |
| HEVC 540p | 60 | 63 Mbit/s (mehr nicht geprüft) | – |
| H.264 und HEVC 720p | 60 | 119 Mbit/s (mehr nicht geprüft) | – |

Die Grenze hängt also am Bit je Bild, nicht am HEVC-Budget `cfg+0x10c`, das H.264 gar nicht setzt. Warum 360p sie erreicht und 720p nicht, ist nicht geklärt. Der Host deckelt deshalb das Mittel: 360p 30 Mbit/s, 540p 60, 720p 120, bei 30 fps je die Hälfte. Mehr verlangt der Client vergebens, der Modus bleibt.

**Mit Deckel (Build `b98ef0be`, HDMI-Kabel steckt, Client verlangt 200 Mbit/s, beim Host 159):** H.264 360p mit 60 und 30 fps, HEVC 360p, HDR 360p mit 30 fps, H.264 540p und 720p auf dem Home-Bildschirm je 6 s; in Mario je 25 s H.264 360p mit 60 und 30 fps und HEVC 360p. Alle im verlangten Modus, ohne Rückfall, keine fehlenden Bilder, längste Lücke 24 ms (37 ms bei 30 fps), fehlerfrei dekodiert. In Mario flossen dabei tatsächlich bis 7,1 Mbit/s, den Deckel selbst hat also kein Bildinhalt erreicht. Zuvor (`ba86e148`, 360p noch 60 Mbit/s) liefen je 5 s alle drei Modi mit H.264, HEVC und HDR bei 60 und 30 fps; nur H.264 360p fiel dort zurück, was zum tieferen Deckel führte. H.264 1080p und HEVC 4K blieben auf `b98ef0be` unverändert (keine fehlenden Bilder, fehlerfrei dekodiert).

**Gemergter Stand 0.2.0 (`5900b297`, PR 9 mit den 13.00-Teilen darauf, HDMI-Kabel steckt), Testclient, je 6 s auf dem Home-Bildschirm und 25 s in Mario, 17 Läufe:** Die Wahl des nächstgelegenen Modus stimmt: `854x480` bekam 960x540 (H.264, und in Mario HEVC), `1600x900` 1920x1080, `1280x800` 1280x720, `960x544` 960x540 (HEVC, und in Mario H.264). Mit 200 Mbit/s am Client griffen die Deckel ohne Rückfall: H.264 360p mit 30 und 15 Mbit/s (60 und 30 fps), HEVC 540p mit 60, HDR 540p mit 30 (30 fps), H.264 720p mit 120, HEVC 720p mit 60 (30 fps), in Mario HEVC 540p mit 60 und H.264 360p mit 30. Dazu HDR 360p, H.264 und HEVC 1080p und HEVC 4K. Alle in der erwarteten Größe, keine fehlenden Bilder, längste Lücke 22 ms (36 ms bei 30 fps), fehlerfrei dekodiert, keine Warnung im Log. Kein Experimental-Schalter war an. Der Rückfall eines kleinen Modus ist auf diesem Stand nur im Unit-Test geprüft; live lief er auf der Variante davor (oben, „Rückfall“).

Nicht geprüft: echtes Moonlight auf dem Build mit Deckel, ein kleiner Modus, der seinen Deckel wirklich ausschöpft, eine echte PS Vita auf 13.00 (auf 10.20 lief sie, „Modi unter 1080p auf Firmware 10.20“).

### Firmware-Sonde auf 10.20 und 10.40 (2026-11-05, Bericht eines Mitwirkenden)

`firmware_probe.elf` aus diesem Stand (`docs/firmware-probe.md`), je ein Lauf ohne Optionen und ohne Proben, auf dem
Startbildschirm. PS5 Pro mit 10.20: Host 0.3.1 lief, `QUIT` hielt ihn an, am Ende lief das installierte ELF wieder. PS5 mit
10.40: kein Host lief, keiner wurde gestartet. Beide Läufe 147 s, der Startbildschirm blieb im Vordergrund. Hier nicht
nachgemessen; die Angaben stammen aus den beiden `report.txt`.

| | 10.20 (Pro) | 10.40 |
| --- | --- | --- |
| Datensatz des Hosts | `0x2c8` (−0x18), als einzige Größe von −0x40 bis +0x20 angenommen; Baukasten = `configure()` | ebenso |
| H.264 1080p60, 3 s | 180 Bilder, 34 Slices je Bild | 180 Bilder, 34 Slices |
| HEVC 8/10 Bit nur mit `cfg+0x24` | 17 Slices | 17 Slices |
| QP 20/24 in `0xc4`/`0xc8` gegen ohne | 3,5-fache Bytes; 52 scheitert an `Start` (`0x80c78000`) | 4,8-fache Bytes; 52 ebenso |
| `SetVideoBitrate` von 6 auf 20 Mbit/s | danach 1 Bild, dann `0x80c78000` | 0 Bilder, dann `0x80c78000` |
| ResourceArbitrator | per NID, Firmware in der Tabelle | per NID (`0xde0`, `0xfe0`, `0x1220`, `0x1360`), 10.40 nicht in der Tabelle |

- Auf beiden lehnt der Validator dieselben Wörter ab (`0x80`, `0x188`, `0x1fc`, `0x2b0`), die Bitratenfelder von 13.00
  lassen `Start` scheitern, ebenso H.264 10 Bit (`0x80c70004`) und HEVC mit den Remote-Play-Feldern von 13.00 (`0x81950001`).
- Alle zwölf Modi liefern Bilder (360p60, 540p60, 720p60, 1080p30, 1440p60, 2160p60 in H.264 und HEVC).
- **H.264-Slices:** Die Slice-Felder von 0.3.1 (`0x40`/`0x44`/`0x48`) nimmt die Firmware an, die Zahl der Slices ändern
  sie nicht: 360p 23, 540p und 1080p 34, 720p, 1440p und 2160p 45. HEVC: 6, 9, 12, 17, 23, 34. Über 32 Slices lehnen die
  Hardware-Decoder von FFmpeg unter Windows ab.

### Firmware-Sonde auf 9.60 (2026-11-06, PS5, nicht Pro)

`firmware_probe.elf` aus 0.3.4 mit dem Datensatz von 10.x, ein Lauf ohne Optionen, 137 s, ein Titel blieb im Vordergrund. Der
Host lief, `QUIT` hielt ihn an, am Ende lief er wieder. Die Angaben stammen aus `report.txt`.

- **Datensatz:** Von −`0x40` bis +`0x20` öffnet nur `0x2c0` (−`0x20`), ohne innere Köpfe. `0x2c8` (10.x), `0x2d0`, `0x2e0` (13.00)
  und `0x2e8` (13.60) lehnt der Validator ab (`0x81950001`), die übrigen Größen enden mit `0x81950008`.
- **Bilder:** H.264 und HEVC 8/10 Bit (nur `cfg+0x24`) in 360p, 540p, 720p, 1080p und 1440p. 2160p scheitert an `OpenVideo` mit
  `0x81950008`; `video out mode` war `0x34`, also ohne das 4K-Bit. Slices wie auf 10.x: H.264 1080p 34, HEVC 17.
- **Rate:** `cfg+0xc4`/`+0xc8` sind hier kein QP: 0/0, 36/40, 28/32, 20/24 und 52/0 ergaben alle 4,6 bis 4,7 Mbit/s bei einem IDR
  alle 6 Bilder, und 52 scheitert nicht an `Start`. Ohne erzwungene IDR kamen 0,65 bis 0,86 Mbit/s. `0xc0` lässt sich nicht setzen
  (`0x80c7000d`, auch mit 1 bis 16). Die Phase `ratesearch` fand den QP in `cfg+0xac`/`+0xb0`, 0x18 tiefer als auf 10.x. Startbildschirm,
  1 s, IDR alle 6 Bilder: ohne 4692 kbit/s, 30/34 4627, 27/31 5061, 24/28 6275, 21/25 8032, 18/22 11016, 15/19 15761, 12/16 19315,
  10/14 22097, 8/12 28197, größtes Bild von 29007 bis 238665 Bytes. Der Host setzt ihn wie auf 10.x. Im Spiel nicht gemessen; das Bild sah beim Tester etwas besser aus.
  `SetVideoBitrate`: 1 bis 12 Mbit/s antworten 0 und ändern das größte Bild nicht, 16 und 20 Mbit/s beenden den Kanal (`0x80c78000`).
- **Validator:** schützt `0x80`, `0x168`, `0x1d8`, `0x2a8`.
- **Systembibliothek:** `libSceAvcap2` hat 5 Exporte weniger als 13.00 (`ReadVideo3`, `GetImagePasteMemory`,
  `ChangeScanInResolution`, `GetVideoEncoderCapability`, `RequestSkipFrame`). ResourceArbitrator per NID an den Offsets von 10.20.

### Erster Stream auf 9.60 (2026-11-06, PS5, nicht Pro, Moonlight für Android)

Host 0.3.4 mit dem Datensatz `0x2c0`, Angaben aus `sunshine.log`.

- **Stream:** „PS5 Screen“, H.264 1080p60, angefragt 14988 kbit/s, Ton über Avcap2 (Opus, 2 Kanäle), Controller und Maus, etwa eine
  Minute. Spielstart aus Moonlight (Uncharted) lief, auch nach dem Einschalten der Konsole. Die Bitrate im Spiel ist nicht gemessen.
- **Die App sieht `/data` hier nicht:** Sie meldete `/data/pldmgr` als fehlend, obwohl der Ordner da ist. `app.quit` blieb liegen, also
  schloss sie sich nicht, und der Start scheiterte mit `PPSA47989 is in front` (Antwort 409). `sunshine.conf` blieb leer. Seit
  `FILE`/`INSTALL` geht sie über den Host: Schalter für HEVC, HDR, 1440p/4K, Surround und „Below 1080p“ landen in `sunshine.conf`,
  „Save and restart host“ und „Restart host“ liefen, der Bericht wurde gespeichert, `/app0/app.quit` schloss die App in 2 s.
- **Start at boot:** Der Slot `local/prospero-sunshine` startete den Host nach jedem Einschalten.
- **1440p im Spiel:** Uncharted lief mit `video out mode` `0x44`, ohne das Bit `0x20`. Bei einem Client mit 1440p antwortete das
  System mit einer Ablehnung, der Host lieferte 1080p (HEVC 10 Bit, 17 Slices).
- **Zwei Abstürze auf 0.3.4 ungeklärt:** Die Konsole ging nach „Save and restart host“ aus und beim nächsten Öffnen der App. Danach ließ es sich
  nicht wiederholen: je 2 Minuten mit leerer Datei, HEVC und HDR, 1440p/4K und allen Schaltern, dann beide Knöpfe. Die Logs enden
  vor dem Absturz. Dieselbe Zeile zu kstuff stand auch in den Läufen ohne Absturz.
- **`-1` beim Spielstart:** Lag an einem alten Host aus dem Slot des Payload Managers, Datensatz `0x2c8`, `OpenVideo` `0x81950001`.
- **Auf 0.4.0:** Host mit diesen Änderungen auf 0.4.0 gestartet, Uncharted aus Moonlight für Android gestartet (`Started PPSA05684`),
  2 Minuten Stream, HEVC HDR, angefragt 118988 kbit/s, geliefert 50000. Die Webseite lief auf 9.60 nicht.
- **Dritter Absturz:** Die Konsole ging aus, als `host_restart.elf` gesendet wurde, etwa 2 Minuten nachdem Uncharted in derselben Sitzung
  geschlossen worden war. Die hochgeladenen Dateien hatten schon die richtigen Prüfsummen, `host_restart.log` blieb unverändert, und das
  Hostlog hat keine Zeile zum Beenden. Nach dem Neustart lief alles. Ursache ungeklärt.
- **409 bei offenem Titel:** „Not starting PPSA05684: CUSA06534 is still open“, der Client meldete Fehler 409. „PS5 Screen“ startete,
  das Spiel ließ sich dann an der Konsole öffnen.
- **DualSense am Telefon:** Laut Besitzer spürbare Trigger-Effekte in Uncharted, DualSense per Bluetooth an einem Android-17-Telefon,
  Client ist der Artemis-Fork von R3VALATiON (Branch `feature/native-dualsense-adaptive`). Keine Vibration; die Motorwerte waren in
  dieser Sitzung nicht im Log, ob das Spiel sie setzt, ist also nicht gemessen.
- **Haptik als Rumble:** `sceAvcap2OpenHaptic` öffnete mit Zweck 2, Encoder 2 und der RA-Id, `ReadHaptic` lieferte während eines Streams mit
  virtuellem Pad 47 Pakete je Sekunde zu 2048 Byte, Float Stereo (`docs/re/haptics.md`). In 2:40 Minuten Uncharted standen vorher 5 Trigger-Zeilen
  und keine Motorzeile im Debug-Log, mit dem Kanal 1271 Motorzeilen. Laut Besitzer gibt es damit Vibration in Uncharted, in der Fassung mit
  Quadratwurzel aber viel zu stark, fast ein Brummen. Sie kommt bei ihm nur mit „Emulate rumble support with vibration“ im Client; warum, ist
  ungeklärt, und ob dann der DualSense oder das Telefon vibriert, ist nicht geprüft.
- **Einstellungen:** `haptic_mode`, `haptic_strength`, `trigger_knock_strength` und `trigger_force` stehen laut Besitzer in App und Webseite, und
  die Stärke wirkte im laufenden Stream. `trigger_force` änderte laut Besitzer nichts Deutliches. Die schwächere Rumble-Fassung
  (linear, höchstens 128 von 255) ist nach dem Umbau nicht mehr beurteilt worden.
- **HD nicht fertig:** Mit dem Patch 0022 und dem Client `prospero-ps5` kam die Wellenform an. Mit zwei Rahmen je Bericht (Client v8, Weg über
  48 kHz) fand der Besitzer die Vibration deutlicher und abgestuft, aber mit Mikroaussetzern und bei Dauervibration im Auto wachsender
  Verzögerung. Client v9 mit Laufzeitmessung und v10 mit dem direkten 3-kHz-Weg gaben keine Vibration mehr, auch nicht bei 200 %, obwohl
  v10 47 Berichte je Sekunde ohne Fehler, Lücken oder Verluste meldete. Ursache nicht gefunden. Der Besitzer hat die Arbeit daran beendet, die
  Konsole steht auf `rumble`. Wer weitermacht, beginnt beim Stand von v8.
- **Vibration in einem PS4-Titel:** Laut Besitzer einfache Vibration in EA Sports UFC 3 am selben DualSense, an einem 8BitDo Ultimate 2C
  Wireless dagegen keine. Nicht im Log gemessen.

### Profil je Spiel und eigene Liste (2026-11-04, 10.20, PS5 Pro)

Gemessen wie oben (Moonlight für Linux, Hostlog), Host aus `keyboard-mouse-pad` mit Patch 0016 und 0017,
`keyboard_mouse_profile_by_game = 1`.

- **Liste beim Start:** Der Host legte `keyboard_profiles.txt` an, mit den 41 installierten Titeln als Kommentarzeilen:
  Spiele, Homebrew (auch mit eigenen Präfixen wie `WBRT`), Forwarder und die Steam-Spiele von Playden (`FAKE…`). Titel aus
  der Karte stehen mit ihrem Profil darin, etwa `PPSA10595 = fighting # TEKKEN™8 (from the title map)`.
- **Eigene Liste:** `PPSA03527 = shooter` aktiviert: „Keyboard pad profile shooter for PPSA03527, from your list“. Danach
  ein eigenes Profil `[gta]` auf Basis von `shooter` (Leertaste springt, Shift rennt, E und F steigen ein, R lädt nach,
  das Rad wechselt die Waffe): „Keyboard pad profile gta for PPSA03527, from your list“; laut Besitzer wie in der
  PC-Fassung.
- **Karte:** It Takes Two (`PPSA02343`) und Elden Ring (`PPSA04609`) bekamen `action`, „from the title map“.
- **Abdeckung der installierten Spiele:** Von etwa 25 PS5-Spielen auf der Konsole fand die Karte 18 (Titel-ID, Store-Konzept
  aus `param.json` oder englischer Name). Es fehlen u. a. F1 25, EA FC 26, WWE 2K26 und GTA III Definitive Edition, die bei
  Wikidata nicht als PS5-Spiele mit Genre stehen; für sie ist die eigene Liste da.
- Unit-Tests `title_profile_test` (Titelschlüssel, Namen, sortierte Tabellen, Liste, eigene Profile, neue Titel) und
  `titles_test` (Store-Konzept, englischer Name).

Nicht geprüft: die Menütasten außerhalb eines Spiels live (kein Logeintrag in dieser Sitzung), ein Spielwechsel während
eines Streams, PS4-Titel live.

### Tastatur und Maus als Controller (2026-11-04, 10.20, PS5 Pro)

Gemessen vom Besitzer mit Moonlight für Linux auf einem PC ohne Controller, dazu Hostlog (`sunshine.log`) und Kernel-Log der
Konsole. Host aus `keyboard-mouse-pad` (0.3.0 mit dem Tastatur-Patch, heute 0017), per ELF-Loader gestartet.

- **Pad beim ersten Tastendruck:** „The keyboard and mouse of a client without a controller play as virtual pad 0“, im
  Kernel-Log `DEVICE_ADDED` und `DEVICE_OWNER_CHANGED` auf den Benutzer der Konsole. Kein „Keyboard connected“: die geteilte
  Tastatur und Maus der Konsole entstehen für dieses Gerät nicht.
- **GTA III Definitive Edition (Profil `shooter`):** Laufen mit W A S D, Kamera mit der Maus, Zielen und Schießen mit den
  Maustasten, der Waffen-Cheat (R2 R2 L1 R2 ← ↓ → ↑ ← ↓ → ↑) über 3, Q und die Pfeiltasten eingegeben. Im Auto mit der
  Fahrzeugebene (Feststelltaste): Gas, Bremse und Lenkung mit Anlauf; laut Besitzer „perfekt“.
- **Ein Controller übernimmt:** Ein DualShock 4 am selben Gerät (USB): „Controller [0] takes over the keyboard's virtual pad
  0“, keine Frage „Who's using this controller?“. Beim Abziehen „virtual pad 0 detached“, die Konsole meldet die Trennung;
  die nächste Taste legte nach 8 s wieder Pad 0 an. Ein zweiter Controller am selben Gerät wurde Pad 1 mit Frage.
- **Zwei Geräte:** Ein DualShock 4 an einem anderen Gerät als Spieler 1 (It Takes Two), danach die Tastatur dieses PCs:
  „virtual pad 1 attached, the console asks who uses it“. F1 (PS) bewirkte für Spieler 2 nichts. Am Ende beide Pads gelöst.
- **Menütasten:** Ein Menü in Elden Ring reagiert nur auf das Steuerkreuz, nicht auf den linken Stick. Mit gehaltener Taste
  rechts der Leertaste (Rechts-Alt auf einer spanischen Tastatur) W und S auf dem Steuerkreuz: Das Menü ließ sich bedienen,
  sechs Wechsel im Log („menu keys“, „back to action“).
- **Mausempfindlichkeit:** Strg+Alt mit dem Mausrad, je Schritt im Log und in `keyboard_sensitivity.txt` je Gerät und Profil
  gespeichert. GTA III und Elden Ring begrenzen die Drehgeschwindigkeit des rechten Sticks selbst: Über etwa 1000 % drehte
  die Kamera laut Besitzer nicht schneller (ein Versuchsbuild ging bis 10 000 %).
- **Moonlight-Clients:** Ein Gerät mit einem gepaarten, aber ausgeschalteten Controller bekam das Tastatur-Pad; ein noch per
  USB steckender Controller übernahm es sofort beim Verbinden (Moonlight-Log „Gamepad 0 (player 0) is: PS4 Controller“).
- Unit-Test `keyboard_pad_test`: Tasten, Sticks, Moonlight-Kürzel, Maus, Radtipps, sieben Profile, Anlauf, Fahrzeugebene,
  Menütasten in jedem Tastaturlayout, Create und PS, Empfindlichkeit je Gerät, Einstellungen.

Nicht geprüft: die Profile `driving`, `fighting`, `platformer` und `sports` in einem Spiel, Moonlight für Windows, macOS,
Android und iOS, Mac-, japanische und koreanische Tastaturen (nur im Unit-Test), der absolute Mausmodus, die Kürzel
Strg+Alt+Shift während des Spiels, Firmware 13.00.

### Tastatur-Pad nach dem Review von PR 20 (2026-11-05, 10.20, PS5 Pro)

Host aus `keyboard-mouse-pad` (0.3.2 mit den Patches 0017 und 0018 und den Korrekturen aus dem Review), vorübergehend per
ELF-Loader gestartet. Moonlight für Linux auf einem PC ohne Controller, dazu Hostlog und Kernel-Log der Konsole.

- **Maus und Rad allein öffnen kein Pad:** Im zweiten Stream (gut 1,5 Minuten) bewegte der Besitzer nur die Maus und drehte
  das Rad, dazu ein Klick, um das Fenster nach vorn zu holen. Kein „play as virtual pad“, im Kernel-Log kein `DEVICE_ADDED`.
  Im ersten Stream hatte ein Klick ins Bild das Pad geöffnet (die linke Maustaste ist in `standard` R2), wie vorgesehen.
- **Die erste Taste öffnet es:** im dritten Stream 3 s nach dem Verbinden „PS5 virtual pad 0 attached as the host's
  controller“ (die Rolle aus 0.3.2). Die Konsole wies es dem Benutzer zu, ohne „Wer benutzt diesen Controller?“.
- **Spiel und Menüs:** „menu keys, no game running“ auf dem Home-Bildschirm, `gta` für GTA III aus der eigenen Liste. Dort
  laut Besitzer Laufen, Kamera, Zielen und Schießen mit der Maus und der Waffen-Cheat (3 3 Q 3 und die Pfeiltasten)
  einwandfrei.
- **Streamende:** „pad 0 detached (0), the host role is free“; im nächsten Stream öffnete die erste Taste das Pad wieder.
- **Mausempfindlichkeit:** Strg+Alt mit dem Rad, rund 60 Schritte im Log, bei schon offenem Pad. `keyboard_sensitivity.txt`
  hielt danach den letzten Wert (750 % für `standard`), eine `.new`-Datei blieb nicht übrig.

- **Nach dem zweiten Review (das Pad entsteht beim Druck, bevor es ihn sieht):** gut 40 s Maus, Rad, Shift und M einzeln
  und Strg+Alt mit dem Rad (drei Schritte im Log; in `standard` ist Strg kein Knopf): kein Pad, kein `DEVICE_ADDED`. W
  öffnete es als Pad des Hosts, dem Benutzer zugewiesen; am Streamende „the host role is free“.

Nicht geprüft (nur im Unit-Test oder im Code): kein Pad frei (vier belegt) und der neue Versuch nach 10 s, Strg zuerst in
`shooter` oder `sports`, eine Taste, die nach dem Abziehen eines Controllers wiederholt wird.

### Host-Rolle am Gerät (2026-11-05, 13.00, Testclient, Home-Bildschirm, nicht veröffentlicht)

Host `commit=roles-1` (der Stand dieses Commits ohne die Doku), drei gepairte Identitäten des Linux-Testclients (A, B, C),
alle H.264 1080p60 auf „PS5 Screen“ mit `--no-script`; B, C und A beim zweiten Mal treten per `/resume` bei. Ablauf und Log:

- A allein: Pad 0 „attached as the host's controller“.
- B tritt bei: Pad 1 „the console asks who uses it“. Drei PS-Drücke von B (`--tick-kind ps`): dreimal „PS or Create
  ignored, only the host has them“.
- A geht (sein `/cancel` beendet nur seine Sitzung): „pad 0 detached (0), the host's device keeps the role“.
- C tritt bei und bekommt den freien Slot 0: „the console asks who uses it; the host is away and keeps the role“. Zwei
  Select-Drücke von C (`--tick-kind back`, Create): beide ignoriert.
- C geht, A tritt wieder bei: Pad 0 „attached as the host's controller“.
- A geht, dann B; danach sind alle Pads abgemeldet (`STREAMS` leer). Alle vier Streams fehlerfrei dekodiert (1795, 4479,
  883 und 704 Bilder).
- Ein Lauf davor ohne Beitritt (B, C und A rief `/launch` statt `/resume`): Jedes Gerät war allein, wurde Host, und nach
  seinem Abmelden war die Rolle frei („the host role is free“).

Nicht geprüft: echtes Moonlight, ein echter DualSense an der Konsole, und ob der Dialog „Wer benutzt diesen Controller?“
einem Gast den Nutzer des Hosts anbietet.

### Release 0.4.0 auf der Konsole (2026-11-06, 13.00)

`main` mit PR 27 (Webseite) und 0.3.4 zusammengeführt, App aus `build/app` kopiert und neu registriert, sie zeigt
`v01.000.021`; nach „Restart host“ meldet das Kernel-Log `version=0.4.0 commit=a2b4231a` und „Web page on port 47990“.
Die Seite liefert `index.html` mit dem eigenen Zertifikat (`CN=ProsperoSunshine web page`, Adressen der Konsole), ohne Login
antworten `/api/overview`, `/api/settings` und `/api/logs` mit 401, ein falsches Login ebenso. Testclient, Home-Bildschirm:
H.264 und HEVC 1080p je 30 s mit 1793 und 1792 Bildern, ohne Lücke, längste Pause 20 ms, fehlerfrei dekodiert, je 5805
Tonpakete ohne Fehler. Die Bedienung der Seite mit Login prüfte ein Mitwirkender auf 10.20 und am iPhone (PR 27).

### Release 0.3.4 auf der Konsole (2026-11-06, 13.00)

App aus `build/app` kopiert und neu registriert, sie zeigt `v01.000.020`; nach „Restart host“ meldet das Kernel-Log
`version=0.3.4 commit=29a15422`. Testclient, Home-Bildschirm: H.264 und HEVC 1080p je 30 s mit 1795 und 1793 Bildern, ohne
Lücke, längste Pause 19 ms, fehlerfrei dekodiert, je 5805 Tonpakete ohne Fehler.

### Gast-Pad erst bei der ersten Eingabe (2026-11-06, 13.00, Testclient, Home-Bildschirm, nicht veröffentlicht)

Issue #26 (13.60): Jedes Gerät meldete zwei Controller, der zweite drückte nie etwas, und die Frage „Wer benutzt diesen
Controller?“ für ihn blieb stehen. Host `commit=171a5874` mit der Änderung (vor dem Commit), per ELF-Loader gestartet:

- `--pads 2`, nur Pad 0 aktiv: „pad 0 attached as the host's controller“, „pad 1 announced, opens when it is used“; Pad 1
  öffnete nie, am Ende „pad 1 gone, never used“. Danach zeigte der Startbildschirm keinen Dialog.
- `--pads 2 --tick-pad 1`: Pad 1 öffnete beim ersten Druck („attached, the console asks who uses it“), am Ende abgemeldet.
- A mit Controller, B tritt ohne Controller bei und drückt W: „pad 1 announced“, 4 ms später „keyboard and mouse … play as
  virtual pad 1“ und „attached, the console asks who uses it“. Alle Streams ohne Lücke, fehlerfrei dekodiert.

Nicht geprüft: echtes Moonlight mit Steam Input oder iOS, und der Fall, in dem das Phantom-Pad als erstes ankommt und damit
das Pad des Hosts ist (dann bliebe der echte Controller ein Gast seines eigenen Geräts).

**Bestätigt vom Melder** (Issue #26, 13.60, Testbuild `v0.3.3-pads-1`, 2026-11-06): Im `sunshine.log` seines Streams „pad 0
attached as the host's controller“, „pad 1 announced, opens when it is used“, am Ende „pad 1 gone, never used“; er meldet,
dass es jetzt geht. Der erste Versuch davor endete mit „Couldn't bind RTSP server to port [48010], Address already in use“:
der vorige Host lief noch.

### Release 0.3.3 auf der Konsole (2026-11-06, 13.00)

App aus `build/app` kopiert und neu registriert, sie zeigt `v01.000.019` und installierte beim Start das gebündelte ELF;
nach „Restart host“ meldet das Kernel-Log `version=0.3.3 commit=461bdc8c`. Der neue Hilfetext zu „PS for all players“ passt
in die Zeile unter den Settings. Testclient, Home-Bildschirm: H.264 und HEVC 1080p je 30 s mit 1793 und 1790 Bildern, ohne
Lücke, längste Pause 19 ms, fehlerfrei dekodiert, je 5800 Tonpakete ohne Fehler; danach Pad 0 abgemeldet, „the host role
is free“.

**Tastatur-Pad auf 13.00** (derselbe Host, per ELF-Loader gestartet, vor dem App-Build): Testclient ohne Controller
(`--pads 0`, neu) mit `--kbm-seq`: Maus rechts und links, ein Radschritt, dann W und S. Maus und Rad öffneten weder ein Pad
noch die Maus der Konsole; W öffnete „PS5 virtual pad 0 attached as the host's controller“ und „The keyboard and mouse of a
client without a controller play as virtual pad 0“, am Streamende abgemeldet. Mit Controller (ohne `--pads 0`) gingen
dieselben Eingaben wie bisher an die virtuelle Tastatur und Maus der Konsole. Beide Streams ohne Lücke, fehlerfrei
dekodiert. Nicht geprüft auf 13.00: ein Spiel, die Profile, ein Gast mit Tastatur.

### Raumklang folgt einem breiteren Stream (PR 16, 2026-11-06, 13.00, Testclient, Home-Bildschirm)

Host `commit=c15327c3` (der PR-Stand), Schalter „Surround“ an, Konsole mit Stereo-Ausgabe. A streamt Stereo (40 s), B tritt
nach 10 s per `/resume` mit 5.1 bei (`--surround 51`, 20 s), beide H.264 1080p60 mit `--no-script`.

- Hostlog: A öffnet Typ 0 („2 channels“). Mit B: „sharing the channel another stream reads“, „6 channels“, dann beim Leser
  von A „12 channels from the console for 2 speakers“, also der Wechsel auf Typ 3. Keine Warnung, kein verworfenes Paket.
- A: 2391 Bilder ohne Lücke, fehlerfrei dekodiert, 7796 Tonpakete ohne Dekodierfehler, RMS je Kanal 0,0073 / 0,0062, auch
  über den Wechsel hinweg.
- B: 1183 Bilder ohne Lücke, fehlerfrei dekodiert, 6 Kanäle, RMS 0,0059 / 0,0054 / 0 / 0 / 0 / 0 (die Konsole gibt nur
  Stereo aus).

Nicht geprüft: eine Konsole mit 5.1-Ausgabe und ein abgelehnter Typ 3 (dann bleibt es bei Stereo bis zur nächsten Sitzung).

### Webseite (2026-11-05, 13.00, Branch `webui`, nicht veröffentlicht)

Host und App aus dem Arbeitsstand des Branches, über die App installiert. Den Login hat für den Test `WEBLOGIN SET` vom PC
gesetzt (dafür lag kurz `control_lan` im Datenordner, danach gelöscht und der Host neu gestartet; der Steuer-Socket war
danach vom PC aus wieder geschlossen). Gemessen mit curl vom PC im LAN:

- Ohne Login war 47990 zu; nach `WEBLOGIN SET` lieferte die Seite 200, die API ohne Cookie 401.
- Anmelden: ohne den Header `X-Prospero` 403, mit ihm 200 und ein Cookie `Secure; HttpOnly; SameSite=Strict`, die
  Content-Security-Policy im Kopf. Fünf falsche Passwörter je 401, der sechste Versuch 429 mit 30 s Wartezeit. Im
  `sunshine.log` nur „login from“ mit der Adresse, kein Passwort; ein Login dauerte rund 0,1 s.
- Übersicht, Geräte, Einstellungen (ohne die Experimental-Schalter) und Logs lieferten die Daten der Konsole.
- „Restart host“ über die API: nach etwa 4 s war der Host wieder da, der Login blieb, die alte Sitzung bekam 401.
- `RELISTEN`: „Starting the services anew“, danach „Web page on port 47990“ und ein neuer Login ging.
- Der Diagnosebericht über die Seite kam in 0,03 s, ohne Adressen und Gerätenamen.
- In der App: der Eintrag „Web page“ zeigte die Adresse und „On for tester“; „Turn off“ schloss 47990 sofort. Später „Set login“ über die
  Bildschirmtastatur (Name, dann Passwort als Punkte mit Zeichenzahl): „The web page is on“, und ein Login vom PC mit diesen
  Daten bekam 200.
- Die Oberfläche selbst lief gegen einen Nachbau der API im Browser am PC, auch in Handybreite; den echten Host konnte der
  Browser des Tests wegen des selbst signierten Zertifikats nicht öffnen.

Der Diagnosebericht, die Sperre über alle Adressen (20 Fehlversuche) und das Lesen nur des Log-Endes kamen mit einem zweiten
aufgespielten Stand; auf ihm liefen Bericht, Logs, Übersicht, Login und „Restart host“ erneut, die Sperre über alle Adressen
nur im Unit-Test, das Log-Ende nur mit einem kurzen Log.

Danach hat der Besitzer die Seite im Browser seines Handys gegen die Konsole geöffnet, sich angemeldet und sie angesehen:
ohne Befund.

Nicht geprüft: ein Browser am PC gegen die Konsole, PIN, „End
stream“, Umbenennen und Entfernen über die Seite, eine Adresse außerhalb des LAN, echter Ruhemodus.

### Webseite nach der Testrunde auf 10.20 (2026-11-05, 13.00, Testbuild `v0.3.2-webui-2`)

Host `bc38f990` aus dem Testbuild, App-Ordner und ELF per FTP aufgespielt, der alte Host beendet und das ELF gesendet.

- Zertifikat der Seite, mit `openssl s_client` vom PC gelesen: CN „ProsperoSunshine web page“, `CA:FALSE`, Key Usage
  Digital Signature, EKU TLS Web Server Authentication, SAN `localhost`, die Adresse der Konsole und 127.0.0.1, gültig
  825 Tage. `web_cert.pem` und `web_key.pem` lagen danach im Datenordner.
- `sunshine_state.json` gesichert und durch 15.283 Null-Bytes ersetzt, Host neu gestartet: „Moved the unreadable ...
  to sunshine_state.json.corrupt“, `/serverinfo` mit einer neuen uniqueid, die neue Datei 112 Bytes. Ein frischer
  Testclient (`pair.py`) paarte sich (PairStatus=1), die Datei wuchs auf 1.378 Bytes. Danach die Sicherung zurückgespielt;
  nach dem nächsten Start war die alte uniqueid wieder da.
- Ablehnen einer Anfrage (dafür lag kurz `control_lan` im Datenordner, danach umbenannt): `PAIRINGS` zeigte die Anfrage,
  `CANCELPIN <id>` „OK cancelled“, ein zweites „ERR no such request“, ohne ID „ERR usage“. Der wartende Client bekam
  HTTP 400 „Pairing request cancelled by operator“; derselbe Client paarte sich gleich danach ohne Wartezeit.
- „Restart host“ hat der Besitzer am Handy auf der Seite gedrückt: im Kernel-Log „restart asked for“, „host stopped“,
  dann vom Helfer „relaunch after the old host was gone (1250 ms): sent the host to the ELF loader“ und der neue Host bis
  „Web page on port 47990“. Danach lief nur noch ein Host-Prozess, der Steuer-Socket war wieder nur lokal.

Nicht geprüft: der Neustart auf 10.20, Safari auf dem iPhone mit dem neuen Zertifikat, „Turn on“/„Forget login“ und
„Deny“ in der App, R2/R3 in der Tastatur der App, „End stream“ auf dem Geräte-Tab, das gedrosselte Zertifikats-Log.

### Testbuild für 13.40 (2026-11-05, 13.00)

Pre-Release `v0.3.1-fw1340-1` für Issue #14: 13.40 und alles bis 13.60 bekommt den Datensatz von 13.60. Gemessen nur auf
13.00, wo sich nichts ändert (die 288 Datensätze in `capture_records_test` gleich): Testclient, Mario-64-Demo, je 30 s H.264
mit 1795 Bildern bei 59,94 fps und HEVC mit 1794 Bildern bei 59,95 fps, fehlerfrei dekodiert, Ton ohne Fehler. Auf 13.40
ungetestet, der Bericht des Testers steht aus.

### Berichte eines Mitwirkenden (2026-11-04, Discord, PS5 Pro mit 10.20)

- **HDR-Farben unter Windows:** Moonlight unter Windows mit HDR-Bildschirm und Hardware-Decoding, HEVC 10 Bit HDR 1920x1080,
  GTA III (Titelmenü). Laut Bericht und Foto vom Bildschirm stimmen die Farben jetzt, mit dem Host ab 0.2.4 (HDR10-Metadaten
  statt Nullen). Moonlights Statistik im Foto: rund 58 fps, keine verlorenen Bilder. Windows-Screenshots desselben Streams
  wirken überstrahlt; das liegt an der Aufnahme von HDR-Inhalten, nicht am Stream. Hier nicht gemessen.
- **Tailscale:** Mit [ps5-tailscale](https://github.com/holdmysocks/ps5-tailscale) ab 0.7.3 (sein Beitrag, UDP 47998–48000 in
  den Standardports) erreichte Moonlight auf iOS den Host über die Tailnet-Adresse der Konsole. Hier nicht geprüft.

### Berichte nach 0.3.1 (2026-11-05)

Hier nicht nachgemessen; die Angaben stammen aus den Berichten.

- **Slices bei H.264 auf 10.20 (Mitwirkender, Discord, PS5 Pro):** Host 0.3.1, Moonlight unter Windows mit H.264 1920x1080@60
  und Hardware-Decoding (D3D11VA). `host_diag.log`: `firmware 10.x: no rate fields, QP 24/28`, `OpenVideo rc=0`,
  `Start rc=0`, `first frame has 34 slices, more than hardware decoders on Windows take`, `capture open ok`. Die Felder
  `cfg+0x40/0x44/0x48` werden also angenommen und wirken nicht; der Rückfall greift nicht, weil nichts abgelehnt wird.
  Moonlights Log zeigt wiederholt `avcodec_send_packet() failed: Invalid data found when processing input` und „Resetting
  decoder due to consistent failure“, auf dem Bildschirm kein Bild.
- **Tailscale mit einer Konsole in einem anderen Haushalt (derselbe Mitwirkende):** aktuelles ps5-tailscale und Host 0.3.1,
  Moonlight streamte über das Tailnet.
- **Firmware 13.40 (GitHub-Issue 14, PS5 Slim Digital):** Testbuild `0.3.1-fw1340-1` (13.40 bekommt den Datensatz von
  13.60): `capture OpenVideo rc=0`, `Start rc=0`, `capture open ok`, Ton über Avcap2; Moonlight auf Android zeigte H.264.
  Eingabe und lange Sitzungen sind dort noch nicht berichtet.

### Raumklang auf 13.60 (2026-11-04, Bericht in PR 16 von BuffMcBigHuge)

Ein Mitwirkender baute parallel zu 0.3.1 denselben Weg (Typ 3 bei 5.1/7.1) und maß auf 13.60 mit echten 5.1-Quellen. Hier
nicht nachgemessen; die Angaben stammen aus seinem PR.

- Konsole auf HDMI, Gerätetyp AV-Verstärker, 5.1, Linear PCM. Quelle: die App „Surround Sound Studio“ (6-Kanal-PCM, ihr
  Audioport im Modus 6) und 007 First Light im Spiel. In Typ 3 trugen nur Kanal 0 und 1 Ton, 2–11 waren exakt 0.
- Mit erzwungenem 5.1-LPCM auf dem HDMI-Kabel (AV-Control: `port:HDMI ... 48k 5.1 ... fmt:LPCM`) blieb die Aufnahme gleich:
  Center und Surrounds lagen mit etwa −3 dB auf links/rechts, LFE war still.
- Sein Fernseher meldet im EDID nur 2-Kanal-LPCM. Seine Deutung: Die Systemmischung der Konsole folgt dem EDID und bleibt bei
  2.0, das Kabelformat ändert sie nicht. Offen ist der Test mit einem Gerät, das 6 oder 8 Kanäle LPCM meldet (AV-Receiver
  oder EDID-Emulator).
- `cfg+0x10 = 2` (AudioSecond) ist ebenfalls stereo; `cfg+0x10 = 1` (AudioAout) öffnet mit `rc=0`, liefert aber keine Pakete.
- Für 7.1 nimmt er Sunshines Reihenfolge an (hinten vor seitlich), der Host seit 0.3.1 die von `sceAudioOut` (seitlich vor
  hinten). Für 5.1 ergeben beide dasselbe. Gemessen ist keine.

### Belegter Steuerport (2026-11-05, 13.00, GitHub-Issue 24)

Auf 11.40 stürzte der Host beim Stream-Start ab: `SIGSEGV` im Thread `rtsp::handler`, Lesezugriff auf 0. Die Rücksprünge
aus dem Kernel-Log des Nutzers, auf das ELF von 0.3.2 abgebildet: `cmd_announce` → `session::start` → `start_broadcast` →
`control_server_t::bind` → `net::host_create`. Dort liest Sunshine `host->socket`, auch wenn `enet_host_create` mit
`nullptr` gescheitert ist (UDP 47999 nicht zu binden). Patch 0019 fängt das ab und loggt Port und errno.

Nachgestellt mit `out/port_hold_probe.elf` (bindet UDP 47999 für 60 s) und dem Testbuild `v0.3.2-fw1140-1` (der Patch hieß
dort 0017): Der Testclient bekam auf `ANNOUNCE` HTTP 500, `sunshine.log` meldete `ENet couldn't open UDP port 47999: Address
already in use (errno 48)` und `Couldn't bind Control server to port [47999]`, der Host lief weiter
(`SUNSHINE_SERVER_FREE`). Nach Freigabe des Ports, Mario-64-Demo: H.264 und HEVC 1080p je 30 s mit 1796 Bildern, ohne
Lücke, längste Pause 21 ms, fehlerfrei dekodiert. Laut Nutzer hielt ps5-tailscale den Port: Er hatte die Konsole selbst
unter „Game streaming“ eingetragen; ohne den Eintrag streamt er mit Tailscale (Bericht, hier nicht gemessen).

Auf `main` übernommen als Patch 0019 (2026-11-06, 13.00, Host mit allem bis PR 16): Mit der Probe auf 47999 scheiterte der
Stream mit RTSP 500, `sunshine.log` wie oben, der Host blieb bei `SUNSHINE_SERVER_FREE`. Nach Freigabe des Ports,
Home-Bildschirm: H.264 und HEVC 1080p je 20 s mit 1193 Bildern, ohne Lücke, fehlerfrei dekodiert, Ton ohne Fehler.

### Release 0.3.2 auf der Konsole (2026-11-05, 13.00)

App aus `build/app` kopiert und neu registriert, sie zeigt `v01.000.018`; nach „Restart host“ meldet das Kernel-Log
`version=0.3.2 commit=86c5eb2d` und `only the host can press PS and Create`. Testclient, Mario-64-Demo: H.264 und HEVC 1080p
je 30 s mit 1795 und 1796 Bildern, ohne Lücke, längste Pause 23 und 21 ms, fehlerfrei dekodiert, keine Tonfehler. Die
Host-Rolle selbst ist im Abschnitt zu Issue 15 gemessen, nicht noch einmal auf diesem Build.

### Release 0.3.1 auf der Konsole (2026-11-04, 13.00)

App aus `build/app` kopiert und neu registriert, sie zeigt `v01.000.017`; nach „Restart host“ meldet `host_diag.log`
`version=0.3.1 commit=8fa42cd5`. Testclient, Home-Bildschirm, Surround aus: H.264 und HEVC 1080p je 20 s mit 1194 und 1193
Bildern, ohne Lücke, längste Pause 28 und 21 ms, fehlerfrei dekodiert, je 3812 Tonpakete.

### Für 0.3.1 (2026-11-04, 13.00, Testclient, Home-Bildschirm)

**Welcher der 12 Kanäle welcher Lautsprecher ist, ließ sich hier nicht messen.**

- `channel_map_probe` spielte aus einer Payload über `sceAudioOut` (Port MAIN, Float, 8 Kanäle in beiden Reihenfolgen, dazu
  Stereo) nacheinander einen Ton je Kanal und las dabei Typ 3. `sceAudioOutOutput` gab 0x100 zurück, in der Aufnahme kam der
  Ton aber nicht an, auch nicht in Stereo: Kanal 0 und 1 blieben beim Pegel des Home-Bildschirms (RMS um 0,005), 2–11 bei 0.
  Ton einer Payload gehört also nicht zur Aufnahme. Die Porttypen BGM, VOICE und AUX lehnte `sceAudioOutOpen` mit
  0x80260011 ab.
- `sceAudioOutGetPortState` meldet `channels=2`: Die Konsole gibt hier Stereo aus.
- Teardown, Hauptmenü mit Musik (`surround_probe`): Typ 0 RMS 0,083/0,083, Typ 3 nur Kanal 0 und 1 (0,078/0,070), 2–11 bei 0.
- Solange die Konsole Stereo ausgibt, tragen nur Kanal 0 und 1 Ton. Die Toneinstellung der Konsole wurde nicht geändert. Der
  Host nimmt deshalb für die ersten acht Kanäle die 8-Kanal-Reihenfolge von `sceAudioOut` an (L, R, C, LFE, Surround L/R,
  hinten L/R) und lässt 8–11 weg (`src/audio/channel_map.hpp`). Belegt ist das nicht.

**Raumklang im Host**, Schalter „Surround“ (`surround_sound = 1`) in der App:

- 5.1, H.264 1080p, 15 s: Moonlight bekam 6 Kanäle (`streams=4 coupled=2`), RMS je Kanal 0,0095 / 0,0076 / 0 / 0 / 0 / 0, 895
  Bilder ohne Lücke, fehlerfrei dekodiert. Hostlog: `12 channels from the console for 6 speakers`.
- 7.1, HEVC 1080p, 15 s: 8 Kanäle, nur die vorderen zwei mit Ton, ohne Lücke, fehlerfrei dekodiert.
- Stereo, H.264, 15 s: wie bisher, Typ 0.
- Zwei Geräte: A mit 5.1 (25 s), B mit Stereo tritt nach 5 s bei (15 s). Beide ohne Lücke und mit Ton (A 4810, B 2814
  Pakete); B bekommt den Mix aus denselben 12 Kanälen (`sharing the channel another stream reads`).
- Schalter aus, 5.1 angefragt: 6 Kanäle, nur vorne links und rechts mit Ton, Typ 0 wie vor 0.3.1.
- Die Experimentell-Reihe der App mit sechs Kacheln auf der Konsole angesehen; „Surround“ ein- und wieder ausgeschaltet und
  gespeichert, `sunshine.conf` danach wie vorher.

**Slices bei H.264 auf 10.x.** Der Host setzt dort für H.264 `cfg+0x40 = 1`, `cfg+0x44 = 4`, `cfg+0x48 = 1`. Scheitert das
Öffnen damit, öffnet er sofort ohne sie und lässt sie bis zum Neustart weg, wenn es dann geht. Das erste Bild jedes Öffnens
mit den Feldern wird gelesen und seine Slices geloggt (`first frame has N slices`). Geprüft nur in den Unit-Tests: Die Records
ändern sich allein für H.264 auf 10.x (48 von 240 Zeilen der Golden-Datei), 13.00 bleibt byte-gleich. Auf 10.20 inzwischen
gemessen: wirkungslos, weiter 34 Slices (oben, „Berichte nach 0.3.1“).

### Release 0.3.0 auf der Konsole (2026-11-04, 13.00)

App aus `build/app` kopiert und neu registriert, sie zeigt `v01.000.016`; nach „Restart host“ meldet `host_diag.log`
`version=0.3.0 commit=4b56ec87`. Testclient, Home-Bildschirm: H.264 und HEVC 1080p je 20 s mit 1196 und 1193 Bildern, ohne
Lücke, längste Pause 19 ms, fehlerfrei dekodiert. Drei Geräte (A H.264 Kanal 0, B HEVC Kanal 1, C HEVC tritt Kanal 1 bei):
A 2995, B 2391, C 1482 Bilder in 50, 40 und 25 s, ohne Lücke (mit 0.2.4 bekam C im selben Fall 1377 Bilder).

### Für 0.3.0 (2026-11-04, 13.00, Testclient, Home-Bildschirm)

**Zwei Controller an einem Gerät.** Der Testclient kann mit `--pads N` mehrere Controller melden, wie Moonlight mit mehreren
Gamepads (Maske `activeGamepadMask` bei jedem Ereignis). Mit `--pads 2`: „virtual pad 1 attached, the console asks who uses it“,
1195 Bilder in 20 s ohne Lücke, am Ende beide Pads gelöst. Ein Mitwirkender spielte auf 10.20 mit zwei Controllern an einem
echten Moonlight-Client (Bericht mit Bildschirmfoto, hier nicht gemessen).

**Übernahme ohne Neuöffnen.** Geht das Gerät, das einen Kanal hält, übernimmt der nächste Stream die offene Sitzung
(`leaving channel 0 open for the stream that takes it over`, `took the open channel 0 over, no reopen`). Längste Pause beim
übernehmenden Gerät: Kanal 0 31 und 48 ms (zwei Läufe), Kanal 1 19 ms (vorher 185 ms). Keine fehlende Bildnummer.

**Erstes Bild beim Beitreten.** Bisher verwarf das beitretende Gerät 123 Bilder („Waiting for IDR frame“), bis es nach 2,1 s
selbst ein IDR anforderte: Das Schlüsselbild, das der Host beim Beitreten anfordert, geht vor dem ersten Video-Ping des Clients
raus, wenn Sunshine dessen Adresse noch nicht kennt. Mit einem weiteren Schlüsselbild nach 0,5 s kam das erste Bild nach
0,61–0,66 s, mit zwei nach 0,25 und 1 s nach 0,39–0,40 s (je drei Läufe, H.264 1080p), ohne eigene Anforderung des Clients.
Das haltende Gerät blieb ohne Lücke.

**Was beim Gerät ankommt (`STREAMS`).** Zwei Geräte (H.264 auf Kanal 0, HEVC auf Kanal 1), zweimal im Abstand von 5 s
abgefragt: `sent_kbps` 1498–2223 bei 4148 kbit/s Vorgabe (Home-Bildschirm, wenig Bewegung), `rtt_ms=1`, `recoveries=2` je
Gerät (der Testclient fordert zu Beginn und in der Mitte ein IDR an), `pads=2` beim Gerät mit zwei Controllern.

**PS nur für Spieler 1.** Mit der Voreinstellung (`ps_button_all_players` nicht gesetzt) meldet der Host „only player 1 can
press PS“. Zwei PS-Drücke von Pad 1: „PS ignored, only player 1 has it“, der Home-Bildschirm blieb unverändert. PS von Pad 0
öffnete wie bisher das Kontrollzentrum. Die Einstellungsseite der App mit der neuen Kachel „PS for all players“ (zwei Reihen
zu vier) auf der Konsole angesehen, Navigation per Steuerkreuz bis zur neuen Kachel geprüft.

**Raumklang (`surround_probe`).** `OpenAudio` mit Encoder-Typ 3 (Purpose 2) geht auf 13.00: 47 Pakete/s zu 49152 Byte, also
1024 Abtastwerte × 12 Kanäle × 32-Bit-Float (Typ 0: 8192 Byte, 2 Kanäle). Auf dem Home-Bildschirm tragen nur Kanal 0 und 1 Ton,
mit fast denselben Pegeln wie Typ 0 (RMS 0,0055/0,0052 gegen 0,0053/0,0051); Kanal 2–11 sind still. Welcher Kanal welcher
Lautsprecher ist, braucht eine Quelle mit echtem Raumklang; der Host liefert weiter Stereo.

### Release 0.2.4 auf der Konsole (2026-11-04, 13.00)

Die App aus `build/app` auf die Konsole kopiert und neu registriert; sie zeigt `v01.000.015`. Nach „Restart host“ meldet
`host_diag.log` `version=0.2.4 commit=f6d55610`. Testclient auf dem Home-Bildschirm:

- H.264 1080p, 20 s: 1194 Bilder, keine fehlende Bildnummer, längste Pause 19 ms, fehlerfrei dekodiert.
- HEVC 1080p, 20 s: 1193 Bilder, keine fehlende Bildnummer, längste Pause 19 ms, fehlerfrei dekodiert.
- Zwei Geräte, A mit H.264 (30 s), B mit HEVC (25 s, 10 s später): B bekam den zweiten Kanal auf Profil 175 (`sunshine.log`),
  A 1795 und B 1492 Bilder, keine fehlende Bildnummer, längste Pause 34 bzw. 23 ms, beide dekodiert und mit Ton.

### Zweiter Aufnahmekanal im Host (2026-11-04, 13.00, Release 0.2.4)

Will ein weiteres Gerät einen anderen Codec, einen anderen Dynamikbereich oder eine andere Auflösung bis 1080p, öffnet
der Host für dieses Gerät einen zweiten Kanal auf Profil 175. Bei gleichem Modus hängt es sich wie bisher an den laufenden
Stream an.

Gemessen mit Testclient A und B, dem Mario-64-Demo und `STREAMS` (Steuerkanal für den Test im LAN offen). B kam jeweils
10 s nach A dazu:

| Fall | A | B | Ergebnis |
| --- | --- | --- | --- |
| gleicher Modus | H.264 1080p, Kanal 0 | `role=joined channel=0` | A 1795, B 1499 Bilder |
| H.264 und HEVC | H.264 1080p, Kanal 0 | HEVC 1080p, Kanal 1 | B dekodiert als HEVC Main |
| 1080p und 720p | H.264 1080p, Kanal 0 | H.264 1280x720, Kanal 1 | B dekodiert als 1280x720 |
| SDR und HDR | HEVC 1080p SDR, Kanal 0 | HEVC 10 Bit mit `--hdr`, Kanal 1 | `/resume` nahm B an, B ist Main 10 |
| A geht zuerst | H.264, 25 s, Kanal 0 | HEVC, 40 s, Kanal 1 | B hatte danach allein weiter Bild und Ton |

In allen Fällen gab es keine fehlende Bildnummer. Die längste Pause lag bei 37 ms, beide Ströme dekodierten mit ffmpeg
fehlerfrei, und beide Geräte bekamen Ton (A ging zuerst: B 7789 Audiopakete in 40 s).

Im `host_diag.log` öffnete, lief und schloss `capture2` viermal ohne Fehler. `Initialize` machte der andere Kanal, und
`Terminate` blieb dem Kanal überlassen, der zuletzt schloss.

**Nachgemessen am 2026-11-04 mit 0.2.4 (`f6d55610`), drei Testclients, Home-Bildschirm.** A öffnete H.264 1080p auf Kanal 0,
B kam 10 s später mit HEVC 1080p auf Kanal 1, C weitere 10 s später. Die Zuordnung steht in `sunshine.log`:

| Fall | C oder B | Ergebnis |
| --- | --- | --- |
| drittes Gerät, H.264 1080p | C | hängt sich an Kanal 0 (`joined the running stream on channel 0`) |
| drittes Gerät, HEVC 1080p | C | hängt sich an Kanal 1 |
| drittes Gerät, H.264 1280x720 | C | hängt sich an Kanal 0 und bekommt dessen 1080p (`the running stream delivers 1920x1080`) |
| drittes Gerät, HEVC mit HDR | C | bei `/resume` abgewiesen: `Resume refused: both capture channels run without HDR` |
| Übernahme in Kanal 1 | B geht nach 25 s, C bleibt | C übernimmt Kanal 1 im selben Modus; das Neuöffnen kostet einmal 185 ms Pause, kein fehlendes Bild |
| 4K für Kanal 1 | B verlangt 3840x2160 | `the second channel delivers up to 1920x1080`, B dekodiert 1920x1080 |
| Kanal 1 allein | A geht nach 25 s, B läuft 320 s | B: 19176 Bilder, längste Pause 25 ms, Ton durchgehend 200 Pakete/s (63798), keine Dekodierfehler |

In allen Fällen fehlte keine Bildnummer, alle Ströme dekodierten mit ffmpeg fehlerfrei, und jedes Gerät bekam Ton.

Nicht geprüft:

- ob eigene Bitraten bei gleichem Modus etwas ändern: wohl nicht, weil das System dann einen Encoder teilt;
- das echte Moonlight;
- andere Firmware als 13.00.

### Laufende Streams in der App, `STREAMS` und `ENDSTREAM` (2026-11-04, 13.00, Release 0.2.4)

Testclient A und B (zwei gekoppelte Geräte) mit dem Mario-64-Demo, Steuerkanal für den Test per `control_lan` im LAN
geöffnet, danach wieder entfernt:

- **Zwei Geräte:** `STREAMS` lieferte für beide H.264 1920x1080@60 mit 4148 kbit/s und einem Pad, A mit `role=capture`
  und B mit `role=joined`. Die Sekunden zählten mit. `DEVICES` markierte beide als streamend.
- **`ENDSTREAM` für B:** Antwort `OK ended`, Pad 1 wurde gelöst. A lief weiter: 2693 Bilder in 45 s, ohne fehlende
  Bildnummer, längste Pause 22 ms.
- **Fehlerantworten:** Für B danach `ERR not streaming`, für eine unbekannte UUID `ERR no such device`, ohne UUID
  `ERR usage: ENDSTREAM <uuid>`.
- **`ENDSTREAM` für den Halter der Aufnahme:** Mit A und B lief B als `role=capture` weiter
  („this one takes it over“), ohne fehlende Bildnummer, längste Pause 22 ms.
- **`ENDSTREAM` für den letzten Stream:** Danach war `STREAMS` leer, das Mario-Demo lief weiter, und der Host war frei.
  Alle Pads wurden gelöst.

Nicht geprüft: die Karte „Streaming now“ in der App, solange ein Stream läuft. Ohne Stream zeigt die App die Startseite wie
bisher. Während eines Streams entstehen keine Screenshots, weil sie die Aufnahme belegen.

### HDR-Metadaten, Slices, 120 fps, zweiter Kanal (2026-11-04, 13.00, Release 0.2.4)

**HDR-Metadaten.** Der Host schickte bei HDR im Steuerkanal nur Nullen (`hdr_info_raw_t(true)` ohne Werte). moonlight-common-c
meldet sie trotzdem als gültig (`LiGetHdrMetadata`), und Moonlight für Windows reicht sie an die Swapchain weiter. Ein
Tester sah dort mit Hardware-Decoding übersättigte Farben (GitHub-Issue 2). Sunshine unter Windows schickt immer die
Primärfarben von BT.2020 und D65. Der Host schickt jetzt die üblichen HDR10-Werte (`src/video/hdr10_metadata.hpp`).

- Gemessen: Testclient mit `--hevc10` und dem Mario-64-Demo. Er bekam `primaries 35400,14600 8500,39850 6550,2300 white
  15635,16450 lum 1000/50 cll 1000 fall 400 fullframe 400`.
- Der Stream bleibt Main 10 in `yuv420p10le`, BT.2020 mit PQ und begrenztem Bereich, ohne fehlende Bildnummer.
- Der erste Versuch dieses Laufs scheiterte direkt nach dem Neustart des Hosts am RTSP-`OPTIONS`; der zweite lief.
- Am Windows-HDR-Bildschirm laut Bericht des Testers inzwischen richtig (oben, „Berichte eines Mitwirkenden“).

**Slices bei H.264.** Auf 10.x kommt H.264 mit einer Slice je Makroblockzeile, also 34 bei 1080p. Hardware-Decoder unter
Windows nehmen höchstens 32. Remote Play setzt für H.264 `cfg+0x40 = 1`, `cfg+0x44 = Anzahl` und `cfg+0x48 = 1`
(`SceRemotePlay` 0x6f9df..0x6fa32); der Host lässt diese Felder auf 0.

- `out/slice_rate_probe.elf` auf 13.00 mit dem Mario-64-Demo: H.264 1080p brachte in jeder Variante 1 Slice je Bild.
  Getestet: ohne Felder, `0x44` = 0, 1, 4, 16 und 32, jeweils mit `0x40 = 0x48 = 1`, `0x44 = 16` allein, und 720p mit 16.
  Start lief jedes Mal mit 0.
- Auf 13.00 klären die Felder also nichts, die Konsole teilt dort ohnehin nicht. Ob sie auf 10.x die Slice-Zahl setzen, ist
  offen; dafür muss die Probe auf 10.x laufen.

**120 fps.** Der Host macht aus einer Anfrage über 60 fps schon immer 60 (`video_ps5.cpp`, `low_rate`).

- Mit dem Mario-64-Demo (Bildausgabe `0x144`, 120 Hz) lehnt `OpenVideo` `cfg+0x1c = 2` mit `0x80c70004` ab. Das gilt für
  H.264 und HEVC in 1080p und für H.264 in 720p; 1440p scheiterte schon an der Zuteilung.
- `cfg+0x1c = 3` gibt `0x81950001`, also ungültig.
- Über das Timing-Feld gibt es demnach kein 120 fps.

**Zweiter Aufnahmekanal.** Probe `out/dual_open_probe.elf`, Host gestoppt:

- Hält ein Kanal das Profil 180, verweigert der ResourceArbitrator Profil 185 sofort (`refused`), mit und ohne Spiel. Profil
  182 (4K) verweigert er, solange das Mario-Demo läuft.
- Profil 175 bekommt er neben 180, auf dem Startbildschirm wie mit dem Mario-Demo. Beide Kanäle liefern gleichzeitig
  60 fps, ohne Lesefehler, die längste Pause bei 22 ms. Mit dem Spiel gemessen:
  - HEVC 1080p neben H.264 1080p: 481 und 480 Bilder in 8 s.
  - H.264 1080p neben H.264 720p: 482 und 480.
  - HEVC 1080p neben HEVC 720p: 481 und 479.
  Jeder Kanal hat dabei seinen eigenen Strom, mit eigenen Größen und Parametersätzen.
- Gleicher Codec und gleiche Auflösung mit 20 und 5 Mbit/s: Beide Kanäle zeigten Sekunde für Sekunde dieselben Bildzahlen,
  Bitraten und größten Bilder. Vermutlich teilt das System dann einen Encoder, und die Rate des zweiten gilt nicht. Das ist
  nicht nachgewiesen; dafür müssten die beiden Ströme verglichen werden.
- Alle Varianten schlossen und meldeten sich sauber ab. Der Host streamte danach normal.

**H.264-Bitrate zur Laufzeit.** Probe `out/bitrate_steps_probe.elf`, H.264 1080p60, geöffnet mit 30 Mbit/s, dann per
`SetVideoBitrate` 5, 30 und 5 Mbit/s für je 5 s:

- Jeder Aufruf gab 0 zurück, ohne Lesefehler und ohne Pause über 20 ms.
- Gemessen: bei 5 Mbit/s 5,4, zurück bei 30 bis 18 Mbit/s (der Rest hängt vom Bild ab), dann wieder 2,6 Mbit/s.
- H.264 kann also bis zum Wert beim Öffnen wieder steigen.

### Langsame Zuteilung der Aufnahme (2026-11-04, 13.00, Release 0.2.3)

Ein Tester auf 13.60 (GitHub-Issue 1, nicht selbst gemessen): Der erste Stream scheiterte, weil `RUNNING` zweimal in Folge
nicht innerhalb von 2 s zugeteilt wurde (Profil 182, aktiv `1,100,190`); der nächste Stream zehn Sekunden später bekam die
Zuteilung sofort. Der Host öffnet die Aufnahme erst nach dem ersten Ping des Clients, und Moonlight wartet 10 s auf das erste
Videopaket (`FIRST_FRAME_TIMEOUT_SEC` in moonlight-common-c). Der Host wartet jetzt bis zu 6 s und schreibt
`RUNNING granted after N ms`, wenn es 500 ms oder länger dauerte.

Geprüft: Testclient, drei H.264-Streams hintereinander, ohne fehlende Bildnummer; die Zuteilung kam jedes Mal ohne diese Zeile,
also in unter 500 ms. Nicht geprüft: eine wirklich langsame Zuteilung, sie ließ sich hier nicht herbeiführen.

### Wecken aus Moonlight (2026-11-03, 13.00, GitHub-Issue 10)

Frage: Läuft ein Payload im Ruhemodus weiter, sodass der Host Moonlights Wake-on-LAN-Paket empfangen und die Konsole wecken
könnte? Gemessen mit `src/rest_probe.cpp` (lauscht auf UDP 9 und 7, schreibt alle 10 s eine Zeile, meldet Sprünge der Uhr).

- **Wach:** Magic Packets vom PC an die Adresse der Konsole, an die Broadcast-Adresse des Netzes und an 255.255.255.255, je auf Port 9 und 7, kamen
  alle an und wurden als Magic Packet für die MAC der Konsole erkannt.
- **Ruhemodus (mit Freigabe des Besitzers, `ps5_rest`, Homescreen, kein Spiel, kein Stream):** Die Probe tickte noch rund
  20 s, dann nichts mehr. Nach 40 und 60 s gingen dieselben Magic Packets raus; die Konsole antwortete auf Ping und blieb im
  Ruhemodus, ELF-Loader, FTP und klogsrv waren zu. Nach `ps5_wake` (11 s) meldete die Probe eine Lücke von 91 s, und keins der
  Pakete kam an, auch nicht nachträglich. Der Host antwortete danach wieder auf `serverinfo`.

Ergebnis: Payloads stehen im Ruhemodus still. Der Host kann die Konsole also nicht auf Moonlights Paket hin wecken, und die
Konsole selbst reagiert nur auf das `WAKEUP` von Remote Play (mit Registrierungsschlüssel, wie `ps5_wake`). Wecken geht nur
von außen, etwa mit einem Remote-Play-Client wie chiaki-ng oder einem Relais, das Moonlights Paket in dieses `WAKEUP`
übersetzt.

### Zweiter Spieler von einem anderen Gerät (2026-11-03, 13.00, Release 0.2.2)

Ein Stream von einem anderen Gerät wird nicht mehr abgelehnt, er hängt sich an den laufenden: Der erste Stream hält die
Aufnahme und gibt jedes Bild an alle Streams weiter, das Mikrofon, das den Audiokanal geöffnet hat, jedes Audiopaket. Der neue
Stream muss denselben Codec und denselben Dynamikumfang verlangen. Ab dem zweiten Pad öffnet der Host das Pad ohne Nutzer
(`PS_PAD_NO_USER`), und die Konsole fragt, wer es benutzt (GitHub-Issue 11).

- **Probe `src/pad_user_probe.cpp`:** Ein Pad für Nutzer 0xFFFFFFFF meldet das System als DualSense ohne Nutzer und öffnet
  die Nutzerauswahl nur für dieses Gerät (Systemlog `SpecificDevice`; Screenshot: „Who's using this controller?“ mit „Add
  User“ und dem angemeldeten Nutzer).
- **Zwei Testclients** mit zwei gekoppelten Identitäten (`CLIENT_DIR=out/moonlight-client2 OUT_TAG=2` für `run_smoke.sh`),
  Mario 64 vorn: A streamte 50 s, B kam nach 14 s per `/resume` dazu und lief 25 s. Hostlog: „another device joined the
  running stream (2 streams)“, „sharing the channel another stream reads“, „virtual pad 1 attached, the console asks who uses
  it“. Beide 1080p H.264 mit 59,94 fps, ohne fehlende Bildnummern und fehlerfrei dekodiert, beide mit Spielton (−16,8 und
  −16,1 dBFS). Im Bild von B stand die Abfrage über dem Spiel.
- **Gast:** Mit dem Pad von B Links, Kreuz („Add User“), dann „Quick Play“ unter „Play as a One-Time Guest“ (die Lizenz hat
  der Besitzer gelesen und freigegeben). Danach zeigte das Bild oben rechts „Guest1“ mit Controller, und
  `sceUserServiceGetLoginUserIdList` lieferte zwei angemeldete Nutzer. In allen drei Läufen hatten beide Streams keine
  Lücken.
- Das Pad eines Geräts, das gegangen ist, bleibt bis zum Ende der Sitzung: Sunshine hält die Pads eines Clients für ein
  Wiederverbinden. Im Lauf ging Pad 1 erst mit dem Ende von A.
- `pair.py` schickt den PIN jetzt nebenher: Der `PIN`-Befehl antwortet erst, wenn das ganze Koppeln fertig ist, und lief
  vorher nach 10 s ins Leere („Invalid uniqueid“).

- **HEVC:** Dasselbe mit `--hevc` für beide (A 45 s, B 25 s): beide 59,94 fps, ohne fehlende Bildnummern, fehlerfrei als
  HEVC dekodiert, beide mit Spielton.
- **Übernahme:** A lief 20 s, B kam nach 10 s dazu und lief 40 s. Als A ging, meldete der Host 7 ms später „the stream holding
  the capture ended, this one takes it over“, die Aufnahme stand 4 ms danach, der Ton kam nach gut 100 ms wieder („channel
  reopened“). B: 2275 Bilder ohne fehlende Bildnummer, längste Pause zwischen zwei Bildern 31 ms, fehlerfrei dekodiert.
- **Beenden mit zwei Spielern:** Vorher beendete ein `/cancel` von B auch den Stream von A (A: „terminated by host“), mit
  `close_game_on_quit` hätte es das Spiel geschlossen. Seit Patch `0012-quit-with-another-player` endet nur der Stream des
  Geräts, das beendet, solange ein anderes Gerät streamt. Gemessen: A 35 s, B nach 10 s für 15 s mit `/cancel` am Ende.
  Hostlog: „Quit from one device while another streams: ending only its stream“, Pad 1 sofort entfernt; A lief die vollen
  35 s ohne fehlende Bildnummer weiter, die App endete erst mit A.

Nicht geprüft: ein Spiel für zwei Spieler (keins installiert), also ob ein Spiel den Gast als zweiten Spieler annimmt; das
echte Moonlight; über das Internet; HDR mit zwei Streams; drei oder vier Spieler; Firmware 10.20.

**Tester, 0.2.2 auf 10.20 (PS5 Pro, GitHub-Issue 11, nicht selbst gemessen):** Overcooked! All You Can Eat mit bis zu vier
Spielern, DualSense an der Konsole und drei Moonlight-Clients (Switch im LAN, iPhone über 4G, Android in einem anderen Haus),
und alle Mischungen aus DualSense und Moonlight bis vier. Jedes Moonlight-Pad kam ohne Nutzer und bekam nach 3–7 s seinen
eigenen. Keine Sendefehler an den Pads, Beenden und Übernahme wie oben. Zwei Mängel: Die Übernahme öffnete die Aufnahme im
Modus des übernehmenden Geräts (1080p mit 35 statt 720p mit 16 Mbit/s für alle), und ein Gerät mit HDR wurde erst nach
dem Start des Streams abgewiesen, Moonlight zeigte nur „error code -1“.

**Behoben (2026-11-04, 13.00, Testclient, Release 0.2.3):**

- **Übernahme behält den Modus:** A mit 1280x720 und 8 Mbit/s angefragt (der Host öffnete mit 5708 kbit/s), B mit 1080p
  und 20 Mbit/s angehängt. Als A ging: „keeping the running mode 1280x720@60, 5708 kbit/s“, B lief weiter in 1280x720,
  ohne fehlende Bildnummer, längste Pause 33 ms.
- **Angebot während eines Streams:** `ServerCodecModeSupport` war 769 ohne Stream, 1 während H.264 lief und 257 während
  HEVC ohne HDR lief; danach wieder 769.
- **HDR beim Start abgewiesen** (Patch `0013-join-with-matching-hdr`): `/resume` mit `hdrMode=1` (neu: `pair.py launch
  --hdr`) während eines Streams ohne HDR von einem anderen Gerät bekam 409 mit „Another device is streaming without HDR.
  Turn HDR off in Moonlight to join.“, während H.264 und während HEVC lief. Der laufende Stream lief ohne Lücke weiter.

Nicht geprüft: wie Moonlight auf dem Handy die Meldung zeigt, und ein HDR-Stream, zu dem ein Gerät ohne HDR kommt.

### Neuer Stream vom selben Gerät löst den alten ab (2026-11-03, 13.00)

Moonlight Web von außerhalb, hinter einem Proxy mit SSL-Inspection (Besitzer): Mehrere Versuche hingen alle gleich. Der Stream
startete auf der PS5, aber kein Controller kam an. Nach rund 17 s startete Moonlight Web neu, und der Host lehnte das ab, weil
der erste Stream die Aufnahme noch hielt („capture busy“). Der erste endete erst 30 bis 50 s später. Seitdem beendet der Host
den alten Stream, wenn dasselbe Gerät (dasselbe Zertifikat) einen neuen startet. Ein anderes Gerät wird weiter abgelehnt.

- **Testclient:** Stream A lief, nach 8 s startete dieselbe Identität per `/resume` Stream B (`NO_CANCEL=1` für A, sonst hätte
  As `/cancel` die App beendet). Der Host beendete A, B hatte die Aufnahme 12 ms nach seinem Start, lief 15 s mit 899 Bildern,
  ohne Lücken und fehlerfrei dekodiert.
- **Moonlight Web, Besitzer:** Erster Versuch hing, 16 s später übernahm der zweite („ending its old one“). In ihm wurden
  Tastatur und Maus angelegt, der Besitzer meldet, dass alles funktionierte.

Nicht geprüft: zwei Geräte, die sich abwechselnd verdrängen wollen (das zweite wird abgelehnt), und ob Moonlight Web hinter
dem Proxy immer beim zweiten Versuch durchkommt.

### Tastatur und Maus (2026-11-03, 13.00, Release 0.2.1)

Moonlight-Tastatur und -Maus gehen auf eine virtuelle Tastatur und Maus der Konsole (`docs/re/keyboard-mouse.md`). Gemessen
mit `src/kbm_probe.cpp` und mit dem Host über `stream_test.exe --kbm-seq`, der Tasten als Windows-Tastencodes schickt wie
Moonlight für PC.

- **Probe:** Anlegen von Tastatur und Maus liefert 0, das System meldet „Keyboard connected“ und „Mouse connected“ und
  ordnet beide dem Benutzer zu. Dreimal Rechts und einmal Links schoben den Fokus im Startmenü von ProsperoEden zwei
  Kacheln weiter auf Mario 64 (Screenshot), beim Entfernen kam „… disconnected“.
- **Erste Taste:** Je eine neue Tastatur, eine Taste nach 0, 50, 150, 300, 600 und 1000 ms: Nur die nach 0 ms ging verloren
  (Hintergrundwechsel im Systemlog). Mit 12 s Pause davor kamen auch 100, 250, 500 und 1000 ms an.
- **Host, Startmenü:** Rechts, Rechts, Links aus dem Stream, je 120 ms gehalten. Mit der ersten Fassung kamen nur zwei an:
  Die erste Taste ging unten und oben 1 ms auseinander raus, weil das Loslassen während der Wartezeit nach dem Anlegen
  eintraf. Seit Tasten mindestens 30 ms gehalten werden, kamen alle drei an (drei Hintergrundwechsel in der erwarteten
  Richtung). Die Tastatur entstand beim ersten Tastendruck und ging mit dem Ende des Streams, ebenso die Maus. Der
  Ein-Pixel-Ruck, den Sunshine bei jedem Streamstart schickt, legt keine Maus mehr an (Hostlog).
- **Host, Browser:** Auf einer Testseite vom PC kamen Tasten erst an, nachdem das Textfeld angeklickt und die
  Bildschirmtastatur offen war. Dann standen `a`, `b`, `c`, ein mit gehaltener linker Shift-Taste getipptes `B` und der als
  Text geschickte String `Hi5` im Feld: `abcBHi5`. Die Seite meldete `keydown` mit `KeyH` und `shift=true`. Den Zeiger des
  Browsers bewegte die Maus nicht.
- **Host, Planet Zoo, Hauptmenü:** Die Maus zeigte einen Zeiger im Spiel. Nach oben gefahren stand er am oberen Rand, eine
  Bewegung um +300, +200 schob ihn um etwa 150, 110 Bildpunkte, eine weitere setzte ihn genau auf „OK“ eines Hinweises, und
  der Linksklick schloss ihn (Bilder aus dem aufgezeichneten Stream). Die allererste Bewegung nach dem Anschließen kam im Spiel
  nicht an. Ein zweiter virtueller Controller neben dem des Streams öffnete „Who's using this controller?“.
- Unit-Test `keyboard_map_test`: Tastencodes, Modifier, ASCII-Zeichen, gehaltene Tasten, Maustasten und Radschritte.

Danach mit Moonlight Web vom Besitzer benutzt: Tastatur und Maus wurden im Stream angelegt (Hostlog).

Nicht geprüft: Rad und horizontales Rad, rechte, mittlere und Seitentasten, Strg, Alt und die Windows-Taste, absolute
Mausposition (Moonlight im Remote-Desktop-Modus), mehrere Clients gleichzeitig und Firmware 10.20.

### Streamen ohne Bildschirm (2026-11-02, 13.00, Testclient)

Das HDMI-Kabel wurde bei laufender Konsole vom Besitzer gezogen, vorher lief 4K an einem Fernseher. Host 0.1.9, HEVC, 4K verlangt, `stream_test` mit `--no-script`:

| Vordergrund | `video out mode` | Geliefert | Bilder | Dauer | ffmpeg |
| --- | --- | --- | --- | --- | --- |
| Mario-64-Demo | `0x144` | 1080p | 894 | 15 s | fehlerfrei |
| Home, Demo im Hintergrund | `0x144` | 1080p | 894 | 15 s | fehlerfrei |
| Home, kein Spiel offen | `0x13c` | 4K | 882 | 15 s | fehlerfrei |
| GTA III, aus dem Stream gestartet | `0x13c` | 4K | 1712 | 30 s | fehlerfrei, Ton −24,9 dBFS |

Kein Lauf wurde vom Host abgebrochen. Ein weiterer Lauf auf dem Home-Bildschirm zeigte im aufgezeichneten 4K-Bild die Oberfläche der Konsole. GTA III kam in den 30 s nur bis zu den Logos und schwarzen Ladebildern, vom eigentlichen Spiel ist also noch nichts geprüft.

Wieder eingesteckt bei laufender Konsole (Testclient, 0.2.0): `video out mode` blieb auf dem Home-Bildschirm `0x13c`, die Streams liefen weiter, auch 4K („Modi unter 1080p“).

Damit folgt die Aufnahme zumindest nach dem Abziehen nicht dem angeschlossenen Bildschirm, sondern der zuletzt eingestellten Ausgabe, und die bleibt ohne Kabel bestehen. Bei `0x144` (4K bei 120 Hz) fehlt das Bit `4K_SCAN_IN`, und auch mit Kabel lieferte die Demo beim Test für 0.1.9 nur 1080p. Das stützt die Vermutung bei `Is4KRunnable: 4K NG(4K120)`.

Nicht geprüft: Einschalten oder Aufwachen ohne Kabel, ausgeschalteter Fernseher mit Kabel, das echte Moonlight ohne Kabel, ein HDMI-Dummy-Stecker, und ob sich die Ausgabe per Software setzen lässt (`sceVideoOutSysConfigureOutputMode` steht im SDK, ungeprüft). Freie Auflösungen wie `2556x1179` gibt die Aufnahme mit ihren festen Modi nicht her.

### Geräteliste in der App, Release 0.1.9 (2026-11-02)

[v0.1.9](https://github.com/Darkatek7/ProsperoSunshine/releases/tag/v0.1.9) auf `af75da0f`, CI grün, Katalog-PR gemergt; die Anzeige im Store und eine Installation daraus sind nicht geprüft. Vor dem Release auf der Konsole installiert und neu registriert (`01.000.010`, in der App angezeigt), Host meldet 0.1.9 mit Commit `af75da0f`. Mario-64-Demo mit dem Testclient, je rund 20 s: H.264 1080p 1147 Bilder bei 59,94 fps, HEVC 1132 Bilder bei 59,93 fps, 0 Lücken, ffmpeg fehlerfrei. HEVC verlangte 4K und bekam 1080p, das System verweigert der Demo 4K wie bisher; 4K lief in diesem Test also nicht.

Moonlight auf einem iPhone (Besitzer): Koppeln mit PIN und getipptem Namen `iPhone`. Der Eintrag ersetzte den alten mit demselben Zertifikat, es blieb bei acht Geräten. Der anschließende Stream (HEVC, „PS5 Screen“) verlangte `2556x1179@60`, lieferte 1080p, `device_modes.txt` hielt die Anfrage fest. Rund 57 s Stream, Ende durch den Client (`Ping Timeout`).

Gemessen auf 13.00 mit einem eigens gekoppelten Testgerät und `stream_test.exe` (`pair.py --dir`), die acht vorhandenen Einträge blieben unberührt:

- **Liste:** Die Kachel „Paired devices“ öffnet die Liste, neun Einträge lassen sich rollen. Läuft noch ein alter Host, sagt die Liste, dass er neu gestartet werden muss.
- **Umbenennen:** `Moonlight 1` wurde zu `Moonlight 2`, die Liste und die Meldung zeigten es sofort, und der Host nannte das Gerät im Stream danach so.
- **Infozeile:** Nach einem Stream stand dort `Last asked for 1920x1080@60`, die Datei `device_modes.txt` hatte den Eintrag. Während des Streams zeigte das aufgezeichnete Bild `Streaming now, 1920x1080@60`.
- **Entfernen im laufenden Stream:** Die Rückfrage nannte das Ende des Streams. Nach Cross endete der Stream beim Client mit `terminated_by_host 1`, statt die vorgesehenen 60 s liefen rund 20 s. Das Gerät und sein Modus waren weg, `serverinfo` meldete `SUNSHINE_SERVER_FREE`, und `sunshine_state.json` war danach byte-gleich mit dem Stand vor dem Test.
- **Ausgesperrt:** Dieselbe Identität bekam danach von `serverinfo` und `launch` den Status 401 („Certificate verification failed“).

Nicht geprüft: Umbenennen und Entfernen zusammen mit dem echten Moonlight und was es beim Rauswurf anzeigt, die Liste während eines echten Streams, das Entfernen, während ein Spiel statt „PS5 Screen“ läuft, und ein tatsächlich unter 1080p angefragter Modus. Solange der Dialog „Who's using this controller?“ für das Pad des Streams offen ist, kommt der Tastendruck nicht in der App an.

### Name beim Koppeln (2026-11-02, nicht veröffentlicht)

Moonlight für PC schickt beim Koppeln fest `devicename=roth` (`app/backend/nvpairingmanager.cpp` in moonlight-qt), der Host übernahm das. Gemessen auf
13.00 mit `pair.py --name roth --manual-pin`: Die Karte zeigte „Moonlight“, nach der PIN fragte die App nach dem Namen, vorausgefüllt mit „Moonlight 1“.
Options bestätigte, gespeichert war `Moonlight 1` mit Leerzeichen, die App meldete „Paired with Moonlight 1“, das Koppeln lief durch. Nicht geprüft: Abbrechen im
Namensschritt, ein selbst getippter Name und das echte Moonlight. Der Testeintrag wurde danach entfernt.

### Berichte von Testern zu 0.1.7 (2026-11-02, nicht selbst gemessen)

- **13.60, PS5 Pro, Nachtrag des zweiten Nutzers (Issue 1, ohne neuen Bericht):** Mit einem 4K-Fernseher statt des 1080p-Monitors und `min_log_level = none` meldet der Nutzer 4K HEVC HDR bei 150 Mbit/s ohne Knacken und Bitraten-Warnungen, DualSense am Laptop und adaptive Trigger in Astro Bot. Das passt zu `video out mode 0x10` als Ursache der 1080p und zum Verdacht auf das Logging; gemessen ist beides nicht.

- **13.60 (Issue 1):** Ein Nutzer meldet eine Stunde Spielen in 4K60 mit HDR und Controller. Der Diagnosebericht dazu fehlt noch.
- **13.60, PS5 Pro, zweiter Nutzer (Issue 1, mit Bericht):** HEVC 10 bit bei 1080p startet (`variant=minimal`, `rc=0`). 4K scheitert bei `OpenVideo` mit
  `0x81950008` in beiden Varianten, auch bei 50 Mbit/s; der Host fällt auf 1080p zurück. `video out mode` ist dort `0x10`, also nach `docs/re/hdr.md` nur
  `60Hz` ohne 4K, WQHD oder HDR; dass die Bildausgabe die Ursache ist, ist nicht bewiesen. Der Nutzer meldet fehlende Bilder bei 80 und 150 Mbit/s
  und knackenden Ton beim Blättern in der App, 120 fps verbanden nicht. In der Sitzung lief `min_log_level = verbose`, und aufeinanderfolgende
  Logzeilen liegen etwa 3 ms auseinander; ob das die Ursache ist, ist nicht geprüft.
- **10.40 (Issue 2):** Der Bericht zeigt, dass die Aufnahme jetzt öffnet und HEVC startet (`variant=minimal`, `rc=0`). Der Nutzer nennt das Bild
  „choppy“ und sah bei H.264 und mit nur 1440p/4K einen weißen Bildschirm. Client und Ursache sind nicht geklärt; von den weißen Läufen gibt es
  keinen Bericht.

## Bauen und testen

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1
```

Baut im Docker-Image `prosperosunshine-sdk:v0.45` zuerst fehlende Abhängigkeiten (`out/deps`, erster Lauf ~30 min, vor allem Boost), dann alles mit CMake nach `out/`. Portable Tests unter ASan/UBSan:

```powershell
docker run --rm --mount "type=bind,source=$PWD,target=/repo" prosperosunshine-sdk:v0.45 bash scripts/test.sh
```

Host starten: `out/prospero_sunshine.elf` an den ELF-Loader (Port 9021) schicken. Steuer-Socket TCP 47995: `STATUS`, `PAIRINGS`, `PIN <pin>`, `QUIT`. Testclient, Pairing und Smoke-Test: `tools/moonlight/README.md`.

## Wichtige ABI-Erkenntnisse (FW 13.00)

- Avcap2-Videokonfiguration 0x2E0 Bytes; `0x2A4 = 2` zusammengesetzter Bildschirm; RA-Profil 180 (keine Benutzer-ID); Details in `src/platform/ps5/capture.c`.
- Audio-Konfiguration 0x38 Bytes; `cfg+0x1C` ist dieselbe RA-Id 180.
- Descriptor-Wort 9 = Aufnahmezeit in TSC-Ticks (~1,596 GHz); Wort 3 Bildtyp; Wort 4 Framezähler (`docs/avcap2-descriptor.md`).
- `sceAvcap2*` werden per Name über `kernel_dynlib_dlsym` aufgelöst; Export-Tabelle mit 59 NIDs in `docs/re/avcap2-exports.md`.
- `__builtin_cpu_supports` und AVX-Typen in `immintrin.h` funktionieren auf dem `__SCE__`-Ziel nicht ohne Umweg (siehe `scripts/deps/nanors.sh`).
- `std::jthread` braucht `-fexperimental-library` (im CMake gesetzt).

## Verwandtes Repository ps5-mcp

Die direkte Avcap2-Screenshot-Methode für `ps5_screenshot` ist dort umgesetzt und live geprüft (59 Python-Tests, Selbsttest mit elf Prüfungen bestanden). Nicht veröffentlicht; Restpunkte: veralteter Kopfkommentar in `payload/avcap_probe/avcap_probe.c`, ungetesteter Release-Download des neuen ELF.

## Herkunft und Lizenz

Sunshine `a679b059` und alle Bibliotheken mit Pins in `deps.lock`; Lizenztexte nach dem Build in `out/deps/licenses/`. Eigene Änderungen an Sunshine nur als begründete Patches in `patches/sunshine/` (0001 libvirtualhid, 0002 NVENC/VAAPI-Konstanten, 0003/0004 ohne libcurl). Analysen von Sony-Binärdateien enthalten keine Sony-Daten; die entschlüsselten Module liegen nur im ignorierten `.cache/re`.
