PIO ?= pio

.PHONY: help check test coverage build clean monitor compile-db lsp-% webui

help: ## Show available targets
	@grep -E '^[a-zA-Z_%-]+:.*##' $(MAKEFILE_LIST) | sort | \
		awk 'BEGIN {FS = ":.*## "}; {printf "  \033[36m%-20s\033[0m %s\n", $$1, $$2}'

webui: ## Build web UIs (miner + provisioning SPAs) into webui/{miner,prov}/dist/
	cd webui && pnpm install --frozen-lockfile && pnpm --filter miner build && pnpm --filter prov build

check: ## Static analysis (cppcheck) for all default envs
	$(PIO) check --skip-packages

check-%: ## Static analysis for one env (e.g. make check-bitaxe-601)
	$(PIO) check --skip-packages -e $*

test: ## Run host unit tests (ASIC and non-ASIC envs)
	$(PIO) test -e native
	$(PIO) test -e native-noasic

coverage: test ## Coverage report (gcovr)
	# Exclude the vendored breadboard (.breadboard) — bb covers its own code; counting it
# here couples TM's gate to bb and makes every bb pin bump perturb TM coverage.
	gcovr --root . --filter 'components/' --exclude '\.breadboard' --print-summary --coveralls gcovr-coveralls.json

build: ## Build default envs (tdongle-s3 + bitaxe-601)
	$(PIO) run

# Force-regenerate the per-board sdkconfig from committed deltas whenever those
# inputs change. ESP-IDF loads the existing generated sdkconfig.<board> as its
# base and only fills in defaults for ABSENT symbols, so a drifted value — even
# "# CONFIG_X is not set" — silently persists across every rebuild. (This stranded
# the S2 for weeks: its generated config had bb_event autoregister stale-off.)
# Deleting the generated file forces a clean rebuild from sdkconfig.defaults +
# sdkconfig/<board>. mtime-gated: only deletes when an input is newer, so normal
# incremental builds stay fast.
sdkconfig.%: sdkconfig.defaults sdkconfig/%
	rm -f $@

build-%: sdkconfig.% ## Build specific env (e.g. make build-tdongle-s3)
	$(PIO) run -e $*

compile-db: ## Generate compile_commands.json for all boards (clangd LSP)
	$(PIO) run -t compiledb -e bitaxe-601
	$(PIO) run -t compiledb -e bitaxe-403
	$(PIO) run -t compiledb -e bitaxe-650
	$(PIO) run -t compiledb -e tdongle-s3
	$(PIO) run -t compiledb -e esp32-s2-mini
	$(PIO) run -t compiledb -e esp32-c3-supermini
	$(PIO) run -t compiledb -e esp32-wroom32-cyd

lsp-%: ## Switch clangd to <env> (e.g. make lsp-bitaxe-601) — generates DB and updates symlink
	$(PIO) run -t compiledb -e $*
	bash $$(ls -d $$HOME/.cloak/profiles/dangernoodle/plugins/cache/dangernoodle-marketplace/espidf-clangd-lsp* 2>/dev/null | head -1)/scripts/compile-db-refresh.sh --variant $* --force

flash-%: ## Flash specific env app-only (e.g. make flash-bitaxe-601)
	$(PIO) run -e $* -t upload

flash-factory-%: ## Erase flash + write factory image — fresh device / re-enter prov mode (e.g. make flash-factory-tdongle-s3)
	$(PIO) run -e $* -t flash-factory

monitor: ## Serial monitor
	$(PIO) device monitor

clean: ## Clean build artifacts
	$(PIO) run -t clean
