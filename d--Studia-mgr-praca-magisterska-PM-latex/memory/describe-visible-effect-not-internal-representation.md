---
name: describe-visible-effect-not-internal-representation
description: "Opisuję to, co kierowca faktycznie widzi/doświadcza (np. rosnące drzewo), nie wewnętrzną liczbową reprezentację, która istnieje tylko w kodzie"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 12c681df-546e-46e9-aab4-666658a2096e
---

Gdy system wewnętrznie liczy coś jako surową wartość (np. wskaźnik punktowy 0--1000), ale użytkownik/kierowca nigdy tej liczby nie widzi — bo jest ona przetworzona na wizualizację (rosnące drzewo, sad) — praca ma skupiać się na tym, co kierowca faktycznie widzi/doświadcza, a wewnętrzny mechanizm liczbowy wspomnieć zdawkowo, bez zakresu/wartości startowej itp.

**Why:** Napisałem w 3.6.2, że "wynik klasyfikacji przekładany jest na wskaźnik punktowy w zakresie 0--1000, rozpoczynający się od wartości środkowej" — user zwrócił uwagę, że ten zakres istnieje tylko w kodzie i w pracy nie powinniśmy się do niego odnosić; można wspomnieć, że tak to jest zrobione, ale efektem końcowym i tym, co ma być opisane, jest wizualizacja (rosnące drzewo/sad).

**How to apply:** Dotyczy całej pracy, nie tylko tego fragmentu — przy opisie dowolnego mechanizmu, gdzie kod utrzymuje surową liczbę/stan wewnętrzny nieprezentowany wprost użytkownikowi, priorytet ma opis widocznego efektu (UI, zachowanie urządzenia), a nie liczbowej reprezentacji z kodu. Powiązane: [[no-raw-code-identifiers-in-prose]] (nie wypisywać identyfikatorów), [[code-is-not-design-rationale]] (kod nie jest założeniem), [[scope-limited-to-cel-i-wymagania]].
