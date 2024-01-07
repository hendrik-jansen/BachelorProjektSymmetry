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

bool variable_sorting = false;

bool groups = false;

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

std::vector<std::vector<int>> symmetries;
std::vector<std::vector<int>> symmetry_groups;
int *sorted_variables;

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

  sorted_variables = new int[variables];
  for (int i = 1; i <= variables; i++)
  {
    sorted_variables[i - 1] = i;
  }
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
      // std::sort(clause.begin(), clause.end(), [](int i, int j)
      //           { return abs(i) < abs(j); });
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

bool check_clause_symmetry(Clause *c1, Clause *c2, int var1, int var2)
{
  if (c1->size != c2->size)
  {
    return false;
  }

  auto c1_literals = c1->literals;
  auto c2_literals = c2->literals;

  for (int i = 0; i < c1->size; i++)
  {
    bool found = false;
    for (int j = i; j < c2->size; j++)
    {
      if (c1_literals[i] == c2_literals[j] ||
          (c1_literals[i] == var1 && c2_literals[j] == var2) ||
          (c1_literals[i] == -var2 && c2_literals[j] == -var1))
      {
        // after finding a matching literal, move it back
        // so only unmatched literals have to be considered
        found = true;
        int tmp = c2_literals[i];
        c2_literals[i] = c2_literals[j];
        c2_literals[j] = tmp;
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

bool check_symmetry(int var1, int var2)
{
  auto &var1_occs = matrix[var1];
  auto &var2_occs = matrix[var2];
  for (int i = 0; i < var1_occs.size(); i++)
  {
    bool found = false;
    for (int j = i; j < var2_occs.size(); j++)
    {
      if (check_clause_symmetry(var1_occs[i], var2_occs[j], var1, var2))
      {
        found = true;
        // after finding a matching clause, move it back
        // so only unmatched clauses have to be considered
        Clause *tmp = var2_occs[i];
        var2_occs[i] = var2_occs[j];
        var2_occs[j] = tmp;
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

void sort_variables()
{
  std::sort(sorted_variables, sorted_variables + variables, [](int i, int j)
            { return matrix[i].size() < matrix[j].size() || 
                    (matrix[i].size() == matrix[j].size() && 
                     matrix[-i].size() < matrix[-j].size()); });

  // for(int i = 0; i < variables; i++){
  //   message("%d: %d", i, sorted_variables[i]);
  // }
}

void find_symmetries()
{
  int checked_pairs = 0;
  for (int i = 0; i < variables; i++)
  {
    int var1 = sorted_variables[i];
    std::vector<int> group = {var1};
    for (int j = i + 1; j < variables; j++)
    {
      checked_pairs++;
      if (checked_pairs > 1000000000) {
        return;
      }
      int var2 = sorted_variables[j];
      // message("%d: %d, %d:%d", i, var1, j, var2);
      if (matrix[var1].size() != 0 && 
          matrix[var1].size() == matrix[var2].size() && 
          matrix[-var1].size() == matrix[-var2].size())
      {
        if (check_symmetry(var1, var2) && check_symmetry(-var1, -var2))
        {
          if (groups) 
          {
            group.push_back(var2);
            int tmp = sorted_variables[i+1];
            sorted_variables[i+1] = sorted_variables[j];
            sorted_variables[j] = tmp;
            i++;
          } else {
            symmetries.push_back({var1, var2});
          }
        }
      }
      else if(variable_sorting)
      {
        break;
      }
    }
    if (group.size() > 1) {
      symmetries.push_back(group);
    }
  }
  message("paires checked: %d", checked_pairs);
}

void group_symmetries() {
  for(int i = 0; i < symmetries.size(); i++) {
    for (int j = i + 1; j < symmetries.size(); j++) {
      if (symmetries[i][1] == symmetries[j][0])
      {

      }
    }
  }
}

// void find_symmetries()
// {
//   for (int i = 0; i < candidates.size(); i++)
//   {
//     if (check_symmetry(candidates[i][0], candidates[i][1]) && check_symmetry(-candidates[i][0], -candidates[i][1]))
//     {
//       symmetries.push_back(candidates[i]);
//     }
//   }
// }

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
    else if (!strcmp(arg, "-s") || !strcmp(arg, "--sorting"))
      variable_sorting = true;
    else if (!strcmp(arg, "-g") || !strcmp(arg, "--groups"))
      groups = true;
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

  if (variable_sorting)
  {
    sort_variables();
  }

  find_symmetries();

  group_symmetries();

  // message("found %d candidates", candidates.size());

  // find_symmetries();

  message("found %d symmetries", symmetries.size());
  for (auto sym : symmetries)
  {
    if (groups) 
    {
      printf("found symmetry group: ");
      for (auto var : sym) 
      {
        printf("%d ", var);
      }
      printf("\n");
    }
    else
    {
      printf("-%d %d 0\n", sym[0], sym[1]);
    }
  }

  release();
}
