
• Anche su Kiwipete il fallback e' sotto l'1%, mentre S64 sale a ~11.8 Mn/s. Il prossimo lavoro utile e' quindi incrementale: aggiornare solo righe/zone dirty invece di rifare tutta close. Raccolgo i riferimenti delle
modifiche.Anche su Kiwipete il fallback resta sotto l'1%. Il prossimo taglio deve quindi essere incrementale/localizzato, non eliminare il fallback. Raccolgo riferimenti e chiudo con raccomandazione tecnica.

• Explored
└ Read state64.h, state64.c, main.c

• Ran git status --short
└  M README.md
?? .idea/
… +4 lines (ctrl + t to view transcript)
?? src/
?? tests/

• Updated Plan
└ ✔ Aggiungere statistiche fallback S64
✔ Esporre benchmark diagnostico leggero
✔ Eseguire misure su startpos/FEN critiche
✔ Scegliere prossimo collo di bottiglia

────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

• Ho aggiunto diagnostica sul fallback S64 e l’ho collegata al benchmark.

Nuova struct in src/state64.h:20:

typedef struct {
unsigned long long candidates;
unsigned long long certified;
unsigned long long fallback;
unsigned long long fallback_rejected;
} State64Stats;

API:

void state64_stats_reset(void);
State64Stats state64_stats_get(void);

Il conteggio avviene in state64_mark_if_legal(): se il contesto PIN/CHECK/EVASION/enemy_attack/EP certifica la mossa, conta come certified; altrimenti passa da fallback.

Ho aggiunto la colonna s64 fb% al benchmark in src/main.c:21.

Risultato startpos depth 5:

depth  nodes      s64 Mn/s   s64 fb%
1      20         7.70       0.00
2      400        1.32       0.00
3      8902       3.26       0.00
4      197281     4.82       0.22
5      4865609    8.73       0.30

Su Kiwipete depth 4:

depth  nodes      s64 Mn/s   s64 fb%
1      40         16.12      0.00
2      1731       4.88       0.06
3      69287      7.06       0.65
4      2919688    11.79      0.54

Questo è il dato importante: il fallback è già praticamente irrilevante. Siamo sotto l’1% anche su una posizione tattica con arrocco, pin e pezzi attivi.

Quindi il prossimo collo di bottiglia non è più “eliminare fallback”. È la ricostruzione completa della chiusura a ogni nodo:

State64 -> close completa -> tutte le righe/layer

La direzione giusta ora è incrementale:

apply move
mark dirty rows/rays
recompute only affected GEOM/CLEAR/ATTACK/LEGAL

In pratica: non più state64_close(s) globale dopo ogni mossa, ma qualcosa tipo:

state64_close_delta(s, from, to, captured_sq)

Le zone sporche iniziali sono:

from
to
captured square, se presente
rays attraversanti from/to/captured
king neighborhoods
EP/castling dependent squares

I numeri dicono che l’idea autoindotta ormai funziona; ora serve renderla locale.
