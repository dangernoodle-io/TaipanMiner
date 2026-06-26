.PHONY: setup test clean

setup:
	python3 -m venv .venv
	.venv/bin/pip install -r requirements.txt

test:
	.venv/bin/python -m unittest discover tests

clean:
	rm -rf .venv
