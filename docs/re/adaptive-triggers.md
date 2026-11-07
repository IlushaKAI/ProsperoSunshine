# Adaptive Trigger und Haptik zum Moonlight-Client (FW 13.00)

Stand: 31. Oktober 2026. Klärt den offenen Punkt aus ROADMAP P9.10. Grundlage: gepinnte Quellen (`deps.lock`), Quellen der
Clients auf GitHub und statische Analyse der entschlüsselten `libScePad` und `SceRemotePlay` (`tools/re/funcdump.py`).
Am selben Tag live gemessen: Teardown sendet Effekte an das virtuelle Pad, sie kommen über Moonlight PC am DualSense an.

## Ergebnis

| Frage | Antwort | Beleg |
| --- | --- | --- |
| Protokoll hat Trigger-Meldung? | **Ja**, fertig im gepinnten Stand | Quelle |
| Protokoll hat Haptik? | **Nein** | Quelle |
| Client setzt Trigger um? | Nur Moonlight PC (Qt); iOS und Android nicht | Qt im Nutzertest am DualSense bestätigt, sonst Quelle |
| Feld 5 enthält Trigger-Effekte? | **Ja** | Teardown: Modus `0x21`, spürbar am DualSense |
| Spiele füllen es bei unserem Pad? | **Ja**, Teardown; GTA III in zwei Sitzungen nicht | Messung unten |

## 1. Protokoll

**Trigger: vorhanden.** Der Host ruft es in `set_triggers` auf (`src/sunshine/platform_ps5.cpp`).

- Sunshine: `gamepad_feedback_e::set_adaptive_triggers` und `make_adaptive_triggers` (`.cache/deps/src/sunshine/src/platform/common.h:124,224`),
  Versand als Typ `0x5503` (`src/stream.cpp:74,262-275,1105-1120`). Kein Patch in `patches/sunshine/` berührt das.
- `moonlight-common-c`: Empfang in `src/ControlStream.c:216,1078-1086`, Callback in `src/Limelight.h:478-481`. Ohne Callback
  des Clients läuft die Meldung in einen leeren Standard-Callback (`src/FakeCallbacks.c:42,146`), schadet also nicht.
- Inhalt: `event_flags` (`0x04` rechts, `0x08` links), Typ links und rechts, je 10 Bytes Parameter. Der Kommentar in
  `ControlStream.c:79-82` nennt das Format: undurchsichtige Bytes, die so an den Controller gehen.
- Eine Fähigkeit des Clients dafür gibt es nicht (`LI_CCAP_*` in `Limelight.h:784-789`). Der Host sendet also blind.

**Haptik: kein Weg.** Das Protokoll kennt nur `rumble`, `rumble_triggers`, Bewegung, RGB, Player-LEDs und Trigger. Vorsicht
beim Suchen: `ENABLE_HAPTICS_MAGIC` (`InputStream.c:663-683`) schaltet nur die Rumble-Meldungen der alten GFE frei.

## 2. Clients

| Client | Stand | Trigger | Beleg |
| --- | --- | --- | --- |
| moonlight-qt | Tag `v6.2.0` | **implementiert** | `app/streaming/session.cpp:254-276`, `input/gamepad.cpp:964-977`: `SDL_GameControllerSendEffect`, nur bei `SDL_CONTROLLER_TYPE_PS5` |
| moonlight-ios | `master` `02dc978` (keine Releases) | **nicht implementiert** | `Limelight/Stream/Connection.m:491-493` setzt `setAdaptiveTriggers` nicht; `GCDualSenseGamepad` nur zur Typerkennung (`ControllerSupport.m:574`) |
| moonlight-android | Tag `v12.2` | **nicht implementiert** | `app/src/main/jni/moonlight-core/callbacks.c:426-428` setzt den Callback nicht |

- Eine GitHub-Codesuche nach `AdaptiveTrigger` traf bei iOS und Android nichts, bei Qt vier Dateien.
- „Implementiert“ heißt hier: Der Code ist da. Am echten DualSense ist es nicht probiert. Qt reicht `validFlag0 = eventFlags`
  durch (`session.cpp:267`); mit Flags 0 passiert am Controller vermutlich nichts, der Host muss sie also setzen.
- Nicht geprüft: Moonlight Web, Forks, Verhalten über Bluetooth, ob der Client die Effekte am Stream-Ende löst.

Nutzen daher: **unter den offiziellen Clients nur Moonlight PC mit angeschlossenem DualSense.** Am iPhone ändert sich nichts.

Nachtrag (2026-11-07, Bericht in Issue 30, hier nicht gemessen): Der Besitzer einer PS5 mit 9.60 spürte Trigger-Effekte in
Uncharted mit einem DualSense per Bluetooth an einem Android-17-Telefon, Client ist der Artemis-Fork von R3VALATiON (Branch
`feature/native-dualsense-adaptive`).

## 3. Konsole

Der Host liest den Block schon: `ps_pad_read_output` füllt `output->triggers[24]` (`src/platform/ps5/pad.c:99,113`),
`poll_pad_output` reicht Änderungen an den Client weiter und schreibt sie auf Log-Stufe `debug` ins Log. `pad_output_probe`
protokolliert den Block ebenfalls.

### Statisch belegt (Disassembly, nicht ausgeführt)

Der Export `LKXfw7VJYqg` aus `pad-output.md` heißt `scePadVirtualDeviceGetRemoteSetting`: Der NID ergibt sich rechnerisch aus
dem Namen. Genauso `scePadVirtualDeviceAddDevice` (`0xd8a0`) und `scePadVirtualDeviceInsertData` (`0xda20`).

Der Schreibweg und der Leseweg benutzen dieselbe Aufteilung von 24 Bytes:

| Byte | `scePadSetTriggerEffect` (`libScePad 0x7680`, Umwandlung `0x7c90`) | Remote Play (`0x24a850`) |
| --- | --- | --- |
| 0 | Maske des Aufrufs (`0x8edc`) | durchgereicht |
| 1 | Modus, wenn Maskenbit 0 | Typ A |
| 2–11 | Parameter dazu | 10 Bytes A |
| 12 | Modus, wenn Maskenbit 1 | Typ B |
| 13–22 | Parameter dazu | 10 Bytes B |
| 23 | von der Umwandlung nicht gezielt gesetzt gesehen | durchgereicht |

- `scePadSetTriggerEffect` schreibt als Modus `0x05`, `0x21`, `0x22`, `0x25`, `0x26` und schickt die 24 Bytes per ioctl
  `0x8010485a` an `/dev/hid`. Das sind die Modi des DualSense
  ([Übersicht](https://gist.github.com/Nielk1/6d54cc2c00d2201ccb8c2720ad7538db): `0x05` aus, `0x21` Feedback, `0x25` Weapon, `0x26` Vibration, `0x22` Bow).
- Remote Play dekodiert die Parameter in `0x24a160` genau wie dort beschrieben (Bit i = Zone i, je 3 Bit Stärke) und
  setzt den Typ unter einer Bedingung (`0x23c860`, Bedeutung unbekannt) auf `0x05`, also aus.
- `0x24a850` vergleicht die 24 Bytes mit dem letzten Stand und reicht Änderungen über `0x23b640` weiter. Das geschieht nur für
  Pad-Arten 6 und 7 (`[+0x78]`). Das ist eine Remote-Play-interne Sperre, unser direkter Lesezugriff läuft nicht darüber.
- Das Format passt 1:1 zu Moonlights 1 Byte Typ plus 10 Bytes (`DS_EFFECT_PAYLOAD_SIZE`).

### Von außen belegt

`chiaki-ng` (`8179a2d`) empfängt von der PS5 eine Trigger-Meldung und gibt sie an den DualSense weiter:
`lib/src/streamconnection.c:480-494` (Typ links und rechts, je 10 Bytes), `lib/include/chiaki/takion.h:31`. Über Remote Play
kommen die Effekte also an, und zwar in dieser Form. Dass sie aus diesem Feld stammen, ist abgeleitet.

### Gefolgert, nicht belegt

- **Der Kernel legt den ioctl-Block unverändert in Feld 5 ab:** Der Kernel ist nicht analysiert.
- **Was der Kernel bei Maske nur L2 mit dem anderen Trigger tut** (behalten oder nullen): unbekannt.

### Gemessen

- **Ruhewert:** `030500000000000000000000050000000000000000000000`, also Maske `03` und Modus `05` (aus) an Byte 1 und 12. So stand
  es am RemoteEye-Pad (Probe) und am Pad des Hosts im Stream (Debug-Log).
- **GTA III, PS5-Fassung:** Der Vibrationsmodus wechselt auf 2, der Block bleibt auf dem Ruhewert, auch mit R2 und L2 bei Fäusten.
  Im Spielstand gab es weder Waffe noch Fahrzeug, die Messung ist also **ohne Aussage**.
- **GTA III im Stream (Log-Stufe `debug`):** Zwei Sitzungen, in einer rund 66 s, 186 Motor-Werte, Block unverändert. Die Szene ist
  nicht festgehalten, GTA III bleibt also offen.
- **Teardown, Werkzeug:** Nur der zweite Block wechselt: `21 fe03 48922409 …`. Nach der Modus-Übersicht ist das Widerstand über die
  Zonen 1–9, Stärke 2 von 8. An und aus je einzeln, Abstand Sekunden. Er folgt nicht einfach dem Druck auf R2: Einmal blieb er über
  zwei Minuten an, auch während nur L2 gehalten wurde. L2 allein änderte den ersten Block nicht.
- **Teardown, Auto:** Beide Blöcke `21 fe03 00000000 …`. Laut Nutzer werden L2 und R2 am DualSense spürbar härter, im Log passend
  (15:18:57 Konsolenuhr). An und aus je einzeln, Abstand Sekunden, also keine Drossel nötig.
- **Byte 0 und 23:** In allen Messungen `03` und `00`, auch wenn sich nur ein Block änderte.
- **Testclient:** `stream_test` bekam beim Anlegen des Pads `flags=0xc types=5/5`, den Ruhewert an beide Trigger.

## 4. Füllen Spiele das Feld bei unserem Pad?

**Bekannt:** Klassisches Rumble eines Titels kommt am virtuellen Pad an, wenn nur dieses verbunden ist (`STATUS.md`).
`scePadSetVibration` und `scePadSetTriggerEffect` prüfen in `libScePad` beide denselben Handle-Eintrag und gehen dann per ioctl an
`/dev/hid`, nur mit anderer Nummer (`0x80184822`, `0x8010485a`).

**Vermutet:** Trigger laufen genauso. Dafür spricht, dass das Feld in genau der Abfrage steht, die Remote Play für seine
virtuellen Pads benutzt.

**Offen, und das kann die Sache kippen:**

- Unser Pad nutzt Klasse 3, Subtyp 1 (öffentlicher Typ 1, Tabelle `libScePad 0x1fab0`/`0x1fabc`). Welchen Typ Remote Play für
  seine Pads anlegt, steht zur Laufzeit fest (`0x40654`, kein direkter Aufrufer gefunden). Der Kernel könnte Effekte je Typ
  verwerfen.
- Ein Titel könnte Effekte je nach Controller-Information weglassen.
- Geklärt für Teardown: Das Pad bekommt Effekte. Andere Titel sind nicht gemessen; Mario 64 und Homebrew UI Lab sagen nichts dazu.

## Messung (entscheidet das Urteil)

**Am einfachsten über den Host:** In `/data/prosperosunshine/sunshine.conf` die Zeile `min_log_level = debug` eintragen, in der App
„Restart host“ wählen und das Spiel über Moonlight spielen, physischer DualSense an der Konsole aus. Jede Änderung steht dann als
`PS5 virtual pad 0 triggers <hex>` in `/data/prosperosunshine/sunshine.log`. Das Log vor dem nächsten Host-Start sichern und die Zeile
danach wieder entfernen. Systemeinstellung „Stärke der Trigger-Effekte“ nicht auf Aus.

**Ohne Stream über das Probe:** Handle des RemoteEye-Pads wie in `pad-output.md` nach `probe_handle`, das Spiel mit diesem Pad steuern.
`pad_output_probe.elf` schreibt zehn Minuten lang `pad_output triggers=<hex>` ins Kernel-Log.

| Lauf | Titelart | Was tun | Erwartung, wenn es geht |
| --- | --- | --- | --- |
| A, Gegenprobe | Homebrew UI Lab oder PS4-Titel | Mitlesen | Block bleibt auf dem Ruhewert |
| B, entscheidend | PS5-nativer Titel mit bekannten Effekten, z. B. Astro's Playroom (vorinstalliert oder kostenlos), ein Shooter oder GTA III mit Waffe oder Fahrzeug | Effekt auslösen, R2 und L2 getrennt, wenn der Titel das hergibt | Byte 1 und/oder 12 wechseln von `05` auf `21`, `25`, `26`, … |
| C, nur wenn B leer bleibt | Derselbe Titel, physischer DualSense | Prüfen, ob der Effekt in dieser Szene überhaupt kommt | Effekt spürbar, sonst war B ohne Aussage |

**Aus dem Log ablesen:**

1. Ändern sich die Bytes im Takt der Spielszene?
2. Welcher Block gehört zu welchem Trigger?
3. Was steht in Byte 0 und 23, und bleibt der andere Block stehen, wenn nur ein Trigger wechselt?
4. Wie viele Änderungen pro Sekunde kommen im schlimmsten Fall? Der Host würde jede einzeln senden.
5. Steht beim Titelende wieder `05`/Nullen in den Blöcken?

**Auswertung:**

- B zeigt Effekte: **machbar**, Plan unten.
- B bleibt leer, C zeigt den Effekt: Der Weg ist für Pads dieses Typs tot. Das wäre nur mit einem anderen Pad-Typ zu retten, und
  der ist ein eigener Spike mit Risiko für die Eingabe.
- A wechselt wild: Das Feld ist etwas anderes, die Deutung oben ist falsch.

Die Messung ändert nichts am Spielzustand und braucht keinen Ruhemodus.

## Haptik

**Für Moonlight nicht machbar.** Auf der Konsole gibt es den Weg: `sceAvcap2OpenHaptic`/`ReadHaptic`
(`docs/re/avcap2-exports.md:75,84`), in `SceRemotePlay` die Zeichenketten `AvCap audioThread haptic ...`, und `chiaki-ng` empfängt
einen eigenen Haptik-Audiostrom (`streamconnection.c:192,1239`). Aber das Protokoll hat keinen Kanal dafür, also bräuchte es eine
eigene Erweiterung in Host und jedem Client. Die Hüllkurve auf Rumble abzubilden wäre denkbar, ist aber weder versucht noch ist das
Format von `ReadHaptic` gelesen. Das bleibt außerhalb von P9.10.

## Urteil

- **Adaptive Trigger: machbar und im Host umgesetzt.** Nutzen nur mit Moonlight PC und DualSense.
- **Haptik: nicht machbar** ohne eigene Protokollerweiterung.
- **Zuordnung bestätigt:** Der zweite Block ist R2. Mit dem Werkzeug stand im Log nur der zweite Block auf `0x21`, und laut Nutzer wurde
  nur R2 hart. Der erste Block ist damit L2 (einzeln nicht gemessen, das Auto setzt beide).

## Umsetzung

- `map_trigger_effects` in `src/input/gamepad_map.hpp` setzt die Flags nur für den Trigger, dessen Block sich geändert hat (Test in
  `tests/gamepad_map_test.cpp`). `set_triggers` in `src/sunshine/platform_ps5.cpp` stellt die Meldung in Sunshines Warteschlange.
- Beim Anlegen eines Pads geht der Ruhewert an beide Trigger, weil der Merker leer ist. Das löst auch einen Trigger, der von einer
  früheren Sitzung noch hart ist.

## Grenze: Trigger bleibt nach dem Stream hart

Live bestätigt: Endet der Stream am Client während eines Effekts, bleibt der Trigger am DualSense hart. Beim nächsten Verbinden wird
er weich. Der Host kann das nicht lösen: `free_gamepad` sendet zwar „aus“, aber der Client hat die Verbindung dann schon beendet.
Moonlight PC v6.2.0 schließt den Controller beim Ende (`input.cpp:232`, `SDL_GameControllerClose`), ohne die Effekte zurückzusetzen.
Die eigentliche Lösung läge in Moonlight. Ob der Trigger auch beim Ausschalten des Controllers frei wird, ist nicht geprüft.
