# SmartRoom: direkte Integration ohne Coding-Agent

Basis: main 7be3cd0b222a2f11e0de1e394ba967e93ef9d85e und PR #1 bei 330a57daf0a98e6f3d2a304a8d7ec008cbc218dc.

## Paket 1: Build-Skript und Konfigurationshygiene

Implementiert: expliziter IDF_PATH, Argumentpruefung, explizite serielle Portauswahl,
korrekte Fehlerweitergabe, keine Passworterfassung im WLAN-Helfer,
widerspruchsfreie Defaults ohne WLAN-Zugangsdaten und Ignore-Regeln fuer lokale Daten.
Acht Build-Skript-Tests mit einem simulierten idf.py wurden erfolgreich ausgefuehrt.
Das ist KEIN ESP-IDF-Firmware-Build und KEIN Hardwaretest.

Wichtig: Ignore-Regeln entfernen keine schon versionierten Dateien.
Bestehendes sdkconfig kann Defaults uebersteuern und muss separat geprueft werden.
Das Entfernen von Zugangsdaten aus Defaults entfernt sie nicht aus der Historie.
Echte offengelegte Zugangsdaten muessen gewechselt werden. Keine Historie umschreiben.

## Noch zu implementieren und nachzuweisen

1. Beide Quellstaende zusammenfuehren, einen modularen C++-Einstiegspunkt herstellen.
   Manifest/Lock/CBOR-Patch/SDK-APIs pruefen und reproduzierbaren C6-Build herstellen.
   Vollstaendige Host-Suite in Debug/Release und Firmware-Build in CI einbinden.
2. IR RX19/TX18 beibehalten; Originalzeiten speichern, Pegel und RMT-Zeitumrechnung,
   Ressourcen, lange Sequenzen, Aufnahmegrenzen, Timeout/Abbruch und Pufferlebensdauer korrigieren.
   Keine Behauptung universeller Protokollunterstuetzung oder gemessener Traegerfrequenz.
3. NVS ohne automatisches globales Loeschen; versionierte validierte Records,
   kollisionssichere Identitaet, wiederherstellbare Schreibvorgaenge und stabile Endpunktzuordnung.
   Fehler-/Stromausfall-Simulationen, Listen/Umbenennen/Loeschen getrennt vom Factory-Reset.
4. Task-Zustaendigkeiten und begrenzte Queues statt volatile-Synchronisation.
   Matter-Modellaenderungen im korrekten Kontext, keine blockierenden Hardware-/Flash-Aufrufe dort.
5. Digitaler Matter-Aktor 'IR-Befehl anlernen': On startet, Off bricht ab;
   Erfolg/Duplikat/Timeout/Fehler setzen zurueck. Kein physischer Taster erforderlich.
   Vollstaendige dynamische Bridge-Metadaten, einmaliges Replay pro akzeptiertem On,
   keine Wiedergabe bei Off/Boot/internen Updates; Loeschen und Neustart testen.
6. Vorhandenen WS2813-Encoder aus main modular integrieren, 144 statt145 LEDs;
   Datenpin ungesetzt bis bestaetigt. GPIO19 ist fuer IR RX reserviert.
   RMT-Budget fuer IR TX, RX und LED TX gemeinsam validieren.
   TLV-Temperaturdecoder, Farbmodi/Attribute/Transitions und Farbraumangaben korrigieren.
   Nicht unterstuetzte Eigenschaften nicht bewerben; keine erfundenen Primaries.
7. SDK-generierte Onboarding-Daten statt fest codierter QR-Texte/PIN als manueller Code.
   QR-Helfer, deutsche Anleitungen und Build-/WLAN-Skripte konsistent halten.
8. Erweiterbare Sensorprovider mit Ressourcen-/Busverwaltung, Abfrageplanung,
   Einheiten, ungueltigen/veralteten Werten und Matter-Fabriken.
   Keine fiktiven Sensoren im Produktivpfad.
9. Benannte Geraeteprofile fuer gelernte Lampen-/Beamerbefehle und Klimapresets.
   PAC EM82: kompletter HVAC-Codec erst nach Protokoll- und Mitschnittnachweis.

## Abhaengigkeiten und Abnahme

Offen: bestaetigter LED-Datenpin und DI/BI-Verdrahtung, reale Flash-Groesse,
Versorgungs-/USB-Verdrahtung, Pegelwandler, Sensormodelle und PAC-Fernbedienungssignale.
Keine Pins oder Hardwareeigenschaften stillschweigend annehmen.
Keine gleichzeitige externe5V/USB-Versorgung ungeprueft empfehlen.

Hardwareabnahme: Commissioning, Multi-Admin, Lernen/Abbruch/Duplikate/Rauschen,
volle Speicher-/Endpunktkapazitaet, wiederholtes Ausloesen, Neustart-Identitaeten,
Loeschen, Stromausfall beim Speichern, RGB und gleichzeitiger IR/RGB-Betrieb.
Apple Home/Alexa-Darstellung und Live-Endpunkterkennung bleiben praktisch zu pruefen.
Nicht mergen oder als vollstaendig/funktionsgeprueft bezeichnen, bevor die Nachweise vorliegen.
