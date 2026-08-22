// The parser.
//
// Recursive descent over the token vector, growing one construct at a time.
// It holds a cursor and nothing else that outlives a parse, so a Parser is
// made, asked once, and thrown away.
//
// Statements have no terminator. Where one ends is decided by what the parser
// is looking at, not by punctuation - the helpers that carry that weight
// arrive with the constructs that need them.
//
// Parse errors are reported one at a time and parsing stops at the first.
// Unlike the checker, there is nothing useful to say after the stream has
// desynced.
#pragma once

#include "Ast.h"
#include "Diag.h"
#include "Token.h"

#include <memory>
#include <string>
#include <vector>

namespace shalimar {

class Parser {
public:
    Parser(const std::vector<Token> &tokens, Diagnostics &diagnostics);

    // Null when the program did not parse; the reason is in the diagnostics.
    std::unique_ptr<Program> parse();

private:
    const std::vector<Token> &tokens_;
    Diagnostics &diag_;
    size_t index_ = 0;
    bool failed_ = false;

    // End of input is virtual: peeking past the end manufactures an
    // EndOfInput token rather than indexing out of bounds, which is what lets
    // a missing brace be reported as a missing brace.
    const Token &peek(size_t ahead = 0) const;
    const Token &current() const { return peek(0); }
    bool at(Tok kind) const { return current().kind == kind; }
    bool atOperator(const char *spelling) const;
    const Token &advance();
    bool match(Tok kind);

    // Records the error and sets the failed flag; every caller returns
    // immediately after, so one error is reported and parsing stops.
    void fail(const std::string &text);
    void fail(int line, const std::string &text);
    bool expect(Tok kind, const std::string &text);

    // 'Unexpected '*'' for a token that is there, and a sentence for one that
    // is not: running off the end has no spelling to quote.
    std::string unexpected() const;
    void failUnexpected() { fail(unexpected()); }
    int lastLine() const;

    // A print command must be the first token on its line. Indentation does
    // not count - the lexer dropped it - so this compares the token's line
    // with its predecessor's.
    bool startsLine(size_t at) const;

    // Whether the current token can begin a term. Used to find the end of a
    // print item list, where a missing entry ends the list rather than
    // reporting anything.
    bool startsTerm() const;

    std::unique_ptr<Function> parseFunction();
    Block parseBlock();
    StmtPtr parseStatement();
    StmtPtr parsePrint();
    // One method per precedence tier, loosest first. '^' is the only
    // right-associative one and recurses into itself for that reason.
    ExprPtr parseExpression();
    ExprPtr parseOr();
    ExprPtr parseAnd();
    ExprPtr parseComparison();
    ExprPtr parseAdditive();
    ExprPtr parseMultiplicative();
    ExprPtr parsePower();
    ExprPtr parsePrimary();
};

}  // namespace shalimar
