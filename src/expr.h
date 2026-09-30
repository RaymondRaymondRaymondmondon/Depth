#pragma once
// The Study's math expression engine (Master Reference: expr.cpp). Parses what a student types (implicit
// multiplication, ln|x|, sin^2 x, e^(3x), unicode pi and roots), evaluates it, and compares answers by value at random
// points (antiderivatives up to a constant). Also the numeric tools the course verifier uses: derivatives, definite
// and improper integrals. No raylib.
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace expr {

struct Node;
using NodeP = std::shared_ptr<const Node>;
struct Node {
    enum Kind { NUM, VAR, ADD, SUB, MUL, DIV, POW, NEG, FUNC, FACT } k = NUM;
    double v = 0;               // NUM
    std::string name;           // VAR, FUNC
    NodeP a, b;
};

struct Expr {
    NodeP root;
    std::string text;
    bool ok() const { return (bool)root; }
};
using Vars = std::map<std::string, double>;

bool Parse(const std::string& text, Expr& out, std::string* err = nullptr);
Expr P(const std::string& text);                       // parse or an empty Expr (for data the verifier already checked)
double Eval(const Expr& e, const Vars& vars);          // NaN when undefined (a log of a negative, a division by zero)
double Eval(const NodeP& n, const Vars& vars);
std::vector<std::string> Variables(const Expr& e);     // the free variables, sorted
std::string ToString(const Expr& e);                   // a plain canonical form (for echoing what was understood)

// numerics
double Derivative(const Expr& e, const std::string& var, double at, const Vars& others = {});
double Derivative2(const Expr& e, const std::string& var, double at, const Vars& others = {});
// the definite integral of e over [a, b]; either bound may be +-INFINITY. `err` gets an estimate of the error.
double Integrate(const Expr& e, const std::string& var, double a, double b, const Vars& others = {}, double* err = nullptr);

// comparisons at random points of a domain (per variable [lo, hi]; defaults to [0.15, 3.2])
struct Domain { std::map<std::string, std::pair<double, double>> range; std::pair<double, double> def{0.15, 3.2}; };
struct Compare { bool equal = false; int tested = 0; std::string why; };
Compare Equivalent(const Expr& a, const Expr& b, const Domain& d = {}, int points = 8, double tol = 1e-6, unsigned seed = 1);
Compare EquivalentUpToConstant(const Expr& a, const Expr& b, const Domain& d = {}, int points = 8, double tol = 1e-6, unsigned seed = 1);

int RunExprTest();   // depth.exe --expr-test: parsing, equivalence and the numerics on a fixture
}
