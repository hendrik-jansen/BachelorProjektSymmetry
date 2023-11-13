all: one_symmetry

test: test.py
	python test.py

one_symmetry: one_symmetry.cpp
	g++ one_symmetry.cpp -o one_symmetry