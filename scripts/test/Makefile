.PHONY: setup test discover functional soak stress faults matrix clean

setup:
	python3 -m venv .venv
	.venv/bin/pip install -r requirements.txt

test:
	.venv/bin/python -m unittest discover tests

discover:
	./fleet discover $(ARGS)

functional:
	./fleet functional $(ARGS)

soak:
	./fleet soak $(ARGS)

stress:
	./fleet stress $(ARGS)

faults:
	./fleet faults $(ARGS)

matrix:
	./fleet matrix $(ARGS)

clean:
	rm -rf .venv
