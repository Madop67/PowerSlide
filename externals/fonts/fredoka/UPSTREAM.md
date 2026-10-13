# Upstream provenance

- Font: Fredoka <https://github.com/hafontia/Fredoka-One>
- Source file: `ofl/fredoka/Fredoka[wdth,wght].ttf` from <https://github.com/google/fonts>
- Source SHA-256:
  `2ba02e68b152868aef9ba28e24b3648c7d457fe6f25c761f2c2c53fb61a73fc8`
- License: SIL Open Font License 1.1 (`OFL.txt`); no Reserved Font Name is declared.

`Fredoka-SemiBold.ttf` is a static instance of the variable font, made with
fontTools 4.55.3:

```text
fonttools varLib.instancer "Fredoka[wdth,wght].ttf" wght=600 wdth=100 -o Fredoka-SemiBold.ttf
```

Result SHA-256:
`ef0f4e6827504f128ce1d078937d9cc8572e559b1ec80c3186e1a476822c7053`

Dear ImGui's TrueType loader reads only a variable font's default instance
(Light 300), so the settings UI embeds this static SemiBold cut instead.
CMake embeds it into the executable at configure time.
