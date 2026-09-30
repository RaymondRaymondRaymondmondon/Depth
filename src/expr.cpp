// The Study's expression engine (see expr.h).
#include "expr.h"
#include <algorithm>
#include <cstdio>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>

namespace expr {
namespace {

const double NaN = std::numeric_limits<double>::quiet_NaN();
NodeP Num(double v) { auto n = std::make_shared<Node>(); n->k = Node::NUM; n->v = v; return n; }
NodeP Var(const std::string& s) { auto n = std::make_shared<Node>(); n->k = Node::VAR; n->name = s; return n; }
NodeP Bin(Node::Kind k, NodeP a, NodeP b) { auto n = std::make_shared<Node>(); n->k = k; n->a = a; n->b = b; return n; }
NodeP Fn(const std::string& f, NodeP a) { auto n = std::make_shared<Node>(); n->k = Node::FUNC; n->name = f; n->a = a; return n; }
NodeP Neg(NodeP a) { auto n = std::make_shared<Node>(); n->k = Node::NEG; n->a = a; return n; }
NodeP Fact(NodeP a) { auto n = std::make_shared<Node>(); n->k = Node::FACT; n->a = a; return n; }

// function names, longest first so "arcsin" wins over "sin" and "sinh" over "sin"
const char* FUNCS[] = {"arcsin", "arccos", "arctan", "arcsec", "arccsc", "arccot", "sinh", "cosh", "tanh", "asin", "acos", "atan",
                       "sqrt", "cbrt", "sin", "cos", "tan", "sec", "csc", "cot", "exp", "abs", "log", "ln"};
const char* INVERSE(const std::string& f) {
    if (f == "sin") return "arcsin"; if (f == "cos") return "arccos"; if (f == "tan") return "arctan";
    if (f == "sec") return "arcsec"; if (f == "csc") return "arccsc"; if (f == "cot") return "arccot";
    return nullptr;
}
const char* WORDVARS[] = {"theta", "alpha", "beta", "gamma", "lambda", "omega", "phi", "rho", "sigma", "tau", "mu"};

struct Tok { enum T { NUM, FUNC, VAR, CONST, OP, END } t = END; double v = 0; std::string s; char op = 0; size_t pos = 0; };

std::string Normalize(const std::string& in) {   // unicode a student might paste or type
    std::string s;
    for (size_t i = 0; i < in.size();) {
        auto starts = [&](const char* u) { size_t n = strlen(u); return in.compare(i, n, u) == 0 ? n : 0; };
        size_t n;
        if ((n = starts("\xCF\x80"))) { s += "pi"; i += n; }                 // π
        else if ((n = starts("\xE2\x88\x9A"))) { s += "sqrt"; i += n; }      // √
        else if ((n = starts("\xC2\xB7")) || (n = starts("\xC3\x97")) || (n = starts("\xE2\x8B\x85"))) { s += "*"; i += n; }   // · × ⋅
        else if ((n = starts("\xE2\x88\x92"))) { s += "-"; i += n; }         // −
        else if ((n = starts("\xC2\xB2"))) { s += "^2"; i += n; }            // ²
        else if ((n = starts("\xC2\xB3"))) { s += "^3"; i += n; }            // ³
        else if ((n = starts("\xCE\xB8"))) { s += "theta"; i += n; }         // θ
        else if ((n = starts("\xE2\x88\x9E"))) { s += "inf"; i += n; }       // ∞
        else { s += in[i]; i++; }
    }
    return s;
}

bool Lex(const std::string& src, std::vector<Tok>& out, std::string* err) {
    std::string s = Normalize(src);
    for (size_t i = 0; i < s.size();) {
        char c = s[i];
        if (isspace((unsigned char)c)) { i++; continue; }
        if (isdigit((unsigned char)c) || (c == '.' && i + 1 < s.size() && isdigit((unsigned char)s[i + 1]))) {
            size_t j = i; while (j < s.size() && (isdigit((unsigned char)s[j]) || s[j] == '.')) j++;
            Tok t; t.t = Tok::NUM; t.v = atof(s.substr(i, j - i).c_str()); t.pos = i; out.push_back(t); i = j; continue;
        }
        if (isalpha((unsigned char)c)) {
            size_t j = i; while (j < s.size() && isalpha((unsigned char)s[j])) j++;
            std::string w = s.substr(i, j - i);
            // split a run of letters into functions, constants and one-letter variables ("xe" = x*e, "sinx" = sin x)
            for (size_t k = 0; k < w.size();) {
                bool matched = false;
                for (const char* f : FUNCS) { size_t n = strlen(f); if (w.compare(k, n, f) == 0) { Tok t; t.t = Tok::FUNC; t.s = f; t.pos = i + k; out.push_back(t); k += n; matched = true; break; } }
                if (matched) continue;
                if (w.compare(k, 2, "pi") == 0) { Tok t; t.t = Tok::CONST; t.v = 3.14159265358979323846; t.s = "pi"; t.pos = i + k; out.push_back(t); k += 2; continue; }
                if (w.compare(k, 3, "inf") == 0) { Tok t; t.t = Tok::CONST; t.v = INFINITY; t.s = "inf"; t.pos = i + k; out.push_back(t); k += 3; continue; }
                for (const char* wv : WORDVARS) { size_t n = strlen(wv); if (w.compare(k, n, wv) == 0) { Tok t; t.t = Tok::VAR; t.s = wv; t.pos = i + k; out.push_back(t); k += n; matched = true; break; } }
                if (matched) continue;
                if (w[k] == 'e') { Tok t; t.t = Tok::CONST; t.v = 2.71828182845904523536; t.s = "e"; t.pos = i + k; out.push_back(t); k++; continue; }
                Tok t; t.t = Tok::VAR; t.s = std::string(1, w[k]); t.pos = i + k; out.push_back(t); k++;
            }
            i = j; continue;
        }
        if (strchr("+-*/^()|!,", c)) { Tok t; t.t = Tok::OP; t.op = c; t.pos = i; out.push_back(t); i++; continue; }
        if (c == '[' || c == '{') { Tok t; t.t = Tok::OP; t.op = '('; t.pos = i; out.push_back(t); i++; continue; }
        if (c == ']' || c == '}') { Tok t; t.t = Tok::OP; t.op = ')'; t.pos = i; out.push_back(t); i++; continue; }
        if (err) *err = std::string("I don't know the symbol '") + c + "'";
        return false;
    }
    Tok e; e.t = Tok::END; e.pos = s.size(); out.push_back(e);
    return true;
}

struct Parser {
    std::vector<Tok> t; size_t i = 0; std::string err; int absDepth = 0;
    const Tok& Cur() const { return t[i]; }
    bool IsOp(char c) const { return Cur().t == Tok::OP && Cur().op == c; }
    bool Fail(const std::string& m) { if (err.empty()) err = m; return false; }
    // can the current token begin an operand (for implicit multiplication)?
    bool StartsOperand() const {
        const Tok& c = Cur();
        if (c.t == Tok::NUM || c.t == Tok::VAR || c.t == Tok::CONST || c.t == Tok::FUNC) return true;
        if (c.t == Tok::OP && c.op == '(') return true;
        if (c.t == Tok::OP && c.op == '|' && absDepth == 0) return true;   // (inside |...| a bar closes)
        return false;
    }
    NodeP Expr() {
        NodeP l = Term(); if (!l) return nullptr;
        while (IsOp('+') || IsOp('-')) {
            char op = Cur().op; i++;
            NodeP r = Term(); if (!r) return nullptr;
            l = Bin(op == '+' ? Node::ADD : Node::SUB, l, r);
        }
        return l;
    }
    NodeP Term() {
        NodeP l = Unary(); if (!l) return nullptr;
        for (;;) {
            if (IsOp('*') || IsOp('/')) { char op = Cur().op; i++; NodeP r = Unary(); if (!r) return nullptr; l = Bin(op == '*' ? Node::MUL : Node::DIV, l, r); }
            else if (StartsOperand()) { NodeP r = Power(); if (!r) return nullptr; l = Bin(Node::MUL, l, r); }   // implicit: 2x, x(x+1), x e^x
            else break;
        }
        return l;
    }
    NodeP Unary() {
        if (IsOp('-')) { i++; NodeP a = Unary(); return a ? Neg(a) : nullptr; }
        if (IsOp('+')) { i++; return Unary(); }
        return Power();
    }
    NodeP Power() {
        NodeP b = Postfix(); if (!b) return nullptr;
        if (IsOp('^')) { i++; NodeP e = Unary(); if (!e) return nullptr; return Bin(Node::POW, b, e); }   // right-assoc via Unary -> Power
        return b;
    }
    NodeP Postfix() {
        NodeP p = Primary(); if (!p) return nullptr;
        while (IsOp('!')) { i++; p = Fact(p); }
        return p;
    }
    // a function's argument without parentheses: numbers, variables and bracketed groups multiplied together, up to
    // the next operator or function ("sin 2x" = sin(2x); "sin x cos x" = sin(x) cos(x)); a power binds inside ("sin x^2")
    NodeP BareArg() {
        NodeP a = Power(); if (!a) return nullptr;
        while ((Cur().t == Tok::NUM || Cur().t == Tok::VAR || Cur().t == Tok::CONST || IsOp('(')) && Cur().t != Tok::FUNC) { NodeP r = Power(); if (!r) return nullptr; a = Bin(Node::MUL, a, r); }
        return a;
    }
    NodeP Primary() {
        const Tok c = Cur();
        switch (c.t) {
        case Tok::NUM: i++; return Num(c.v);
        case Tok::CONST: i++; return Num(c.v);
        case Tok::VAR: i++; return Var(c.s);
        case Tok::FUNC: {
            i++;
            std::string f = c.s == "asin" ? "arcsin" : c.s == "acos" ? "arccos" : c.s == "atan" ? "arctan" : c.s;
            NodeP power;
            if (IsOp('^')) {   // sin^2 x = (sin x)^2; sin^-1 x = arcsin x
                i++;
                bool neg = false; if (IsOp('-')) { neg = true; i++; }
                if (Cur().t != Tok::NUM) { Fail("after " + f + "^ I expect a number, like sin^2(x)"); return nullptr; }
                double p = Cur().v; i++;
                if (neg && p == 1 && INVERSE(f)) f = INVERSE(f); else power = Num(neg ? -p : p);
            }
            NodeP arg;
            if (IsOp('(')) { i++; arg = Expr(); if (!arg) return nullptr; if (!IsOp(')')) { Fail("a '(' is never closed"); return nullptr; } i++; }
            else if (IsOp('|')) arg = Primary();
            else if (StartsOperand()) arg = BareArg();
            else { Fail(f + " needs something to act on, like " + f + "(x)"); return nullptr; }
            if (!arg) return nullptr;
            NodeP n = Fn(f, arg);
            return power ? Bin(Node::POW, n, power) : n;
        }
        case Tok::OP:
            if (c.op == '(') { i++; NodeP e = Expr(); if (!e) return nullptr; if (!IsOp(')')) { Fail("a '(' is never closed"); return nullptr; } i++; return e; }
            if (c.op == '|') { i++; absDepth++; NodeP e = Expr(); absDepth--; if (!e) return nullptr; if (!IsOp('|')) { Fail("an absolute value bar '|' is never closed"); return nullptr; } i++; return Fn("abs", e); }
            Fail(std::string("unexpected '") + c.op + "'");
            return nullptr;
        default: Fail("the expression ends too soon"); return nullptr;
        }
    }
};

double FactD(double n) { return (n < 0 || n != floor(n)) ? tgamma(n + 1) : tgamma(n + 1); }
double EvalN(const Node* n, const Vars& v) {
    switch (n->k) {
    case Node::NUM: return n->v;
    case Node::VAR: { auto it = v.find(n->name); return it == v.end() ? NaN : it->second; }
    case Node::ADD: return EvalN(n->a.get(), v) + EvalN(n->b.get(), v);
    case Node::SUB: return EvalN(n->a.get(), v) - EvalN(n->b.get(), v);
    case Node::MUL: return EvalN(n->a.get(), v) * EvalN(n->b.get(), v);
    case Node::DIV: { double d = EvalN(n->b.get(), v); return d == 0 ? NaN : EvalN(n->a.get(), v) / d; }
    case Node::NEG: return -EvalN(n->a.get(), v);
    case Node::FACT: return FactD(EvalN(n->a.get(), v));
    case Node::POW: {
        double b = EvalN(n->a.get(), v), e = EvalN(n->b.get(), v);
        if (b < 0 && e != floor(e)) {   // a real odd root of a negative (x^(1/3)) when the exponent is p/q with q odd
            for (int q = 3; q <= 15; q += 2) { double p = e * q; if (fabs(p - round(p)) < 1e-9) { double r = pow(-b, e); return ((long long)llround(p) % 2) ? -r : r; } }
            return NaN;
        }
        return pow(b, e);
    }
    case Node::FUNC: {
        double x = EvalN(n->a.get(), v);
        const std::string& f = n->name;
        if (f == "sin") return sin(x); if (f == "cos") return cos(x); if (f == "tan") return tan(x);
        if (f == "sec") return 1 / cos(x); if (f == "csc") return 1 / sin(x); if (f == "cot") return cos(x) / sin(x);
        if (f == "arcsin") return fabs(x) <= 1 ? asin(x) : NaN; if (f == "arccos") return fabs(x) <= 1 ? acos(x) : NaN; if (f == "arctan") return atan(x);
        if (f == "arcsec") return fabs(x) >= 1 ? acos(1 / x) : NaN; if (f == "arccsc") return fabs(x) >= 1 ? asin(1 / x) : NaN; if (f == "arccot") return x == 0 ? 1.5707963267948966 : atan(1 / x) + (x < 0 ? 3.141592653589793 : 0);
        if (f == "sinh") return sinh(x); if (f == "cosh") return cosh(x); if (f == "tanh") return tanh(x);
        if (f == "sqrt") return x >= 0 ? sqrt(x) : NaN; if (f == "cbrt") return cbrt(x);
        if (f == "exp") return exp(x); if (f == "abs") return fabs(x);
        if (f == "ln") return x > 0 ? log(x) : NaN;
        if (f == "log") return x > 0 ? log10(x) : NaN;
        return NaN;
    }
    }
    return NaN;
}
void CollectVars(const Node* n, std::set<std::string>& out) { if (!n) return; if (n->k == Node::VAR) out.insert(n->name); CollectVars(n->a.get(), out); CollectVars(n->b.get(), out); }
int Prec(const Node* n) { switch (n->k) { case Node::ADD: case Node::SUB: return 1; case Node::MUL: case Node::DIV: return 2; case Node::NEG: return 3; case Node::POW: return 4; default: return 5; } }
std::string Str(const Node* n) {
    auto wrap = [&](const Node* c, int p) { std::string s = Str(c); return Prec(c) < p ? "(" + s + ")" : s; };
    char buf[64];
    switch (n->k) {
    case Node::NUM: if (n->v == floor(n->v) && fabs(n->v) < 1e15) snprintf(buf, sizeof buf, "%.0f", n->v); else snprintf(buf, sizeof buf, "%.10g", n->v); return buf;
    case Node::VAR: return n->name;
    case Node::ADD: return Str(n->a.get()) + " + " + wrap(n->b.get(), 2);
    case Node::SUB: return Str(n->a.get()) + " - " + wrap(n->b.get(), 2);
    case Node::MUL: return wrap(n->a.get(), 2) + "*" + wrap(n->b.get(), 3);
    case Node::DIV: return wrap(n->a.get(), 2) + "/" + wrap(n->b.get(), 3);
    case Node::NEG: return "-" + wrap(n->a.get(), 4);
    case Node::POW: return wrap(n->a.get(), 5) + "^" + wrap(n->b.get(), 4);
    case Node::FACT: return wrap(n->a.get(), 5) + "!";
    case Node::FUNC: return n->name + "(" + Str(n->a.get()) + ")";
    }
    return "?";
}
struct Rng { unsigned s; double U() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s >> 8) / 16777216.0; } };

// adaptive Gauss-Kronrod (7/15 points) on a finite interval of a transformed integrand
template <class F> double GK(F&& f, double a, double b, double& errOut, int depth) {
    static const double xk[8] = {0.991455371120812639, 0.949107912342758525, 0.864864423359769073, 0.741531185599394440, 0.586087235467691130, 0.405845151377397167, 0.207784955007898468, 0.0};
    static const double wk[8] = {0.022935322010529225, 0.063092092629978553, 0.104790010322250184, 0.140653259715525919, 0.169004726639267903, 0.190350578064785410, 0.204432940075298892, 0.209482141084727828};
    static const double wg[4] = {0.129484966168869693, 0.279705391489276668, 0.381830050505118945, 0.417959183673469388};
    double c = (a + b) / 2, h = (b - a) / 2, k = 0, g = 0;
    for (int j = 0; j < 8; j++) {
        if (j == 7) { double fc = f(c); k += wk[7] * fc; g += wg[3] * fc; continue; }
        double f1 = f(c - h * xk[j]), f2 = f(c + h * xk[j]);
        k += wk[j] * (f1 + f2);
        if (j % 2 == 1) g += wg[j / 2] * (f1 + f2);
    }
    k *= h; g *= h;
    double e = fabs(k - g);
    if ((e <= 1e-10 * std::max(1.0, fabs(k)) || depth > 30) || !std::isfinite(k)) { errOut += e; return k; }
    return GK(f, a, c, errOut, depth + 1) + GK(f, c, b, errOut, depth + 1);
}
}  // namespace

bool Parse(const std::string& text, Expr& out, std::string* err) {
    out = Expr{};
    std::vector<Tok> toks;
    std::string e;
    if (!Lex(text, toks, &e)) { if (err) *err = e; return false; }
    Parser p; p.t = std::move(toks);
    if (p.Cur().t == Tok::END) { if (err) *err = "the answer is empty"; return false; }
    NodeP root = p.Expr();
    if (root && p.Cur().t != Tok::END) { const Tok& c = p.Cur(); p.Fail(c.t == Tok::OP && c.op == ')' ? "there is a ')' with no '(' before it" : "I couldn't read the part near position " + std::to_string(c.pos + 1)); root = nullptr; }
    if (!root) { if (err) *err = p.err.empty() ? "I couldn't read that" : p.err; return false; }
    out.root = root; out.text = text;
    return true;
}
Expr P(const std::string& text) { Expr e; Parse(text, e, nullptr); return e; }
double Eval(const NodeP& n, const Vars& v) { return n ? EvalN(n.get(), v) : NaN; }
double Eval(const Expr& e, const Vars& v) { return Eval(e.root, v); }
std::vector<std::string> Variables(const Expr& e) { std::set<std::string> s; CollectVars(e.root.get(), s); return {s.begin(), s.end()}; }
std::string ToString(const Expr& e) { return e.root ? Str(e.root.get()) : ""; }

double Derivative(const Expr& e, const std::string& var, double at, const Vars& others) {
    Vars v = others;
    auto f = [&](double x) { v[var] = x; return Eval(e, v); };
    double h = 1e-3 * std::max(1.0, fabs(at));
    // Richardson on central differences
    double d1 = (f(at + h) - f(at - h)) / (2 * h), d2 = (f(at + h / 2) - f(at - h / 2)) / h;
    return (4 * d2 - d1) / 3;
}
double Derivative2(const Expr& e, const std::string& var, double at, const Vars& others) {
    Vars v = others;
    auto f = [&](double x) { v[var] = x; return Eval(e, v); };
    double h = 1e-3 * std::max(1.0, fabs(at));
    double d1 = (f(at + h) - 2 * f(at) + f(at - h)) / (h * h), d2 = (f(at + h / 2) - 2 * f(at) + f(at - h / 2)) / (h * h / 4);
    return (4 * d2 - d1) / 3;
}
double Integrate(const Expr& e, const std::string& var, double a, double b, const Vars& others, double* err) {
    Vars v = others;
    auto f = [&](double x) { v[var] = x; double y = Eval(e, v); return std::isfinite(y) ? y : 0.0; };
    double sign = 1;
    if (a > b) { std::swap(a, b); sign = -1; }
    double e1 = 0, r;
    if (std::isinf(a) && std::isinf(b)) {   // x = t / (1 - t^2) on (-1, 1)
        r = GK([&](double t) { double d = 1 - t * t; return f(t / d) * (1 + t * t) / (d * d); }, -1, 1, e1, 0);
    } else if (std::isinf(b)) {             // x = a + t / (1 - t) on [0, 1)
        r = GK([&](double t) { double d = 1 - t; return f(a + t / d) / (d * d); }, 0, 1, e1, 0);
    } else if (std::isinf(a)) {             // x = b - t / (1 - t)
        r = GK([&](double t) { double d = 1 - t; return f(b - t / d) / (d * d); }, 0, 1, e1, 0);
    } else r = GK(f, a, b, e1, 0);
    if (err) *err = e1;
    return sign * r;
}

static Compare CompareImpl(const Expr& a, const Expr& b, const Domain& d, int points, double tol, unsigned seed, bool upToConst) {
    Compare c;
    if (!a.ok() || !b.ok()) { c.why = "an expression didn't parse"; return c; }
    std::set<std::string> names;
    for (auto& s : Variables(a)) names.insert(s);
    for (auto& s : Variables(b)) names.insert(s);
    Rng r{seed ? seed : 1};
    double base = 0; bool haveBase = false;
    for (int tries = 0; c.tested < points && tries < points * 40; tries++) {
        Vars v;
        for (auto& n : names) { auto it = d.range.find(n); auto rg = it == d.range.end() ? d.def : it->second; v[n] = rg.first + (rg.second - rg.first) * r.U(); }
        double x = Eval(a, v), y = Eval(b, v);
        if (!std::isfinite(x) || !std::isfinite(y) || fabs(x) > 1e9 || fabs(y) > 1e9) continue;   // (off the domain, or too near a pole)
        double diff = x - y;
        if (upToConst) { if (!haveBase) { base = diff; haveBase = true; c.tested++; continue; } diff -= base; }
        if (fabs(diff) > tol * std::max({1.0, fabs(x), fabs(y)})) {
            char buf[160]; snprintf(buf, sizeof buf, "they differ: %.6g vs %.6g", x, y);
            c.why = buf; return c;
        }
        c.tested++;
    }
    if (c.tested < points) { c.why = "too few points where both are defined"; return c; }
    c.equal = true;
    return c;
}
Compare Equivalent(const Expr& a, const Expr& b, const Domain& d, int points, double tol, unsigned seed) { return CompareImpl(a, b, d, points, tol, seed, false); }
Compare EquivalentUpToConstant(const Expr& a, const Expr& b, const Domain& d, int points, double tol, unsigned seed) { return CompareImpl(a, b, d, points, tol, seed, true); }

// ============================================================================ the self-test
int RunExprTest() {
    int fails = 0, n = 0;
    auto eq = [&](const char* a, const char* b, bool want, bool upToC = false) {
        n++;
        Expr A, B; std::string ea, eb;
        if (!Parse(a, A, &ea) || !Parse(b, B, &eb)) { printf("FAIL parse: '%s' (%s) / '%s' (%s)\n", a, ea.c_str(), b, eb.c_str()); fails++; return; }
        Domain d; d.range["C"] = {0, 0};
        Compare c = upToC ? EquivalentUpToConstant(A, B, d) : Equivalent(A, B, d);
        if (c.equal != want) { printf("FAIL %s: '%s' vs '%s' (%s)  [read as %s | %s]\n", want ? "should match" : "should differ", a, b, c.why.c_str(), ToString(A).c_str(), ToString(B).c_str()); fails++; }
    };
    auto bad = [&](const char* a) { n++; Expr A; std::string e; if (Parse(a, A, &e)) { printf("FAIL: '%s' should not parse (read as %s)\n", a, ToString(A).c_str()); fails++; } };
    auto num = [&](double got, double want, const char* what) { n++; if (!(fabs(got - want) <= 1e-6 * std::max(1.0, fabs(want)))) { printf("FAIL %s: %.10g, want %.10g\n", what, got, want); fails++; } };
    // how students type things
    eq("2x", "2*x", true); eq("x e^x", "x*exp(x)", true); eq("xe^x - e^x", "e^x (x - 1)", true);
    eq("sin^2 x", "(sin(x))^2", true); eq("sin^2(x) + cos^2 x", "1", true); eq("sin x cos x", "sin(2x)/2", true);
    eq("sin 2x", "2 sin x cos x", true); eq("ln|x|", "ln(abs(x))", true); eq("ln x^2", "2 ln x", true);
    eq("1/2 x^2", "x^2/2", true); eq("(1/2)x^2", "0.5x^2", true); eq("e^(3x)", "(e^x)^3", true);
    eq("x^-1", "1/x", true); eq("2^-x", "(1/2)^x", true); eq("sqrt x", "x^(1/2)", true);
    eq("\xE2\x88\x9A(x+1)", "sqrt(x+1)", true); eq("\xCF\x80 x", "pi*x", true); eq("x\xC2\xB2", "x^2", true);
    eq("arctan(x)", "tan^-1 x", true); eq("arcsin x", "asin(x)", true); eq("sec^2 x", "1 + tan^2 x", true);
    eq("|x - 1| + 2|x|", "abs(x-1) + 2 abs(x)", true); eq("x ln x - x", "x(ln x - 1)", true);
    eq("-x^2", "-(x^2)", true); eq("2^3^2", "2^9", true); eq("3!", "6", true);
    eq("x^(1/3)", "cbrt(x)", true); eq("[x+1]{x-1}", "x^2 - 1", true);
    eq("x^2", "x^3", false); eq("sin x", "cos x", false); eq("ln x", "log x", false);
    eq("e^x", "e^x + 1", false); eq("x/2", "2/x", false); eq("1/(x+1)", "1/x + 1", false);
    // antiderivatives up to a constant
    eq("x e^x - e^x + C", "e^x (x - 1)", true, true); eq("sin^2 x / 2", "-cos^2 x / 2 + C", true, true);
    eq("ln|x| + C", "ln|3x|", true, true); eq("x^2 + C", "x^2 + x", false, true);
    bad("2x +"); bad("(x + 1"); bad("x + 1)"); bad("sin"); bad("|x"); bad("x $ 2"); bad("");
    // numerics
    Expr f = P("x^3"); num(Derivative(f, "x", 2), 12, "d/dx x^3 at 2"); num(Derivative2(f, "x", 2), 12, "d2/dx2 x^3 at 2");
    num(Integrate(P("x e^(-x)"), "x", 0, INFINITY), 1, "int_0^inf x e^-x");
    num(Integrate(P("1/(1+x^2)"), "x", -INFINITY, INFINITY), 3.14159265358979, "int 1/(1+x^2) over R");
    num(Integrate(P("1/sqrt(x)"), "x", 0, 1), 2, "int_0^1 1/sqrt x");
    num(Integrate(P("sin(x)^2"), "x", 0, 3.14159265358979), 1.5707963267949, "int_0^pi sin^2");
    num(Integrate(P("ln(x)"), "x", 0, 1), -1, "int_0^1 ln x");
    num(Integrate(P("e^(-x^2)"), "x", -INFINITY, INFINITY), 1.7724538509055, "gaussian");
    num(Integrate(P("x"), "x", 2, 0), -2, "reversed bounds");
    printf("expr-test: %d checks, %d failed\n%s\n", n, fails, fails ? "FAILED" : "OK");
    return fails ? 1 : 0;
}
}