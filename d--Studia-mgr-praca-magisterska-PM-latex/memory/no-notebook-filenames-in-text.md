---
name: no-notebook-filenames-in-text
description: W tekście pracy nie wskazywać konkretnych nazw plików notebooków (ML_training.ipynb itp.) przy opisie modeli uczenia maszynowego
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 12c681df-546e-46e9-aab4-666658a2096e
---

Opisując proces przygotowania danych, trening czy eksport modeli uczenia maszynowego w pracy, nie wolno przywoływać konkretnych nazw plików Jupyter Notebook z [project/](../../../../../../d/Studia/mgr/praca%20magisterska/PM_latex/project) (np. `ML_training.ipynb`, `ML_driving_style.ipynb`, `ML_noneco.ipynb`). Opisywać proces i modele merytorycznie (np. "wytrenowano i porównano dwa warianty modelu", "w ramach przygotowania danych uczących"), bez odsyłania do nazw plików źródłowych.

**Why:** Użytkownik wprost zastrzegł to jako zasadę obowiązującą przy pisaniu o modelach ML w rozdziale Projekt — nazwy notebooków to szczegół implementacyjny/roboczy, nieodpowiedni dla tekstu pracy dyplomowej.

**How to apply:** Dotyczy całego rozdziału Projekt (i dalszych), zwłaszcza podrozdziałów o przygotowaniu danych i treningu modelu ([[no-invented-rationale]] — osobna, ale pokrewna zasada o niezmyślaniu uzasadnień). Jeśli trzeba odróżnić dwa warianty/etapy prac nad modelem, opisywać je opisowo (np. "pierwsze podejście" / "podejście kanoniczne", "wariant oparty na regułach kinematycznych" / "wariant oparty na zużyciu paliwa"), nie nazwą pliku.
