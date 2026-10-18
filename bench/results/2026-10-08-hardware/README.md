# Hardware-Nachweise vom 8. Oktober 2026

Der zugehörige Bericht ist [2026-10-08-hardware.md](../2026-10-08-hardware.md).
`receipt.json` beschreibt den geprüften Stand, die Fallzuordnung und die
verbleibenden Einschränkungen. Quellhashes beziehen sich auf die tatsächlichen
Build-Snapshots; der korrigierte CLI-Test ist zusätzlich separat archiviert.

`fuzzy/` und `misty/` enthalten unveränderte öffentliche Testlogs und Zustände.
Eine `.exit` mit Wert 1 kann ein erwarteter Disconnect-Flush, ein gescheiterter
Vorbereitungslauf oder ein echter offener Fehlerfall sein. Die Zuordnung steht
im Receipt; solche Dateien dürfen nicht pauschal als bestandene Tests zählen.
Nur `drop-done-*-v2` verwendet nachweislich die instrumentierte Verlustvariante.
Nur `normal-host-full-gid1` qualifiziert die komplette normale Host-MR-Matrix.

`ab-summary.json`, `cq-probe-summary.json` und `cq-serve-summary.json` enthalten
die Auswertung beziehungsweise jeden einzelnen Modellrequest. Die zwei Ranks
desselben Versuchs sind keine unabhängigen Stichproben. Die archivierten
Laborskripte benötigen das konkrete Testverzeichnis und die privaten Sicherungen
auf den Hosts; sie sind keine allgemeinen Installationswerkzeuge.

Private Container-Konfigurationen, Tokens und vollständige Modellantworten
sind nicht enthalten. `restored-*.json` dokumentiert den wiederhergestellten
Originalbetrieb; `kernel-final-summary.json` den abschließenden Kernel-Scan.
