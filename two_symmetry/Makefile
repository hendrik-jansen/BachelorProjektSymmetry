all: one_symmetry two_symmetry

test: test.py one_symmetry
	python test.py one_symmetry
	python test.py one_symmetry --clauseswapping
	python test.py one_symmetry --sortclauses
	python test.py one_symmetry --sortliterals

one_symmetry: one_symmetry.cpp
	g++ one_symmetry.cpp -o one_symmetry

two_symmetry: two_symmetry.cpp
	g++ two_symmetry.cpp -o two_symmetry

