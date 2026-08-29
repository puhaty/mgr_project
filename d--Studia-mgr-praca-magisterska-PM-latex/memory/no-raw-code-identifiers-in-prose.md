---
name: no-raw-code-identifiers-in-prose
description: W tekście pracy nie wypisywać nazw zmiennych/parametrów z kodu poza tabelami; wartości liczbowe i nazwy narzędzi/algorytmów są OK
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 12c681df-546e-46e9-aab4-666658a2096e
---

W prozie rozdziału Projekt nie odwoływać się do surowych nazw zmiennych, parametrów czy plików z kodu/notebooków (np. `n_estimators`, `max_features="sqrt"`, `fuel_cons`, `l_per_100km`, `driving_style_model.h`, `gen_params.py`) — dla czytelnika pracy, który nie zna kodu, taka nazwa nic nie znaczy. Zamiast tego opisywać znaczenie słownie, z zachowaniem wartości liczbowych.

**Doprecyzowane wyjątki (co jest OK):**
- Identyfikator dozwolony w prozie, jeśli jest **jasno nazwany w pobliskiej tabeli** — wtedy w tekście dodać odnośnik `tabela~\ref{...}` zamiast rozwlekle tłumaczyć (np. `a_mean`/`abs_a_mean` odwołane do tabeli cech).
- **Wartości liczbowe i szesnastkowe stałe** (np. `0xFF`, `0xFFFF`, konkretne liczby) można podawać wprost — to nie są "zmienne", tylko dane.
- **Nazwy narzędzi/algorytmów/klas modeli** (np. `ExtraTreesClassifier`, `RandomForestClassifier`, `emlearn`, `MinMaxScaler`, `GroupShuffleSplit`, `GroupKFold`) są akceptowalne — to nazwy własne metod, analogicznie do cytowania nazw bibliotek w literaturze.
- Jeśli zestaw parametrów/wartości jest długi (hiperparametry modelu), **zrobić z tego tabelę** zamiast wyliczać je zdanie po zdaniu w prozie.

**Why:** User poprawił kilka fragментów (3.3 CAN listen-only mode, 3.4 fuel_cons/l_per_100km, 3.5 hiperparametry ExtraTrees/RandomForest, eksport do C) — usuwając gołe nazwy zmiennych z prozy, ale zachowując liczby, nazwy narzędzi i tabelaryczne odwołania. Kluczowy cytat: "nie odnoś się za bardzo do zmiennych w kodzie, bo to nic nie mówi przy czytaniu projektu, chyba że w jakiejś tabeli, gdzie jest to jasno nazwane" oraz przy poprawkach: "wartości liczbowe możesz podawać", "tutaj nazwy modeli są ok, tylko parametry z notebooka słabe", "tutaj też dobrze tabelę zastosować, a nie wypisywać wszystko".

**How to apply:** Dotyczy całego rozdziału Projekt (i dalszych, np. 3.6 implementacja). Przy opisie kodu/notebooków: pisać co robi dana rzecz i jakie ma wartości, bez podawania identyfikatora, chyba że pasuje jeden z wyjątków powyżej. Powiązane: [[no-notebook-filenames-in-text]] (nie nazywać plików .ipynb), [[no-invented-rationale]] (nie zmyślać uzasadnień).
