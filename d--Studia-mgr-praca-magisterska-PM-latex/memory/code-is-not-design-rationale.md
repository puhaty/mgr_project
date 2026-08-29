---
name: code-is-not-design-rationale
description: "Nigdy nie piszę w pracy 'przyjęty w kodzie' ani nie odnoszę się wprost do kodu jako źródła; odnoszę się do projektu ogólnie, pomijam źródło, albo pytam usera"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 12c681df-546e-46e9-aab4-666658a2096e
---

Gdy zachowanie systemu opisane w kodzie (nawet z komentarzem tłumaczącym "dlaczego") nie pasuje albo tylko częściowo pasuje do deklarowanej w pracy filozofii projektu (np. nudging — pozytywne wzmocnienie zamiast kar/ostrzeżeń), nie cytuję komentarza z kodu jako źródła założenia projektowego pracy. Kod może mieć niedociągnięcia — praca ma opisywać zamysł/cel w sposób spójny i "idealny", nie być zapisem 1:1 implementacji.

**Why:** Napisałem, że asymetria punktacji (kara za agresywną jazdę większa niż nagroda za eco) wynika "z udokumentowanego w kodzie założenia" nawiązującego do loss aversion — user zwrócił uwagę, że to gryzie się z zasadami nudgingu opisanymi w Celu Pracy, i wprost zastrzegł: "kod nie jest założeniem! jak masz się odnieść do kodu, jako wzoru, to lepiej zapytaj mnie". Doprecyzowuje to [[no-invented-rationale]] w drugą stronę: tam chodziło o niezmyślanie uzasadnień, tu — o nietraktowanie komentarza w kodzie jako automatycznie ważnego uzasadnienia do pracy, zwłaszcza gdy dotyczy filozofii/psychologii projektu (nudging, motywacja), a nie czystego faktu inżynierskiego (np. redukcja rozmiaru pliku — to wciąż OK cytować, bo jest neutralne/mierzalne).

Powtórka incydentu (koszt jednostkowy zdarzeń eksploatacyjnych, podrozdział o statystykach oszczędności): napisałem "przyjęty w kodzie, wprost oznaczony jako założenie, a nie pomiar" — user znowu to odrzucił i doprecyzował zasadę szerzej: **w ogóle nie pisać "przyjęty w kodzie" ani wprost odnosić się do kodu jako źródła** w tekście pracy. Zamiast tego: albo odnieść się do "projektu" ogólnie (bez słowa "kod"), albo w ogóle nie wskazywać źródła, albo zapytać usera o właściwe sformułowanie/uzasadnienie.

**How to apply:** Nie używać w tekście pracy słowa "kod" jako podmiotu/źródła stwierdzenia (frazy typu "przyjęty w kodzie", "udokumentowane w kodzie", "kod zakłada") — nawet przy neutralnych, inżynierskich faktach. Zamiast tego: (1) sformułować fakt bezosobowo/jako cechę projektu ("przyjęto koszt...", "w projekcie założono...", "system nalicza..."), (2) jeśli podstawa faktu wymaga wyjaśnienia (np. skąd wzięła się dana wartość) i nie jest jasna z Celu Pracy/Wymagań — zapytać usera zamiast zgadywać albo powoływać się na kod. Przy opisie mechanizmów UX/motywacyjnych opierać narrację na już zaakceptowanych założeniach z rozdziału Cel Pracy / wymagań (np. "zgodnie z zasadą nudgingu"). Fakty liczbowe/techniczne nadal można podawać wprost, po prostu bez frazy "w kodzie".
