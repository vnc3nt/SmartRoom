# SmartRoom (ESP-IDF + ESP-Matter, ESP32-C6)

## Status
Diese Firmware wurde auf **ESP-IDF + ESP-Matter** umgestellt. Ziel aus der Repository-Konfiguration:
- `CONFIG_IDF_TARGET="esp32c6"` (aus `sdkconfig`)

> Wichtig: Dieses Repository enthält aktuell keine nachweisbare Board-spezifische Pinbelegung außer den Kconfig-Defaults. Deshalb ist die Hardware-Konfiguration explizit per Kconfig erforderlich.

## Reproduzierbare Toolchain / Versionen
Gepinnte Komponenten:
- ESP-IDF: `5.5.0` (siehe `dependencies.lock`)
- ESP-Matter: `1.4.0` (siehe `idf_component.yaml`)

Empfohlener Setup:
1. ESP-IDF 5.5.0 installieren/aktivieren
2. Im Repo bauen:
   - `idf.py set-target esp32c6`
   - `idf.py reconfigure`
   - `idf.py build`
   - `idf.py -p <PORT> flash monitor`

## Architektur
Module (Main):
- `app_main.cpp`: Matter-Node, Aggregator, Bridged-Endpunkte, Worker-Queues
- `smartroom_ir_rmt.*`: Raw-IR Capture/Replay via RMT
- `smartroom_ir_codec.*`: Normalisierung, Duplikaterkennung, stabile Signal-ID
- `smartroom_store.*` + `smartroom_store_record.*`: NVS-Persistenz (Version + CRC)
- `smartroom_learning.*`: Entprellte Long-Press-Lernzustandsmaschine
- `smartroom_rgb.*`: RGB-Backend-Auswahl (sicherer Default: deaktiviert)

## Lernen und IR-Wiedergabe
- Langer Tastendruck (konfigurierbar) startet den Lernmodus.
- IR wird als **demodulierte Pulszeiten** per RMT erfasst (nicht nur NEC-Bytes).
- Validierung gegen Rauschen/Überlauf, sichere Grenzen (`CONFIG_SMARTROOM_IR_MAX_SYMBOLS`).
- Duplikaterkennung über timing-tolerante Normalisierung (nicht rohe Byte-Gleichheit).
- Persistenz in eigener NVS-Namespace `smartroom_ir` mit Record-Version und CRC.
- Gelernte Signale werden als bridged `On/Off Plug-in Unit` Endpunkte unter einem Aggregator erstellt.
- `On` löst genau eine IR-Wiedergabe aus, danach wird der OnOff-Status asynchron wieder auf `false` gesetzt. `Off` sendet nichts.

### Physikalische Limitierung (wichtig)
Ein **demodulierender IR-Empfänger** kann die ursprüngliche Trägerfrequenz nicht messen; gespeichert wird nur die Hüllkurve (Puls-/Pausenzeiten). Deshalb kann nicht garantiert werden, dass jedes proprietäre Protokoll bzw. jedes Gerät zuverlässig replizierbar ist.

## RGB-Endpunkt
Zusätzlicher Matter-Endpunkt: `extended_color_light` (On/Off, Helligkeit, Farbe).

Backend-Auswahl per Kconfig:
- `SMARTROOM_RGB_BACKEND_NONE` (Default, sicher)
- `SMARTROOM_RGB_BACKEND_ANALOG` (LEDC + MOSFET-Treiber)
- `SMARTROOM_RGB_BACKEND_WS2812` (aktuell als nicht unterstützt blockiert, bis dedizierter Treiber aktiv)

Sicherheitsprinzip:
- Kein RGB-Ausgang, solange keine gültige Hardware-Konfiguration vorliegt.
- Keine stillschweigende Pin-Annahme.

## Verdrahtungshinweise
- IR-LED **nicht direkt** an GPIO mit hohem Strom betreiben; geeigneten Treibertransistor verwenden.
- Externe Versorgung für LED-Strips, gemeinsame Masse (GND) mit ESP.
- Bei 5V-WS2812 i.d.R. Pegelwandler für Datensignal notwendig.
- Analoge RGB-Strips nur über MOSFET-Treiberstufe.

## Konfiguration (Kconfig)
Wichtige Optionen in `Kconfig.projbuild`:
- IR: RX/TX GPIO, RMT-Auflösung, Standard-Trägerfrequenz, Symbol-Limits
- Button: GPIO, Debounce, Long-Press, Lern-Timeout
- RGB: Backend-Typ, GPIOs, Polarität, LED-Anzahl/Channel-Order

## Persistenz und Wartung
- Namespace: `smartroom_ir`
- Kommissionierungsdaten bleiben erhalten (kein globales NVS-Löschen durch Lernfunktion).
- APIs für `list/delete/clear` sind getrennt vom Factory-Reset vorgesehen (`smartroom_learned_signal_count`, `smartroom_learned_signal_delete`, `smartroom_learned_signal_clear`); Factory-Reset wird vom Lernpfad nicht ausgelöst.

## Home-App / UI-Semantik
- Gelernte Signale erscheinen als eigene bridged Aktor-Endpunkte.
- Wiederholtes Auslösen erfolgt über momentanes `On` (automatisches Rücksetzen auf `Off`).

Einschränkung:
- Live-Discovery/Refresh-Verhalten von Apple Home bei dynamischen Endpunkten ist controllerabhängig; ggf. Controller-App neu öffnen/refreshen (nicht-destruktiver Fallback).

## Partitionen
Verwendet benutzerdefinierte Partitionstabelle `partitions.csv` mit dediziertem NVS-Bereich.

## Tests
Host-Tests unter `tests/host`:
- Duplikatnormalisierung
- Persistenz-Record-Integrität (CRC/Decode)
- Lernzustandsmaschine (Long-Press/Cancel)

Ausführung:
- `cmake -S tests/host -B tests/host/build && cmake --build tests/host/build && ctest --test-dir tests/host/build`

## Troubleshooting
- Bei Build-Fehlern zuerst `idf.py reconfigure` nach Toolchain-/Komponentenwechsel ausführen.
- Bei fehlender RGB-Ausgabe prüfen, ob ein gültiges Backend konfiguriert wurde.
- Bei Lernproblemen auf IR-Empfängerposition, Umgebungslicht und Signalrauschen achten.
