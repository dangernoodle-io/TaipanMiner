# TaipanMiner WebUI

Modern Svelte + Vite frontend for the TaipanMiner dashboard, replacing the legacy HTML/CSS/JS UI in `components/taipan_web/`. This is a new replacement SPA shipped as part of the `jae/ui-redesign` branch.

## Quick Start

```bash
cd webui
npm install
cp .env.example .env
```

Edit `.env` to point at your miner:
```
VITE_MINER_URL=http://bitaxe-403-1.local
```

Then:
```bash
npm run dev
```

Visit `http://localhost:5173` in your browser. The dev server fetches live stats from the miner's HTTP API (CORS is pre-configured to allow `*` origin).

## Build

```bash
npm run build
```

Output goes to `dist/`. This bundle can be embedded into the firmware's `/` endpoint to replace the legacy UI.

## Development→Firmware

When ready to ship:

1. Run `npm run build` to generate optimized assets
2. Convert the `dist/` files into C byte arrays (using breadboard's `bb_embed_assets()` CMake helper, similar to the current `components/taipan_web/` setup)
3. Update the firmware's `/` route handler to serve the new UI
4. Update CI/release workflows to rebuild the firmware

For now, this runs standalone against any TaipanMiner device that exposes its `/api/*` endpoints over HTTP.

## Scripts

- `npm run dev` — dev server on port 5173
- `npm run build` — optimize for production
- `npm run preview` — local preview of production build
- `npm run check` — TypeScript check
