# Interface fonts

Loaded by file at run time by `MarburgumUI::Font` (`Source/MarburgumIIClient/Private/UI/MarburgumUITheme.cpp`) and staged
with the content (`+DirectoriesToAlwaysStageAsUFS=(Path="Marburgum/Fonts")` in `Config/DefaultGame.ini`). Unmodified
files; the SIL Open Font License 1.1 allows bundling them with software, and its text travels with them here.

| File | Family | Version | Source | Licence |
| --- | --- | --- | --- | --- |
| FiraSans-Light/Regular/Medium/SemiBold/Bold/Italic.ttf | Fira Sans | 4.301 | https://github.com/mozilla/Fira (Arch `ttf-fira-sans` 1:4.301-3) | OFL 1.1, Reserved Font Name "Fira" — `OFL-FiraSans.txt` |
| FiraSansCondensed-SemiBold.ttf | Fira Sans Condensed | 4.301 | as above | OFL 1.1 — `OFL-FiraSans.txt` |
| NotoSerif-Regular/Italic.ttf | Noto Serif | 2.015 | https://github.com/notofonts/latin-greek-cyrillic (Arch `noto-fonts` 1:2026.11.01-1) | OFL 1.1 — `OFL-NotoSerif.txt` |

Both families cover Latin Extended-A, so Slovene č, š, ž and their capitals render without fallback. When a file is
missing the theme falls back to the engine's Roboto and logs `MB2UI role=client event=font_fallback`.
