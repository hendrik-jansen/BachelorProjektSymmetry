all: two_symmetry

two_symmetry: two_symmetry.cpp
	g++ -W -Wall -O3 two_symmetry.cpp -o two_symmetry

test: test.py two_symmetry
	python test.py two_symmetry
	python test.py two_symmetry --sorting
	python test.py two_symmetry --unique
	python test.py two_symmetry --sorting --unique