---
name: diagrams-tikz-self-made
description: Schematy/diagramy generuję sam w TikZ (tex/fig/*.tikz); zdjęcia i zrzuty ekranu dostarcza użytkownik
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 12c681df-546e-46e9-aab4-666658a2096e
---

Podział pracy przy rysunkach w pracy magisterskiej: schematy blokowe, diagramy i wykresy koncepcyjne generuję sam w TikZ; użytkownik dostarcza tylko zdjęcia (sprzęt, montaż) i zrzuty ekranu UI.

**Why:** Użytkownik chciał edytowalnych schematów (pytał o mermaid/PlantUML); wybraliśmy TikZ — wektorowy, spójny typograficznie z pracą, edytowalny w źródle, bez zewnętrznego renderowania. Doprecyzowuje to zapis w CLAUDE.md "o rysunki pytaj, będę dawał", który dotyczy tylko zdjęć/zrzutów.

**How to apply:** Źródła TikZ jako osobne pliki w `tex/fig/*.tikz`, włączane przez `\input` w figure; pakiet tikz + biblioteki (arrows.meta, backgrounds, calc, fit, positioning) są już w preambule main.tex. Bitmapy od użytkownika trafiają do `tex/img/` (\graphicspath). Zob. [[user-compiles-latex-himself]].
