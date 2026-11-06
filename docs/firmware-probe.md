# Firmware-Sonde

Stand: 5. November 2026. `firmware_probe.elf` sammelt in einem Lauf von wenigen Minuten, was die Portierung des Hosts auf
Firmware 10.20 herausfinden musste (`docs/firmware-porting.md`), für eine Firmware, die noch niemand gemessen hat. Der
Bericht sagt am Ende, was der Host auf dieser Firmware bräuchte. Validiert am 5. November 2026 mit der Sonde aus diesem
Stand auf einer PS5 Pro mit 10.20 (Host 0.3.1 lief, die Sonde hielt ihn an und startete ihn wieder) und einer PS5 mit
10.40 (kein Host lief), je 147 s: Auf beiden fand sie den Datensatz des Hosts (`0x2c8`), den QP in `cfg+0xc4`/`+0xc8`,
HEVC 8 und 10 Bit nur mit `cfg+0x24` und alle Modi von 360p bis 2160p in beiden Codecs. H.264 kommt auch mit den
Slice-Feldern von 0.3.1 in 34 Slices je 1080p-Bild (`STATUS.md`, „Firmware-Sonde auf 10.20 und 10.40“). Am 6. November 2026 fand
sie auf 9.60 in 137 s einen Datensatz, den der Host noch nicht kannte (`0x2c0`, `STATUS.md`, „Firmware-Sonde auf 9.60“).

## Wann sie läuft

- **Nur, wenn der Nutzer sie startet:** Er schickt das ELF an den ELF-Loader (Port 9021), oder der Host tut es auf den
  Steuerbefehl `PROBE` hin (unten). Nichts startet sie von selbst. `firmware_probe.elf` liegt im Release-Archiv.
- **Nie während eines Streams:** Sie fragt den Host über den Steuer-Socket (`STATUS`, `127.0.0.1:47995`). Läuft ein
  Stream, hört sie sofort auf und schreibt nur das in den Bericht. Antwortet der Host dreimal nicht, während er auf 47989
  lauscht, gilt er ebenfalls als beschäftigt: Die Sonde hält dann lieber an, als einen Stream abzuschneiden. Kurz bevor
  sie den Host anhält, fragt sie noch einmal. Ein Moonlight-Start zwischen `/launch` und RTSP rutscht durch beide
  Prüfungen; das Fenster ist kurz.
- **Den Host hält sie an und startet ihn wieder:** `QUIT` über den Steuer-Socket, am Ende das installierte ELF an den
  Loader (`src/host_control.hpp`). Dafür muss niemand in der App Stop und Start drücken.
- **Sie lädt nichts hoch:** Alles bleibt auf der Konsole. Der Nutzer liest den Bericht und entscheidet, was er teilt.
- Während des Laufs sollte kein Spiel starten oder enden, sonst schlagen Aufnahmen mit `0x81950009` fehl. Ein offenes
  Spiel im Vordergrund stört nicht.

## Start über den Host (`PROBE`)

Für einen Knopf „Run firmware probe“ auf der Diagnose-Seite der App und später auf der Web-Seite aus Issue #21: Liegt das
ELF unter `/data/prosperosunshine/firmware_probe.elf`, schickt der Host es auf `PROBE` über den Steuer-Socket an den
ELF-Loader und antwortet `OK started`. Läuft ein Stream, lehnt er mit `ERR a stream is running` ab; fehlt das ELF oder der
Loader, mit `ERR` und dem Grund. Ab da läuft alles wie oben: Die Sonde fragt `STATUS`, hält den Host mit `QUIT` an und
startet ihn am Ende wieder. Während des Laufs antwortet der Host also nicht; ob die Sonde fertig ist, steht in
`firmware_probe/progress.txt`, das Ergebnis in `report.txt`. Wer den Knopf baut, sollte vorher sagen, dass der Lauf etwa
2,5 Minuten dauert, den Host so lange anhält und dass in der Zeit kein Spiel starten oder enden sollte.

## Ergebnisse

In `/data/prosperosunshine/firmware_probe/`:

| Datei | Inhalt |
| --- | --- |
| `report.txt` | Befunde mit den Codes dahinter und eine Zusammenfassung, was der Host bräuchte. Wird nach jeder Phase neu geschrieben, damit auch ein abgebrochener Lauf zeigt, was er bis dahin fand |
| `detail.log` | Eine Zeile pro Versuch, die Datensätze als Hex |
| `capture.log` | Die Schritte von `capture.c` (die letzten 128 KiB) |
| `progress.txt` | Die laufende Phase, am Ende `done` |
| `samples/` | Nur mit Zustimmung, siehe unten |

**Wenn die Konsole hängt:** Vor jedem Aufruf, der hängen könnte (jedes `OpenVideo`, `OpenAudio`, `SetVideoBitrate`, die
Abfragen von Avcap2 und ResourceArbitrator, `QUIT` und der Neustart des Hosts), schreibt die Sonde eine Zeile mit `->`
nach `detail.log`, mit `fsync` auf die Platte, und dieselbe Zeile ins klog. Die letzte `->`-Zeile ist der Aufruf, an dem
es hing.

**Wenn die Sonde nicht fertig wird** (`progress.txt` steht nicht auf `done`, etwa nach einem Absturz oder Hänger), bleibt
der Host angehalten: In der App „Start host“ drücken. Ein Knopf in der App kann das später selbst tun, wenn sich
`progress.txt` nicht mehr ändert.

## Was im Bericht steht

Der Bericht ist dafür gedacht, ihn so, wie er ist, in einem Issue zu posten:

- **Drin:** Firmware (Nummer und Systemversion), ob es eine Pro ist (`sceKernelHasTrinityMode`; `sceKernelIsTrinityMode` sagt nur, ob der Pro-Modus gerade an ist, und folgt dem Titel im Vordergrund), die Version der App
  und des zuletzt gestarteten Hosts, ob ein Spiel oder eine App oder der Startbildschirm im Vordergrund ist (am Anfang
  und am Ende, und ob sich das geändert hat), die Ergebnisse jeder Phase.
- **Nicht drin:** welcher Titel im Vordergrund ist (die Title-ID wird nur verglichen, nie geschrieben), MAC- und
  IP-Adressen, Konto, Benutzername, Konsolen-ID, Seriennummer, `sunshine.conf` (dort steht der Name, den der Nutzer dem
  Host gab). Als Rückfallebene maskiert `src/probe_scrub.hpp` jede Zeile, die wie eine IPv4- oder MAC-Adresse aussieht
  (`tests/probe_scrub_test.cpp`).

## Video- und Audio-Proben

Kurze Proben (H.264, HEVC 8 und 10 Bit, PCM, Opus) zum Dekodieren am PC. Sie zeigen, was während des Laufs auf dem
Bildschirm ist und was die Konsole abspielt, auf dem Startbildschirm also auch Avatar und Benutzernamen. Deshalb:

- **Eigene Zustimmung:** Nur mit einer leeren Datei `/data/prosperosunshine/firmware_probe.samples`. Die Sonde löscht
  sie beim Start, sie gilt also für einen Lauf.
- **Bleiben auf der Konsole,** in `firmware_probe/samples/`, getrennt vom Bericht. Jeder Lauf löscht die Proben des
  vorigen.
- **Nicht in öffentliche Issues.** Weitergeben nur, wenn ein Entwickler danach fragt.

## Optionen

- `/data/prosperosunshine/firmware_probe.phases`: eine Zeile mit Phasennamen, durch Leerzeichen getrennt; dann laufen
  nur diese.
- `/data/prosperosunshine/firmware_probe.samples`: siehe oben.
- `/data/prosperosunshine/host_restart.next`: welches ELF am Ende gestartet wird (ein Pfad, oder `none`); ohne die Datei
  das installierte `/data/prosperosunshine/prospero_sunshine.elf`, und nur, wenn vorher ein Host lief.

## Phasen

1. **environment:** Firmware, Pro oder nicht, Versionen von App und Host, was im Vordergrund ist, TSC pro Nanosekunde,
   Kosten einer klog- und einer Dateizeile.
2. **host:** der Datensatz des Hosts selbst (`configure()`) auf dieser Firmware: H.264 1080p60 für 3 s (Slices,
   IDR-Abstand, fps nach TSC, Deskriptoren), HEVC 8 und 10 Bit, Audio PCM und Opus.
3. **symbols:** die 59 Exporte von `libSceAvcap2` gegen die Offsets von 13.00, der ResourceArbitrator (per NID und per
   Tabelle, aktive Ressourcen), `GetVideoEncoderCapability`, `GetVideoOutMode`.
4. **layout:** `OpenVideo` nur öffnend für jede Datensatzgröße von −0x40 bis +0x20, mit und ohne die inneren Köpfe und
   die Bitratenfelder von 13.00; danach `Start` mit denen, die durchgehen. Die erste, die Bilder liefert, ist die
   Grundlage der weiteren Phasen.
5. **guards:** 180 in jedes freie Wort; welche lehnt der Validator ab.
6. **values:** 2 in jedes freie Wort des Encoder-Blocks, Bytes über 1 s gegen die Basis. So fiel auf 10.20 der QP auf.
7. **rate:** die Bitratenfelder von 13.00, der QP in `0xc4`/`0xc8` (ohne, 36/40, 28/32, 20/24, 52), `SetVideoBitrate`
   mitten im Stream (beim Eröffnungswert und bei 20 Mbit/s).
   **ratesearch** und **qp** laufen nur, wenn `firmware_probe.phases` sie nennt. `qp` misst QP-Paare von 36/40 bis 8/12 am Offset des
   Hosts. `ratesearch` dauert einige Minuten: 40000000 und 20 in jedes freie Wort,
   `0xc0` als Modus, Bitratenfelder an anderen Stellen, `SetVideoBitrate` von 1 bis 16 Mbit/s. Für eine Firmware, auf der weder
   die Bitratenfelder von 13.00 noch der QP von 10.x wirken (9.60).
8. **codecs:** H.264 10 Bit, HEVC nur mit `cfg+0x24`, HEVC mit den Remote-Play-Feldern von 13.00, HEVC 10 Bit.
9. **modes:** 360p60, 540p60, 720p60, 1080p30, 1440p60, 2160p60, in H.264 und HEVC.
10. **purposes:** Zweck 0–7 und die Ressourcen 180, 177, 185, 175, 182.

## Bauen und Tests

`cmake --build <build> --target firmware_probe host_restart` baut `firmware_probe.elf` und `host_restart.elf` (das nur
den Host neu startet, wie oben). `scripts/test.sh` prüft:

- `tests/probe_record_test.c`: Der Datensatz-Baukasten der Sonde (`src/platform/ps5/probe_record.h`) ergibt mit den
  Entscheidungen des Hosts in 768 Kombinationen byteweise den Datensatz von `configure()`, dazu die Forschungshaken in
  `capture.c` (Datensatz umschreiben, nur öffnen, Zahl der Versuche, letzter Fehler). Der Host aktiviert sie nie.
- `tests/probe_scrub_test.cpp`: was maskiert wird und was nicht.
- `tests/host_control_test.cpp`: dass die Sonde nur bei einer Client-Adresse in `STATUS` einen Stream annimmt, und dass
  Schweigen als beschäftigt gilt, solange der Host lauscht, und dass `send_elf` ein fehlendes ELF meldet.
