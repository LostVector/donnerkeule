# Detailliertes Transport-Profiling

Stand: 2026-10-16. Linux-Modulbau und Kernel-Traces wurden auf fuzzy/misty
geprüft; siehe [Treiberbericht](../bench/results/2026-10-16-verbs-parity.md).
Die Messungen verwenden den erhaltenen älteren HIP-Verifier. Der neue
optionale Userspace-Detailprofiling-Code ist damit weiterhin nur lokal geprüft.

Das Profiling soll erklären, welche Arbeit und welche Wartezeiten einen WR
verlängern. Eine Verbesserung wird anschließend **ohne Profiling** gemessen.
Tracepoints, GPU-Event-Marker und Aufzeichnung verändern die Laufzeit des
beobachteten Ablaufs. Profilierte `RESULT`-Zeilen sind mit `profiled=1` markiert.

## Abschaltung für den Produktionsbetrieb

Standardmäßig ist der neue Tracepoint `thunderbolt_ibverbs:tbv_perf` deaktiviert.
Sein Linux-Static-Key verhindert Zeitstempellesen und Trace-Aufzeichnung.
Zusätzliche Completion-Zähler pro Rail werden nur für einen bei aktivem
Tracepoint aufgebauten Batch verwendet. Im ausgeschalteten Zustand bleiben
die kleinen Prüfungen und Metadatenfelder; der Zusatzaufwand ist noch nicht
auf Hardware gemessen.

Das Testprogramm aktiviert seine Detailaufzeichnung ausschließlich mit
`--profile NEUE_DATEI.jsonl`. Ohne die Option entstehen weder eine Datei noch
zusätzliche HIP-Events, Prozess-CPU-Messungen oder Profiling-Zeitstempel.
Die bisherigen `RESULT`-/`PHASE`-Messungen des Tests bleiben erhalten.

Die Detailinstrumentierung kann zusätzlich beim Bauen entfernt werden:

```sh
make TBV_PROFILING=0 KDIR=/pfad/zum/kernel/build modules
TBV_PROFILING=0 tools/build-gpu-verbs.sh /tmp/tbv_gpu_verbs

# Im Nix-Container auf fuzzy, sobald er wieder verfügbar ist:
nix build "path:$PWD#thunderbolt-ibverbs-no-profiling"
```

Bei direktem Import von `nix/module.nix` entspricht dies
`enableProfiling = false`. Der Test weist `--profile` in diesem Build zurück.
Andere bestehende Tracepoints bleiben erhalten. Der ältere Summenzähler
`dmabuf_profile` ist unabhängig davon und bleibt für Produktion auf `0`.
Das Detailprofiling benötigt `dmabuf_profile=1` nicht und ändert keine
Transport-, MR-, Ownership- oder Retry-Einstellung.

## Messpunkte

Jeder Treibereintrag enthält Anfang/Ende in lokalen monotonen Nanosekunden,
Phase, Status und ein Zusatzfeld `aux`. Token, Quell-/Ziel-QP und PSN ordnen ihn
einem WR zu; Offset bezeichnet den Batch, Rail die lokale physische Rail.
`tx=1` bedeutet Quelle dieses WRs, `tx=0` Empfänger. Lokale WR-IDs werden am
Verbs-Rand und für TX-Jobs mitgeführt; sie werden nicht ins Protokoll eingefügt.
GPU-Adressen, rkeys und Nutzdaten werden vom neuen Tracepoint nicht erfasst.

Im DMA-BUF-Pfad bezeichnet `rail` jetzt den eindeutigen Lane-Index im Batch;
die früheren linklokalen Rail-Nummern kollidierten bei mehreren Kabeln.
`lane_post_chunk` misst einen Posting-Abschnitt, `lane_post` die gesamte
verstrichene Posting-Zeit einer Rail einschließlich der zwischenliegenden
Abschnitte ihrer Geschwister-Rails. Diese Intervalle überlappen.

| Bereich | Phasen | Was sich damit unterscheiden lässt |
|---|---|---|
| WR | `tx_queue`, `tx_job`, `tx_wr`, `tx_finish` | Globale Serialisierung, eigentliche Arbeit und Verbs-Abschluss |
| Vorbereitung | `tx_paths_lock`, `rx_paths`, `rx_verbs_begin`, `control_rx_lock` | Routensuche, Sperren, QP-/MR-Prüfung und RQ-Bereitschaft |
| Speicher/Deskriptoren | `mr_attach_index`, `lane_alloc`, `lane_build`, `batch_build` | Erster MR-Import am NHI, Allokation, Adressbau und Host-Staging |
| Posting | `post_queue`, `post_worker`, `lane_post`, `batch_post` | Warten auf NHI-Worker, parallele NHIs und Kosten je Rail |
| DMA | `dma_first`, `dma_last`, `batch_dma_done`, `tx_dma_wait` | Erste/letzte Callbacks je Rail, gesamter Batch und Warten der Quelle |
| RX-Abschluss | `rx_callback_worker`, `rx_worker_lock`, `rx_reap`, `rx_finish`, `batch_release` | Callback→Worker-Verzug, RX-Sperre, Placement, Drain/Freigabe und CQ |
| Steuerung | `control_exchange`, `xdomain_lock`, `xdomain_request`, `control_dispatch`, `control_service`, `control_response` | Lock, Roundtrip, empfangsseitige Vorbereitung und Antwortversand |
| Kontrollring | `control_ring_submit`, `control_reply_wait`, `control_reply` | Optionaler Ringversand und Antwortwartezeit |
| DONE | `done_submit`, `done_send`, `done_received`, `done_wait` | Versand, Eingang und tatsächliche Wartezeit; früh eingetroffenes DONE bleibt sichtbar |
| Erholung | `dma_watchdog`, `status_fallback`, `prepare_retry_sleep`, `status_retry_sleep` | Ausgebliebene Benachrichtigung, STATUS-Fallback, BUSY/RNR und Retry-Pausen |
| Verbs-Rand | `verbs_post_send`, `verbs_post_recv`, `cq_publish`, `cq_push`, `cq_notify` | Auch im normalen Verbs-Modus: Posting, CQ-Veröffentlichung und Benachrichtigung |

`verbs_post_*` umfasst einen eingereichten WR-Verbund; WR-ID und Länge nennen
dessen ersten WR. Der Test reicht einzelne WRs ein. `mr_attach_index` hat
keinen Token, weil es die langlebige MR-Abbildung misst. Sein Zeitintervall
liegt im ersten `lane_build`/`batch_build`. `lane_build` enthält im Host-MR-
Staging-Modus auch die Kopie, im DMA-BUF-Modus die Adressauflösung.

Die DMA-Marker messen **Callback-Zeitpunkte**, keine Zeitstempel der NHI-
Hardware. Interruptlatenz, Ring-Worker und tatsächliche DMA-Dauer sind darin
enthalten. Optional erfasste IRQ-/Workqueue-Ereignisse helfen beim Trennen.
Es gibt zwei Marker je Rail statt einer Trace-Aufzeichnung je 4-KiB-Frame.
Nur während aktiver Detailmessung zählt ein zusätzlicher Atomic die Frames.

Im GPU-Test werden jede Iteration und die einzelnen Posting-/CQ-Aufrufe
aufgezeichnet. Dazu kommen leere Polls, Spin-/Schlafaufrufe und Prozess-CPU-
Mikrosekunden. GPU-Event-Paare messen RX-Vorlesen, Produzent, TX-/RX-Kopien
(nur Host-MR), Konsument/Verifikation und die kleine Ergebnis-Kopie. Events
werden wiederverwendet und nach der ohnehin vorhandenen Stream-Synchronisation
ausgelesen; das Profiling fügt keine weitere Synchronisation ein.

## Aufzeichnung auf fuzzy und misty

Die passenden Module und die isolierte HIP-/Verbs-Laufzeit müssen zuvor
vorbereitet sein. Ein Modulwechsel verwendet weiterhin den dokumentierten
Lab-Ablauf in `GPU_VERBS.md`. Der Capture-Runner lädt keine Module, ändert
keine Parameter und beendet keine Dienste.

Der Runner erstellt eine eigene Tracefs-Instanz und schaltet den globalen
Tracer nicht um. Root bzw. entsprechende Tracefs-Rechte und zugängliches
Tracefs sind erforderlich. Auf **beiden Hosts** denselben Fall starten;
Rank 0 zuerst. Für eine zunächst eindeutige Zuordnung Tiefe 1 verwenden.

```sh
# Beispiel auf fuzzy (Rank 0). lab enthält die vorhandene isolierte Laufzeit.
lab=/home/sn/tbv/profile-lab
out="$lab/fuzzy-10304"
sudo python3 tools/tbv-profile-capture.py --out "$out" --timeout 120 -- \
  tools/lab-gpu-verbs-run.sh "$lab" \
  --device usb4_rdma0 --gid-index 1 --op WRITE_IMM --bytes 10304 \
  --depth 1 --iterations 128 --rounds 1 --spin-us 50 --persistent-rq \
  --profile /lab/fuzzy-10304/gpu.jsonl

# Auf misty analog; dessen Pfad heißt misty-10304. Zusätzlich:
# --host MANAGEMENT_IP_VON_FUZZY
```

Der `/lab/...`-Pfad im GPU-Test bezeichnet dieselbe neue Capture-Datei innerhalb
der vorhandenen Lab-Mounts. Bei einer normalen HIP-Installation statt des
Lab-Wrappers direkt das Testprogramm starten und einen gewöhnlichen absoluten
Pfad unter `--out` für `--profile` verwenden. Der Name `gpu.jsonl` erlaubt die
automatische gemeinsame Auswertung. Beide Pfade müssen neu sein.

`--system-events` ergänzt Scheduler-, IRQ-, SoftIRQ- und Workqueue-Ereignisse.
Damit steigen Datenmenge und Beobachtungsaufwand. `--buffer-kb` setzt den
begrenzten Ringpuffer **je CPU** (Standard 2048 KiB). Für kurze Diagnosefälle
beginnen; bei Overruns den Fall kürzen oder bewusst den Puffer vergrößern.

Gespeichert werden `kernel.trace`, `gpu.jsonl`, stdout/stderr und `capture.json`
mit Kernel/Boot-ID, Modul-Build-ID, Parametern, Buffer-Verluststatistik sowie
CPU-/Interrupt-/Treiberzustand vor und nach dem Lauf, soweit zugänglich.
Der GPU-Recorder begrenzt sich auf 200.000 Einträge und meldet Überläufe im
Footer. Die Dateiausgabe erfolgt nach den gemessenen Fällen. Beim nächsten Fall
kann sie den Peer trotzdem verzögern; für Vergleiche daher eine Größe/Operation
je Prozess verwenden.

Nach Erfolg, Fehler, Timeout, SIGTERM oder Ctrl-C deaktiviert der Runner seine
Events und entfernt die Instanz. Andere bestehende Trace-Nutzer behalten
ihren Zustand. Wenn ein Cleanup-Schritt fehlschlägt, steht das im Receipt und
der Lauf endet mit Fehler. Bei SIGKILL oder Rechnerausfall kann Python keinen
Cleanup ausführen: eine eventuell verbliebene `instances/tbv-perf-*`-Instanz
vor dem Produktionsbetrieb kontrollieren und ihre Events deaktivieren.

## Auswertung

Die Capture-Verzeichnisse beider Hosts auf einem Rechner zusammenführen:

```sh
python3 tools/tbv-profile-report.py \
  --capture fuzzy=PFAD_ZU_FUZZY_CAPTURE \
  --capture misty=PFAD_ZU_MISTY_CAPTURE \
  --out NEUES_REPORT_VERZEICHNIS --require-clean
```

Es entstehen `report.md`, `summary.json` und eine mit Perfetto/Chrome Trace
lesbare `timeline.json`. Die Verteilungen enthalten N, Mittelwert, P50, P95,
P99 und Maximum je Host/Fall/Phase. Rohdaten, Ausreißer und Fehler bleiben
erhalten. Ein fehlender Footer, verlorene Einträge, unvollständig erfasste TX-WRs, ein fehlgeschlagener Lauf,
geänderte Modulparameter oder Watchdog-/Fallback-/Retry-Ereignisse werden
sichtbar gemeldet; `--require-clean` liefert dafür Exitcode 2, schreibt die
Diagnose aber trotzdem.

Abgeleitete Größen:

- `tx_unattributed`: Teil des WR-Intervalls, den die Vereinigung der ausgewählten
  obersten TX-Phasen noch nicht abdeckt. Ein hoher Wert zeigt, wo feinere
  Instrumentierung fehlt. Verschachtelte Phasen werden nicht doppelt gezählt.
- `post_to_first_callback` und `dma_callback_span`: Posting-Beginn→erster
  Callback und erster→letzter Callback je Rail. Auch bei einem bereits während
  des Postings laufenden DMA sind die Intervalle gültig.
- `tx_callback_resume`: Letzter Batch-Callback→Rückkehr des TX-Wartens.
- `cq_visibility_delay`: CQ-Marker unmittelbar vor der Veröffentlichung→
  Abholung im Testprogramm, zugeordnet über lokale QP, WR-ID und WC-Opcode.
  Enthält die kleine Veröffentlichungsarbeit sowie beim persistenten RQ die
  Wiederbereitstellung der WQE. Übersetzte generische Receive-IDs lassen sich
  nicht immer exakt zuordnen; hierfür wird keine künstliche Zeit berechnet.

CPU- und Kernel-Nanosekunden stammen auf **demselben Host** aus derselben
monotonen Uhr. Jeder Host/Lauf erhält in der Timeline einen eigenen Ursprung;
die zwei Uhren werden nicht voneinander abgezogen. Token/QPs/PSN/Offset dienen
zur logischen Zuordnung zwischen den Hosts. GPU-Events liefern eigenständige
Dauern und werden nicht auf die CPU-Timeline gelegt. Systemereignisse behalten
die geringere Präzision des Ftrace-Textpräfixes.

Queue, Job, Batch, Bau und deren Unterphasen überlappen. Auch RX-Vorbereitung
ist bereits in der senderseitigen PREPARE-Wartezeit enthalten. Die Tabellen
dürfen deshalb nicht zu einer scheinbaren Gesamtlatenz addiert werden.
Zeitintervalle zeigen verstrichene Zeit einschließlich Descheduling. Prozess-
CPU umfasst nicht die Kernel-Worker oder Interrupts auf anderen CPUs.

## Vorgehen bei den ersten Hardwareläufen

1. Beide Builds mit/ohne Detailinstrumentierung im Nix-Container bzw. gegen
   den gebooteten Kernel bauen. Off/On zuerst mit kurzen vollständig
   verifizierten WRITE_IMM-Fällen auf beiden Hosts prüfen.
2. Je 10304 B, 1 MiB und 20 MiB+64 B bei Tiefe 1 profilieren. Die erste
   Iteration einschließlich `mr_attach_index` als kalten Start getrennt von
   den späteren Iterationen betrachten. Rohdaten vollständig behalten.
3. Den normalen Modus `dmabuf_verbs=0` mit Host-MR und den direkten Modus
   mit DMA-BUF jeweils unter identischen Testoptionen erfassen. Danach Tiefe 3
   messen, um Queue-Serialisierung und Überlappung zu erkennen. DONE und
   Rail-Zahl zwischen Kandidaten explizit im Receipt vergleichen.
4. Pro Experiment genau einen durch die Timeline belegten Engpass bearbeiten.
   Mögliche erste Kandidaten: Frame-Posting/Allokation, NHI-Worker-Verzug,
   RX-Reaping, PREPARE/DONE oder die GPU-Release-Synchronisation.
5. Den Effekt anschließend ohne `--profile`, ohne Capture-Runner und mit
   `dmabuf_profile=0` in mehreren vollständigen ABBA-Blöcken prüfen. Gleicher
   Code-/Modulstand, Pufferfluss, Rail-/Linkzustand, Tiefe und CQ-Spinbudget;
   jedes Byte weiter verifizieren. Veränderte IRQ-/Watchdog-Zustände separat
   untersuchen, nicht als schnelle oder langsame Transportmessung mitteln.

Der Host-MR-Kontrolltest verwendet explizite HIP-Kopien. Gufos normaler Adapter
verwendet GPU-gemappte Hostfenster und andere Staging-/Summen-Kernels. Daher
qualifizieren diese Messungen keinen Gufo-Speedup. Die gemeinsamen Kernel-
Tracepoints funktionieren auch unter Gufos normalem Verbs-Adapter; die neuen
GPU-Marker gehören bisher zum Transporttest. Die DMA-BUF-Verbs-Integration
und derselbe Profiling-Vergleich im echten Gufo-Produzenten/Konsumenten sowie
unter Modelllast bleiben der nächste Anwendungsschritt.
