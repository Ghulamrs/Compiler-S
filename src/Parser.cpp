#include "Parser.h"

namespace shalimar {

Parser::Parser(const std::vector<Token> &tokens, Diagnostics &diagnostics)
    : tokens_(tokens), diag_(diagnostics) {}

const Token &Parser::peek(size_t ahead) const {
    static const Token endOfInput;
    size_t k = index_ + ahead;
    return k < tokens_.size() ? tokens_[k] : endOfInput;
}

bool Parser::atOperator(const char *spelling) const {
    return current().kind == Tok::Operator && current().text == spelling;
}

const Token &Parser::advance() {
    const Token &t = current();
    if (index_ < tokens_.size()) ++index_;
    return t;
}

bool Parser::match(Tok kind) {
    if (!at(kind)) return false;
    advance();
    return true;
}

int Parser::lastLine() const {
    return tokens_.empty() ? 1 : tokens_.back().line;
}

std::string Parser::unexpected() const {
    if (current().kind == Tok::EndOfInput) return "Program ends unfinished";
    return "Unexpected '" + spellingOf(current()) + "'";
}

void Parser::fail(const std::string &text) {
    fail(current().line, text);
}

void Parser::fail(int line, const std::string &text) {
    if (failed_) return;
    diag_.error(line > 0 ? line : lastLine(), text);
    failed_ = true;
}

bool Parser::expect(Tok kind, const std::string &text) {
    if (match(kind)) return true;
    fail(text);
    return false;
}

bool Parser::startsLine(size_t at) const {
    if (at == 0) return true;
    if (at >= tokens_.size()) return false;
    return tokens_[at].line != tokens_[at - 1].line;
}

std::unique_ptr<Program> Parser::parse() {
    std::unique_ptr<Program> program(new Program());

    while (index_ < tokens_.size() && !failed_) {
        // Only declarations and 'fun' definitions may appear at global scope.
        if (!at(Tok::Fun)) { failUnexpected(); return nullptr; }
        std::unique_ptr<Function> f = parseFunction();
        if (failed_ || !f) return nullptr;
        program->add(std::move(f));
    }
    return failed_ ? nullptr : std::move(program);
}

// fun <outputs> = name(inputs) { body }
//
// '=' does duty as the separator between the output list and the name, which
// is the cause of the '>=' case below rather than a design. Because the lexer
// matches the longest operator first, 'fun <>= main()' - the closing '>' of
// the list written hard against the separator - arrives as one '>=' token.
// That spelling parsed before '>=' was an operator at all, so it is read here
// as the '>' plus the '=' rather than silently breaking programs over an
// operator they do not use.
std::unique_ptr<Function> Parser::parseFunction() {
    const int line = current().line;
    advance();                                            // 'fun'

    if (!atOperator("<")) { failUnexpected(); return nullptr; }
    advance();

    if (atOperator(">=")) {
        advance();
    } else {
        if (!atOperator(">")) { failUnexpected(); return nullptr; }
        advance();
        if (!atOperator("=")) { failUnexpected(); return nullptr; }
        advance();
    }

    if (!at(Tok::Identifier)) { failUnexpected(); return nullptr; }
    std::string name = advance().text;

    if (!expect(Tok::ParensOpen, unexpected())) return nullptr;
    if (!expect(Tok::ParensClose, unexpected())) return nullptr;

    Block body = parseBlock();
    if (failed_) return nullptr;

    return std::unique_ptr<Function>(new Function(Prototype(name, line), std::move(body)));
}

Block Parser::parseBlock() {
    Block body;
    if (!expect(Tok::BraceOpen, "Missing '{' to start block")) return body;

    // The loop detects end of input by the index rather than by a terminator
    // token, because tokenize() emits none.
    while (index_ < tokens_.size() && !at(Tok::BraceClose)) {
        StmtPtr s = parseStatement();
        if (failed_) return body;
        if (s) body.push_back(std::move(s));
    }
    if (!expect(Tok::BraceClose, "Missing '}' to close block")) return body;
    return body;
}

StmtPtr Parser::parseStatement() {
    if (at(Tok::PrintLine) || at(Tok::PrintInline)) return parsePrint();
    if (atDeclaration()) return parseDeclaration();
    if (at(Tok::If))    return parseIf();
    if (at(Tok::While)) return parseWhile();
    if (at(Tok::For))   return parseFor();

    if (at(Tok::Break) || at(Tok::Continue)) {
        const bool isBreak = at(Tok::Break);
        const int line = advance().line;
        if (loopDepth_ == 0) {
            fail(line, std::string("'") + (isBreak ? "break" : "continue") +
                       "' outside a loop");
            return nullptr;
        }
        if (isBreak) return StmtPtr(new Break(line));
        return StmtPtr(new Continue(line));
    }

    // An identifier followed by one of the four assignment spellings can only
    // begin a statement. That is the whole of how a statement boundary is
    // found here: there is no terminator, and the parser decides by what it
    // is looking at.
    if (at(Tok::Identifier)) {
        const Tok next = peek(1).kind;
        if (next == Tok::Assign || next == Tok::PlusAssign || next == Tok::MinusAssign ||
            (next == Tok::Operator && peek(1).text == "=")) {
            return parseAssignment();
        }
    }
    failUnexpected();
    return nullptr;
}

// 'int' opens a term when it is 'int(x)' and does not when it is 'int k : 5'.
// One token of lookahead separates them.
bool Parser::atDeclaration() const {
    if (!at(Tok::Int) && !at(Tok::Real) && !at(Tok::Char)) return false;
    return peek(1).kind != Tok::ParensOpen;
}

const Type *Parser::scalarTypeHere() {
    if (match(Tok::Int))  return Type::intType();
    if (match(Tok::Real)) return Type::realType();
    if (match(Tok::Char)) return Type::charType();
    return nullptr;
}

StmtPtr Parser::parseDeclaration() {
    const int line = current().line;
    const Type *type = scalarTypeHere();

    if (!at(Tok::Identifier)) { failUnexpected(); return nullptr; }
    const std::string name = advance().text;

    if (at(Tok::BracketOpen)) {
        diag_.unsupported(line, "an array declaration");
        failed_ = true;
        return nullptr;
    }

    ExprPtr initial;
    if (match(Tok::Assign)) {
        initial = parseExpression();
        if (failed_) return nullptr;
    }
    return StmtPtr(new Declare(type, name, std::move(initial), line));
}

// 'x : e', and the three spellings that mean the same thing. '=' is accepted
// silently in this position; '+:' and '-:' are expanded here into 'x : x + e'
// and 'x : x - e', so that nothing downstream has to know they existed.
StmtPtr Parser::parseAssignment() {
    const int line = current().line;
    const std::string name = advance().text;
    const Tok how = advance().kind;

    ExprPtr value = parseExpression();
    if (failed_) return nullptr;

    if (how == Tok::PlusAssign || how == Tok::MinusAssign) {
        const Binary::Op op = how == Tok::PlusAssign ? Binary::Op::Add : Binary::Op::Subtract;
        value.reset(new Binary(op, ExprPtr(new Var(name)), std::move(value)));
    }
    return StmtPtr(new Assign(ExprPtr(new Var(name)), std::move(value), line));
}

StmtPtr Parser::parseIf() {
    const int line = current().line;
    advance();

    std::unique_ptr<If> node(new If(line));
    ExprPtr condition = parseExpression();
    if (failed_) return nullptr;
    Block body = parseBlock();
    if (failed_) return nullptr;
    node->addBranch(std::move(condition), std::move(body));

    while (at(Tok::ElseIf)) {
        advance();
        ExprPtr next = parseExpression();
        if (failed_) return nullptr;
        Block branch = parseBlock();
        if (failed_) return nullptr;
        node->addBranch(std::move(next), std::move(branch));
    }
    if (match(Tok::Else)) {
        Block otherwise = parseBlock();
        if (failed_) return nullptr;
        node->setElse(std::move(otherwise));
    }
    return StmtPtr(node.release());
}

StmtPtr Parser::parseWhile() {
    const int line = current().line;
    advance();
    ExprPtr condition = parseExpression();
    if (failed_) return nullptr;
    ++loopDepth_;
    Block body = parseBlock();
    --loopDepth_;
    if (failed_) return nullptr;
    return StmtPtr(new While(std::move(condition), std::move(body), line));
}

// Two written forms:
//
//     for i : start to end [step s]     inclusive of end
//     for i < n [step s]                0 to n - 1
//
// The second is sugar for the first and is expanded here, which is why 'step'
// rides along unchanged and why nothing downstream knows the difference. The
// '<' needs no lookahead to disambiguate: a '<' in that position was a parse
// error in every earlier version of the language, so nothing legal changed
// meaning when it was given one.
StmtPtr Parser::parseFor() {
    const int line = current().line;
    advance();

    if (!at(Tok::Identifier)) { failUnexpected(); return nullptr; }
    const std::string name = advance().text;

    ExprPtr start;
    ExprPtr end;
    if (atOperator("<")) {
        advance();
        ExprPtr count = parseExpression();
        if (failed_) return nullptr;
        start.reset(new IntLit(0));
        end.reset(new Binary(Binary::Op::Subtract, std::move(count), ExprPtr(new IntLit(1))));
    } else {
        if (!expect(Tok::Assign, unexpected())) return nullptr;
        start = parseExpression();
        if (failed_) return nullptr;
        if (!expect(Tok::To, unexpected())) return nullptr;
        end = parseExpression();
        if (failed_) return nullptr;
    }

    ExprPtr step;
    if (match(Tok::Step)) {
        step = parseExpression();
        if (failed_) return nullptr;
    }

    ++loopDepth_;
    Block body = parseBlock();
    --loopDepth_;
    if (failed_) return nullptr;

    return StmtPtr(new For(name, std::move(start), std::move(end),
                           std::move(step), std::move(body), line));
}

// A print command must be the first token on its line. The rule's real
// purpose is at most one print command per line, since a second one
// necessarily has the first's items before it.
StmtPtr Parser::parsePrint() {
    if (!startsLine(index_)) {
        fail(std::string(at(Tok::PrintLine) ? "'?'" : "'?\?'") + " must start its line");
        return nullptr;
    }
    const bool newline = at(Tok::PrintLine);
    const int line = current().line;
    advance();

    std::unique_ptr<Print> node(new Print(newline, line));
    while (startsTerm() && !startsLine(index_) && !looksLikeNewStatement(index_)) {
        ExprPtr item = parseExpression();
        if (failed_) return nullptr;
        node->add(std::move(item));
    }
    return StmtPtr(node.release());
}

// Where a print item list stops. This is load-bearing rather than a
// convenience: a token missing from it does not produce an error, it silently
// ends the list. Anything added to the expression grammar as a prefix has to
// be added here too.
bool Parser::startsTerm() const {
    switch (current().kind) {
    case Tok::IntLiteral:
    case Tok::RealLiteral:
    case Tok::Identifier:
    case Tok::ParensOpen:
        return true;
    case Tok::Operator:
        // Unary minus, and nothing else in the operator class: a term cannot
        // begin with '*'.
        return current().text == "-";
    default:
        return false;
    }
}

bool Parser::looksLikeNewStatement(size_t at) const {
    if (at + 1 >= tokens_.size()) return false;
    if (tokens_[at].kind != Tok::Identifier) return false;
    const Token &next = tokens_[at + 1];
    switch (next.kind) {
    case Tok::Assign:
    case Tok::PlusAssign:
    case Tok::MinusAssign:
        return true;
    case Tok::Operator:
        return next.text == "=";
    default:
        return false;
    }
}

ExprPtr Parser::parseExpression() {
    return parseOr();
}

ExprPtr Parser::parseOr() {
    ExprPtr left = parseAnd();
    while (!failed_ && atOperator("|")) {
        advance();
        ExprPtr right = parseAnd();
        if (failed_) return nullptr;
        left.reset(new Binary(Binary::Op::Or, std::move(left), std::move(right)));
    }
    return left;
}

ExprPtr Parser::parseAnd() {
    ExprPtr left = parseComparison();
    while (!failed_ && atOperator("&")) {
        advance();
        ExprPtr right = parseComparison();
        if (failed_) return nullptr;
        left.reset(new Binary(Binary::Op::And, std::move(left), std::move(right)));
    }
    return left;
}

ExprPtr Parser::parseComparison() {
    ExprPtr left = parseAdditive();
    while (!failed_) {
        Binary::Op op;
        if      (atOperator("="))  op = Binary::Op::Equal;
        else if (atOperator("!=")) op = Binary::Op::NotEqual;
        else if (atOperator("<"))  op = Binary::Op::Less;
        else if (atOperator(">"))  op = Binary::Op::Greater;
        else if (atOperator("<=")) op = Binary::Op::LessEqual;
        else if (atOperator(">=")) op = Binary::Op::GreaterEqual;
        else break;
        advance();
        ExprPtr right = parseAdditive();
        if (failed_) return nullptr;
        left.reset(new Binary(op, std::move(left), std::move(right)));
    }
    return left;
}

ExprPtr Parser::parseAdditive() {
    ExprPtr left = parseMultiplicative();
    while (!failed_ && (atOperator("+") || atOperator("-"))) {
        const Binary::Op op = atOperator("+") ? Binary::Op::Add : Binary::Op::Subtract;
        advance();
        ExprPtr right = parseMultiplicative();
        if (failed_) return nullptr;
        left.reset(new Binary(op, std::move(left), std::move(right)));
    }
    return left;
}

ExprPtr Parser::parseMultiplicative() {
    ExprPtr left = parsePower();
    while (!failed_) {
        Binary::Op op;
        if      (atOperator("*")) op = Binary::Op::Multiply;
        else if (atOperator("/")) op = Binary::Op::Divide;
        else if (atOperator("%")) op = Binary::Op::Modulus;
        else break;
        advance();
        ExprPtr right = parsePower();
        if (failed_) return nullptr;
        left.reset(new Binary(op, std::move(left), std::move(right)));
    }
    return left;
}

// '^' is right-associative: '2^3^2' is 2^(3^2), which is 512 and not 64.
ExprPtr Parser::parsePower() {
    ExprPtr base = parsePrimary();
    if (failed_ || !atOperator("^")) return base;
    advance();
    ExprPtr exponent = parsePower();
    if (failed_) return nullptr;
    return ExprPtr(new Binary(Binary::Op::Power, std::move(base), std::move(exponent)));
}

ExprPtr Parser::parsePrimary() {
    // Unary minus folds the negation into its operand here and returns it as
    // one finished term, before the caller can see a following '^'. That is
    // why '-2^2' is '(-2)^2' and evaluates to 4, unlike ordinary maths
    // notation. On the exponent side the recursion in parsePower() puts it
    // back the usual way round, so '2^-2' is 2^(-2).
    if (atOperator("-")) {
        advance();
        ExprPtr operand = parsePrimary();
        if (failed_) return nullptr;
        // A negated literal becomes the literal, so '-5' is one constant
        // rather than a subtraction from zero. Anything else is the
        // subtraction, which is all the language needs a unary minus to be.
        if (operand->isIntLiteral()) {
            const int32_t value = static_cast<const IntLit &>(*operand).value();
            return ExprPtr(new IntLit(-value));
        }
        if (operand->isRealLiteral()) {
            const double value = static_cast<const RealLit &>(*operand).value();
            return ExprPtr(new RealLit(-value));
        }
        return ExprPtr(new Binary(Binary::Op::Subtract,
                                  ExprPtr(new IntLit(0)), std::move(operand)));
    }
    if (at(Tok::IntLiteral)) {
        const Token &t = advance();
        return ExprPtr(new IntLit(t.intValue));
    }
    if (at(Tok::RealLiteral)) {
        const Token &t = advance();
        return ExprPtr(new RealLit(t.realValue));
    }
    if (at(Tok::Identifier)) {
        const Token &t = advance();
        return ExprPtr(new Var(t.text));
    }
    if (at(Tok::ParensOpen)) {
        advance();
        ExprPtr inner = parseExpression();
        if (failed_) return nullptr;
        if (!expect(Tok::ParensClose, unexpected())) return nullptr;
        return inner;
    }
    failUnexpected();
    return nullptr;
}

}  // namespace shalimar
