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
    failUnexpected();
    return nullptr;
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
    while (startsTerm()) {
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
        return true;
    default:
        return false;
    }
}

ExprPtr Parser::parseExpression() {
    return parsePrimary();
}

ExprPtr Parser::parsePrimary() {
    if (at(Tok::IntLiteral)) {
        const Token &t = advance();
        return ExprPtr(new IntLit(t.intValue));
    }
    failUnexpected();
    return nullptr;
}

}  // namespace shalimar
