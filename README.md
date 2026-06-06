# Connect-4 WASM starter package

This package is the updated starter for turning your C++ Connect-4 engine into a GitHub Pages-hosted WebAssembly analysis app.

## Files included

- `docs/index.html` — browser UI
- `src/bindings.cpp` — exported wrapper layer
- `build-wasm.sh` — Emscripten build script
- `README.md` — setup instructions

## Add your source files

Copy these files from your engine into `src/`:

- `engine.cpp`
- `engine.h`
- `bitboard.h`
- `evaluator.h`
- `tt.h`

Do not include `main.cpp`, `uci.cpp`, or `uci.h` in the first WASM build.

## Build

```bash
chmod +x build-wasm.sh
./build-wasm.sh
```

This generates:

- `docs/engine.js`
- `docs/engine.wasm`

## Run locally

Use a local server because browsers often block direct `.wasm` loading from `file://`.

```bash
python -m http.server 8000
```

Then open:

```text
http://localhost:8000/docs/
```

## GitHub Pages deploy

1. Push the project to GitHub.
2. Go to Settings → Pages.
3. Choose deploy from branch.
4. Select `main` and `/docs`.
5. Save.

## Important note

If compile errors happen, they will most likely come from your current source files rather than the wrapper or HTML shell. In that case, fix the C++ compile issues first, then rebuild.
