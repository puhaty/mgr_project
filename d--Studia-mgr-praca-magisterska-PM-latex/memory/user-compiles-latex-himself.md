---
name: user-compiles-latex-himself
description: Nie uruchamiać kompilacji LaTeX — użytkownik sam kompiluje pracę i zgłasza wynik
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 12c681df-546e-46e9-aab4-666658a2096e
---

Użytkownik sam kompiluje pracę magisterską (LaTeX) i informuje o wyniku; odrzucił próbę uruchomienia builda (SConstruct) przez agenta.

**Why:** Kompilacja po mojej stronie jest zbędna i niechciana — użytkownik ma własny workflow kompilacji i weryfikuje PDF u siebie.

**How to apply:** Po edycji plików .tex nie uruchamiać scons/pdflatex/latexmk; poprosić użytkownika o kompilację albo po prostu zakończyć i czekać na jego feedback.
