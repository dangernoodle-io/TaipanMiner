PIO ?= pio

.PHONY: help check test coverage build clean monitor compile-db lsp-% webui

help: ## Show available targets
	@grep -E '^[a-zA-Z_%-]+:.*##' $(MAKEFILE_LIST) | sort | \
		awk 'BEGIN {FS = ":.*## "}; {printf "  \033[36m%-20s\033[0m %s\n", $$1, $$2}'

webui: ## Build web UI (Svelte SPA) into webui/dist/
	cd webui && npm ci && npm run build

check: ## Static analysis (cppcheck) for all default envs
	$(PIO) check --skip-packages

check-%: ## Static analysis for one env (e.g. make check-bitaxe-601)
	$(PIO) check --skip-packages -e $*

test: ## Run host unit tests
	$(PIO) test -e native

coverage: test ## Coverage report (gcovr)
	gcovr --root . --filter 'components/' --print-summary --coveralls gcovr-coveralls.json

build: ## Build default envs (tdongle-s3 + bitaxe-601)
	$(PIO) run

build-%: ## Build specific env (e.g. make build-tdongle-s3)
	$(PIO) run -e $*

compile-db: ## Generate compile_commands.json for all boards (clangd LSP)
	$(PIO) run -t compiledb -e bitaxe-601
	$(PIO) run -t compiledb -e bitaxe-403
	$(PIO) run -t compiledb -e bitaxe-650
	$(PIO) run -t compiledb -e tdongle-s3

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
