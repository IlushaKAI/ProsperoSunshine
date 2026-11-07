# Haptik als Rumble

Spiele für die PS5 steuern den DualSense über einen Audio-ähnlichen Haptik-Kanal und setzen die klassischen Motoren vermutlich nicht
(`pad-output.md`). Moonlight hat dafür keinen Kanal, aber die zwei Motoren. Der Host liest deshalb mit `haptic_mode` in
`sunshine.conf` (App: „Vibration“) während eines Streams `sceAvcap2ReadHaptic` und gibt den Pegel als Rumble weiter
(`poll_haptics` in `src/sunshine/platform_ps5.cpp`, `src/platform/ps5/haptic_envelope.hpp`).

## Gemessen auf 9.60

`sceAvcap2OpenHaptic(h**, cfg*0x18, u8)` öffnet mit dem Aufbau des ersten Blocks der Audio-Konfiguration: Größe `0x18`, Art 0, Zweck 2,
Encoder 2, RA-Id, Flag 0. Encoder 0 und 1 geben `0x81950001`. Danach endete der Host dreimal Millisekunden später, ohne dass ein Paket
im Log stand, ohne die Stelle zu zeigen.

Seitdem läuft das Lesen in `ps_haptic_guarded`, und `haptic_attempt` legt `haptic_blocked` an, wenn der Host mit offenem Kanal stirbt.
Solange diese Datei da ist, bleibt der Kanal zu.
`haptic_code.bin` enthält den Code der Bibliothek um `ReadHaptic`.

Der Fehler war die Signatur. Der Code zeigt `sceAvcap2ReadHaptic(h, buffer, header, flags)`: Die Funktion ruft den gemeinsamen Ring-Reader
mit `buffer + 8`, Kapazität `0x800`, `header`, Headergröße `0x18` und `flags`. Mit der Form von `ReadAudio` wurde die Kapazität `0x10000`
zum Header, und der Lesezugriff schlug bei Adresse `0x10000` fehl (Signal 11).

In 2 Minuten 40 Sekunden Uncharted mit `min_log_level = debug` standen Trigger-Blöcke, aber keine Motorwerte im Log.

Danach kamen Daten, auch mit einem virtuellen Pad und mit Encoder 2: 47 Pakete je Sekunde zu 2048 Byte, also Float Stereo, kein Opus
(`opus_decode` meldete ein ungültiges Paket). Nur die ersten 64 Frames eines Pakets sind Samples, der Rest ist Füllung: In allen 381 aktiven
Paketen eines Mitschnitts lag die ganze Energie im ersten Viertel. 64 Frames bei 47 Paketen je Sekunde sind 3 kHz, die Rate der
Bluetooth-Haptik des DualSense. Beide Kanäle sind fast gleich. Es gibt Klicks von 20 bis 40 ms und Abklingen über 1 bis 2 s mit Spitzen um 0,8.

Die ersten Fassungen hielten alle 256 Frames für Samples, also 12 kHz. Das zerhackte die Wellenform 47-mal je Sekunde und ließ sie auf einem
Viertel der Zeit leise wirken. Laut Besitzer klang es im HD-Modus dumpf und abgehackt. Die erste Fassung nahm außerdem Pakete mit winzigen Werten für 16 Bit und meldete daraus einen Pegel von 0,13: ein ständiger Rumble von 80 bis 96
von 255. Laut Besitzer war die Vibration damit spürbar, aber nicht punktgenau.

Moonlight bekommt zwei Motorwerte, also bleibt es eine Annäherung an die Haptik eines DualSense. `HapticEnvelope` nimmt Spitzen statt
Mittelwerte und lässt in etwa 60 ms los; der Anteil unter 120 Hz geht zum großen Motor, der Rest zum kleinen. In einem Gefecht erreichten
die Spitzen 0,77 und der tiefe Anteil 0,49. Mit Quadratwurzel und voller Motorstärke fand der Besitzer das viel zu stark, daher linear,
höchstens 128 von 255, und was länger als eine Viertelsekunde anhält, klingt aus.

`haptic_strength` (Prozent, Vorgabe 100) skaliert die Vibration in beiden Betriebsarten, `trigger_knock_strength` (Vorgabe 70) die Stärken des Effekts
`0x26`, den Uncharted beim Schießen mit Stärke 7 in allen Zonen schickt. Der Host prüft alle 2 s, ob sich `sunshine.conf` geändert hat;
beides gilt also Sekunden nach dem Speichern, auch im laufenden Stream, ohne Neustart von Host oder Konsole.

## HD

Zwei Motorwerte können die Wellenform nicht tragen. Artemis Extended kann sie: Es spricht mit Apollo Extended ein Protokoll, das
`patches/sunshine/0022-dualsense-haptics.patch` übernimmt. Der Client bittet alle 2 s mit `0x5506` (Version, Ein, Laufzeit, Anfrage), der Host
antwortet mit einer Trigger-Meldung `0x5503`, Flags `0x40`, Typen `B` und `W`, `left` beginnt mit `P`, 1, Ein, 0, Anfrage. `0x5504` trägt dann
die Daten. Für Moonlight bedeutet `0x5504` Spieler-LEDs, deshalb geht es nur an Clients, die gebeten haben.

Apollo schickt vier Kanäle bei 48 kHz alle 3 ms. Dieser Host schickt mit Flag `0x04` die Haptik gleich im Format des Controllers: 3 kHz, 8 Bit
Stereo, zwei Rahmen zu 64 Byte je Paket des Kanals, 47 Pakete je Sekunde. Das braucht ein gepatchter Client (`prospero-ps5`).

Der Client schickt dem DualSense einen Bericht `0x33` je zwei Rahmen, Teilpaket `0xD2`, also 47 je Sekunde. Mit einem Rahmen je Bericht, 94 je
Sekunde, wuchs auf einem Galaxy S23 mit Android 17 die Verzögerung, und Trigger reagierten erst nach Sekunden: Sie stehen in derselben Warteschlange.
Mit `0x5507` meldet der Client alle 2 s seine Zähler, der Host schreibt sie als `Client Bluetooth:` ins Log. Ein Feature-Report als Laufzeitmessung
kostete die Vibration: 65 bis 137 ms in Ruhe, um 300 ms bei Dauervibration, oft keine Antwort binnen 3 s. Er ist wieder entfernt.

## Stand

HD ist nicht fertig. Gefühlt hat sie nur Client v8: Rahmen über den 48-kHz-Weg, zwei Rahmen je Bericht `0x33`, Teilpaket `0xD2`, Puffer 8.
Zwischen v8 und v9 kamen das Warten auf den zweiten Rahmen, die Zähler `0x5507` und die Laufzeitmessung dazu; v10 entfernte die Messung und
übergab Rahmen direkt. Keine der beiden Versionen vibrierte, die Messung allein erklärt das also nicht. Offen bleibt, was davon es war.

Das Trennen von Verzögerung im Client und im Funkweg geht von der App aus nicht: `sendData` meldet Erfolg, bevor ein Bericht die Funkstrecke
erreicht. Nach Protokoll und Praxis stammen die Fakten zum Format aus SAxense von Sdore (https://apps.sdore.me/SAxense).

Sony, Studios und Forschung beschreiben Haptik als eigens gestaltetes Signal. Am Host ist deshalb nichts nach Szenen zu modulieren: Die
Wellenform kommt fertig aus dem Spiel, es geht nur darum, sie unverändert zuzustellen.
