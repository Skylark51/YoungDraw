# JACS publication workflow

YoungDraw provides **File > JACS publication SVG** in addition to its original
SVG export. Select **Yes** for a 240 pt single-column figure or **No** for
a 504 pt double-column figure. The SVG keeps its vector viewBox and receives
the selected physical width with a proportional height.

The preflight reviews the **final printed dimensions**:

- Overall height must not exceed 660 pt (including the caption allowance).
- Effective text size must be at least 4.5 pt.
- Effective stroke width must be at least 0.5 pt.
- Embedded raster images require separate source-image DPI verification:
  300 dpi color, 600 dpi grayscale, 1200 dpi black-and-white line art.

Warnings do not silently mutate the drawing. Exporting despite warnings
requires an explicit acknowledgement. The legacy SVG export is unchanged.
This is a technical preflight, **not a guarantee of editorial acceptance**.
Place figure labels/captions and perform visual verification at the final
size before submission.

Source: [JACS Author Guidelines, Appendix 2](https://researcher-resources.acs.org/publish/author_guidelines?coden=jacs)
(accessed 2026-11-02).

## Data integrity

- Aromatic pyridine-like N and carbon, charged N/O, and dative metal
  coordination receive separate valence handling. Ambiguous metal oxidation
  states and nonstandard bonding still require chemical review.
- Graph table edits require 2–100 state rows. Blank labels and energy cells remain
  allowed to represent omitted points/paths.
- Native/CDXML/SVG saves use unique, adjacent temporary files, flush all
  content, then replace the destination. An abandoned old .youngdraw.tmp
  file no longer blocks a subsequent save.

## Verification

GitHub Actions builds Windows Release and runs CTest, including checks for
valence, graph data, publication SVG output, and atomic file replacement.
`YoungDraw.exe` is refreshed on main only after passing these checks.
