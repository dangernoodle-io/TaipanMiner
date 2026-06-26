.PHONY: setup test clean

# Minimum Python version floor (TA-450): 3.11 matches CI target.
_PY_OK := $(shell python3 -c 'import sys; print(1 if sys.version_info >= (3,11) else 0)' 2>/dev/null)

setup:
	@if [ "$(_PY_OK)" != "1" ]; then \
	    PY_VER=$$(python3 -c 'import sys; print(".".join(map(str,sys.version_info[:2])))' 2>/dev/null || echo "unknown"); \
	    echo "Error: Python 3.11+ required (found $$PY_VER). Install Python 3.11+ and retry." >&2; \
	    exit 1; \
	fi
	python3 -m venv .venv
	.venv/bin/pip install -r requirements.txt

test:
	.venv/bin/python -m unittest discover tests

clean:
	rm -rf .venv
