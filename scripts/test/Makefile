.PHONY: setup test discover functional soak stress faults matrix clean

setup:
	python3 -m venv .venv
	.venv/bin/pip install -r requirements.txt

test:
	.venv/bin/python -m unittest discover tests

discover:
	.venv/bin/python fleet.py discover $(ARGS)

functional:
	.venv/bin/python fleet.py functional $(ARGS)

soak:
	.venv/bin/python fleet.py soak $(ARGS)

stress:
	.venv/bin/python fleet.py stress $(ARGS)

faults:
	.venv/bin/python fleet.py faults $(ARGS)

matrix:
	.venv/bin/python fleet.py matrix $(ARGS)

clean:
	rm -rf .venv
