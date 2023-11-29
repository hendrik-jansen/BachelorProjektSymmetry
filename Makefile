all: one_symmetry

test: test.py one_symmetry
	python test.py one_symmetry
	python test.py one_symmetry --clauseswapping
	python test.py one_symmetry --sortclauses
	python test.py one_symmetry --sortliterals


test_noswap: test.py one_symmetry_noswap
	python test.py one_symmetry_noswap

test_noswap_nosort: test.py one_symmetry_noswap_nosort
	python test.py one_symmetry_noswap_nosort

one_symmetry: one_symmetry.cpp
	g++ one_symmetry.cpp -o one_symmetry

one_symmetry_noswap: one_symmetry_noswap.cpp
	g++ one_symmetry_noswap.cpp -o one_symmetry_noswap

one_symmetry_noswap_nosort: one_symmetry_noswap_nosort.cpp
	g++ one_symmetry_noswap_nosort.cpp -o one_symmetry_noswap_nosort


