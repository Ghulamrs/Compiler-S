#include "Resolve.h"

#include "Builtin.h"

#include <algorithm>
#include <set>

namespace shalimar {
namespace {

class Names : public NodeVisitor {
public:
    bool callsOnly = false;
    std::vector<std::string> found;

    void walk(Block &body) { for (StmtPtr &s : body) s->accept(*this); }
    void walk(ExprPtr &e) { if (e) e->accept(*this); }

    void visit(IntLit &) override {}
    void visit(RealLit &) override {}
    void visit(StrLit &) override {}
    void visit(Blank &) override {}
    void visit(Break &) override {}
    void visit(Continue &) override {}

    void visit(Var &n) override { if (!callsOnly) found.push_back(n.name()); }

    void visit(ArrayLit &n) override { for (ExprPtr &e : n.elements()) walk(e); }
    void visit(Index &n) override { walk(n.baseRef()); walk(n.indexRef()); }
    void visit(Dim &n) override { walk(n.base()); walk(n.axis()); }
    void visit(Precision &n) override { walk(n.places()); }
    void visit(Convert &n) override { walk(n.operand()); }
    void visit(Binary &n) override { walk(n.left()); walk(n.right()); }

    void visit(Call &n) override {
        found.push_back(n.callee());
        for (ExprPtr &e : n.arguments()) walk(e);
    }

    void visit(Declare &n) override {
        if (!callsOnly) found.push_back(n.name());
        for (ExprPtr &e : n.extents()) walk(e);
        walk(n.initial());
    }
    void visit(Assign &n) override { walk(n.target()); walk(n.expr()); }
    void visit(CompoundAssign &n) override { walk(n.target()); walk(n.expr()); }
    void visit(MultiAssign &n) override {
        if (!callsOnly)
            for (const std::string &name : n.names()) found.push_back(name);
        walk(n.call());
    }
    void visit(CallStmt &n) override { walk(n.call()); }
    void visit(Return &n) override { for (ExprPtr &e : n.exprs()) walk(e); }
    void visit(Print &n) override { for (ExprPtr &e : n.items()) walk(e); }
    void visit(If &n) override {
        for (If::Branch &b : n.branches()) { walk(b.condition); walk(b.body); }
        if (n.hasElse()) walk(n.elseBody());
    }
    void visit(While &n) override { walk(n.condition()); walk(n.body()); }
    void visit(For &n) override {
        if (!callsOnly) found.push_back(n.variable());
        walk(n.start()); walk(n.end()); walk(n.step()); walk(n.body());
    }
};

std::string globalName(StmtPtr &statement) {
    Declare *declaration = dynamic_cast<Declare *>(statement.get());
    return declaration ? declaration->name() : std::string();
}

}

std::vector<std::string> calledNames(Program &program) {
    Names names;
    names.callsOnly = true;
    for (std::unique_ptr<Function> &f : program.functions()) names.walk(f->body());
    for (StmtPtr &g : program.globals()) g->accept(names);
    return names.found;
}

std::vector<std::string> mentionedNames(Function &function) {
    Names names;
    names.walk(function.body());
    return names.found;
}

bool Resolver::resolve(Unit &main, std::vector<Unit> &others) {
    if (others.empty()) return true;

    std::set<std::string> have;
    for (std::unique_ptr<Function> &f : main.program->functions())
        have.insert(f->proto().name);

    std::vector<std::string> wanted = calledNames(*main.program);
    std::set<size_t> reached;
    bool clashed = false;

    while (!wanted.empty()) {
        const std::string name = wanted.back();
        wanted.pop_back();

        if (have.count(name) || findBuiltin(name) >= 0) continue;

        if (name == "main") continue;

        std::vector<size_t> answering;
        for (size_t u = 0; u < others.size(); ++u) {
            for (std::unique_ptr<Function> &f : others[u].program->functions()) {
                if (f && f->proto().name == name) { answering.push_back(u); break; }
            }
        }

        if (answering.empty()) continue;

        if (answering.size() > 1) {

            std::string where = others[answering[0]].name;
            for (size_t i = 1; i < answering.size(); ++i)
                where += (i + 1 == answering.size() ? " and " : ", ") + others[answering[i]].name;
            diag_.error(0, 0, "'" + name + "' is in " + where + " - it can be in one");
            clashed = true;
            have.insert(name);
            continue;
        }

        Unit &from = others[answering[0]];
        std::unique_ptr<Function> taken;
        std::vector<std::unique_ptr<Function>> &functions = from.program->functions();
        for (size_t i = 0; i < functions.size(); ++i) {
            if (!functions[i] || functions[i]->proto().name != name) continue;
            taken = std::move(functions[i]);
            break;
        }
        if (!taken) continue;

        have.insert(name);
        if (reached.insert(answering[0]).second) used_.push_back(from.name);

        std::vector<std::string> mentions = mentionedNames(*taken);
        for (StmtPtr &g : from.program->globals()) {
            if (!g) continue;
            const std::string global = globalName(g);
            if (global.empty()) continue;
            if (std::find(mentions.begin(), mentions.end(), global) == mentions.end())
                continue;
            main.program->addGlobal(StmtPtr(g.release()));
        }

        Names calls;
        calls.callsOnly = true;
        calls.walk(taken->body());
        for (const std::string &next : calls.found) wanted.push_back(next);
        main.program->add(std::move(taken));
    }

    return !clashed;
}

std::vector<std::string> calledNamesIn(Stmt &statement) {
    Names calls;
    calls.callsOnly = true;
    statement.accept(calls);
    return calls.found;
}

std::vector<std::string> calledNamesIn(Function &function) {
    Names calls;
    calls.callsOnly = true;
    calls.walk(function.body());
    return calls.found;
}

}
