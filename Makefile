PIO ?= pio

.PHONY: help check test coverage build clean monitor compile-db lsp-% webui

help: ## Show available targets
	@grep -E '^[a-zA-Z_%-]+:.*##' $(MAKEFILE_LIST) | sort | \
		awk 'BEGIN {FS = ":.*## "}; {printf "  \033[36m%-20s\033[0m %s\n", $$1, $$2}'

# PlatformIO does not regenerate sdkconfig.<env> when sdkconfig.defaults or the
# per-board overlay changes — so a stale on-disk sdkconfig.<env> can mask
# defaults edits (e.g. a CONFIG_LWIP_MAX_SOCKETS reduction that breaks an
# httpd_start invariant only on fresh CI builds). Regenerate any sdkconfig.<env>
# whose source inputs are newer.
.PHONY: _sdkconfig-fresh-% _sdkconfig-fresh-all

_sdkconfig-fresh-%:
	@if [ -f sdkconfig.$* ]; then \
		srcs="sdkconfig.defaults"; \
		[ -f "sdkconfig/$*" ] && srcs="$$srcs sdkconfig/$*"; \
		if [ -n "$$(find $$srcs -newer sdkconfig.$* 2>/dev/null)" ]; then \
			echo "stale sdkconfig.$*: regenerating from defaults"; \
			rm -f sdkconfig.$*; \
		fi; \
	fi

_sdkconfig-fresh-all:
	@for f in sdkconfig.*; do \
		[ -f "$$f" ] || continue; \
		case $$f in *.bak|*.orig) continue;; esac; \
		env=$${f#sdkconfig.}; \
		srcs="sdkconfig.defaults"; \
		[ -f "sdkconfig/$$env" ] && srcs="$$srcs sdkconfig/$$env"; \
		if [ -n "$$(find $$srcs -newer "$$f" 2>/dev/null)" ]; then \
			echo "stale $$f: regenerating from defaults"; \
			rm -f "$$f"; \
		fi; \
	done

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

build: _sdkconfig-fresh-all ## Build default envs (tdongle-s3 + bitaxe-601)
	$(PIO) run

build-%: _sdkconfig-fresh-% ## Build specific env (e.g. make build-tdongle-s3)
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
