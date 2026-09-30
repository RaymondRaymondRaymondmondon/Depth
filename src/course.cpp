// Course packs: loading, and the ingestion tools (see course.h and study/INGEST.md).
#include "course.h"
#include "expr.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;
using expr::Expr;

// ============================================================================ files
namespace {
std::string ReadText(const std::string& p) { std::ifstream f(p, std::ios::binary); if (!f) return ""; std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
bool Exists(const std::string& p) { std::error_code ec; return fs::exists(p, ec); }
std::string ExeDir() {
#ifdef _WIN32
    char b[MAX_PATH]; DWORD n = GetModuleFileNameA(nullptr, b, MAX_PATH);
    return fs::path(std::string(b, n)).parent_path().string();
#else
    return ".";
#endif
}
std::string Lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
std::string Trim(const std::string& s) { size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n"); return a == std::string::npos ? "" : s.substr(a, b - a + 1); }
std::string Squash(const std::string& s) { std::string o; for (char c : s) if (!isspace((unsigned char)c)) o += (char)tolower((unsigned char)c); return o; }
std::vector<std::string> Strs(const Json& a) { std::vector<std::string> v; for (auto& x : a.a) if (x.type == Json::STR) v.push_back(x.s); return v; }
}

std::string FindCoursesRoot() {
    std::vector<std::string> tries = {"study/courses", ExeDir() + "/study/courses", ExeDir() + "/../../study/courses"};
    for (auto& t : tries) { std::error_code ec; if (fs::is_directory(t, ec)) return fs::path(t).lexically_normal().string(); }
    return "";
}

bool LoadCourse(const std::string& dir, Course& out, std::string* err) {
    Json c;
    if (!LoadJsonFile(dir + "/pack/course.json", c, err)) return false;
    out = Course{};
    out.dir = dir; out.id = c["id"].Str(); out.code = c["code"].Str(); out.title = c["title"].Str(); out.term = c["term"].Str(); out.version = c["version"].Int();
    for (auto& u : c["units"].a) {
        CourseUnit cu; cu.id = u["id"].Str(); cu.title = u["title"].Str(); cu.type = u["type"].Str("computable"); cu.status = u["status"].Str("planned");
        Json uj, ij;
        if (LoadJsonFile(dir + "/pack/units/" + cu.id + ".json", uj, nullptr)) for (auto& s : uj["skills"].a) cu.skills.push_back({s["id"].Str(), s["name"].Str()});
        if (LoadJsonFile(dir + "/pack/items/" + cu.id + ".json", ij, nullptr))
            for (auto& it : ij["items"].a) { if (it["status"].Str("shipped") == "shipped") cu.items++; else cu.quarantined++; }
        out.units.push_back(cu);
    }
    return true;
}
std::vector<Course> LoadCourses(const std::string& root) {
    std::vector<Course> v;
    std::error_code ec;
    if (root.empty() || !fs::is_directory(root, ec)) return v;
    for (auto& e : fs::directory_iterator(root, ec)) {
        if (!e.is_directory()) continue;
        std::string name = e.path().filename().string();
        if (!name.empty() && name[0] == '_') continue;   // fixtures (the seeded-error test pack)
        Course c; if (LoadCourse(e.path().string(), c, nullptr)) v.push_back(c);
    }
    std::sort(v.begin(), v.end(), [](const Course& a, const Course& b) { return a.term == b.term ? a.id < b.id : a.term > b.term; });
    return v;
}

// ============================================================================ the verifier
namespace {

enum Sev { S_ERR, S_WARN };
struct Issue { Sev sev; std::string unit, item, gate, msg; };

struct Pack {
    std::string dir, root;   // course folder, and its pack/
    Json course, conventions, objectives;
    std::vector<std::string> unitIds;
    std::map<std::string, Json> units, know, anchors, blueprints, items, gates, quarantine;
    std::map<std::string, std::string> coverage;   // page id -> used / out_of_scope / unreadable
    std::map<std::string, const Json*> cardById, skillById, objById;
    std::map<std::string, std::string> cardUnit, skillUnit;
    std::vector<Issue> issues;
    std::map<std::string, std::map<std::string, std::string>> gateResult;   // item -> gate -> "pass" / reason
    bool quiet = false;
    void Add(Sev s, const std::string& unit, const std::string& item, const std::string& gate, const std::string& msg) {
        issues.push_back({s, unit, item, gate, msg});
        if (!item.empty() && s == S_ERR) { auto& g = gateResult[item][gate]; if (g.empty() || g == "pass") g = msg; }
    }
    void Pass(const std::string& item, const std::string& gate) { auto& g = gateResult[item][gate]; if (g.empty()) g = "pass"; }
};

// ---------------------------------------------------------------- coverage and page ids
void LoadCoverage(Pack& P) {
    std::istringstream in(ReadText(P.root + "/coverage.md"));
    std::string line;
    while (std::getline(in, line)) {
        std::string t = Trim(line);
        if (t.size() < 3 || t[0] != '-') continue;
        t = Trim(t.substr(1));
        size_t bar = t.find('|'); if (bar == std::string::npos) continue;
        std::string id = Trim(t.substr(0, bar)), rest = t.substr(bar + 1);
        size_t bar2 = rest.find('|');
        std::string st = Lower(Trim(bar2 == std::string::npos ? rest : rest.substr(0, bar2)));
        if (!id.empty() && id[0] == '`') id = id.substr(1, id.size() - 2);
        // a page range: SRC/p0001-p0400
        size_t dash = id.find("-p"), slash = id.rfind("/p");
        if (dash != std::string::npos && slash != std::string::npos && slash < dash) {
            std::string pre = id.substr(0, slash);
            int a = atoi(id.c_str() + slash + 2), b = atoi(id.c_str() + dash + 2);
            for (int k = a; k <= b; k++) { char buf[16]; snprintf(buf, sizeof buf, "/p%04d", k); P.coverage[pre + buf] = st; }
        } else P.coverage[id] = st;
    }
}
// a citation is good when it, or a prefix of it (SRC/5.4 for SRC/5.4/ex3), is logged as used
bool Cited(const Pack& P, const std::string& id, std::string* why) {
    std::string cur = id;
    for (;;) {
        auto it = P.coverage.find(cur);
        if (it != P.coverage.end()) { if (it->second == "used") return true; if (why) *why = id + " is logged as " + it->second; return false; }
        size_t s = cur.rfind('/'); if (s == std::string::npos || s == 0) break;
        cur = cur.substr(0, s);
    }
    if (why) *why = id + " is not in coverage.md";
    return false;
}

// ---------------------------------------------------------------- math helpers
expr::Domain DomainOf(const Json& item) {
    expr::Domain d;
    const Json& dm = item["domain"];
    for (auto& kv : dm.o) if (kv.second.Size() == 2) d.range[kv.first] = {kv.second[(size_t)0].Num(), kv.second[(size_t)1].Num()};
    d.range["C"] = {0, 0};   // the constant of integration
    d.range["K"] = {0, 0};
    return d;
}
bool IsWordAnswer(const std::string& s) { std::string l = Lower(Trim(s)); return l == "diverges" || l == "converges" || l == "dne" || l == "none"; }
double Num(const std::string& s) {
    std::string l = Lower(Trim(s));
    if (l == "inf" || l == "infinity" || l == "+inf") return INFINITY;
    if (l == "-inf" || l == "-infinity") return -INFINITY;
    return expr::Eval(expr::P(s), {});
}
bool Near(double a, double b, double rel, double abs = 1e-9) { return std::isfinite(a) && std::isfinite(b) && fabs(a - b) <= std::max(abs, rel * std::max(fabs(a), fabs(b))); }
bool UpToC(const Json& item) {
    if (item["upToC"].Bool()) return true;
    for (auto& c : item["check"].a) if (c["type"].Str() == "antiderivative") return true;
    return false;
}
// does answer `a` match key `k` under this item's comparison rules?
bool Matches(const Json& item, const std::string& a, const std::string& k, std::string* why = nullptr) {
    std::string form = item["form"].Str("expr");
    if (form == "choice") return Lower(Trim(a)) == Lower(Trim(k));
    if (form == "multi") { auto norm = [](std::string s) { std::string o; for (char c : s) if (isalnum((unsigned char)c)) o += (char)tolower((unsigned char)c); std::sort(o.begin(), o.end()); return o; }; return norm(a) == norm(k); }
    if (IsWordAnswer(a) || IsWordAnswer(k)) return Lower(Trim(a)) == Lower(Trim(k));
    Expr ea, ek;
    std::string e1, e2;
    if (!expr::Parse(a, ea, &e1)) { if (why) *why = "can't parse '" + a + "': " + e1; return false; }
    if (!expr::Parse(k, ek, &e2)) { if (why) *why = "can't parse the key '" + k + "': " + e2; return false; }
    if (form == "numeric") {
        double x = expr::Eval(ea, {}), y = expr::Eval(ek, {});
        double tol = item["tol"].Num(1e-6);
        if (!Near(x, y, tol)) { if (why) { char b[120]; snprintf(b, sizeof b, "%.8g vs %.8g", x, y); *why = b; } return false; }
        return true;
    }
    expr::Compare c = UpToC(item) ? expr::EquivalentUpToConstant(ea, ek, DomainOf(item), 10, 1e-6, 7) : expr::Equivalent(ea, ek, DomainOf(item), 10, 1e-6, 7);
    if (!c.equal && why) *why = c.why;
    return c.equal;
}

// the integral near a bound, to tell convergence from divergence numerically
bool Diverges(const Expr& f, const std::string& var, double a, double b, const std::string& at, std::string* why) {
    std::vector<double> I;
    for (int k = 1; k <= 7; k++) {
        double lo = a, hi = b;
        double s = pow(10.0, -k);   // for a finite singular bound
        if (at == "b") { if (std::isinf(b)) hi = pow(10.0, k + 1); else hi = b - s * std::max(1.0, fabs(b - a)); }
        else { if (std::isinf(a)) lo = -pow(10.0, k + 1); else lo = a + s * std::max(1.0, fabs(b - a)); }
        I.push_back(expr::Integrate(f, var, lo, hi));
    }
    double d1 = fabs(I[5] - I[4]), d2 = fabs(I[6] - I[5]);
    char buf[160]; snprintf(buf, sizeof buf, "partial integrals %.5g, %.5g, %.5g", I[4], I[5], I[6]);
    if (why) *why = buf;
    return d2 > 1e-4 && d2 > 0.9 * d1;   // (differences that stop shrinking: 1/x, 1/x^0.9; slow log divergence needs another check)
}

// one item's (or one part's) gate 1: the key checked a second way, every step checked, every distractor wrong
void Gate1(Pack& P, const std::string& unit, const std::string& id, const Json& item, bool& hadCheck) {
    std::string form = item["form"].Str("expr");
    std::string key = item["answer"].Str();
    auto err = [&](const std::string& m) { P.Add(S_ERR, unit, id, "1 computation", m); };
    const std::string var = item["var"].Str("x");
    expr::Domain dom = DomainOf(item);
    auto pointsIn = [&](int n) { std::vector<double> v; auto it = dom.range.find(var); auto r = it == dom.range.end() ? dom.def : it->second; for (int i = 0; i < n; i++) v.push_back(r.first + (r.second - r.first) * (i + 0.5) / n); return v; };

    // a single check against a candidate answer: does `ans` satisfy it?
    auto Satisfies = [&](const Json& c, const std::string& ans, std::string* why) -> bool {
        std::string ty = c["type"].Str();
        if (ty == "diverges") {
            Expr f = expr::P(c["integrand"].Str());
            bool dv = Diverges(f, c["var"].Str(var), Num(c["a"].Str("0")), Num(c["b"].Str("inf")), c["at"].Str("b"), why);
            return (Lower(Trim(ans)) == "diverges") == dv;
        }
        if (IsWordAnswer(ans)) { if (why) *why = "a word answer against a " + ty + " check"; return false; }
        Expr ea; std::string pe;
        if (!expr::Parse(ans, ea, &pe)) { if (why) *why = "can't parse '" + ans + "': " + pe; return false; }
        if (ty == "antiderivative") {
            Expr f = expr::P(c["of"].Str());
            if (!f.ok()) { if (why) *why = "the check's integrand doesn't parse"; return false; }
            int good = 0;
            for (double x : pointsIn(9)) {
                expr::Vars v{{var, x}, {"C", 0}, {"K", 0}};
                double d = expr::Derivative(ea, var, x, {{"C", 0}, {"K", 0}}), y = expr::Eval(f, v);
                if (!std::isfinite(d) || !std::isfinite(y)) continue;
                if (!Near(d, y, 1e-5, 1e-6)) { if (why) { char b[160]; snprintf(b, sizeof b, "d/d%s of the answer is %.7g at %s=%.3g, the integrand is %.7g", var.c_str(), d, var.c_str(), x, y); *why = b; } return false; }
                good++;
            }
            if (good < 5) { if (why) *why = "too few points in the domain"; return false; }
            return true;
        }
        if (ty == "derivative") {   // the answer is d/dvar of `of`
            Expr F = expr::P(c["of"].Str());
            int good = 0;
            for (double x : pointsIn(9)) {
                double d = expr::Derivative(F, var, x), y = expr::Eval(ea, {{var, x}});
                if (!std::isfinite(d) || !std::isfinite(y)) continue;
                if (!Near(d, y, 1e-5, 1e-6)) { if (why) { char b[160]; snprintf(b, sizeof b, "d/d%s of %s is %.7g at %.3g, the answer gives %.7g", var.c_str(), c["of"].Str().c_str(), d, x, y); *why = b; } return false; }
                good++;
            }
            if (good < 5) { if (why) *why = "too few points in the domain"; return false; }
            return true;
        }
        if (ty == "arclength") {   // the answer is the arc length integrand of `of`: sqrt(1 + f'^2), f' found numerically
            Expr F = expr::P(c["of"].Str());
            int good = 0;
            for (double x : pointsIn(9)) {
                double d = expr::Derivative(F, var, x), y = expr::Eval(ea, {{var, x}}), want = sqrt(1 + d * d);
                if (!std::isfinite(d) || !std::isfinite(y)) continue;
                if (!Near(y, want, 1e-5, 1e-6)) { if (why) { char b[160]; snprintf(b, sizeof b, "sqrt(1 + f'^2) is %.7g at %.3g, the answer gives %.7g", want, x, y); *why = b; } return false; }
                good++;
            }
            if (good < 5) { if (why) *why = "too few points in the domain"; return false; }
            return true;
        }
        if (ty == "solves") {   // the answer is a number that makes `expr` (in `var`) zero
            double v = expr::Eval(ea, {}), r = expr::Eval(expr::P(c["expr"].Str()), {{c["var"].Str("b"), v}});
            if (!std::isfinite(r) || fabs(r) > 1e-7) { if (why) { char b[120]; snprintf(b, sizeof b, "substituting the answer leaves %.7g, not 0", r); *why = b; } return false; }
            return true;
        }
        if (ty == "integral") {   // the answer is an integrand: its integral over [a, b] must equal `value` (found another way)
            double errEst = 0, I = expr::Integrate(ea, c["var"].Str(var), Num(c["a"].Str()), Num(c["b"].Str()), {}, &errEst);
            double v = Num(c["value"].Str());
            if (!Near(I, v, 1e-5, 1e-7)) { if (why) { char b[160]; snprintf(b, sizeof b, "the integrand integrates to %.9g over [%s, %s], expected %.9g", I, c["a"].Str().c_str(), c["b"].Str().c_str(), v); *why = b; } return false; }
            return true;
        }
        if (ty == "definite") {
            Expr f = expr::P(c["integrand"].Str());
            double errEst = 0, I = expr::Integrate(f, c["var"].Str(var), Num(c["a"].Str()), Num(c["b"].Str()), {}, &errEst);
            double v = expr::Eval(ea, {});
            if (!Near(I, v, 1e-5, 1e-7)) { if (why) { char b[160]; snprintf(b, sizeof b, "the integral is %.9g numerically, the answer is %.9g", I, v); *why = b; } return false; }
            return true;
        }
        if (ty == "equal") {
            Expr e2 = expr::P(c["expr"].Str());
            expr::Compare r = c["upToC"].Bool() ? expr::EquivalentUpToConstant(ea, e2, dom, 10) : expr::Equivalent(ea, e2, dom, 10);
            if (!r.equal && why) *why = "not equal to " + c["expr"].Str() + ": " + r.why;
            return r.equal;
        }
        if (ty == "value") {
            double x = expr::Eval(ea, {}), y = Num(c["expr"].Str());
            if (!Near(x, y, c["tol"].Num(1e-7))) { if (why) { char b[120]; snprintf(b, sizeof b, "%.9g vs %.9g", x, y); *why = b; } return false; }
            return true;
        }
        if (ty == "ode") {   // y' = rhs(x, y); the answer y(x) may hold C (tested at several C)
            Expr rhs = expr::P(c["rhs"].Str());
            std::string yv = c["y"].Str("y");
            for (double C : {0.0, 0.7, -1.3}) {
                bool hasC = false; for (auto& n : expr::Variables(ea)) if (n == "C") hasC = true;
                if (!hasC && C != 0) break;
                int good = 0;
                for (double x : pointsIn(7)) {
                    double y = expr::Eval(ea, {{var, x}, {"C", C}}), d = expr::Derivative(ea, var, x, {{"C", C}});
                    double r = expr::Eval(rhs, {{var, x}, {yv, y}});
                    if (!std::isfinite(y) || !std::isfinite(d) || !std::isfinite(r)) continue;
                    if (!Near(d, r, 1e-5, 1e-6)) { if (why) { char b[160]; snprintf(b, sizeof b, "y' = %.6g but the equation gives %.6g at %s=%.3g", d, r, var.c_str(), x); *why = b; } return false; }
                    good++;
                }
                if (good < 4) { if (why) *why = "too few points in the domain"; return false; }
            }
            if (c.Has("at")) {
                double y0 = expr::Eval(ea, {{var, Num(c["at"].Str())}, {"C", 0}});
                if (!Near(y0, Num(c["y0"].Str()), 1e-7)) { if (why) *why = "the initial condition fails"; return false; }
            }
            return true;
        }
        if (ty == "euler") {
            Expr rhs = expr::P(c["rhs"].Str());
            double x = Num(c["x0"].Str()), y = Num(c["y0"].Str()), h = Num(c["h"].Str());
            int n = c["n"].Int();
            for (int k = 0; k < n; k++) { y += h * expr::Eval(rhs, {{var, x}, {c["y"].Str("y"), y}}); x += h; }
            double v = expr::Eval(ea, {});
            if (!Near(v, y, c["tol"].Num(1e-6))) { if (why) { char b[120]; snprintf(b, sizeof b, "Euler gives %.9g, the answer is %.9g", y, v); *why = b; } return false; }
            return true;
        }
        if (ty == "series") {   // sum from n0 of term(n), compared by partial sums (with a tail estimate by extrapolation)
            Expr t = expr::P(c["term"].Str());
            std::string nv = c["var"].Str("n");
            long n0 = c["n0"].Int(1), N = c["terms"].Int(200000);
            double s = 0, sHalf = 0;
            for (long n = n0; n < n0 + N; n++) { s += expr::Eval(t, {{nv, (double)n}}); if (n == n0 + N / 2) sHalf = s; }
            double v = expr::Eval(ea, {});
            if (!Near(v, s, c["tol"].Num(1e-4)) && !Near(v, s + (s - sHalf), c["tol"].Num(1e-4))) { if (why) { char b[120]; snprintf(b, sizeof b, "the partial sums reach %.9g, the answer is %.9g", s, v); *why = b; } return false; }
            return true;
        }
        if (why) *why = "unknown check type '" + ty + "'";
        return false;
    };

    if (form == "choice" || form == "multi") {
        std::set<std::string> keys; for (char ch : key) if (isalnum((unsigned char)ch)) keys.insert(std::string(1, (char)tolower((unsigned char)ch)));
        int found = 0;
        for (auto& ch : item["choices"].a) {
            std::string k = Lower(ch["key"].Str());
            bool right = keys.count(k) > 0;
            if (right) found++;
            if (!right && ch["why"].Str().empty()) err("choice " + k + " has no explanation of why it is wrong");
            if (!right && ch["misconception"].Str().empty()) err("choice " + k + " comes from no listed misconception");
            if (!right && !ch["misconception"].Str().empty() && !P.cardById.count(ch["misconception"].Str())) err("choice " + k + " cites a missing misconception card " + ch["misconception"].Str());
            // a mathematical choice is checked against the item's checks: exactly the keyed ones may satisfy them
            if (ch.Has("expr") && item["check"].Size()) {
                bool ok = true; std::string why;
                for (auto& c : item["check"].a) if (!Satisfies(c, ch["expr"].Str(), &why)) { ok = false; break; }
                hadCheck = true;
                if (right && !ok) err("the keyed choice " + k + " fails the check: " + why);
                if (!right && ok) err("choice " + k + " is also correct (arguably right distractor)");
            }
        }
        if (found != (int)keys.size() || keys.empty()) err("the key '" + key + "' doesn't match the choices");
        return;
    }
    if (form == "parts") {
        int pi = 0;
        for (auto& part : item["parts"].a) {
            Json merged = part;   // parts inherit the item's var/domain
            if (!merged.Has("var") && item.Has("var")) merged.o.push_back({"var", item["var"]});
            if (!merged.Has("domain") && item.Has("domain")) merged.o.push_back({"domain", item["domain"]});
            Gate1(P, unit, id + "#" + std::to_string(++pi), merged, hadCheck);
            for (auto& g : P.gateResult[id + "#" + std::to_string(pi)]) if (g.second != "pass") P.Add(S_ERR, unit, id, "1 computation", "part " + std::to_string(pi) + ": " + g.second);
        }
        if (pi < 2) err("a multi-part item needs at least two parts");
        return;
    }
    if (form == "text") return;   // rubric-graded: gates 2-4 only

    // expr / numeric
    if (key.empty()) { err("no answer key"); return; }
    if (!IsWordAnswer(key)) { Expr k; std::string pe; if (!expr::Parse(key, k, &pe)) { err("the key doesn't parse: " + pe); return; } }
    if (item["check"].Size() == 0) err("no second-way check (antiderivative, definite, equal, value, ode, euler, series, diverges)");
    for (auto& c : item["check"].a) {
        std::string why;
        hadCheck = true;
        if (!Satisfies(c, key, &why)) err("the key fails its " + c["type"].Str() + " check: " + why);
    }
    // the constant of integration, per the conventions
    if (UpToC(item) && !item["noC"].Bool() && P.conventions["constantOfIntegration"].Bool(true)) {
        bool hasC = false; for (auto& n : expr::Variables(expr::P(key))) if (n == "C") hasC = true;
        if (!hasC) err("an antiderivative key without + C (conventions.json)");
    }
    // every wrong answer must really be wrong, and name its mistake
    for (auto& w : item["wrong"].a) {
        std::string wa = w["answer"].Str();
        if (w["mistake"].Str().empty()) err("wrong answer '" + wa + "' names no mistake");
        if (Matches(item, wa, key)) { err("wrong answer '" + wa + "' is equivalent to the key"); continue; }
        bool passesAll = item["check"].Size() > 0;
        for (auto& c : item["check"].a) { std::string why; if (!Satisfies(c, wa, &why)) { passesAll = false; break; } }
        if (passesAll) err("wrong answer '" + wa + "' passes the check (arguably correct)");
    }
    // every solution step that carries math is checked
    int si = 0;
    for (auto& s : item["solution"].a) {
        si++;
        const Json& m = s["math"]; if (m.IsNull()) continue;
        Expr l = expr::P(m["lhs"].Str()), r = expr::P(m["rhs"].Str());
        if (!l.ok() || !r.ok()) { err("step " + std::to_string(si) + ": its math doesn't parse"); continue; }
        expr::Domain d = dom; for (auto& kv : m["domain"].o) if (kv.second.Size() == 2) d.range[kv.first] = {kv.second[(size_t)0].Num(), kv.second[(size_t)1].Num()};
        expr::Compare cmp = m["upToC"].Bool() ? expr::EquivalentUpToConstant(l, r, d, 10) : expr::Equivalent(l, r, d, 10);
        if (!cmp.equal) err("step " + std::to_string(si) + ": " + m["lhs"].Str() + " = " + m["rhs"].Str() + " is false (" + cmp.why + ")");
        hadCheck = true;
    }
}

// ---------------------------------------------------------------- tier audit
struct Profile { int n = 0; double median = 0; int lo = 0, hi = 0; std::set<std::string> forms; };
Profile ProfileOf(const Json& anchors) {
    Profile p; std::vector<int> st;
    for (auto& a : anchors["anchors"].a) { st.push_back(a["steps"].Int()); p.forms.insert(a["form"].Str("expr")); }
    p.n = (int)st.size();
    if (st.empty()) return p;
    std::sort(st.begin(), st.end());
    p.lo = st.front(); p.hi = st.back();
    p.median = st.size() % 2 ? st[st.size() / 2] : 0.5 * (st[st.size() / 2 - 1] + st[st.size() / 2]);
    return p;
}
const char* TIERS[4] = {"easy", "medium", "hard", "challenging"};
int TierIx(const std::string& t) { for (int i = 0; i < 4; i++) if (t == TIERS[i]) return i; return -1; }
const char* TWISTS[] = {"extra step", "unfamiliar setup", "earlier unit", "word problem", "returns", "improper", "setup only"};

void TierAudit(Pack& P, const std::string& unit, const Json& it, const Profile& pr) {
    std::string id = it["id"].Str();
    int t = TierIx(it["tier"].Str());
    auto err = [&](const std::string& m) { P.Add(S_ERR, unit, id, "6 tier audit", m); };
    int steps = it["steps"].Int(), cue = it["cue"].Int(-1), nsk = (int)it["skills"].Size();
    if (t < 0) { err("unknown tier '" + it["tier"].Str() + "'"); return; }
    if (steps <= 0) err("no step count");
    if (cue < 0 || cue > 2) err("cue must be 0 (none), 1 (cued) or 2 (named)");
    if (pr.n == 0) { err("the unit has no anchors to measure against"); return; }
    switch (t) {
    case 0:
        if (nsk != 1) err("Easy uses one skill");
        if (steps >= pr.median && !(pr.median <= 1 && steps == 1)) err("Easy needs fewer steps than the anchor median (" + std::to_string(steps) + " vs " + std::to_string(pr.median) + ")");
        if (cue < 1) err("Easy names or strongly cues the method");
        break;
    case 1:
        if (steps < pr.lo || steps > pr.hi) err("Medium matches the anchors' step count (" + std::to_string(pr.lo) + "-" + std::to_string(pr.hi) + "), not " + std::to_string(steps));
        if (!pr.forms.count(it["form"].Str("expr"))) err("Medium uses an anchor's answer form");
        break;
    case 2: {
        std::string tw = Lower(it["twist"].Str());
        bool named = false; for (auto* w : TWISTS) if (tw.find(w) != std::string::npos) named = true;
        if (!named) err("Hard names its twist (extra step, unfamiliar setup, earlier unit, word problem, ...)");
        if (cue != 0) err("Hard doesn't cue the method");
        if (steps < pr.median) err("Hard is an anchor plus a twist: at least the median step count");
    } break;
    case 3:
        if (nsk < 3) err("Challenging combines three or more skills");
        if (cue != 0) err("Challenging doesn't cue the method");
        if (it["form"].Str() != "parts") err("Challenging is multi-part");
        break;
    }
    P.Pass(id, "6 tier audit");
}

// ---------------------------------------------------------------- loading a pack
bool LoadPack(Pack& P, const std::string& dir, const std::string& onlyUnit) {
    P.dir = dir; P.root = dir + "/pack";
    std::string e;
    if (!LoadJsonFile(P.root + "/course.json", P.course, &e)) { P.Add(S_ERR, "", "", "structure", e); return false; }
    if (!LoadJsonFile(P.root + "/conventions.json", P.conventions, &e)) P.Add(S_ERR, "", "", "structure", "conventions.json: " + e);
    if (!LoadJsonFile(P.root + "/objectives.json", P.objectives, &e)) P.Add(S_ERR, "", "", "structure", "objectives.json: " + e);
    for (auto& o : P.objectives["objectives"].a) P.objById[o["id"].Str()] = &o;
    LoadCoverage(P);
    for (auto& u : P.course["units"].a) {
        std::string id = u["id"].Str();
        if (u["status"].Str("planned") == "planned") continue;
        if (!onlyUnit.empty() && id != onlyUnit) continue;
        P.unitIds.push_back(id);
        auto load = [&](const char* sub, std::map<std::string, Json>& into, bool required) {
            std::string path = P.root + "/" + sub + "/" + id + ".json";
            if (!Exists(path)) { if (required) P.Add(S_ERR, id, "", "structure", std::string("missing ") + sub + "/" + id + ".json"); return; }
            std::string er; if (!LoadJsonFile(path, into[id], &er)) P.Add(S_ERR, id, "", "structure", er);
        };
        load("units", P.units, true); load("knowledge", P.know, true); load("anchors", P.anchors, true);
        load("blueprints", P.blueprints, true); load("items", P.items, true); load("quarantine", P.quarantine, false);
        std::string gp = P.root + "/verify/" + id + ".gates.json";
        if (Exists(gp)) { std::string er; if (!LoadJsonFile(gp, P.gates[id], &er)) P.Add(S_ERR, id, "", "structure", er); }
    }
    // every card and skill of every built unit (also when checking one unit: prerequisites may point elsewhere)
    for (auto& u : P.course["units"].a) {
        std::string id = u["id"].Str();
        if (u["status"].Str("planned") == "planned") continue;
        if (!P.know.count(id)) LoadJsonFile(P.root + "/knowledge/" + id + ".json", P.know[id], nullptr);
        if (!P.units.count(id)) LoadJsonFile(P.root + "/units/" + id + ".json", P.units[id], nullptr);
    }
    for (auto& kv : P.know) for (auto& c : kv.second["cards"].a) { std::string cid = c["id"].Str(); if (P.cardById.count(cid)) P.Add(S_ERR, kv.first, cid, "5 consistency", "duplicate card id"); P.cardById[cid] = &c; P.cardUnit[cid] = kv.first; }
    for (auto& kv : P.units) for (auto& s : kv.second["skills"].a) { std::string sid = s["id"].Str(); if (P.skillById.count(sid)) P.Add(S_ERR, kv.first, sid, "5 consistency", "duplicate skill id"); P.skillById[sid] = &s; P.skillUnit[sid] = kv.first; }
    return true;
}

// ---------------------------------------------------------------- the checks
void CheckCoverage(Pack& P) {
    for (auto& s : P.course["sources"].a) {
        std::string id = s["id"].Str();
        if (s["rank"].Int() < 1 || s["rank"].Int() > 5) P.Add(S_ERR, "", "", "coverage", "source " + id + " has no rank 1-5");
        if (s.Has("file") && !Exists(P.dir + "/" + s["file"].Str())) P.Add(S_ERR, "", "", "coverage", "source " + id + ": missing file " + s["file"].Str());
        int n = s["pages"].Int();
        int missing = 0; std::string first;
        for (int k = 1; k <= n; k++) { char b[16]; snprintf(b, sizeof b, "/p%04d", k); if (!P.coverage.count(id + b)) { if (!missing) first = id + b; missing++; } }
        for (auto& sec : Strs(s["sections"])) if (!P.coverage.count(id + "/" + sec)) { if (!missing) first = id + "/" + sec; missing++; }
        if (missing) P.Add(S_ERR, "", "", "coverage", "source " + id + ": " + std::to_string(missing) + " pages missing from coverage.md (first " + first + ")");
    }
}

void CheckGraph(Pack& P) {
    // objectives <-> skills, skills <-> cards, prerequisites acyclic
    std::set<std::string> objUsed, cardUsed;
    for (auto& uid : P.unitIds) {
        const Json& U = P.units[uid];
        for (auto& s : U["skills"].a) {
            std::string sid = s["id"].Str();
            if (s["name"].Str().empty()) P.Add(S_ERR, uid, sid, "skill graph", "a skill without a name");
            if (s["cards"].Size() == 0) P.Add(S_ERR, uid, sid, "skill graph", "cites no knowledge card");
            if (s["objectives"].Size() == 0) P.Add(S_ERR, uid, sid, "skill graph", "maps to no objective");
            for (auto& c : Strs(s["cards"])) { if (!P.cardById.count(c)) P.Add(S_ERR, uid, sid, "skill graph", "missing card " + c); cardUsed.insert(c); }
            for (auto& c : Strs(s["misconceptions"])) { if (!P.cardById.count(c)) P.Add(S_ERR, uid, sid, "skill graph", "missing misconception card " + c); cardUsed.insert(c); }
            for (auto& o : Strs(s["objectives"])) { if (!P.objById.count(o)) P.Add(S_ERR, uid, sid, "skill graph", "missing objective " + o); objUsed.insert(o); }
            for (auto& q : Strs(s["prereqs"])) if (!P.skillById.count(q) && q.find(':') == std::string::npos) P.Add(S_ERR, uid, sid, "skill graph", "missing prerequisite " + q);
        }
        for (auto& c : P.know[uid]["cards"].a) {
            std::string cid = c["id"].Str();
            if (!cardUsed.count(cid)) P.Add(S_ERR, uid, cid, "skill graph", "the card belongs to no skill");
            if (c["title"].Str().empty() || c["text"].Str().empty()) P.Add(S_ERR, uid, cid, "3 grounding", "a card needs a title and text");
            bool outside = c["outside"].Bool();
            if (outside && c["note"].Str().empty()) P.Add(S_ERR, uid, cid, "3 grounding", "an outside_sources card must explain itself");
            if (!outside && c["pages"].Size() == 0) P.Add(S_ERR, uid, cid, "3 grounding", "a card with no page id");
            for (auto& pg : Strs(c["pages"])) { std::string why; if (!Cited(P, pg, &why)) P.Add(S_ERR, uid, cid, "3 grounding", why); }
        }
    }
    // an objective of a built unit that no skill covers
    for (auto& o : P.objectives["objectives"].a) {
        bool mine = false; for (auto& u : Strs(o["units"])) if (std::find(P.unitIds.begin(), P.unitIds.end(), u) != P.unitIds.end()) mine = true;
        if (mine && !objUsed.count(o["id"].Str())) P.Add(S_ERR, "", o["id"].Str(), "skill graph", "an objective no skill maps to");
        if (o["page"].Str().empty()) P.Add(S_ERR, "", o["id"].Str(), "skill graph", "an objective without its page id");
    }
    // cycles
    std::map<std::string, int> mark;
    std::function<bool(const std::string&, std::string&)> dfs = [&](const std::string& s, std::string& path) -> bool {
        if (mark[s] == 1) { path = s; return true; }
        if (mark[s] == 2) return false;
        mark[s] = 1;
        auto it = P.skillById.find(s);
        if (it != P.skillById.end()) for (auto& q : Strs((*it->second)["prereqs"])) if (dfs(q, path)) { path = s + " -> " + path; return true; }
        mark[s] = 2;
        return false;
    };
    for (auto& kv : P.skillById) { std::string path; if (dfs(kv.first, path)) { P.Add(S_ERR, P.skillUnit[kv.first], kv.first, "skill graph", "prerequisite cycle: " + path); break; } }
}

void CheckItems(Pack& P, const std::string& uid) {
    const Json& I = P.items[uid];
    const Json& G = P.gates[uid];
    Profile pr = ProfileOf(P.anchors[uid]);
    // the anchors that use each skill (the tier rules measure an item against the anchors of its first skill when
    // there are at least two of them, otherwise against the whole unit)
    std::map<std::string, Profile> skillPr;
    { std::map<std::string, Json> per; for (auto& a : P.anchors[uid]["anchors"].a) for (auto& s : Strs(a["skills"])) { Json& j = per[s]; if (j.type != Json::OBJ) { j.type = Json::OBJ; Json arr; arr.type = Json::ARR; j.o.push_back({"anchors", arr}); } j.o[0].second.a.push_back(a); }
      for (auto& kv : per) { Profile sp = ProfileOf(kv.second); if (sp.n >= 2) skillPr[kv.first] = sp; } }
    std::set<std::string> unitSkills; for (auto& s : P.units[uid]["skills"].a) unitSkills.insert(s["id"].Str());
    std::map<std::string, std::string> seenPrompt;
    for (auto& a : P.anchors[uid]["anchors"].a) {
        std::string aid = a["id"].Str();
        seenPrompt[Squash(a["prompt"].Str())] = aid;
        std::string why;
        if (a["source"].Str().empty() || !Cited(P, a["source"].Str(), &why)) P.Add(S_ERR, uid, aid, "anchors", "an anchor must cite its page: " + why);
        for (auto& s : Strs(a["skills"])) if (!P.skillById.count(s)) P.Add(S_ERR, uid, aid, "anchors", "missing skill " + s);
        if (a["steps"].Int() <= 0) P.Add(S_ERR, uid, aid, "anchors", "an anchor needs its step count");
        if (!a.Has("keyMatch")) P.Add(S_ERR, uid, aid, "anchors", "an anchor must record its independent solve (keyMatch)");
        else if (!a["keyMatch"].Bool() && a["conflict"].Str().empty()) P.Add(S_ERR, uid, aid, "anchors", "the solve disagreed with the key but no conflict is logged");
    }
    // forbidden notation from the conventions
    std::vector<std::pair<std::string, std::string>> forbid;
    for (auto& f : P.conventions["forbid"].a) forbid.push_back({f["text"].Str(), f["why"].Str()});

    int count[4] = {0, 0, 0, 0};
    std::map<std::string, int> perSkill[4];
    std::set<std::string> ids;
    for (auto& it : I["items"].a) {
        std::string id = it["id"].Str();
        if (ids.count(id)) P.Add(S_ERR, uid, id, "5 consistency", "duplicate item id");
        ids.insert(id);
        if (it["status"].Str("shipped") != "shipped") continue;
        int t = TierIx(it["tier"].Str());
        if (t >= 0) { count[t]++; for (auto& s : Strs(it["skills"])) perSkill[t][s]++; }
        auto err = [&](const std::string& gate, const std::string& m) { P.Add(S_ERR, uid, id, gate, m); };

        // structure
        if (it["prompt"].Str().empty()) err("structure", "no prompt");
        if (it["skills"].Size() == 0) err("structure", "lists no skills");
        for (auto& s : Strs(it["skills"])) if (!P.skillById.count(s)) err("structure", "missing skill " + s);
        else if (!unitSkills.count(s) && t < 2) err("structure", "an Easy/Medium item uses a skill from another unit (" + s + ")");
        if (it["hints"].Size() != 3) err("structure", "needs exactly three hints");
        if (it["solution"].Size() == 0) err("structure", "no worked solution");
        if (it["commonMistake"].Str().empty()) err("structure", "the solution must end with the common mistake");
        if (it["reread"].Size() == 0) err("structure", "the solution must point to a page to reread");

        // gate 1
        bool hadCheck = false;
        std::string form = it["form"].Str("expr");
        size_t before = P.issues.size();
        Gate1(P, uid, id, it, hadCheck);
        bool g1ok = true; for (size_t k = before; k < P.issues.size(); k++) if (P.issues[k].sev == S_ERR && P.issues[k].gate == "1 computation" && P.issues[k].item == id) g1ok = false;
        if (g1ok) P.Pass(id, "1 computation");
        std::string label = it["label"].Str();
        if (label == "computed" && !hadCheck) err("1 computation", "labelled Computed but nothing was recomputed");
        if (label != "computed" && label != "sourced" && label != "coursework") err("structure", "label must be computed, sourced or coursework");
        if (label == "coursework" && !it.Has("anchor")) err("structure", "a From-your-coursework item names its anchor");
        if (label == "sourced" && hadCheck) err("structure", "a recomputed item should carry the Computed label");

        // gate 2: the blind solve (answers written by a separate pass that saw only the prompt)
        const Json& b = G["blind"][id.c_str()];
        if (b.IsNull()) err("2 blind solve", "no blind solve recorded");
        else if (form == "parts") {
            size_t np = it["parts"].Size();
            if (b.Size() != np) err("2 blind solve", "the blind solve has " + std::to_string(b.Size()) + " parts, the item " + std::to_string(np));
            else for (size_t k = 0; k < np; k++) {
                Json part = it["parts"][k];
                if (!part.Has("var") && it.Has("var")) part.o.push_back({"var", it["var"]});
                if (!part.Has("domain") && it.Has("domain")) part.o.push_back({"domain", it["domain"]});
                std::string why;
                if (part["form"].Str() == "text") continue;
                if (!Matches(part, b[k].Str(), part["answer"].Str(), &why)) err("2 blind solve", "part " + std::to_string(k + 1) + ": blind '" + b[k].Str() + "' vs key '" + part["answer"].Str() + "' (" + why + ")");
            }
        } else if (form != "text") {
            std::string why;
            if (!Matches(it, b.Str(), it["answer"].Str(), &why)) err("2 blind solve", "blind '" + b.Str() + "' vs key '" + it["answer"].Str() + "' (" + why + ")");
        }
        // a blind solver that found the problem ambiguous sends it to a third pass (INGEST "When checks disagree"),
        // which records its ruling in "adjudicated"; until then the item fails gate 2
        {
            std::string nk = "blind " + id;
            const Json& note = G["notes"][nk.c_str()];
            if (!note.IsNull() && G["adjudicated"][id.c_str()].IsNull()) err("2 blind solve", "the blind solver flagged it and no third pass has ruled: " + note.Str());
            const Json& adj = G["adjudicated"][id.c_str()];
            if (!adj.IsNull() && !adj["ok"].Bool()) err("2 blind solve", "the third pass ruled against it: " + adj["note"].Str());
        }
        if (P.gateResult[id]["2 blind solve"].empty()) P.Pass(id, "2 blind solve");

        // gate 3: grounding (automatic: citations resolve, steps cite cards; plus the grounding pass's verdict)
        if (it["sources"].Size() == 0) err("3 grounding", "cites no source page");
        for (auto& pg : Strs(it["sources"])) { std::string why; if (!Cited(P, pg, &why)) err("3 grounding", why); }
        for (auto& pg : Strs(it["reread"])) { std::string why; if (!Cited(P, pg, &why)) err("3 grounding", "reread: " + why); }
        bool anyInside = false;
        for (auto& c : Strs(it["cards"])) { auto cit = P.cardById.find(c); if (cit == P.cardById.end()) err("3 grounding", "missing card " + c); else if (!(*cit->second)["outside"].Bool()) anyInside = true; }
        if (!anyInside) err("3 grounding", "the answer rests only on outside_sources cards");
        int si = 0;
        for (auto& s : it["solution"].a) {
            si++;
            if (s["skill"].Str().empty() || !P.skillById.count(s["skill"].Str())) err("3 grounding", "step " + std::to_string(si) + " names no skill");
            if (!s["card"].Str().empty() && !P.cardById.count(s["card"].Str())) err("3 grounding", "step " + std::to_string(si) + " cites a missing card");
        }
        const Json& gr = G["grounding"][id.c_str()];
        if (gr.IsNull()) err("3 grounding", "no grounding pass recorded");
        else if (!gr["ok"].Bool()) err("3 grounding", "the grounding pass: " + gr["note"].Str());
        if (P.gateResult[id]["3 grounding"].empty()) P.Pass(id, "3 grounding");

        // gate 4: the adversarial review's verdict
        const Json& ad = G["adversarial"][id.c_str()];
        if (ad.IsNull()) err("4 adversarial", "no adversarial review recorded");
        else if (!ad["ok"].Bool()) err("4 adversarial", ad["note"].Str("rejected"));
        if (P.gateResult[id]["4 adversarial"].empty()) P.Pass(id, "4 adversarial");

        // gate 5: consistency (the first hint never gives the answer; no copied setups; conventions)
        std::string sq = Squash(it["prompt"].Str());


        if (seenPrompt.count(sq) && !(label == "coursework" && seenPrompt[sq] == it["anchor"].Str())) err("5 consistency", "the same prompt as " + seenPrompt[sq]);
        seenPrompt[sq] = id;
        std::string key = it["answer"].Str();
        if (form == "expr" || form == "numeric") {
            std::string h1 = Squash(it["hints"][(size_t)0].Str()), k = Squash(key);
            size_t cpos = k.rfind("+c"); if (cpos != std::string::npos && cpos + 2 == k.size()) k = k.substr(0, cpos);
            if (k.size() >= 3 && h1.find(k) != std::string::npos) err("5 consistency", "the first hint gives away the answer");
            for (size_t h = 0; h < 2; h++) { std::string hs = Squash(it["hints"][h].Str()); if (k.size() >= 4 && hs.find(k) != std::string::npos) err("5 consistency", "hint " + std::to_string(h + 1) + " states the final answer"); }
        }
        std::string all = it["prompt"].Str() + " " + key; for (auto& h : Strs(it["hints"])) all += " " + h; for (auto& s : it["solution"].a) all += " " + s["text"].Str();
        for (auto& f : forbid) if (!f.first.empty() && all.find(f.first) != std::string::npos) err("5 consistency", "'" + f.first + "' breaks the conventions: " + f.second);
        if (P.gateResult[id]["5 consistency"].empty()) P.Pass(id, "5 consistency");

        // gate 6
        auto sp = skillPr.find(it["skills"][(size_t)0].Str());
        TierAudit(P, uid, it, sp != skillPr.end() ? sp->second : pr);
    }
    // templates: the first two expanded instances of each were blind-solved (gate 2 for the template)
    if (I["templates"].Size()) {
        Json inst;
        if (!LoadJsonFile(P.root + "/instances/" + uid + ".json", inst, nullptr)) P.Add(S_ERR, uid, "", "structure", "templates were never expanded (run --course-expand)");
        else for (auto& t : inst["templates"].a) {
            std::string tid = t["template"].Str();
            for (size_t k = 0; k < 2 && k < t["instances"].Size(); k++) {
                std::string sid = tid + "#" + std::to_string(k);
                const Json& b = G["templates"][sid.c_str()];
                if (b.IsNull()) { P.Add(S_ERR, uid, tid, "2 blind solve", "instance " + sid + " was never blind-solved"); continue; }
                std::string why;
                if (!Matches(t["instances"][k], b.Str(), t["instances"][k]["answer"].Str(), &why)) P.Add(S_ERR, uid, tid, "2 blind solve", sid + ": blind '" + b.Str() + "' vs key (" + why + ")");
            }
        }
    }
    // blueprint and bank size
    const Json& B = P.blueprints[uid];
    for (int t = 0; t < 4; t++) if (count[t] < P.course["minPerTier"].Int(10)) P.Add(S_ERR, uid, "", "blueprint", std::string(TIERS[t]) + ": " + std::to_string(count[t]) + " shipped items, needs " + std::to_string(P.course["minPerTier"].Int(10)));
    for (auto& s : unitSkills) {
        if (perSkill[0][s] < 2) P.Add(S_ERR, uid, s, "blueprint", "fewer than two Easy items");
        if (perSkill[1][s] < 2) P.Add(S_ERR, uid, s, "blueprint", "fewer than two Medium items");
        if (perSkill[2][s] < 1) P.Add(S_ERR, uid, s, "blueprint", "no Hard item");
        if (perSkill[3][s] < 1) P.Add(S_ERR, uid, s, "blueprint", "no Challenging item covers it");
        const Json& cell = B["cells"][s.c_str()];
        for (int t = 0; t < 4; t++) { int want = cell[TIERS[t]].Int(0); if (perSkill[t][s] < want) P.Add(S_ERR, uid, s, "blueprint", std::string(TIERS[t]) + ": " + std::to_string(perSkill[t][s]) + " of the blueprint's " + std::to_string(want)); }
    }
}

// quarantine entries must say why
void CheckQuarantine(Pack& P, const std::string& uid) {
    for (auto& q : P.quarantine[uid]["items"].a) if (q["reason"].Str().empty()) P.Add(S_ERR, uid, q["id"].Str(), "quarantine", "a quarantined item must say why");
    for (auto& it : P.items[uid]["items"].a) if (it["status"].Str("shipped") == "quarantined" && it["reason"].Str().empty()) P.Add(S_ERR, uid, it["id"].Str(), "quarantine", "a quarantined item must say why");
}

std::string CourseDirFromArgs(int argc, char** argv, int& i) {
    std::string root = FindCoursesRoot();
    std::string name = (i < argc && argv[i][0] != '-') ? argv[i++] : "";
    if (!name.empty() && Exists(name + "/pack/course.json")) return name;
    if (root.empty()) return "";
    if (name.empty()) { auto cs = LoadCourses(root); return cs.empty() ? "" : cs.front().dir; }
    return root + "/" + name;
}

int Verify(Pack& P, const std::string& dir, const std::string& unit) {
    if (!LoadPack(P, dir, unit)) return 1;
    CheckCoverage(P);
    CheckGraph(P);
    for (auto& u : P.unitIds) { CheckItems(P, u); CheckQuarantine(P, u); }
    int errs = 0; for (auto& is : P.issues) if (is.sev == S_ERR) errs++;
    return errs;
}
}  // namespace

int RunCourseVerify(int argc, char** argv, int i) {
    std::string dir = CourseDirFromArgs(argc, argv, i);
    std::string unit = i < argc ? argv[i] : "";
    if (dir.empty()) { printf("course-verify: no course pack found (study/courses/<id>/pack/course.json)\n"); return 1; }
    Pack P;
    int errs = Verify(P, dir, unit);
    // the per-unit log
    for (auto& u : P.unitIds) {
        std::ofstream log(P.root + "/verify/" + u + ".log", std::ios::binary);
        if (!log) continue;
        log << "course-verify " << P.course["id"].Str() << " v" << P.course["version"].Int() << " unit " << u << "\n";
        for (auto& it : P.items[u]["items"].a) {
            std::string id = it["id"].Str();
            if (it["status"].Str("shipped") != "shipped") { log << id << "  quarantined: " << it["reason"].Str() << "\n"; continue; }
            log << id << " [" << it["tier"].Str() << ", " << it["label"].Str() << "]";
            for (const char* g : {"1 computation", "2 blind solve", "3 grounding", "4 adversarial", "5 consistency", "6 tier audit"}) {
                auto& r = P.gateResult[id][g];
                log << "  " << g[0] << ":" << (r == "pass" ? "ok" : r.empty() ? "--" : "FAIL");
            }
            log << "\n";
        }
        for (auto& is : P.issues) if (is.unit == u || is.unit.empty()) log << (is.sev == S_ERR ? "ERROR " : "warn  ") << is.gate << "  " << is.item << "  " << is.msg << "\n";
    }
    for (auto& is : P.issues) printf("%s [%s] %s %s: %s\n", is.sev == S_ERR ? "ERROR" : "warn ", is.gate.c_str(), is.unit.c_str(), is.item.c_str(), is.msg.c_str());
    int shipped = 0; for (auto& u : P.unitIds) for (auto& it : P.items[u]["items"].a) if (it["status"].Str("shipped") == "shipped") shipped++;
    printf("course-verify %s: %d units, %d shipped items, %d cards, %d skills, %d errors\n", P.course["id"].Str().c_str(), (int)P.unitIds.size(), shipped, (int)P.cardById.size(), (int)P.skillById.size(), errs);
    printf(errs ? "FAILED\n" : "OK\n");
    return errs ? 1 : 0;
}

int RunCourseReport(int argc, char** argv, int i) {
    std::string dir = CourseDirFromArgs(argc, argv, i);
    if (dir.empty()) { printf("course-report: no course pack found\n"); return 1; }
    Pack P;
    int errs = Verify(P, dir, "");
    const Json& C = P.course;
    printf("%s  %s, %s  (pack v%d)\n", C["code"].Str().c_str(), C["title"].Str().c_str(), C["term"].Str().c_str(), C["version"].Int());
    int used = 0, oos = 0, unread = 0, queued = 0; for (auto& kv : P.coverage) { if (kv.second == "used") used++; else if (kv.second == "unreadable") unread++; else if (kv.second == "queued") queued++; else oos++; }
    printf("sources: %d pages read and used, %d out of scope, %d unreadable, %d queued for later units (not read yet)\n", used, oos, unread, queued);
    std::string conflicts = ReadText(P.root + "/conflicts.md");
    int openConf = 0; { std::istringstream in(conflicts); std::string l; while (std::getline(in, l)) if (Trim(l).rfind("- [ ]", 0) == 0) openConf++; }
    int openReports = 0;
    { std::string rd = fs::path(dir).parent_path().parent_path().string() + "/reports"; std::error_code ec; if (fs::is_directory(rd, ec)) for (auto& e : fs::directory_iterator(rd, ec)) { std::string t = ReadText(e.path().string()); if (t.find(C["id"].Str()) != std::string::npos && t.find("status: open") != std::string::npos) openReports++; } }
    for (auto& u : C["units"].a) {
        std::string uid = u["id"].Str();
        printf("\n%s  %s  [%s, %s]\n", uid.c_str(), u["title"].Str().c_str(), u["type"].Str("computable").c_str(), u["status"].Str("planned").c_str());
        if (!P.items.count(uid)) continue;
        int cards = (int)P.know[uid]["cards"].Size(), skills = (int)P.units[uid]["skills"].Size();
        std::set<std::string> objs; for (auto& s : P.units[uid]["skills"].a) for (auto& o : Strs(s["objectives"])) objs.insert(o);
        int tier[4] = {}, lab[3] = {}, quar = 0, failing = 0;
        std::vector<std::string> qwhy;
        for (auto& it : P.items[uid]["items"].a) {
            if (it["status"].Str("shipped") != "shipped") { quar++; qwhy.push_back(it["id"].Str() + ": " + it["reason"].Str()); continue; }
            int t = TierIx(it["tier"].Str()); if (t >= 0) tier[t]++;
            std::string l = it["label"].Str(); lab[l == "computed" ? 0 : l == "sourced" ? 1 : 2]++;
            bool bad = false; for (auto& g : P.gateResult[it["id"].Str()]) if (g.second != "pass") bad = true;
            if (bad) failing++;
        }
        for (auto& q : P.quarantine[uid]["items"].a) { quar++; qwhy.push_back(q["id"].Str() + ": " + q["reason"].Str()); }
        printf("  %d knowledge cards, %d skills, %d objectives covered, %d anchors\n", cards, skills, (int)objs.size(), (int)P.anchors[uid]["anchors"].Size());
        printf("  items: %d easy, %d medium, %d hard, %d challenging  |  %d computed, %d sourced, %d from your coursework\n", tier[0], tier[1], tier[2], tier[3], lab[0], lab[1], lab[2]);
        int inst = 0; { Json ij; if (LoadJsonFile(P.root + "/instances/" + uid + ".json", ij, nullptr)) for (auto& t : ij["templates"].a) inst += (int)t["instances"].Size(); }
        printf("  templates: %d (%d verified instances)\n", (int)P.items[uid]["templates"].Size(), inst);
        printf("  unresolved gate failures among shipped items: %d\n", failing);
        printf("  quarantined: %d\n", quar);
        for (auto& w : qwhy) printf("    %s\n", w.c_str());
    }
    // the verified error rate: errors found after shipping, from the changelog's "error:" lines
    int shippedEver = 0, errorsFound = 0;
    { std::istringstream in(ReadText(fs::path(dir).parent_path().parent_path().string() + "/CHANGELOG.md")); std::string l; while (std::getline(in, l)) if (l.find(C["id"].Str()) != std::string::npos) { size_t p = l.find("items "); if (p != std::string::npos) shippedEver = std::max(shippedEver, atoi(l.c_str() + p + 6)); if (l.find("error:") != std::string::npos) errorsFound++; } }
    printf("\nopen conflicts: %d   open reports: %d   verified error rate: %d of %d shipped items\n", openConf, openReports, errorsFound, shippedEver);
    printf("errors in this pack: %d  (%s)\n", errs, errs ? "run --course-verify for the list" : "ready to study from");
    return errs ? 1 : 0;
}

// ============================================================================ templates
namespace {
struct Rng32 { uint32_t s; uint32_t N() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; } int Range(int a, int b) { return a + (int)(N() % (uint32_t)(b - a + 1)); } };
std::string Fmt(double v) { char b[32]; if (v == floor(v)) snprintf(b, sizeof b, "%.0f", v); else snprintf(b, sizeof b, "%g", v); return b; }
// {a} value, {a:p} parenthesised when negative, {a:c} as a coefficient (1 -> "", -1 -> "-"), {a:s} as a signed term (" + 3" / " - 3")
std::string Subst(const std::string& s, const std::map<std::string, double>& vals) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '{') {
            size_t j = s.find('}', i);
            if (j != std::string::npos) {
                std::string in = s.substr(i + 1, j - i - 1), mod;
                size_t c = in.find(':'); if (c != std::string::npos) { mod = in.substr(c + 1); in = in.substr(0, c); }
                auto it = vals.find(in);
                if (it != vals.end()) {
                    double v = it->second;
                    if (mod == "p") o += v < 0 ? "(" + Fmt(v) + ")" : Fmt(v);
                    else if (mod == "c") o += v == 1 ? "" : v == -1 ? "-" : Fmt(v);
                    else if (mod == "s") o += v < 0 ? " - " + Fmt(-v) : " + " + Fmt(v);
                    else o += Fmt(v);
                    i = j; continue;
                }
            }
        }
        o += s[i];
    }
    return o;
}
bool Constraint(const std::string& c, const std::map<std::string, double>& vals) {
    std::string s = Subst(c, vals);
    if (s.rfind("integer:", 0) == 0) { double v = expr::Eval(expr::P(s.substr(8)), {}); return std::isfinite(v) && fabs(v - round(v)) < 1e-9; }
    const char* ops[] = {"!=", "==", ">=", "<=", ">", "<"};
    for (const char* op : ops) {
        size_t p = s.find(op);
        if (p == std::string::npos) continue;
        double a = expr::Eval(expr::P(s.substr(0, p)), {}), b = expr::Eval(expr::P(s.substr(p + strlen(op))), {});
        if (!std::isfinite(a) || !std::isfinite(b)) return false;
        std::string o = op;
        if (o == "!=") return fabs(a - b) > 1e-9; if (o == "==") return fabs(a - b) <= 1e-9;
        if (o == ">=") return a >= b - 1e-12; if (o == "<=") return a <= b + 1e-12;
        if (o == ">") return a > b; return a < b;
    }
    return false;
}
}  // namespace

int RunCourseExpand(int argc, char** argv, int i) {
    std::string dir = CourseDirFromArgs(argc, argv, i);
    std::string only = i < argc ? argv[i] : "";
    if (dir.empty()) { printf("course-expand: no course pack found\n"); return 1; }
    Pack P;
    if (!LoadPack(P, dir, only)) { printf("course-expand: can't load the pack\n"); return 1; }
    int bad = 0;
    for (auto& uid : P.unitIds) {
        Json out; out.type = Json::OBJ;
        Json arr; arr.type = Json::ARR;
        for (auto& t : P.items[uid]["templates"].a) {
            std::string tid = t["id"].Str();
            std::string body = WriteJson(t["item"], -1);
            int want = t["count"].Int(200);
            Rng32 r{2166136261u ^ (uint32_t)std::hash<std::string>{}(tid)};
            std::set<std::string> seen;
            Json list; list.type = Json::ARR;
            int fails = 0, tries = 0;
            std::string firstFail;
            while ((int)list.a.size() < want && tries < want * 60) {
                tries++;
                std::map<std::string, double> vals;
                for (auto& v : t["vars"].o) {
                    const Json& d = v.second;
                    if (d.Has("choose")) vals[v.first] = d["choose"][(size_t)(r.N() % d["choose"].Size())].Num();
                    else { int a = d["from"].Int(), b = d["to"].Int(); int x; do { x = r.Range(a, b); } while (d["nonzero"].Bool() && x == 0); vals[v.first] = x; }
                }
                for (auto& dv : t["derived"].o) vals[dv.first] = expr::Eval(expr::P(Subst(dv.second.Str(), vals)), {});
                bool ok = true; for (auto& c : Strs(t["constraints"])) if (!Constraint(c, vals)) { ok = false; break; }
                if (!ok) continue;
                std::string keyv; for (auto& kv : vals) keyv += kv.first + "=" + Fmt(kv.second) + ";";
                if (seen.count(keyv)) continue;
                seen.insert(keyv);
                Json inst; std::string perr;
                if (!ParseJson(Subst(body, vals), inst, &perr)) { fails++; if (firstFail.empty()) firstFail = perr; continue; }
                // gate 1 on the instance: the answer is computed from the template's formula and checked a second way
                Pack Q; Q.conventions = P.conventions; Q.cardById = P.cardById;
                bool had = false;
                Gate1(Q, uid, tid, inst, had);
                bool pass = had; for (auto& is : Q.issues) if (is.sev == S_ERR) { pass = false; if (firstFail.empty()) firstFail = keyv + " " + is.msg; }
                if (!pass) { fails++; continue; }
                Json e; e.type = Json::OBJ;
                Json vj; vj.type = Json::OBJ; for (auto& kv : vals) vj.o.push_back({kv.first, JNum(kv.second)});
                e.o.push_back({"vars", vj});
                for (const char* f : {"prompt", "answer", "hints", "solution", "wrong", "check", "domain", "var"}) if (inst.Has(f)) e.o.push_back({f, inst[f]});
                list.a.push_back(e);
            }
            Json tj; tj.type = Json::OBJ;
            tj.o.push_back({"template", JStr(tid)});
            tj.o.push_back({"instances", list});
            arr.a.push_back(tj);
            printf("%s %s: %d instances (%d failed gate 1)%s%s\n", uid.c_str(), tid.c_str(), (int)list.a.size(), fails, firstFail.empty() ? "" : "  first failure: ", firstFail.c_str());
            if (fails || (int)list.a.size() < want) bad++;
        }
        out.o.push_back({"unit", JStr(uid)});
        out.o.push_back({"templates", arr});
        std::error_code ec; fs::create_directories(P.root + "/instances", ec);
        std::ofstream f(P.root + "/instances/" + uid + ".json", std::ios::binary);
        f << WriteJson(out, 1);
    }
    printf(bad ? "course-expand: %d templates need repair\n" : "course-expand: OK\n", bad);
    return bad ? 1 : 0;
}

// ============================================================================ the seeded-error test
// study/courses/_seeded_test is a copy of a real unit with 20 deliberately wrong items slipped in (each lists the error
// it plants in "planted"). The gates, including the recorded sub-agent passes, must catch every one, and must not
// reject the good items around them.
int RunCourseSeedTest(int argc, char** argv, int i) {
    std::string root = FindCoursesRoot();
    std::string dir = (i < argc && argv[i][0] != '-') ? argv[i] : root + "/_seeded_test";
    if (root.empty() || !Exists(dir + "/pack/course.json")) { printf("seed-test: no fixture at %s\n", dir.c_str()); return 1; }
    Pack P;
    Verify(P, dir, "");
    // which items carry a planted error lives apart from the items (pack/planted.json), so the reviewing passes can't see it
    Json plantedMap; LoadJsonFile(dir + "/pack/planted.json", plantedMap, nullptr);
    int planted = 0, caught = 0, falseAlarms = 0;
    for (auto& u : P.unitIds) for (auto& it : P.items[u]["items"].a) {
        std::string id = it["id"].Str();
        bool flagged = false; std::string by;
        for (auto& g : P.gateResult[id]) if (g.second != "pass") { flagged = true; by = g.first + ": " + g.second; break; }
        if (by.size() > 110) by = by.substr(0, 107) + "...";
        std::string kind = plantedMap[id.c_str()].Str(it["planted"].Str());
        if (!kind.empty()) {
            planted++;
            if (flagged) caught++;
            printf("  %-9s %-40s %s%s\n", id.c_str(), kind.c_str(), flagged ? "caught by " : "MISSED", by.c_str());
        } else if (flagged) { falseAlarms++; printf("  %-12s (good item) rejected: %s\n", id.c_str(), by.c_str()); }
    }
    printf("seed-test: %d of %d planted errors caught, %d good items rejected\n", caught, planted, falseAlarms);
    bool ok = planted >= 20 && caught == planted && falseAlarms == 0;
    printf(ok ? "OK\n" : "FAILED\n");
    return ok ? 0 : 1;
}
