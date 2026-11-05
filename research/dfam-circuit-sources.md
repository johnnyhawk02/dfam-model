# DFAM circuit sources for a component-level model

_Compiled 2 Nov 2026_

## Bottom line

Moog has **not published a DFAM schematic**, and I found no public reverse-engineered one or open DIY clone PCB. The practical route is to model DFAM's blocks from Moog's closest published circuit (the Werkstatt-01, same company, same 2010s design era), the large Moog-ladder modeling literature, and measurements of a real DFAM if you have one.

## 1. Closest official schematic: Moog Werkstatt-01 (2014)

- PDF (8 sheets, Moog drawing BRD-10-011-1111 Rev A1): https://cdn.sparkfun.com/datasheets/Kits/Werkstatt_01_Schematics.pdf
- Context and walkthrough: https://news.sparkfun.com/1617
- Licence: Moog copyright, personal non-commercial copy only. Fine for study; don't redistribute.

What's on it (read from the PDF's text layer, so verify against the drawing):
- **VCO (sheet 3):** op-amp and comparator core (TL074, LM393), CD4066 switch, PMP4201 matched pair for the expo converter. Triangle/pulse outputs, PWM.
- **VCF (sheet 4):** transistor ladder built from five PMP4201 matched pairs, NE5532 op-amps, resonance feedback. This is the closest documented relative of DFAM's ladder.
- **VCA:** LM13700 OTA section.
- **EG (sheet 6):** TL074-based attack/decay with diodes and 1 µF/2.2 µF timing caps.

## 2. DFAM itself: what is known

- Block diagram and control ranges come from the manual: https://usermanual.wiki/Pdf/DFAMManual.124050448/html. Two VCOs (triangle/square), VCO 1→2 FM and hard sync, noise/external mixer, ladder VCF with LP/HP switch and resonance, decay envelopes for pitch/VCF/VCA (VCA attack fast/slow), 8-step pitch/velocity sequencer.
- The patch-bay voltages in the manual give useful ground truth for levels and CV scaling.
- Forum threads with PCB photos and component talk (ModWiggler blocks automated reading, open them in a browser):
  - "An Inside Look at the Moog DFAM": https://www.modwiggler.com/forum/viewtopic.php?t=191120
  - "Damn. DFAM." megathread: https://www.modwiggler.com/forum/viewtopic.php?t=190156
- Behringer Edge is a functional DFAM-style clone, but no public schematic either: https://synthanatomy.com/tag/moog-dfam-clone
- Existing software take, for behaviour comparison only: "Shoebird's DFAM" for VCV Rack: https://community.vcvrack.com/t/shoebirds-dfam/16810

## 3. Modeling literature

Ladder filter:
- Huovilainen, "Non-Linear Digital Implementation of the Moog Ladder Filter", DAFx 2004: https://dafx.de/paper-archive/2004/P_061.PDF
- D'Angelo & Välimäki, improved VA ladder model (ICASSP 2013) and related Aalto work: https://aaltodoc.aalto.fi/server/api/core/bitstreams/f87d5b89-72b1-4b8a-85bd-e8ef9511937d/content
- D'Angelo thesis, "Virtual Analog Modeling of Nonlinear Musical Circuits": https://aaltodoc.aalto.fi/items/08b88f1e-4cc4-4dee-a53f-8aa63c5c5da4
- Raph Levien's ladder notes: https://levien.com/ladder.pdf
- DAFX book, ch. 12 (Virtual analog): https://dafx.de/DAFX_Book_Page_2nd_edition/chapter12.html
- Välimäki overview: https://www.musica.ed.ac.uk/wp-content/uploads/Manual_Upload/Valimaki-Virtual-analog.pdf

General approach to look up next: Zavalishin, *The Art of VA Filter Design* (zero-delay feedback); wave digital filters (Werner, Smith); nodal/state-space DK method (Yeh, Holters & Zölzer).

## 4. Suggested plan

1. Use DFAM's manual for the block diagram and CV scaling.
2. Model each block from the Werkstatt-01 equivalent: expo converter + triangle core VCO, 4-pole transistor ladder (with an HP tap/variant for DFAM's HP mode), OTA VCA, op-amp/diode decay envelopes.
3. Choose the depth per block: full nodal/WDF for the ladder (where the character lives), lighter behavioural models for VCO and envelopes.
4. Tune against recordings of a real DFAM (single-step hits with each control swept), since there's no schematic to match component values to.
