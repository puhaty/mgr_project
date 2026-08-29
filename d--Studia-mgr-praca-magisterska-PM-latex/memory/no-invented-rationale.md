---
name: no-invented-rationale
description: W tekście pracy nie wolno zmyślać uzasadnień/metodologii za decyzjami widocznymi tylko w kodzie
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 12c681df-546e-46e9-aab4-666658a2096e
---

Gdy w kodzie widać tylko wynik decyzji (np. `can_app_init(50)` — magistrala CAN skonfigurowana na 50 kbit/s), nie wolno dopisywać w tekście pracy wymyślonego uzasadnienia typu "dobrane eksperymentalnie" czy "odpowiadające pojazdowi testowemu", jeśli nie ma na to żadnego dowodu (komentarza w kodzie, notatki, potwierdzenia użytkownika).

**Why:** Użytkownik ostro zareagował ("jak eksperymentalnie????") na wymyśloną metodologię przy opisie prędkości magistrali CAN w rozdziale Projekt — sam fakt (wartość liczbowa) był prawdziwy, ale narracja "dlaczego" była zmyślona. To fabrykacja w pracy dyplomowej, którą promotor może łatwo zdemaskować pytaniem.

**How to apply:** Opisując decyzje projektowe/implementacyjne widoczne w [project/](../../../../../../d/Studia/mgr/praca%20magisterska/PM_latex/project) (parametry, stałe, wybory konfiguracyjne, wybór podzespołów/modułu sprzętowego), podawać tylko sam fakt/wartość bez domysłów co do "dlaczego akurat tak" ani jakie to przyniosło korzyści (np. niezawodność, odporność na wibracje/temperaturę, skrócenie czasu uruchomienia). Jeśli uzasadnienie jest istotne dla tekstu, najpierw zapytać użytkownika (np. przez AskUserQuestion) zamiast zgadywać. Powtórzony incydent (rozdz. Projekt, opis modułu ESP32-S3-Touch-LCD-4.3B): dopisałem niepotwierdzone korzyści z użycia gotowego modułu zamiast dyskretnych podzespołów — user zareagował "nie pisz takich głupot". Trzeci incydent (walidacja sygnałów CAN, wartość 0xFF): zamiast napisać wprost "wartość 0xFF oznacza brak danych", napisałem "producent pojazdu koduje brak wiarygodnej wartości... rezerwując dla niej wartość maksymalną" — przypisując intencję/metodologię producentowi pojazdu, czego nie da się zweryfikować z kodu. Poprawka: opisywać znaczenie wartości sentinel jako fakt techniczny, bez przypisywania decyzji konkretnemu podmiotowi zewnętrznemu (producentowi, autorowi biblioteki itp.), chyba że jest to udokumentowane źródłem. Reguła dotyczy całej pracy, nie tylko fragmentu o CAN czy o module ESP32.
