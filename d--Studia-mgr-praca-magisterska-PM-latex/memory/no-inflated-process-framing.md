---
name: no-inflated-process-framing
description: "Nie przedstawiać liniowego/jednorazowego procesu prac jako automatycznego, samopodtrzymującego się cyklu; oddzielać historię budowy systemu od stanu pracy urządzenia docelowego"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: a34e4c68-1b77-4721-8ac8-18022eeb3c32
---

Opisując w pracy relację między etapami prac (np. akwizycja danych → trening → wdrożenie) albo zachowanie działającego urządzenia, nie nazywać tego "cyklem" ani nie sugerować automatycznego powtarzania, jeśli w rzeczywistości był to liniowy proces (ewentualnie z 1-2 ręcznymi iteracjami) i/lub "kolejna iteracja" oznacza tylko możliwość przyszłego, osobno przeprowadzanego treningu offline — a nie mechanizm wbudowany w system.

**Why:** Przy opisie architektury systemu (rozdz. Projekt, rys. "cykl-faz") padło sformułowanie, że fazy akwizycji/treningu/eksploatacji "nie są rozłączne w czasie i tworzą cykl" z automatyczną pętlą powrotną. User zwrócił uwagę, że to się kupy nie trzyma: sam proces budowy systemu był w gruncie rzeczy liniowy (zbierz dane → trenuj → wdróż → testuj), a jedyny prawdziwy "cykl" w działającym urządzeniu to współbieżność dwóch funkcji fazy eksploatacji (ocena stylu jazdy w czasie rzeczywistym + równoległa rejestracja danych), a nie automatyczne przechodzenie między fazami.

**How to apply:** Przy opisie przepływu prac/danych w rozdziale Projekt: (1) opisując historię/rozwój systemu — używać neutralnych określeń ("kolejne etapy", "sekwencja") zamiast "cykl"/"pętla", chyba że proces faktycznie się automatycznie powtarza; jeśli była więcej niż jedna iteracja (np. trening dwóch wariantów modelu i wybór lepszego — [[no-invented-rationale]] dot. sprawdzania takich faktów w kodzie/notebookach), opisać to jako konkretną, policzalną liczbę kroków, nie jako nieokreślony "cykl"; (2) opisując zachowanie urządzenia docelowego — skupić się na tym, co faktycznie dzieje się równolegle/w czasie rzeczywistym, a możliwość przyszłego dotrenowania modelu nazywać wprost możliwością/materiałem do doskonalenia, nie zapętloną automatyką. Dotyczy całego rozdziału Projekt, nie tylko rys. cykl-faz.
