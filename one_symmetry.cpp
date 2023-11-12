#define LOGGING

// clang-format on

#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// Linux/Unix system specific.

#include <sys/resource.h>
#include <sys/time.h>

static int verbosity; // -1=quiet, 0=normal, 1=verbose, INT_MAX=logging

static int variables; // Variable range: 1,..,<variables>

static size_t added; // Number of added clauses.

struct Clause
{
#ifndef NDEBUG
  size_t id;
#endif
  unsigned size;
  int literals[];

  // The following two functions allow simple ranged-based for-loop
  // iteration over Clause literals with the following idiom:
  //
  //   Clause *c = ...
  //   for (auto lit : *c)
  //     do_something_with (lit);
  //
  int *begin() { return literals; }
  int *end() { return literals + size; }
};

static std::vector<Clause *> clauses;
static Clause *empty_clause; // Empty clause found.

std::vector<int> symmetries;
std::vector<int> candidates;

static std::vector<Clause *> *matrix;

// Get process-time of this process.  This is not portable to Windows but
// should work on other Unixes such as MacOS as is.

static double process_time(void)
{
  struct rusage u;
  double res;
  if (getrusage(RUSAGE_SELF, &u))
    return 0;
  res = u.ru_utime.tv_sec + 1e-6 * u.ru_utime.tv_usec;
  res += u.ru_stime.tv_sec + 1e-6 * u.ru_stime.tv_usec;
  return res;
}

static void message(const char *fmt, ...)
{
  if (verbosity < 0)
    return;
  fputs("c ", stdout);
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  fputc('\n', stdout);
  fflush(stdout);
}

static void line()
{
  if (verbosity < 0)
    return;
  fputs("c\n", stdout);
  fflush(stdout);
}

static void verbose(const char *fmt, ...)
{
  if (verbosity <= 0)
    return;
  fputs("c ", stdout);
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  fputc('\n', stdout);
  fflush(stdout);
}

// Print error message and 'die'.

static void die(const char *fmt, ...)
{
  fprintf(stderr, "babysat: error: ");
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
  exit(1);
}

static void initialize(void)
{
  assert(variables < INT_MAX);
  unsigned size = variables + 1;

  unsigned twice = 2 * size;

  matrix = new std::vector<Clause *>[twice];

  // We subtract 'variables' in order to be able to access
  // the arrays with a negative index (valid in C/C++).

  matrix += variables;
}

static void connect_literal(int lit, Clause *c)
{
  matrix[lit].push_back(c);
}

static Clause *add_clause(std::vector<int> &literals)
{
  size_t size = literals.size();
  size_t bytes = sizeof(struct Clause) + size * sizeof(int);
  Clause *c = (Clause *)new char[bytes];

#ifndef NDEBUG
  c->id = added;
#endif
  added++;

  assert(clauses.size() <= (size_t)INT_MAX);
  c->size = size;

  int *q = c->literals;
  for (auto lit : literals)
    *q++ = lit;

  // debug(c, "new");
  clauses.push_back(c); // Save it on global stack of clauses.

  // Connect the literals of the clause in the matrix.

  for (auto lit : *c)
    connect_literal(lit, c);

  // Handle the special case of empty and unit clauses.

  if (!size)
  {
    // debug(c, "parsed empty clause");
    empty_clause = c;
  }
  return c;
}

static const char *file_name;
static bool close_file;
static FILE *file;

static void parse_error(const char *fmt, ...)
{
  fprintf(stderr, "babysat: parse error in '%s': ", file_name);
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
  exit(1);
}

static void parse(void)
{
  int ch;
  while ((ch = getc(file)) == 'c')
  {
    while ((ch = getc(file)) != '\n')
      if (ch == EOF)
        parse_error("end-of-file in comment");
  }
  if (ch != 'p')
    parse_error("expected 'c' or 'p'");
  int clauses;
  if (fscanf(file, " cnf %d %d", &variables, &clauses) != 2 || variables < 0 ||
      variables >= INT_MAX || clauses < 0 || clauses >= INT_MAX)
    parse_error("invalid header");
  message("parsed header 'p cnf %d %d'", variables, clauses);
  initialize();
  std::vector<int> clause;

  int lit = 0, parsed = 0;
  size_t literals = 0;
  while (fscanf(file, "%d", &lit) == 1)
  {
    if (parsed == clauses)
      parse_error("too many clauses");
    if (lit == INT_MIN || abs(lit) > variables)
      parse_error("invalid literal '%d'", lit);
    if (lit)
    {
      clause.push_back(lit);
      literals++;
    }
    else
    {
      add_clause(clause);
      clause.clear();
      parsed++;
    }
  }
  if (lit)
    parse_error("terminating zero missing");
  if (parsed != clauses)
    parse_error("clause missing");
  if (close_file)
    fclose(file);
  verbose("parsed %zu literals in %d clauses", literals, parsed);
}

void find_candidates()
{
  for (int i = 1; i <= variables; i++)
  {
    if (matrix[i].size() == matrix[-i].size())
    {
      candidates.push_back(i);
    }
  }
}

bool check_clause_symmetry(Clause *c1, Clause *c2, int var)
{
  if (c1->size != c2->size)
  {
    return false;
  }
  for (int i = 0; i < c1->size; i++)
  {
    bool found = false;
    for (int j = i; j < c2->size; j++)
    {
      if (c1->literals[i] == c2->literals[j] ||
          (c1->literals[i] == var && c2->literals[j] == -var))
      {
        found = true;
        int tmp = c2->literals[i];
        c2->literals[i] = c2->literals[j];
        c2->literals[j] = tmp;
        break;
      }
    }
    if (!found)
    {
      return false;
    }
  }
  return true;
}

bool check_symmetry(int var)
{
  for (int i = 0; i < matrix[var].size(); i++)
  {
    bool found = false;
    for (int j = i; j < matrix[-var].size(); j++)
    {
      if (check_clause_symmetry(matrix[var].at(i), matrix[-var].at(j), var))
      {
        found = true;
        Clause *tmp = matrix[-var].at(i);
        matrix[-var].at(i) = matrix[-var].at(j);
        matrix[-var].at(j) = tmp;
        break;
      }
    }
    if (!found)
    {
      return false;
    }
  }
  return true;
}

void find_symmetries()
{
  for (auto var : candidates)
  {
    if (check_symmetry(var))
    {
      symmetries.push_back(var);
    }
  }
}

static void delete_clause(Clause *c)
{
  delete[] c;
}

static void release(void)
{
  for (auto c : clauses)
    delete_clause(c);
  matrix -= variables;
  delete[] matrix;
}

int main(int argc, char **argv)
{
  for (int i = 1; i != argc; i++)
  {
    const char *arg = argv[i];
    if (!strcmp(arg, "-h") || !strcmp(arg, "--help"))
    {
      exit(0);
    }
    else if (!strcmp(arg, "-l") || !strcmp(arg, "--logging"))
#ifdef LOGGING
      verbosity = INT_MAX;
#else
      die("compiled without logging code (use './configure --logging')");
#endif
    else if (!strcmp(arg, "-q") || !strcmp(arg, "--quiet"))
      verbosity = -1;
    else if (!strcmp(arg, "-v") || !strcmp(arg, "--verbose"))
      verbosity = 1;
    else if (arg[0] == '-')
      die("invalid option '%s' (try '-h')", arg);
    else if (file_name)
      die("too many arguments '%s' and '%s' (try '-h')", file_name, arg);
    else
      file_name = arg;
  }

  if (!file_name)
  {
    file_name = "<stdin>";
    assert(!close_file);
    file = stdin;
  }
  else if (!(file = fopen(file_name, "r")))
    die("could not open and read '%s'", file_name);
  else
    close_file = true;

  message("reading from '%s'", file_name);

  parse();

  find_candidates();

  message("found %d candidates", candidates.size());

  if (candidates.size() < 10000)
  {
    find_symmetries();

    for (auto sym : symmetries)
    {
      message("found symmetry on %d", sym);
    }
  }

  release();
}
