#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace std;

/*
AI-assisted development disclosure (2026-07-24):
- Tool: OpenAI Codex.
- Scope: semantic analysis, stable symbol/function IDs, typed SSA IR, CFG,
  use-def chains, sealed-block SSA construction, MemorySSA, IR verification,
  integration diagnostics, array-argument ABI compatibility, loop-invariant
  load caching, basic-block copy propagation, redundant instruction removal,
  destination-aware integer expression lowering, in-place constant remainder,
  constant- and runtime-trip-count partial loop unrolling, loop-pointer strength
  reduction across local declarations and invariant index offsets,
  destination-register integer accumulator updates, cost-aware direct
   self-tail-call elimination with dependency-scheduled parallel argument
   preservation, and regression fixes.
- Human changes: the contestant selected the architecture, performance goals,
  and implementation scope; no additional manual edits have been made to this
  generated section at the time of this record.
- Validation: all 140 local functional inputs pass semantic/SSA verification;
  all 8 local RISC-V performance probes compile, link, and run successfully.
*/
// 编译错误异常类
//在 C++ 中，runtime_error 是标准库（<stdexcept>）提供的一个标准异常类。
//让 CompileError 继承它，意思是：“CompileError 是一种特殊的运行时错误”。这样它就天然继承了标准错误类的功能，比如可以用 .what() 方法直接获取存放在里面的错误字符串。
struct CompileError : runtime_error {
    //explicit 关键字的意思是“显式的”。它用来防止 C++ 编译器进行意料之外的隐式类型转换
    //如果没有 explicit： 如果某个地方需要一个 CompileError 对象，而你一不小心传了一个普通的字符串 "error"，C++ 会悄悄地帮你自动调用构造函数，把字符串变成 CompileError。这有时会导致难以排查的 Bug。
    //加上 explicit 后： 你必须规规矩矩、清清楚楚地写成 CompileError("错误信息")，绝对不允许偷懒和隐式转换。这是一种很好的编写健壮 C++ 代码的习惯。
    explicit CompileError(const string &msg) : runtime_error(msg) {}
};
// 生成错误消息，包含行号和错误信息
static string lineMsg(int line, const string &msg) {
    ostringstream os;
    os << "line " << line << ": " << msg;
    return os.str();
}
// 抛出编译错误异常
[[noreturn]] static void fail(int line, const string &msg) {
    throw CompileError(lineMsg(line, msg));
}
// 判断字符是否可以作为标识符的起始字符（字母或下划线）
static bool isIdentStart(char c) {
    return isalpha(static_cast<unsigned char>(c)) || c == '_';
}
// 判断字符是否可以作为标识符的组成字符（字母、数字或下划线）
static bool isIdentChar(char c) {
    return isalnum(static_cast<unsigned char>(c)) || c == '_';
}
// 将整数v向上对齐到a的倍数
static int alignTo(int v, int a) {
    return (v + a - 1) / a * a;
}

enum class TokKind {
    End, Ident, IntLit, FloatLit, StringLit,
    KwConst, KwInt, KwFloat, KwVoid, KwIf, KwElse, KwWhile,
    KwBreak, KwContinue, KwReturn,
    Plus, Minus, Star, Slash, Percent, Bang,
    Assign, Eq, Ne, Lt, Gt, Le, Ge, AndAnd, OrOr,
    Comma, Semi, LParen, RParen, LBrace, RBrace, LBracket, RBracket
};
// 词法单元
struct Token {
    TokKind kind = TokKind::End;// 词法单元类型
    string text;// 词法单元文本
    int line = 1;// 词法单元所在行号(后续报错提示)
    int col = 1;// 词法单元所在列号
    long long intVal = 0;// 整数值（仅当kind为IntLit时有效）如果是，存储整数值
    float floatVal = 0.0f;// 浮点数值（仅当kind为FloatLit时有效）如果是，存储浮点数值
    string strVal;// 字符串值（仅当kind为StringLit时有效）如果是，存储字符串值
};
// 词法分析器类
class Lexer {
public:
    explicit Lexer(string src) : src_(std::move(src)) {}

    vector<Token> lexAll() {
        vector<Token> out;
        while (true) {
            Token t = next();
            out.push_back(t);
            if (t.kind == TokKind::End) break;
        }
        return out;
    }

private:
    string src_;
    size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;

    char peek(int off = 0) const {
        size_t p = pos_ + static_cast<size_t>(off);
        return p < src_.size() ? src_[p] : '\0';
    }

    char get() {
        char c = peek();
        if (!c) return '\0';
        ++pos_;
        if (c == '\n') {
            ++line_;
            col_ = 1;
        } else {
            ++col_;
        }
        return c;
    }

    bool match(char c) {
        if (peek() != c) return false;
        get();
        return true;
    }
    // 跳过空白字符和注释
    void skipTrivia() {
        while (true) {
            // 跳过所有空白字符（空格、换行、制表符等）
            while (isspace(static_cast<unsigned char>(peek()))) get();
            // 跳过单行注释
            if (peek() == '/' && peek(1) == '/') {
                while (peek() && peek() != '\n') get();
                continue;
            }
            // 跳过多行注释
            if (peek() == '/' && peek(1) == '*') {
                get();
                get();
                while (peek()) {
                    if (peek() == '*' && peek(1) == '/') {
                        get();
                        get();
                        break;
                    }
                    get();
                }
                continue;
            }
            break;
        }
    }
    // 创建一个Token对象
    Token make(TokKind k, const string &text, int line, int col) {
        Token t;
        t.kind = k;
        t.text = text;
        t.line = line;
        t.col = col;
        return t;
    }

    Token next() {
        skipTrivia();
        int line = line_, col = col_;
        char c = peek();
        if (!c) return make(TokKind::End, "", line, col);

        if (isIdentStart(c)) {
            string s;
            while (isIdentChar(peek())) s.push_back(get());
            static const unordered_map<string, TokKind> kw = {
                {"const", TokKind::KwConst}, {"int", TokKind::KwInt},
                {"float", TokKind::KwFloat}, {"void", TokKind::KwVoid},
                {"if", TokKind::KwIf}, {"else", TokKind::KwElse},
                {"while", TokKind::KwWhile}, {"break", TokKind::KwBreak},
                {"continue", TokKind::KwContinue}, {"return", TokKind::KwReturn},
            };
            auto it = kw.find(s);
            return make(it == kw.end() ? TokKind::Ident : it->second, s, line, col);
        }

        if (isdigit(static_cast<unsigned char>(c)) || (c == '.' && isdigit(static_cast<unsigned char>(peek(1))))) {
            return number(line, col);
        }

        if (c == '"') {
            return stringLiteral(line, col);
        }

        get();
        switch (c) {
            case '+': return make(TokKind::Plus, "+", line, col);
            case '-': return make(TokKind::Minus, "-", line, col);
            case '*': return make(TokKind::Star, "*", line, col);
            case '/': return make(TokKind::Slash, "/", line, col);
            case '%': return make(TokKind::Percent, "%", line, col);
            case ',': return make(TokKind::Comma, ",", line, col);
            case ';': return make(TokKind::Semi, ";", line, col);
            case '(': return make(TokKind::LParen, "(", line, col);
            case ')': return make(TokKind::RParen, ")", line, col);
            case '{': return make(TokKind::LBrace, "{", line, col);
            case '}': return make(TokKind::RBrace, "}", line, col);
            case '[': return make(TokKind::LBracket, "[", line, col);
            case ']': return make(TokKind::RBracket, "]", line, col);
            case '!':
                if (match('=')) return make(TokKind::Ne, "!=", line, col);
                return make(TokKind::Bang, "!", line, col);
            case '=':
                if (match('=')) return make(TokKind::Eq, "==", line, col);
                return make(TokKind::Assign, "=", line, col);
            case '<':
                if (match('=')) return make(TokKind::Le, "<=", line, col);
                return make(TokKind::Lt, "<", line, col);
            case '>':
                if (match('=')) return make(TokKind::Ge, ">=", line, col);
                return make(TokKind::Gt, ">", line, col);
            case '&':
                if (match('&')) return make(TokKind::AndAnd, "&&", line, col);
                break;
            case '|':
                if (match('|')) return make(TokKind::OrOr, "||", line, col);
                break;
        }
        fail(line, string("unexpected character '") + c + "'");
    }

    Token number(int line, int col) {
        size_t start = pos_;
        bool isFloat = false;
        if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
            get();
            get();
            while (isxdigit(static_cast<unsigned char>(peek()))) get();
            if (peek() == '.') {
                isFloat = true;
                get();
                while (isxdigit(static_cast<unsigned char>(peek()))) get();
            }
            if (peek() == 'p' || peek() == 'P') {
                isFloat = true;
                get();
                if (peek() == '+' || peek() == '-') get();
                while (isdigit(static_cast<unsigned char>(peek()))) get();
            }
        } else {
            while (isdigit(static_cast<unsigned char>(peek()))) get();
            if (peek() == '.') {
                isFloat = true;
                get();
                while (isdigit(static_cast<unsigned char>(peek()))) get();
            }
            if (peek() == 'e' || peek() == 'E') {
                isFloat = true;
                get();
                if (peek() == '+' || peek() == '-') get();
                while (isdigit(static_cast<unsigned char>(peek()))) get();
            }
        }
        while (isalpha(static_cast<unsigned char>(peek()))) get();
        string text = src_.substr(start, pos_ - start);
        Token t = make(isFloat ? TokKind::FloatLit : TokKind::IntLit, text, line, col);
        if (isFloat) {
            t.floatVal = strtof(text.c_str(), nullptr);
        } else {
            string core = text;
            while (!core.empty()) {
                char b = core.back();
                if (b == 'u' || b == 'U' || b == 'l' || b == 'L') core.pop_back();
                else break;
            }
            unsigned long long v = strtoull(core.c_str(), nullptr, 0);
            t.intVal = static_cast<long long>(static_cast<int32_t>(v));
        }
        return t;
    }

    Token stringLiteral(int line, int col) {
        get();
        string decoded;
        while (peek() && peek() != '"') {
            char c = get();
            if (c == '\\') {
                char e = get();
                switch (e) {
                    case 'n': decoded.push_back('\n'); break;
                    case 't': decoded.push_back('\t'); break;
                    case 'r': decoded.push_back('\r'); break;
                    case '0': decoded.push_back('\0'); break;
                    case '\\': decoded.push_back('\\'); break;
                    case '"': decoded.push_back('"'); break;
                    default: decoded.push_back(e); break;
                }
            } else {
                decoded.push_back(c);
            }
        }
        if (!match('"')) fail(line, "unterminated string literal");
        Token t = make(TokKind::StringLit, "", line, col);
        t.strVal = decoded;
        return t;
    }
};

enum class BaseType { Int, Float, Void };

struct Type {
    BaseType base = BaseType::Int;
    vector<int> dims;
    bool isVoid() const { return base == BaseType::Void && dims.empty(); }
    bool isArray() const { return !dims.empty(); }
    bool isScalar() const { return dims.empty() && base != BaseType::Void; }
};

struct Expr;
struct InitVal;
struct Stmt;
struct Decl;
using ExprPtr = shared_ptr<Expr>;
using InitPtr = shared_ptr<InitVal>;
using StmtPtr = shared_ptr<Stmt>;
using DeclPtr = shared_ptr<Decl>;

enum class ExprKind { IntLit, FloatLit, StringLit, LVal, Unary, Binary, Call };

struct Expr {
    ExprKind kind = ExprKind::IntLit;
    int line = 1;
    string text;
    long long intVal = 0;
    float floatVal = 0.0f;
    ExprPtr lhs, rhs;
    vector<ExprPtr> args;
    vector<ExprPtr> indices;
};

struct InitVal {
    bool isList = false;
    int line = 1;
    ExprPtr expr;
    vector<InitPtr> items;
};

struct VarDef {
    string name;
    int line = 1;
    vector<ExprPtr> dims;
    InitPtr init;
};

struct Decl {
    bool isConst = false;
    BaseType base = BaseType::Int;
    int line = 1;
    vector<VarDef> defs;
};

struct BlockItem {
    bool isDecl = false;
    DeclPtr decl;
    StmtPtr stmt;
};

enum class StmtKind { Block, Assign, Expr, If, While, Break, Continue, Return };

struct Stmt {
    StmtKind kind = StmtKind::Expr;
    int line = 1;
    vector<BlockItem> items;
    ExprPtr lhs;
    ExprPtr expr;
    ExprPtr cond;
    StmtPtr thenStmt;
    StmtPtr elseStmt;
    StmtPtr body;
};

struct Param {
    BaseType base = BaseType::Int;
    string name;
    int line = 1;
    vector<ExprPtr> dims;
};

struct FuncDef {
    BaseType ret = BaseType::Int;
    string name;
    int line = 1;
    vector<Param> params;
    StmtPtr block;
};

struct TopLevel {
    bool isFunc = false;
    DeclPtr decl;
    shared_ptr<FuncDef> func;
};

struct Program {
    vector<TopLevel> items;
};

class Parser {
public:
    explicit Parser(vector<Token> toks) : toks_(std::move(toks)) {}

    Program parseProgram() {
        Program p;
        while (!at(TokKind::End)) {
            TopLevel item;
            if (at(TokKind::KwConst)) {
                item.decl = parseDecl();
                item.isFunc = false;
                p.items.push_back(item);
                continue;
            }
            BaseType ty = parseFuncOrBType();
            Token name = expect(TokKind::Ident, "identifier expected");
            if (match(TokKind::LParen)) {
                item.isFunc = true;
                item.func = make_shared<FuncDef>();
                item.func->ret = ty;
                item.func->name = name.text;
                item.func->line = name.line;
                if (!at(TokKind::RParen)) {
                    do {
                        item.func->params.push_back(parseParam());
                    } while (match(TokKind::Comma));
                }
                expect(TokKind::RParen, "')' expected");
                item.func->block = parseBlock();
            } else {
                if (ty == BaseType::Void) fail(name.line, "variables cannot have void type");
                item.isFunc = false;
                item.decl = make_shared<Decl>();
                item.decl->isConst = false;
                item.decl->base = ty;
                item.decl->line = name.line;
                item.decl->defs.push_back(parseVarDefAfterName(name));
                while (match(TokKind::Comma)) {
                    Token n = expect(TokKind::Ident, "identifier expected");
                    item.decl->defs.push_back(parseVarDefAfterName(n));
                }
                expect(TokKind::Semi, "';' expected");
            }
            p.items.push_back(item);
        }
        return p;
    }

private:
    vector<Token> toks_;
    size_t pos_ = 0;

    const Token &peek(int off = 0) const {
        size_t p = pos_ + static_cast<size_t>(off);
        if (p >= toks_.size()) return toks_.back();
        return toks_[p];
    }

    bool at(TokKind k) const { return peek().kind == k; }

    bool match(TokKind k) {
        if (!at(k)) return false;
        ++pos_;
        return true;
    }

    Token expect(TokKind k, const string &msg) {
        if (!at(k)) fail(peek().line, msg);
        return toks_[pos_++];
    }

    BaseType parseBType() {
        if (match(TokKind::KwInt)) return BaseType::Int;
        if (match(TokKind::KwFloat)) return BaseType::Float;
        fail(peek().line, "type expected");
    }

    BaseType parseFuncOrBType() {
        if (match(TokKind::KwVoid)) return BaseType::Void;
        return parseBType();
    }

    DeclPtr parseDecl() {
        auto d = make_shared<Decl>();
        d->line = peek().line;
        if (match(TokKind::KwConst)) {
            d->isConst = true;
            d->base = parseBType();
        } else {
            d->base = parseBType();
        }
        Token name = expect(TokKind::Ident, "identifier expected");
        d->defs.push_back(parseVarDefAfterName(name));
        while (match(TokKind::Comma)) {
            Token n = expect(TokKind::Ident, "identifier expected");
            d->defs.push_back(parseVarDefAfterName(n));
        }
        expect(TokKind::Semi, "';' expected");
        return d;
    }

    VarDef parseVarDefAfterName(const Token &name) {
        VarDef v;
        v.name = name.text;
        v.line = name.line;
        while (match(TokKind::LBracket)) {
            v.dims.push_back(parseExp());
            expect(TokKind::RBracket, "']' expected");
        }
        if (match(TokKind::Assign)) {
            v.init = parseInitVal();
        }
        return v;
    }

    InitPtr parseInitVal() {
        auto init = make_shared<InitVal>();
        init->line = peek().line;
        if (match(TokKind::LBrace)) {
            init->isList = true;
            if (!at(TokKind::RBrace)) {
                do {
                    init->items.push_back(parseInitVal());
                } while (match(TokKind::Comma));
            }
            expect(TokKind::RBrace, "'}' expected");
        } else {
            init->expr = parseExp();
        }
        return init;
    }

    Param parseParam() {
        Param p;
        p.line = peek().line;
        p.base = parseBType();
        Token name = expect(TokKind::Ident, "parameter name expected");
        p.name = name.text;
        if (match(TokKind::LBracket)) {
            expect(TokKind::RBracket, "']' expected");
            p.dims.push_back(nullptr);
            while (match(TokKind::LBracket)) {
                p.dims.push_back(parseExp());
                expect(TokKind::RBracket, "']' expected");
            }
        }
        return p;
    }

    StmtPtr parseBlock() {
        auto s = make_shared<Stmt>();
        s->kind = StmtKind::Block;
        s->line = peek().line;
        expect(TokKind::LBrace, "'{' expected");
        while (!at(TokKind::RBrace)) {
            if (at(TokKind::End)) fail(peek().line, "unterminated block");
            BlockItem item;
            if (at(TokKind::KwConst) || at(TokKind::KwInt) || at(TokKind::KwFloat)) {
                item.isDecl = true;
                item.decl = parseDecl();
            } else {
                item.isDecl = false;
                item.stmt = parseStmt();
            }
            s->items.push_back(item);
        }
        expect(TokKind::RBrace, "'}' expected");
        return s;
    }

    StmtPtr parseStmt() {
        auto s = make_shared<Stmt>();
        s->line = peek().line;
        if (at(TokKind::LBrace)) return parseBlock();
        if (match(TokKind::KwIf)) {
            s->kind = StmtKind::If;
            expect(TokKind::LParen, "'(' expected");
            s->cond = parseCond();
            expect(TokKind::RParen, "')' expected");
            s->thenStmt = parseStmt();
            if (match(TokKind::KwElse)) s->elseStmt = parseStmt();
            return s;
        }
        if (match(TokKind::KwWhile)) {
            s->kind = StmtKind::While;
            expect(TokKind::LParen, "'(' expected");
            s->cond = parseCond();
            expect(TokKind::RParen, "')' expected");
            s->body = parseStmt();
            return s;
        }
        if (match(TokKind::KwBreak)) {
            s->kind = StmtKind::Break;
            expect(TokKind::Semi, "';' expected");
            return s;
        }
        if (match(TokKind::KwContinue)) {
            s->kind = StmtKind::Continue;
            expect(TokKind::Semi, "';' expected");
            return s;
        }
        if (match(TokKind::KwReturn)) {
            s->kind = StmtKind::Return;
            if (!at(TokKind::Semi)) s->expr = parseExp();
            expect(TokKind::Semi, "';' expected");
            return s;
        }
        if (match(TokKind::Semi)) {
            s->kind = StmtKind::Expr;
            return s;
        }
        if (at(TokKind::Ident)) {
            size_t save = pos_;
            ExprPtr lv = parseLVal();
            if (match(TokKind::Assign)) {
                s->kind = StmtKind::Assign;
                s->lhs = lv;
                s->expr = parseExp();
                expect(TokKind::Semi, "';' expected");
                return s;
            }
            pos_ = save;
        }
        s->kind = StmtKind::Expr;
        s->expr = parseExp();
        expect(TokKind::Semi, "';' expected");
        return s;
    }

    ExprPtr makeNode(ExprKind k, int line) {
        auto e = make_shared<Expr>();
        e->kind = k;
        e->line = line;
        return e;
    }

    ExprPtr parseLVal() {
        Token name = expect(TokKind::Ident, "identifier expected");
        auto e = makeNode(ExprKind::LVal, name.line);
        e->text = name.text;
        while (match(TokKind::LBracket)) {
            e->indices.push_back(parseExp());
            expect(TokKind::RBracket, "']' expected");
        }
        return e;
    }

    ExprPtr parsePrimary() {
        if (match(TokKind::LParen)) {
            ExprPtr e = parseExp();
            expect(TokKind::RParen, "')' expected");
            return e;
        }
        if (at(TokKind::IntLit)) {
            Token t = toks_[pos_++];
            auto e = makeNode(ExprKind::IntLit, t.line);
            e->intVal = t.intVal;
            e->text = t.text;
            return e;
        }
        if (at(TokKind::FloatLit)) {
            Token t = toks_[pos_++];
            auto e = makeNode(ExprKind::FloatLit, t.line);
            e->floatVal = t.floatVal;
            e->text = t.text;
            return e;
        }
        if (at(TokKind::Ident)) {
            Token name = toks_[pos_++];
            if (match(TokKind::LParen)) {
                auto e = makeNode(ExprKind::Call, name.line);
                e->text = name.text;
                if (!at(TokKind::RParen)) {
                    do {
                        if (at(TokKind::StringLit)) {
                            Token s = toks_[pos_++];
                            auto se = makeNode(ExprKind::StringLit, s.line);
                            se->text = s.strVal;
                            e->args.push_back(se);
                        } else {
                            e->args.push_back(parseExp());
                        }
                    } while (match(TokKind::Comma));
                }
                expect(TokKind::RParen, "')' expected");
                return e;
            }
            --pos_;
            return parseLVal();
        }
        fail(peek().line, "expression expected");
    }

    ExprPtr parseUnary() {
        if (at(TokKind::Plus) || at(TokKind::Minus) || at(TokKind::Bang)) {
            Token op = toks_[pos_++];
            auto e = makeNode(ExprKind::Unary, op.line);
            e->text = op.text;
            e->lhs = parseUnary();
            return e;
        }
        return parsePrimary();
    }

    ExprPtr parseMul() {
        ExprPtr e = parseUnary();
        while (at(TokKind::Star) || at(TokKind::Slash) || at(TokKind::Percent)) {
            Token op = toks_[pos_++];
            auto b = makeNode(ExprKind::Binary, op.line);
            b->text = op.text;
            b->lhs = e;
            b->rhs = parseUnary();
            e = b;
        }
        return e;
    }

    ExprPtr parseAdd() {
        ExprPtr e = parseMul();
        while (at(TokKind::Plus) || at(TokKind::Minus)) {
            Token op = toks_[pos_++];
            auto b = makeNode(ExprKind::Binary, op.line);
            b->text = op.text;
            b->lhs = e;
            b->rhs = parseMul();
            e = b;
        }
        return e;
    }

    ExprPtr parseRel() {
        ExprPtr e = parseAdd();
        while (at(TokKind::Lt) || at(TokKind::Gt) || at(TokKind::Le) || at(TokKind::Ge)) {
            Token op = toks_[pos_++];
            auto b = makeNode(ExprKind::Binary, op.line);
            b->text = op.text;
            b->lhs = e;
            b->rhs = parseAdd();
            e = b;
        }
        return e;
    }

    ExprPtr parseEq() {
        ExprPtr e = parseRel();
        while (at(TokKind::Eq) || at(TokKind::Ne)) {
            Token op = toks_[pos_++];
            auto b = makeNode(ExprKind::Binary, op.line);
            b->text = op.text;
            b->lhs = e;
            b->rhs = parseRel();
            e = b;
        }
        return e;
    }

    ExprPtr parseLAnd() {
        ExprPtr e = parseEq();
        while (at(TokKind::AndAnd)) {
            Token op = toks_[pos_++];
            auto b = makeNode(ExprKind::Binary, op.line);
            b->text = op.text;
            b->lhs = e;
            b->rhs = parseEq();
            e = b;
        }
        return e;
    }

    ExprPtr parseLOr() {
        ExprPtr e = parseLAnd();
        while (at(TokKind::OrOr)) {
            Token op = toks_[pos_++];
            auto b = makeNode(ExprKind::Binary, op.line);
            b->text = op.text;
            b->lhs = e;
            b->rhs = parseLAnd();
            e = b;
        }
        return e;
    }

    ExprPtr parseExp() { return parseAdd(); }
    ExprPtr parseCond() { return parseLOr(); }
};

struct ConstValue {
    BaseType base = BaseType::Int;
    int i = 0;
    float f = 0.0f;
};

static ConstValue zeroValue(BaseType b) {
    ConstValue v;
    v.base = b;
    return v;
}

static ConstValue convertConst(ConstValue v, BaseType to) {
    if (v.base == to) return v;
    ConstValue r;
    r.base = to;
    if (to == BaseType::Int) r.i = static_cast<int>(v.f);
    else r.f = static_cast<float>(v.i);
    return r;
}

static uint32_t floatBits(float f) {
    uint32_t u = 0;
    static_assert(sizeof(float) == sizeof(uint32_t), "float size");
    memcpy(&u, &f, sizeof(u));
    return u;
}

// ===== Semantic analysis and resolved program =====
namespace sysyc {

using SymbolId = int;
using FunctionId = int;

constexpr SymbolId InvalidSymbol = -1;
constexpr FunctionId InvalidFunction = -1;

struct ExprSemantics {
    Type type;
    SymbolId symbol = InvalidSymbol;
    FunctionId function = InvalidFunction;
    bool isLValue = false;
    bool isString = false;
};

struct SemSymbol {
    SymbolId id = InvalidSymbol;
    string name;
    Type type;
    int line = 1;
    bool isConst = false;
    bool isGlobal = false;
    bool isParameter = false;
    bool isParamArray = false;
    const VarDef *varDef = nullptr;
    const Param *param = nullptr;
    vector<ConstValue> constElements;
    vector<ConstValue> globalInitializers;
};

enum class BuiltinEffect {
    None,
    ReadsInput,
    WritesOutput,
    ReadsMemory,
    WritesMemory,
    ReadsAndWritesMemory
};

struct SemFunction {
    FunctionId id = InvalidFunction;
    string name;
    string runtimeName;
    BaseType returnType = BaseType::Int;
    vector<Type> parameterTypes;
    vector<SymbolId> parameters;
    int line = 1;
    bool isBuiltin = false;
    bool isVariadic = false;
    BuiltinEffect effect = BuiltinEffect::None;
    const FuncDef *definition = nullptr;
};

struct SemanticProgram {
    vector<SemSymbol> symbols;
    vector<SemFunction> functions;
    unordered_map<string, SymbolId> globalsByName;
    unordered_map<string, FunctionId> functionsByName;
    unordered_map<const Expr *, ExprSemantics> expressions;
    unordered_map<const VarDef *, SymbolId> variableDefinitions;
    unordered_map<const Param *, SymbolId> parameterDefinitions;
    unordered_map<const FuncDef *, FunctionId> functionDefinitions;
    unordered_map<const VarDef *, vector<ExprPtr>> flattenedInitializers;

    const SemSymbol &symbol(SymbolId id) const {
        return symbols.at(static_cast<size_t>(id));
    }

    SemSymbol &symbol(SymbolId id) {
        return symbols.at(static_cast<size_t>(id));
    }

    const SemFunction &function(FunctionId id) const {
        return functions.at(static_cast<size_t>(id));
    }

    const ExprSemantics &expression(const ExprPtr &expr) const {
        auto it = expressions.find(expr.get());
        if (it == expressions.end()) fail(expr ? expr->line : 1, "internal error: unresolved expression");
        return it->second;
    }
};

class SemanticAnalyzer {
public:
    explicit SemanticAnalyzer(const Program &program) : program_(program) {}

    SemanticProgram analyze() {
        installBuiltins();
        scopes_.push_back({});
        declareTopLevels();
        resolveFunctionBodies();
        scopes_.clear();
        return std::move(result_);
    }

private:
    const Program &program_;
    SemanticProgram result_;
    vector<unordered_map<string, SymbolId>> scopes_;
    FunctionId currentFunction_ = InvalidFunction;
    int loopDepth_ = 0;

    static Type scalarType(BaseType base) {
        Type type;
        type.base = base;
        return type;
    }

    static Type arrayParameterType(BaseType base) {
        Type type;
        type.base = base;
        type.dims.push_back(-1);
        return type;
    }

    static bool isNumericScalar(const Type &type) {
        return type.isScalar() && (type.base == BaseType::Int || type.base == BaseType::Float);
    }

    static bool canConvertScalar(const Type &from, const Type &to) {
        return isNumericScalar(from) && isNumericScalar(to);
    }

    static int productTail(const vector<int> &dims, size_t start) {
        int64_t product = 1;
        for (size_t i = start; i < dims.size(); ++i) {
            if (dims[i] <= 0) fail(1, "array dimension must be positive");
            product *= dims[i];
            if (product > numeric_limits<int>::max()) fail(1, "array is too large");
        }
        return static_cast<int>(product);
    }

    static int wrappedAdd(int a, int b) {
        return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
    }

    static int wrappedSub(int a, int b) {
        return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
    }

    static int wrappedMul(int a, int b) {
        return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b));
    }

    void installBuiltins() {
        addBuiltin("getint", BaseType::Int, {}, BuiltinEffect::ReadsInput);
        addBuiltin("getch", BaseType::Int, {}, BuiltinEffect::ReadsInput);
        addBuiltin("getfloat", BaseType::Float, {}, BuiltinEffect::ReadsInput);
        addBuiltin("getarray", BaseType::Int, {arrayParameterType(BaseType::Int)}, BuiltinEffect::WritesMemory);
        addBuiltin("getfarray", BaseType::Int, {arrayParameterType(BaseType::Float)}, BuiltinEffect::WritesMemory);
        addBuiltin("putint", BaseType::Void, {scalarType(BaseType::Int)}, BuiltinEffect::WritesOutput);
        addBuiltin("putch", BaseType::Void, {scalarType(BaseType::Int)}, BuiltinEffect::WritesOutput);
        addBuiltin("putfloat", BaseType::Void, {scalarType(BaseType::Float)}, BuiltinEffect::WritesOutput);
        addBuiltin("putarray", BaseType::Void,
                   {scalarType(BaseType::Int), arrayParameterType(BaseType::Int)},
                   BuiltinEffect::ReadsMemory);
        addBuiltin("putfarray", BaseType::Void,
                   {scalarType(BaseType::Int), arrayParameterType(BaseType::Float)},
                   BuiltinEffect::ReadsMemory);
        addBuiltin("starttime", BaseType::Void, {}, BuiltinEffect::None, false, "_sysy_starttime");
        addBuiltin("stoptime", BaseType::Void, {}, BuiltinEffect::None, false, "_sysy_stoptime");
        addBuiltin("putf", BaseType::Void, {}, BuiltinEffect::WritesOutput, true);
    }

    FunctionId addBuiltin(const string &name, BaseType ret, vector<Type> params,
                          BuiltinEffect effect, bool variadic = false,
                          string runtimeName = {}) {
        SemFunction function;
        function.id = static_cast<FunctionId>(result_.functions.size());
        function.name = name;
        function.runtimeName = runtimeName.empty() ? name : std::move(runtimeName);
        function.returnType = ret;
        function.parameterTypes = std::move(params);
        function.isBuiltin = true;
        function.isVariadic = variadic;
        function.effect = effect;
        result_.functionsByName[name] = function.id;
        result_.functions.push_back(std::move(function));
        return result_.functions.back().id;
    }

    void declareTopLevels() {
        for (const TopLevel &item : program_.items) {
            if (item.isFunc) declareFunction(*item.func);
            else declareGlobalDeclaration(*item.decl);
        }
    }

    void declareFunction(const FuncDef &definition) {
        if (result_.globalsByName.count(definition.name)) {
            fail(definition.line, "top-level redefinition of " + definition.name);
        }
        auto previous = result_.functionsByName.find(definition.name);
        if (previous != result_.functionsByName.end()) {
            fail(definition.line, "function redefinition of " + definition.name);
        }

        SemFunction function;
        function.id = static_cast<FunctionId>(result_.functions.size());
        function.name = definition.name;
        function.runtimeName = definition.name;
        function.returnType = definition.ret;
        function.line = definition.line;
        function.definition = &definition;
        function.effect = BuiltinEffect::ReadsAndWritesMemory;
        for (const Param &parameter : definition.params) {
            function.parameterTypes.push_back(resolveParameterType(parameter));
        }
        result_.functionsByName[function.name] = function.id;
        result_.functionDefinitions[&definition] = function.id;
        result_.functions.push_back(std::move(function));
    }

    Type resolveParameterType(const Param &parameter) {
        Type type;
        type.base = parameter.base;
        if (parameter.dims.empty()) return type;
        type.dims.push_back(-1);
        for (size_t i = 1; i < parameter.dims.size(); ++i) {
            resolveExpression(parameter.dims[i]);
            int dimension = constInt(parameter.dims[i]);
            if (dimension <= 0) fail(parameter.line, "array parameter dimension must be positive");
            type.dims.push_back(dimension);
        }
        return type;
    }

    void declareGlobalDeclaration(const Decl &declaration) {
        for (const VarDef &definition : declaration.defs) {
            Type type = resolveDeclaredType(declaration.base, definition);
            if (result_.globalsByName.count(definition.name) ||
                result_.functionsByName.count(definition.name)) {
                fail(definition.line, "top-level redefinition of " + definition.name);
            }
            if (declaration.isConst && !definition.init) {
                fail(definition.line, "const object requires an initializer");
            }

            SemSymbol symbol;
            symbol.id = static_cast<SymbolId>(result_.symbols.size());
            symbol.name = definition.name;
            symbol.type = type;
            symbol.line = definition.line;
            symbol.isConst = declaration.isConst;
            symbol.isGlobal = true;
            symbol.varDef = &definition;

            result_.symbols.push_back(symbol);
            result_.globalsByName[symbol.name] = symbol.id;
            scopes_.front()[symbol.name] = symbol.id;
            result_.variableDefinitions[&definition] = symbol.id;

            int count = type.isArray() ? productTail(type.dims, 0) : 1;
            vector<ExprPtr> flattened(static_cast<size_t>(count));
            if (definition.init) {
                resolveInitializerExpressions(definition.init, type.base);
                if (type.isArray()) {
                    flattenInitializerExpressions(definition.init, type, 0, 0, count, flattened);
                } else {
                    flattened[0] = firstInitializerExpression(definition.init);
                }
            }
            result_.flattenedInitializers[&definition] = flattened;

            vector<ConstValue> values(static_cast<size_t>(count), zeroValue(type.base));
            for (int i = 0; i < count; ++i) {
                if (!flattened[static_cast<size_t>(i)]) continue;
                ConstValue value = evaluateConstant(flattened[static_cast<size_t>(i)]);
                values[static_cast<size_t>(i)] = convertConst(value, type.base);
            }
            SemSymbol &stored = result_.symbol(symbol.id);
            stored.globalInitializers = values;
            if (stored.isConst) stored.constElements = values;
        }
    }

    Type resolveDeclaredType(BaseType base, const VarDef &definition) {
        Type type;
        type.base = base;
        for (const ExprPtr &dimensionExpression : definition.dims) {
            resolveExpression(dimensionExpression);
            int dimension = constInt(dimensionExpression);
            if (dimension <= 0) fail(definition.line, "array dimension must be positive");
            type.dims.push_back(dimension);
        }
        return type;
    }

    void resolveFunctionBodies() {
        for (const TopLevel &item : program_.items) {
            if (!item.isFunc) continue;
            resolveFunctionBody(*item.func);
        }
    }

    void resolveFunctionBody(const FuncDef &definition) {
        currentFunction_ = result_.functionDefinitions.at(&definition);
        SemFunction &function = result_.functions.at(static_cast<size_t>(currentFunction_));
        scopes_.push_back({});

        for (size_t i = 0; i < definition.params.size(); ++i) {
            const Param &parameter = definition.params[i];
            SemSymbol symbol;
            symbol.id = static_cast<SymbolId>(result_.symbols.size());
            symbol.name = parameter.name;
            symbol.type = function.parameterTypes[i];
            symbol.line = parameter.line;
            symbol.isParameter = true;
            symbol.isParamArray = symbol.type.isArray();
            symbol.param = &parameter;
            addLocalSymbol(symbol, parameter.line);
            result_.parameterDefinitions[&parameter] = symbol.id;
            function.parameters.push_back(symbol.id);
        }

        resolveStatement(definition.block, false);
        scopes_.pop_back();
        currentFunction_ = InvalidFunction;
    }

    void addLocalSymbol(const SemSymbol &symbol, int line) {
        auto &scope = scopes_.back();
        if (scope.count(symbol.name)) fail(line, "redefinition of " + symbol.name);
        scope[symbol.name] = symbol.id;
        result_.symbols.push_back(symbol);
    }

    SymbolId lookupSymbol(const string &name, int line) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) return found->second;
        }
        auto global = result_.globalsByName.find(name);
        if (global != result_.globalsByName.end()) return global->second;
        fail(line, "undefined variable " + name);
    }

    FunctionId lookupFunction(const string &name, int line) const {
        auto found = result_.functionsByName.find(name);
        if (found == result_.functionsByName.end()) fail(line, "undefined function " + name);
        return found->second;
    }

    void resolveStatement(const StmtPtr &statement, bool createScope = true) {
        if (!statement) return;
        switch (statement->kind) {
            case StmtKind::Block:
                if (createScope) scopes_.push_back({});
                for (const BlockItem &item : statement->items) {
                    if (item.isDecl) resolveLocalDeclaration(*item.decl);
                    else resolveStatement(item.stmt);
                }
                if (createScope) scopes_.pop_back();
                return;
            case StmtKind::Assign: {
                ExprSemantics lhs = resolveExpression(statement->lhs);
                if (!lhs.isLValue || !lhs.type.isScalar()) {
                    fail(statement->line, "assignment target must be a scalar lvalue");
                }
                const SemSymbol &symbol = result_.symbol(lhs.symbol);
                if (symbol.isConst) fail(statement->line, "cannot assign to const " + symbol.name);
                ExprSemantics rhs = resolveExpression(statement->expr);
                requireScalarConversion(rhs.type, lhs.type, statement->line, "assignment type mismatch");
                return;
            }
            case StmtKind::Expr:
                if (statement->expr) resolveExpression(statement->expr);
                return;
            case StmtKind::If:
                requireCondition(resolveExpression(statement->cond), statement->line);
                resolveStatement(statement->thenStmt);
                resolveStatement(statement->elseStmt);
                return;
            case StmtKind::While:
                requireCondition(resolveExpression(statement->cond), statement->line);
                ++loopDepth_;
                resolveStatement(statement->body);
                --loopDepth_;
                return;
            case StmtKind::Break:
                if (loopDepth_ == 0) fail(statement->line, "break outside loop");
                return;
            case StmtKind::Continue:
                if (loopDepth_ == 0) fail(statement->line, "continue outside loop");
                return;
            case StmtKind::Return: {
                BaseType returnType = result_.function(currentFunction_).returnType;
                if (returnType == BaseType::Void) {
                    if (statement->expr) fail(statement->line, "void function cannot return a value");
                } else {
                    if (!statement->expr) fail(statement->line, "non-void function must return a value");
                    ExprSemantics value = resolveExpression(statement->expr);
                    requireScalarConversion(value.type, scalarType(returnType), statement->line,
                                            "return type mismatch");
                }
                return;
            }
        }
    }

    void resolveLocalDeclaration(const Decl &declaration) {
        for (const VarDef &definition : declaration.defs) {
            Type type = resolveDeclaredType(declaration.base, definition);
            if (declaration.isConst && !definition.init) {
                fail(definition.line, "const object requires an initializer");
            }

            SemSymbol symbol;
            symbol.id = static_cast<SymbolId>(result_.symbols.size());
            symbol.name = definition.name;
            symbol.type = type;
            symbol.line = definition.line;
            symbol.isConst = declaration.isConst;
            symbol.varDef = &definition;
            addLocalSymbol(symbol, definition.line);
            result_.variableDefinitions[&definition] = symbol.id;

            int count = type.isArray() ? productTail(type.dims, 0) : 1;
            vector<ExprPtr> flattened(static_cast<size_t>(count));
            if (definition.init) {
                resolveInitializerExpressions(definition.init, type.base);
                if (type.isArray()) {
                    flattenInitializerExpressions(definition.init, type, 0, 0, count, flattened);
                } else {
                    flattened[0] = firstInitializerExpression(definition.init);
                }
            }
            result_.flattenedInitializers[&definition] = flattened;

            if (declaration.isConst) {
                vector<ConstValue> values(static_cast<size_t>(count), zeroValue(type.base));
                for (int i = 0; i < count; ++i) {
                    if (!flattened[static_cast<size_t>(i)]) continue;
                    values[static_cast<size_t>(i)] =
                        convertConst(evaluateConstant(flattened[static_cast<size_t>(i)]), type.base);
                }
                result_.symbol(symbol.id).constElements = std::move(values);
            }
        }
    }

    ExprSemantics resolveExpression(const ExprPtr &expression) {
        if (!expression) fail(1, "internal error: null expression");
        auto existing = result_.expressions.find(expression.get());
        if (existing != result_.expressions.end()) return existing->second;

        vector<pair<ExprPtr, bool>> work;
        work.push_back({expression, false});
        while (!work.empty()) {
            ExprPtr current = work.back().first;
            bool ready = work.back().second;
            work.pop_back();
            if (result_.expressions.count(current.get())) continue;
            if (!ready) {
                work.push_back({current, true});
                switch (current->kind) {
                    case ExprKind::LVal:
                        for (auto it = current->indices.rbegin(); it != current->indices.rend(); ++it) {
                            if (!result_.expressions.count(it->get())) work.push_back({*it, false});
                        }
                        break;
                    case ExprKind::Unary:
                        if (!result_.expressions.count(current->lhs.get())) work.push_back({current->lhs, false});
                        break;
                    case ExprKind::Binary:
                        if (!result_.expressions.count(current->rhs.get())) work.push_back({current->rhs, false});
                        if (!result_.expressions.count(current->lhs.get())) work.push_back({current->lhs, false});
                        break;
                    case ExprKind::Call:
                        for (auto it = current->args.rbegin(); it != current->args.rend(); ++it) {
                            if (!result_.expressions.count(it->get())) work.push_back({*it, false});
                        }
                        break;
                    case ExprKind::IntLit:
                    case ExprKind::FloatLit:
                    case ExprKind::StringLit:
                        break;
                }
                continue;
            }
            result_.expressions[current.get()] = resolveExpressionReady(current);
        }
        return result_.expressions.at(expression.get());
    }

    ExprSemantics resolveExpressionReady(const ExprPtr &expression) {
        ExprSemantics semantics;
        switch (expression->kind) {
            case ExprKind::IntLit:
                semantics.type = scalarType(BaseType::Int);
                break;
            case ExprKind::FloatLit:
                semantics.type = scalarType(BaseType::Float);
                break;
            case ExprKind::StringLit:
                semantics.type = scalarType(BaseType::Void);
                semantics.isString = true;
                break;
            case ExprKind::LVal:
                semantics = resolveLValue(expression);
                break;
            case ExprKind::Unary:
                semantics = resolveUnary(expression);
                break;
            case ExprKind::Binary:
                semantics = resolveBinary(expression);
                break;
            case ExprKind::Call:
                semantics = resolveCall(expression);
                break;
        }
        return semantics;
    }

    ExprSemantics resolveLValue(const ExprPtr &expression) {
        SymbolId symbolId = lookupSymbol(expression->text, expression->line);
        const SemSymbol &symbol = result_.symbol(symbolId);
        Type type = symbol.type;
        if (expression->indices.size() > type.dims.size()) {
            fail(expression->line, "too many indices for " + expression->text);
        }
        for (const ExprPtr &index : expression->indices) {
            ExprSemantics indexSemantics = resolveExpression(index);
            if (!indexSemantics.type.isScalar() || indexSemantics.type.base != BaseType::Int) {
                fail(index->line, "array index must have int type");
            }
        }
        type.dims.erase(type.dims.begin(),
                        type.dims.begin() + static_cast<ptrdiff_t>(expression->indices.size()));
        ExprSemantics semantics;
        semantics.type = type;
        semantics.symbol = symbolId;
        semantics.isLValue = true;
        return semantics;
    }

    ExprSemantics resolveUnary(const ExprPtr &expression) {
        ExprSemantics operand = resolveExpression(expression->lhs);
        if (!isNumericScalar(operand.type)) fail(expression->line, "unary operand must be scalar");
        ExprSemantics semantics;
        semantics.type = expression->text == "!" ? scalarType(BaseType::Int) : operand.type;
        return semantics;
    }

    ExprSemantics resolveBinary(const ExprPtr &expression) {
        ExprSemantics left = resolveExpression(expression->lhs);
        ExprSemantics right = resolveExpression(expression->rhs);
        if (!isNumericScalar(left.type) || !isNumericScalar(right.type)) {
            fail(expression->line, "binary operands must be scalar");
        }

        const string &op = expression->text;
        ExprSemantics semantics;
        if (op == "&&" || op == "||" || op == "==" || op == "!=" ||
            op == "<" || op == ">" || op == "<=" || op == ">=") {
            semantics.type = scalarType(BaseType::Int);
            return semantics;
        }
        if (op == "%") {
            if (left.type.base != BaseType::Int || right.type.base != BaseType::Int) {
                fail(expression->line, "remainder operands must have int type");
            }
            semantics.type = scalarType(BaseType::Int);
            return semantics;
        }
        if (op == "+" || op == "-" || op == "*" || op == "/") {
            semantics.type = scalarType(left.type.base == BaseType::Float || right.type.base == BaseType::Float
                                            ? BaseType::Float
                                            : BaseType::Int);
            return semantics;
        }
        fail(expression->line, "unsupported binary operator " + op);
    }

    ExprSemantics resolveCall(const ExprPtr &expression) {
        FunctionId functionId = lookupFunction(expression->text, expression->line);
        const SemFunction &function = result_.function(functionId);
        if (function.name == "putf") {
            if (expression->args.empty() || expression->args.front()->kind != ExprKind::StringLit) {
                fail(expression->line, "putf requires a format string");
            }
            resolveExpression(expression->args.front());
            for (size_t i = 1; i < expression->args.size(); ++i) {
                ExprSemantics argument = resolveExpression(expression->args[i]);
                if (!isNumericScalar(argument.type)) fail(expression->args[i]->line, "invalid putf argument");
            }
        } else {
            if (!function.isVariadic && expression->args.size() != function.parameterTypes.size()) {
                fail(expression->line,
                     "function " + function.name + " expects " + to_string(function.parameterTypes.size()) +
                         " arguments, got " + to_string(expression->args.size()));
            }
            for (size_t i = 0; i < expression->args.size(); ++i) {
                ExprSemantics argument = resolveExpression(expression->args[i]);
                if (i >= function.parameterTypes.size()) continue;
                requireArgumentType(argument.type, function.parameterTypes[i], expression->args[i]->line);
            }
        }

        ExprSemantics semantics;
        semantics.type = scalarType(function.returnType);
        semantics.function = functionId;
        return semantics;
    }

    void requireArgumentType(const Type &actual, const Type &expected, int line) {
        if (expected.isScalar()) {
            requireScalarConversion(actual, expected, line, "function argument type mismatch");
            return;
        }
        // SysY array arguments are passed as raw base addresses. The callee's
        // declared dimensions define its indexing strides; they are not part
        // of the calling convention and need not match the caller's shape.
        if (!actual.isArray() || actual.base != expected.base) {
            fail(line, "array argument type mismatch");
        }
    }

    void requireScalarConversion(const Type &actual, const Type &expected, int line, const string &message) {
        if (!canConvertScalar(actual, expected)) fail(line, message);
    }

    void requireCondition(const ExprSemantics &condition, int line) {
        if (!isNumericScalar(condition.type)) fail(line, "condition must be scalar");
    }

    void resolveInitializerExpressions(const InitPtr &initializer, BaseType targetBase) {
        if (!initializer) return;
        if (!initializer->isList) {
            ExprSemantics value = resolveExpression(initializer->expr);
            requireScalarConversion(value.type, scalarType(targetBase), initializer->line,
                                    "initializer type mismatch");
            return;
        }
        for (const InitPtr &item : initializer->items) resolveInitializerExpressions(item, targetBase);
    }

    ExprPtr firstInitializerExpression(const InitPtr &initializer) const {
        if (!initializer) return nullptr;
        if (!initializer->isList) return initializer->expr;
        if (initializer->items.empty()) return nullptr;
        return firstInitializerExpression(initializer->items.front());
    }

    static int initializerChildDepth(const Type &type, int depth, int start, int position) {
        int rank = static_cast<int>(type.dims.size());
        int relative = position - start;
        for (int childDepth = depth + 1; childDepth <= rank; ++childDepth) {
            int blockSize = productTail(type.dims, static_cast<size_t>(childDepth));
            if (relative % blockSize == 0) return childDepth;
        }
        return rank;
    }

    void flattenInitializerExpressions(const InitPtr &initializer, const Type &type,
                                       int depth, int start, int limit,
                                       vector<ExprPtr> &output) {
        if (!initializer) return;
        if (!initializer->isList) {
            if (start >= limit) fail(initializer->line, "too many initializer elements");
            output[static_cast<size_t>(start)] = initializer->expr;
            return;
        }
        int position = start;
        for (const InitPtr &child : initializer->items) {
            if (position >= limit) fail(child->line, "too many initializer elements");
            if (child->isList && depth < static_cast<int>(type.dims.size())) {
                int childDepth = initializerChildDepth(type, depth, start, position);
                int subSize = productTail(type.dims, static_cast<size_t>(childDepth));
                if (position + subSize > limit) fail(child->line, "too many initializer elements");
                flattenInitializerExpressions(child, type, childDepth, position, position + subSize, output);
                position += subSize;
            } else {
                flattenInitializerExpressions(child, type, static_cast<int>(type.dims.size()),
                                              position, position + 1, output);
                ++position;
            }
        }
    }

    ConstValue evaluateConstant(const ExprPtr &expression) {
        if (!expression) fail(1, "constant expression expected");
        switch (expression->kind) {
            case ExprKind::IntLit: {
                ConstValue value;
                value.base = BaseType::Int;
                value.i = static_cast<int>(expression->intVal);
                return value;
            }
            case ExprKind::FloatLit: {
                ConstValue value;
                value.base = BaseType::Float;
                value.f = expression->floatVal;
                return value;
            }
            case ExprKind::LVal:
                return evaluateConstantLValue(expression);
            case ExprKind::Unary:
                return evaluateConstantUnary(expression);
            case ExprKind::Binary:
                return evaluateConstantBinary(expression);
            case ExprKind::StringLit:
            case ExprKind::Call:
                break;
        }
        fail(expression->line, "constant expression expected");
    }

    ConstValue evaluateConstantLValue(const ExprPtr &expression) {
        SymbolId symbolId = lookupSymbol(expression->text, expression->line);
        const SemSymbol &symbol = result_.symbol(symbolId);
        if (!symbol.isConst || symbol.constElements.empty()) {
            fail(expression->line, "not a constant: " + expression->text);
        }
        if (expression->indices.size() != symbol.type.dims.size()) {
            fail(expression->line, "constant array access must be fully indexed");
        }
        int64_t index = 0;
        for (size_t i = 0; i < expression->indices.size(); ++i) {
            int value = constInt(expression->indices[i]);
            if (value < 0 || value >= symbol.type.dims[i]) {
                fail(expression->line, "constant index out of range");
            }
            index += static_cast<int64_t>(value) * productTail(symbol.type.dims, i + 1);
        }
        return symbol.constElements.at(static_cast<size_t>(index));
    }

    ConstValue evaluateConstantUnary(const ExprPtr &expression) {
        ConstValue operand = evaluateConstant(expression->lhs);
        if (expression->text == "+") return operand;
        if (expression->text == "-") {
            if (operand.base == BaseType::Float) operand.f = -operand.f;
            else operand.i = wrappedSub(0, operand.i);
            return operand;
        }
        if (expression->text == "!") {
            ConstValue value;
            value.base = BaseType::Int;
            value.i = operand.base == BaseType::Float ? operand.f == 0.0f : operand.i == 0;
            return value;
        }
        fail(expression->line, "unsupported constant unary operator");
    }

    ConstValue evaluateConstantBinary(const ExprPtr &expression) {
        ConstValue left = evaluateConstant(expression->lhs);
        ConstValue right = evaluateConstant(expression->rhs);
        const string &op = expression->text;
        auto truth = [](const ConstValue &value) {
            return value.base == BaseType::Float ? value.f != 0.0f : value.i != 0;
        };
        if (op == "&&" || op == "||") {
            ConstValue value;
            value.base = BaseType::Int;
            value.i = op == "&&" ? truth(left) && truth(right) : truth(left) || truth(right);
            return value;
        }
        bool floating = left.base == BaseType::Float || right.base == BaseType::Float;
        if (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=") {
            bool result = false;
            if (floating) {
                float a = left.base == BaseType::Float ? left.f : static_cast<float>(left.i);
                float b = right.base == BaseType::Float ? right.f : static_cast<float>(right.i);
                if (op == "==") result = a == b;
                else if (op == "!=") result = a != b;
                else if (op == "<") result = a < b;
                else if (op == ">") result = a > b;
                else if (op == "<=") result = a <= b;
                else result = a >= b;
            } else {
                if (op == "==") result = left.i == right.i;
                else if (op == "!=") result = left.i != right.i;
                else if (op == "<") result = left.i < right.i;
                else if (op == ">") result = left.i > right.i;
                else if (op == "<=") result = left.i <= right.i;
                else result = left.i >= right.i;
            }
            ConstValue value;
            value.base = BaseType::Int;
            value.i = result ? 1 : 0;
            return value;
        }
        if (floating) {
            float a = left.base == BaseType::Float ? left.f : static_cast<float>(left.i);
            float b = right.base == BaseType::Float ? right.f : static_cast<float>(right.i);
            ConstValue value;
            value.base = BaseType::Float;
            if (op == "+") value.f = a + b;
            else if (op == "-") value.f = a - b;
            else if (op == "*") value.f = a * b;
            else if (op == "/") value.f = a / b;
            else fail(expression->line, "invalid constant float operator " + op);
            return value;
        }

        ConstValue value;
        value.base = BaseType::Int;
        if (op == "+") value.i = wrappedAdd(left.i, right.i);
        else if (op == "-") value.i = wrappedSub(left.i, right.i);
        else if (op == "*") value.i = wrappedMul(left.i, right.i);
        else if (op == "/") {
            if (right.i == 0) fail(expression->line, "division by zero in constant expression");
            value.i = left.i == numeric_limits<int>::min() && right.i == -1
                          ? numeric_limits<int>::min()
                          : left.i / right.i;
        } else if (op == "%") {
            if (right.i == 0) fail(expression->line, "remainder by zero in constant expression");
            value.i = left.i == numeric_limits<int>::min() && right.i == -1 ? 0 : left.i % right.i;
        } else {
            fail(expression->line, "invalid constant operator " + op);
        }
        return value;
    }

    int constInt(const ExprPtr &expression) {
        ConstValue value = evaluateConstant(expression);
        return value.base == BaseType::Float ? static_cast<int>(value.f) : value.i;
    }
};

} // namespace sysyc

// ===== Typed SSA IR and MemorySSA data model =====
namespace sysyc::ir {

using ValueId = uint32_t;
using InstructionId = uint32_t;
using BlockId = uint32_t;
using MemoryAccessId = uint32_t;
using AliasClassId = uint32_t;

constexpr ValueId InvalidValue = numeric_limits<ValueId>::max();
constexpr InstructionId InvalidInstruction = numeric_limits<InstructionId>::max();
constexpr BlockId InvalidBlock = numeric_limits<BlockId>::max();
constexpr MemoryAccessId InvalidMemoryAccess = numeric_limits<MemoryAccessId>::max();
constexpr AliasClassId UnknownAliasClass = 0;

enum class TypeKind {
    Void,
    I1,
    I32,
    I64,
    F32,
    Pointer,
    Memory,
    Vector
};

struct IRType {
    TypeKind kind = TypeKind::Void;
    TypeKind elementKind = TypeKind::Void;
    uint16_t lanes = 0;

    static IRType voidTy() { return {TypeKind::Void}; }
    static IRType i1() { return {TypeKind::I1}; }
    static IRType i32() { return {TypeKind::I32}; }
    static IRType i64() { return {TypeKind::I64}; }
    static IRType f32() { return {TypeKind::F32}; }
    static IRType pointer() { return {TypeKind::Pointer}; }
    static IRType memory() { return {TypeKind::Memory}; }
    static IRType vector(TypeKind element, uint16_t count) {
        IRType type;
        type.kind = TypeKind::Vector;
        type.elementKind = element;
        type.lanes = count;
        return type;
    }

    bool isVoid() const { return kind == TypeKind::Void; }
    bool isInteger() const { return kind == TypeKind::I1 || kind == TypeKind::I32 || kind == TypeKind::I64; }
    bool isScalar() const { return isInteger() || kind == TypeKind::F32 || kind == TypeKind::Pointer; }
    bool isNumeric() const { return isInteger() || kind == TypeKind::F32; }

    bool operator==(const IRType &other) const {
        return kind == other.kind && elementKind == other.elementKind && lanes == other.lanes;
    }

    bool operator!=(const IRType &other) const { return !(*this == other); }
};

inline IRType lowerScalarType(BaseType base) {
    if (base == BaseType::Int) return IRType::i32();
    if (base == BaseType::Float) return IRType::f32();
    return IRType::voidTy();
}

enum class ValueKind {
    Undef,
    ConstantInt,
    ConstantFloat,
    Parameter,
    GlobalAddress,
    InstructionResult,
    InitialMemory
};

struct Use {
    InstructionId instruction = InvalidInstruction;
    uint32_t operandIndex = 0;

    bool operator==(const Use &other) const {
        return instruction == other.instruction && operandIndex == other.operandIndex;
    }
};

struct Value {
    ValueId id = InvalidValue;
    IRType type;
    ValueKind kind = ValueKind::Undef;
    InstructionId definition = InvalidInstruction;
    int64_t intValue = 0;
    uint32_t floatValue = 0;
    SymbolId symbol = InvalidSymbol;
    uint32_t parameterIndex = 0;
    vector<Use> users;
    string debugName;
};

enum class Opcode {
    Phi,
    Add,
    Sub,
    Mul,
    SDiv,
    SRem,
    FAdd,
    FSub,
    FMul,
    FDiv,
    Neg,
    FNeg,
    And,
    Or,
    Xor,
    Shl,
    AShr,
    LShr,
    ICmp,
    FCmp,
    ZExt,
    SExt,
    Trunc,
    SIToFP,
    FPToSI,
    Bitcast,
    Select,
    Alloca,
    GetElementPtr,
    Load,
    Store,
    Call,
    Branch,
    CondBranch,
    Return,
    Unreachable
};

enum class ComparePredicate {
    None,
    EQ,
    NE,
    SLT,
    SLE,
    SGT,
    SGE,
    OEQ,
    ONE,
    OLT,
    OLE,
    OGT,
    OGE
};

struct Instruction {
    InstructionId id = InvalidInstruction;
    Opcode opcode = Opcode::Unreachable;
    IRType resultType;
    ValueId result = InvalidValue;
    BlockId parent = InvalidBlock;
    vector<ValueId> operands;
    vector<BlockId> targets;
    vector<BlockId> incomingBlocks;
    ComparePredicate predicate = ComparePredicate::None;
    FunctionId callee = InvalidFunction;
    SymbolId object = InvalidSymbol;
    AliasClassId aliasClass = UnknownAliasClass;
    MemoryAccessId memoryAccess = InvalidMemoryAccess;
    int line = 1;
    bool removed = false;

    bool isTerminator() const {
        return opcode == Opcode::Branch || opcode == Opcode::CondBranch ||
               opcode == Opcode::Return || opcode == Opcode::Unreachable;
    }

    bool hasSideEffects() const {
        return opcode == Opcode::Store || opcode == Opcode::Call || isTerminator();
    }
};

enum class MemoryAccessKind {
    LiveOnEntry,
    Use,
    Def,
    Phi
};

struct MemoryIncoming {
    BlockId block = InvalidBlock;
    MemoryAccessId access = InvalidMemoryAccess;
};

struct MemoryAccess {
    MemoryAccessId id = InvalidMemoryAccess;
    MemoryAccessKind kind = MemoryAccessKind::LiveOnEntry;
    AliasClassId aliasClass = UnknownAliasClass;
    BlockId block = InvalidBlock;
    InstructionId instruction = InvalidInstruction;
    MemoryAccessId definingAccess = InvalidMemoryAccess;
    vector<MemoryIncoming> incoming;
    vector<MemoryAccessId> users;
};

struct BasicBlock {
    BlockId id = InvalidBlock;
    string name;
    vector<InstructionId> phis;
    vector<InstructionId> instructions;
    InstructionId terminator = InvalidInstruction;
    vector<BlockId> predecessors;
    vector<BlockId> successors;
    bool sealed = false;
};

struct ConstantIntKey {
    TypeKind kind = TypeKind::I32;
    int64_t value = 0;

    bool operator<(const ConstantIntKey &other) const {
        if (kind != other.kind) return kind < other.kind;
        return value < other.value;
    }
};

struct IRFunction {
    FunctionId semanticFunction = InvalidFunction;
    string name;
    IRType returnType;
    vector<IRType> parameterTypes;
    vector<ValueId> parameters;
    vector<Value> values;
    vector<Instruction> instructionArena;
    vector<BasicBlock> blocks;
    vector<MemoryAccess> memoryAccesses;
    unordered_map<AliasClassId, MemoryAccessId> liveOnEntry;
    map<ConstantIntKey, ValueId> integerConstants;
    unordered_map<uint32_t, ValueId> floatConstants;
    unordered_map<SymbolId, ValueId> globalAddresses;
    unordered_map<uint64_t, ValueId> undefValues;
    BlockId entry = InvalidBlock;

    BlockId createBlock(string blockName) {
        BasicBlock block;
        block.id = static_cast<BlockId>(blocks.size());
        block.name = std::move(blockName);
        blocks.push_back(std::move(block));
        if (entry == InvalidBlock) entry = blocks.back().id;
        return blocks.back().id;
    }

    ValueId createValue(IRType type, ValueKind kind, string debugName = {}) {
        Value value;
        value.id = static_cast<ValueId>(values.size());
        value.type = type;
        value.kind = kind;
        value.debugName = std::move(debugName);
        values.push_back(std::move(value));
        return values.back().id;
    }

    ValueId createParameter(IRType type, uint32_t index, string name) {
        ValueId id = createValue(type, ValueKind::Parameter, std::move(name));
        values[id].parameterIndex = index;
        parameters.push_back(id);
        return id;
    }

    ValueId constantInt(IRType type, int64_t value) {
        ConstantIntKey key{type.kind, value};
        auto found = integerConstants.find(key);
        if (found != integerConstants.end()) return found->second;
        ValueId id = createValue(type, ValueKind::ConstantInt);
        values[id].intValue = value;
        integerConstants[key] = id;
        return id;
    }

    ValueId constantFloat(uint32_t bits) {
        auto found = floatConstants.find(bits);
        if (found != floatConstants.end()) return found->second;
        ValueId id = createValue(IRType::f32(), ValueKind::ConstantFloat);
        values[id].floatValue = bits;
        floatConstants[bits] = id;
        return id;
    }

    ValueId undef(IRType type) {
        uint64_t key = static_cast<uint64_t>(type.kind) |
                       (static_cast<uint64_t>(type.elementKind) << 8) |
                       (static_cast<uint64_t>(type.lanes) << 16);
        auto found = undefValues.find(key);
        if (found != undefValues.end()) return found->second;
        ValueId id = createValue(type, ValueKind::Undef);
        undefValues[key] = id;
        return id;
    }

    ValueId globalAddress(SymbolId symbol, string name) {
        auto found = globalAddresses.find(symbol);
        if (found != globalAddresses.end()) return found->second;
        ValueId id = createValue(IRType::pointer(), ValueKind::GlobalAddress, std::move(name));
        values[id].symbol = symbol;
        globalAddresses[symbol] = id;
        return id;
    }

    InstructionId appendInstruction(BlockId blockId, Opcode opcode, IRType resultType,
                                    vector<ValueId> operands = {}, int sourceLine = 1) {
        if (blockId >= blocks.size()) fail(sourceLine, "internal error: invalid IR block");
        if (blocks[blockId].terminator != InvalidInstruction) {
            fail(sourceLine, "internal error: instruction appended after terminator");
        }
        Instruction instruction;
        instruction.id = static_cast<InstructionId>(instructionArena.size());
        instruction.opcode = opcode;
        instruction.resultType = resultType;
        instruction.parent = blockId;
        instruction.operands = std::move(operands);
        instruction.line = sourceLine;
        if (!resultType.isVoid()) {
            instruction.result = createValue(resultType, ValueKind::InstructionResult);
            values[instruction.result].definition = instruction.id;
        }
        instructionArena.push_back(std::move(instruction));
        Instruction &stored = instructionArena.back();
        for (uint32_t i = 0; i < stored.operands.size(); ++i) addUse(stored.operands[i], stored.id, i);
        if (opcode == Opcode::Phi) blocks[blockId].phis.push_back(stored.id);
        else if (stored.isTerminator()) blocks[blockId].terminator = stored.id;
        else blocks[blockId].instructions.push_back(stored.id);
        return stored.id;
    }

    InstructionId appendPhi(BlockId blockId, IRType type, int line = 1) {
        if (blockId >= blocks.size()) fail(line, "internal error: invalid phi block");
        Instruction instruction;
        instruction.id = static_cast<InstructionId>(instructionArena.size());
        instruction.opcode = Opcode::Phi;
        instruction.resultType = type;
        instruction.parent = blockId;
        instruction.line = line;
        instruction.result = createValue(type, ValueKind::InstructionResult);
        values[instruction.result].definition = instruction.id;
        instructionArena.push_back(std::move(instruction));
        blocks[blockId].phis.push_back(instructionArena.back().id);
        return instructionArena.back().id;
    }

    void addPhiIncoming(InstructionId phiId, BlockId predecessor, ValueId value) {
        Instruction &phi = instructionArena.at(phiId);
        if (phi.opcode != Opcode::Phi) fail(phi.line, "internal error: incoming added to non-phi");
        uint32_t index = static_cast<uint32_t>(phi.operands.size());
        phi.operands.push_back(value);
        phi.incomingBlocks.push_back(predecessor);
        addUse(value, phiId, index);
    }

    void setTargets(InstructionId instructionId, vector<BlockId> targets) {
        Instruction &instruction = instructionArena.at(instructionId);
        if (!instruction.isTerminator()) fail(instruction.line, "internal error: targets on non-terminator");
        for (BlockId target : targets) {
            if (target >= blocks.size()) fail(instruction.line, "internal error: invalid branch target");
        }
        instruction.targets = std::move(targets);
        rebuildEdgesForTerminator(instruction);
    }

    void addUse(ValueId valueId, InstructionId instructionId, uint32_t operandIndex) {
        if (valueId >= values.size()) fail(1, "internal error: invalid IR value");
        values[valueId].users.push_back({instructionId, operandIndex});
    }

    void replaceOperand(InstructionId instructionId, uint32_t operandIndex, ValueId replacement) {
        Instruction &instruction = instructionArena.at(instructionId);
        if (operandIndex >= instruction.operands.size() || replacement >= values.size()) {
            fail(instruction.line, "internal error: invalid IR operand replacement");
        }
        ValueId previous = instruction.operands[operandIndex];
        auto &users = values[previous].users;
        auto oldUse = find(users.begin(), users.end(), Use{instructionId, operandIndex});
        if (oldUse == users.end()) fail(instruction.line, "internal error: missing use-def edge");
        users.erase(oldUse);
        instruction.operands[operandIndex] = replacement;
        addUse(replacement, instructionId, operandIndex);
    }

    void replaceAllUses(ValueId from, ValueId to) {
        if (from == to) return;
        if (from >= values.size() || to >= values.size() || values[from].type != values[to].type) {
            fail(1, "internal error: invalid replace-all-uses");
        }
        vector<Use> uses = values[from].users;
        for (const Use &use : uses) replaceOperand(use.instruction, use.operandIndex, to);
    }

    MemoryAccessId createMemoryAccess(MemoryAccessKind kind, AliasClassId aliasClass,
                                      BlockId block, InstructionId instruction = InvalidInstruction) {
        MemoryAccess access;
        access.id = static_cast<MemoryAccessId>(memoryAccesses.size());
        access.kind = kind;
        access.aliasClass = aliasClass;
        access.block = block;
        access.instruction = instruction;
        memoryAccesses.push_back(std::move(access));
        if (instruction != InvalidInstruction) instructionArena.at(instruction).memoryAccess = access.id;
        return access.id;
    }

    MemoryAccessId getLiveOnEntry(AliasClassId aliasClass) {
        auto found = liveOnEntry.find(aliasClass);
        if (found != liveOnEntry.end()) return found->second;
        MemoryAccessId id = createMemoryAccess(MemoryAccessKind::LiveOnEntry, aliasClass, entry);
        liveOnEntry[aliasClass] = id;
        return id;
    }

    void setDefiningAccess(MemoryAccessId accessId, MemoryAccessId definingId) {
        MemoryAccess &access = memoryAccesses.at(accessId);
        if (access.kind == MemoryAccessKind::LiveOnEntry || access.kind == MemoryAccessKind::Phi) {
            fail(1, "internal error: defining access on invalid MemorySSA node");
        }
        access.definingAccess = definingId;
        memoryAccesses.at(definingId).users.push_back(accessId);
    }

    void addMemoryPhiIncoming(MemoryAccessId phiId, BlockId predecessor, MemoryAccessId incoming) {
        MemoryAccess &phi = memoryAccesses.at(phiId);
        if (phi.kind != MemoryAccessKind::Phi) fail(1, "internal error: incoming on non-memory-phi");
        phi.incoming.push_back({predecessor, incoming});
        memoryAccesses.at(incoming).users.push_back(phiId);
    }

private:
    static void appendUnique(vector<BlockId> &list, BlockId block) {
        if (find(list.begin(), list.end(), block) == list.end()) list.push_back(block);
    }

    void rebuildEdgesForTerminator(const Instruction &instruction) {
        BasicBlock &source = blocks[instruction.parent];
        for (BlockId oldTarget : source.successors) {
            auto &predecessors = blocks[oldTarget].predecessors;
            predecessors.erase(remove(predecessors.begin(), predecessors.end(), source.id), predecessors.end());
        }
        source.successors.clear();
        for (BlockId target : instruction.targets) {
            appendUnique(source.successors, target);
            appendUnique(blocks[target].predecessors, source.id);
        }
    }
};

struct IRGlobal {
    SymbolId semanticSymbol = InvalidSymbol;
    string name;
    Type sourceType;
    vector<ConstValue> initializers;
    bool readOnly = false;
    AliasClassId aliasClass = UnknownAliasClass;
};

struct IRModule {
    vector<IRGlobal> globals;
    vector<IRFunction> functions;
    unordered_map<FunctionId, size_t> functionIndex;
    unordered_map<SymbolId, size_t> globalIndex;

    IRFunction &createFunction(const SemFunction &semantic) {
        IRFunction function;
        function.semanticFunction = semantic.id;
        function.name = semantic.name;
        function.returnType = lowerScalarType(semantic.returnType);
        for (const Type &parameter : semantic.parameterTypes) {
            function.parameterTypes.push_back(parameter.isArray() ? IRType::pointer() : lowerScalarType(parameter.base));
        }
        functionIndex[semantic.id] = functions.size();
        functions.push_back(std::move(function));
        return functions.back();
    }

    IRGlobal &createGlobal(const SemSymbol &semantic, AliasClassId aliasClass) {
        IRGlobal global;
        global.semanticSymbol = semantic.id;
        global.name = semantic.name;
        global.sourceType = semantic.type;
        global.initializers = semantic.globalInitializers;
        global.readOnly = semantic.isConst;
        global.aliasClass = aliasClass;
        globalIndex[semantic.id] = globals.size();
        globals.push_back(std::move(global));
        return globals.back();
    }
};

inline const char *opcodeName(Opcode opcode) {
    switch (opcode) {
        case Opcode::Phi: return "phi";
        case Opcode::Add: return "add";
        case Opcode::Sub: return "sub";
        case Opcode::Mul: return "mul";
        case Opcode::SDiv: return "sdiv";
        case Opcode::SRem: return "srem";
        case Opcode::FAdd: return "fadd";
        case Opcode::FSub: return "fsub";
        case Opcode::FMul: return "fmul";
        case Opcode::FDiv: return "fdiv";
        case Opcode::Neg: return "neg";
        case Opcode::FNeg: return "fneg";
        case Opcode::And: return "and";
        case Opcode::Or: return "or";
        case Opcode::Xor: return "xor";
        case Opcode::Shl: return "shl";
        case Opcode::AShr: return "ashr";
        case Opcode::LShr: return "lshr";
        case Opcode::ICmp: return "icmp";
        case Opcode::FCmp: return "fcmp";
        case Opcode::ZExt: return "zext";
        case Opcode::SExt: return "sext";
        case Opcode::Trunc: return "trunc";
        case Opcode::SIToFP: return "sitofp";
        case Opcode::FPToSI: return "fptosi";
        case Opcode::Bitcast: return "bitcast";
        case Opcode::Select: return "select";
        case Opcode::Alloca: return "alloca";
        case Opcode::GetElementPtr: return "gep";
        case Opcode::Load: return "load";
        case Opcode::Store: return "store";
        case Opcode::Call: return "call";
        case Opcode::Branch: return "br";
        case Opcode::CondBranch: return "condbr";
        case Opcode::Return: return "ret";
        case Opcode::Unreachable: return "unreachable";
    }
    return "unknown";
}

} // namespace sysyc::ir

// ===== IR verifier =====
namespace sysyc::ir {

class IRVerifier {
public:
    static void verify(const IRModule &module) {
        for (const IRFunction &function : module.functions) verifyFunction(function);
    }

    static void verifyFunction(const IRFunction &function) {
        if (function.blocks.empty()) fail(1, "IR verification failed in " + function.name + ": no blocks");
        if (function.entry >= function.blocks.size()) {
            fail(1, "IR verification failed in " + function.name + ": invalid entry block");
        }
        verifyIdentifiers(function);
        verifyCFG(function);
        verifyInstructions(function);
        verifyUseDef(function);
        verifyDominance(function);
        verifyMemorySSA(function);
    }

private:
    static string context(const IRFunction &function, const string &message) {
        return "IR verification failed in " + function.name + ": " + message;
    }

    static void verifyIdentifiers(const IRFunction &function) {
        for (size_t i = 0; i < function.values.size(); ++i) {
            if (function.values[i].id != i) fail(1, context(function, "value ID mismatch"));
        }
        for (size_t i = 0; i < function.instructionArena.size(); ++i) {
            if (function.instructionArena[i].id != i) fail(1, context(function, "instruction ID mismatch"));
        }
        for (size_t i = 0; i < function.blocks.size(); ++i) {
            if (function.blocks[i].id != i) fail(1, context(function, "block ID mismatch"));
        }
        for (size_t i = 0; i < function.memoryAccesses.size(); ++i) {
            if (function.memoryAccesses[i].id != i) fail(1, context(function, "MemorySSA ID mismatch"));
        }
    }

    static void verifyCFG(const IRFunction &function) {
        for (const BasicBlock &block : function.blocks) {
            if (block.terminator == InvalidInstruction) {
                fail(1, context(function, "block " + block.name + " has no terminator"));
            }
            const Instruction &terminator = function.instructionArena.at(block.terminator);
            if (!terminator.isTerminator() || terminator.parent != block.id) {
                fail(terminator.line, context(function, "malformed terminator in " + block.name));
            }
            if (terminator.targets != block.successors) {
                fail(terminator.line, context(function, "terminator targets disagree with CFG in " + block.name));
            }
            for (BlockId successor : block.successors) {
                if (successor >= function.blocks.size()) fail(terminator.line, context(function, "invalid successor"));
                const auto &predecessors = function.blocks[successor].predecessors;
                if (find(predecessors.begin(), predecessors.end(), block.id) == predecessors.end()) {
                    fail(terminator.line, context(function, "missing reverse predecessor edge"));
                }
            }
            for (BlockId predecessor : block.predecessors) {
                if (predecessor >= function.blocks.size()) fail(1, context(function, "invalid predecessor"));
                const auto &successors = function.blocks[predecessor].successors;
                if (find(successors.begin(), successors.end(), block.id) == successors.end()) {
                    fail(1, context(function, "missing reverse successor edge"));
                }
            }
        }
    }

    static void verifyInstructions(const IRFunction &function) {
        vector<int> appearances(function.instructionArena.size(), 0);
        for (const BasicBlock &block : function.blocks) {
            for (InstructionId id : block.phis) {
                const Instruction &instruction = function.instructionArena.at(id);
                if (instruction.opcode != Opcode::Phi || instruction.parent != block.id) {
                    fail(instruction.line, context(function, "invalid phi placement"));
                }
                ++appearances[id];
                if (instruction.operands.size() != instruction.incomingBlocks.size() ||
                    instruction.operands.size() != block.predecessors.size()) {
                    fail(instruction.line, context(function, "phi predecessor count mismatch"));
                }
                for (BlockId predecessor : block.predecessors) {
                    if (count(instruction.incomingBlocks.begin(), instruction.incomingBlocks.end(), predecessor) != 1) {
                        fail(instruction.line, context(function, "phi incoming blocks do not match CFG"));
                    }
                }
            }
            for (InstructionId id : block.instructions) {
                const Instruction &instruction = function.instructionArena.at(id);
                if (instruction.parent != block.id || instruction.opcode == Opcode::Phi || instruction.isTerminator()) {
                    fail(instruction.line, context(function, "invalid instruction placement"));
                }
                ++appearances[id];
            }
            ++appearances[block.terminator];
        }
        for (const Instruction &instruction : function.instructionArena) {
            if (instruction.removed) continue;
            if (appearances[instruction.id] != 1) {
                fail(instruction.line, context(function, "instruction appears in zero or multiple blocks"));
            }
            if (instruction.result == InvalidValue) {
                if (!instruction.resultType.isVoid()) {
                    fail(instruction.line, context(function, "non-void instruction lacks a result"));
                }
            } else {
                if (instruction.result >= function.values.size()) fail(instruction.line, context(function, "invalid result"));
                const Value &value = function.values[instruction.result];
                if (value.kind != ValueKind::InstructionResult || value.definition != instruction.id ||
                    value.type != instruction.resultType) {
                    fail(instruction.line, context(function, "instruction result metadata mismatch"));
                }
            }
            verifyInstructionTypes(function, instruction);
        }
    }

    static void requireOperandCount(const IRFunction &function, const Instruction &instruction,
                                    size_t expected) {
        if (instruction.operands.size() != expected) {
            fail(instruction.line,
                 context(function, string(opcodeName(instruction.opcode)) + " has invalid operand count"));
        }
    }

    static const IRType &operandType(const IRFunction &function, const Instruction &instruction, size_t index) {
        if (index >= instruction.operands.size() || instruction.operands[index] >= function.values.size()) {
            fail(instruction.line, context(function, "invalid operand value"));
        }
        return function.values[instruction.operands[index]].type;
    }

    static void verifyInstructionTypes(const IRFunction &function, const Instruction &instruction) {
        auto sameBinary = [&]() {
            requireOperandCount(function, instruction, 2);
            if (operandType(function, instruction, 0) != instruction.resultType ||
                operandType(function, instruction, 1) != instruction.resultType) {
                fail(instruction.line, context(function, "binary type mismatch"));
            }
        };
        switch (instruction.opcode) {
            case Opcode::Phi:
                for (size_t i = 0; i < instruction.operands.size(); ++i) {
                    if (operandType(function, instruction, i) != instruction.resultType) {
                        fail(instruction.line, context(function, "phi type mismatch"));
                    }
                }
                break;
            case Opcode::Add:
            case Opcode::Sub:
            case Opcode::Mul:
            case Opcode::SDiv:
            case Opcode::SRem:
            case Opcode::And:
            case Opcode::Or:
            case Opcode::Xor:
            case Opcode::Shl:
            case Opcode::AShr:
            case Opcode::LShr:
                sameBinary();
                if (!instruction.resultType.isInteger()) fail(instruction.line, context(function, "integer opcode type"));
                break;
            case Opcode::FAdd:
            case Opcode::FSub:
            case Opcode::FMul:
            case Opcode::FDiv:
                sameBinary();
                if (instruction.resultType.kind != TypeKind::F32) fail(instruction.line, context(function, "float opcode type"));
                break;
            case Opcode::Neg:
            case Opcode::FNeg:
                requireOperandCount(function, instruction, 1);
                if (operandType(function, instruction, 0) != instruction.resultType) {
                    fail(instruction.line, context(function, "unary type mismatch"));
                }
                break;
            case Opcode::ICmp:
            case Opcode::FCmp:
                requireOperandCount(function, instruction, 2);
                if (operandType(function, instruction, 0) != operandType(function, instruction, 1) ||
                    instruction.resultType.kind != TypeKind::I1) {
                    fail(instruction.line, context(function, "comparison type mismatch"));
                }
                break;
            case Opcode::ZExt:
            case Opcode::SExt:
            case Opcode::Trunc:
            case Opcode::SIToFP:
            case Opcode::FPToSI:
            case Opcode::Bitcast:
                requireOperandCount(function, instruction, 1);
                break;
            case Opcode::Select:
                requireOperandCount(function, instruction, 3);
                if (operandType(function, instruction, 0).kind != TypeKind::I1 ||
                    operandType(function, instruction, 1) != instruction.resultType ||
                    operandType(function, instruction, 2) != instruction.resultType) {
                    fail(instruction.line, context(function, "select type mismatch"));
                }
                break;
            case Opcode::Alloca:
                requireOperandCount(function, instruction, 0);
                if (instruction.resultType.kind != TypeKind::Pointer || instruction.object == InvalidSymbol) {
                    fail(instruction.line, context(function, "alloca type mismatch"));
                }
                break;
            case Opcode::GetElementPtr:
                if (instruction.operands.empty() || operandType(function, instruction, 0).kind != TypeKind::Pointer ||
                    instruction.resultType.kind != TypeKind::Pointer) {
                    fail(instruction.line, context(function, "GEP type mismatch"));
                }
                break;
            case Opcode::Load:
                requireOperandCount(function, instruction, 1);
                if (operandType(function, instruction, 0).kind != TypeKind::Pointer || instruction.resultType.isVoid()) {
                    fail(instruction.line, context(function, "load type mismatch"));
                }
                break;
            case Opcode::Store:
                requireOperandCount(function, instruction, 2);
                if (operandType(function, instruction, 0).kind != TypeKind::Pointer ||
                    !instruction.resultType.isVoid()) {
                    fail(instruction.line, context(function, "store type mismatch"));
                }
                break;
            case Opcode::Call:
                break;
            case Opcode::Branch:
                requireOperandCount(function, instruction, 0);
                if (instruction.targets.size() != 1) fail(instruction.line, context(function, "branch target count"));
                break;
            case Opcode::CondBranch:
                requireOperandCount(function, instruction, 1);
                if (operandType(function, instruction, 0).kind != TypeKind::I1 || instruction.targets.size() != 2) {
                    fail(instruction.line, context(function, "conditional branch type"));
                }
                break;
            case Opcode::Return:
                if (instruction.operands.size() > 1 || !instruction.targets.empty()) {
                    fail(instruction.line, context(function, "return shape"));
                }
                if (function.returnType.isVoid()) {
                    if (!instruction.operands.empty()) fail(instruction.line, context(function, "void return value"));
                } else if (instruction.operands.size() != 1 ||
                           operandType(function, instruction, 0) != function.returnType) {
                    fail(instruction.line, context(function, "return type mismatch"));
                }
                break;
            case Opcode::Unreachable:
                requireOperandCount(function, instruction, 0);
                break;
        }
    }

    static void verifyUseDef(const IRFunction &function) {
        vector<size_t> expectedUses(function.values.size(), 0);
        for (const Instruction &instruction : function.instructionArena) {
            if (instruction.removed) continue;
            for (uint32_t i = 0; i < instruction.operands.size(); ++i) {
                ValueId valueId = instruction.operands[i];
                if (valueId >= function.values.size()) fail(instruction.line, context(function, "operand out of range"));
                ++expectedUses[valueId];
            }
        }
        for (const Value &value : function.values) {
            if (value.users.size() != expectedUses[value.id]) {
                fail(1, context(function, "use-def edge count mismatch"));
            }
            vector<Use> sortedUsers = value.users;
            sort(sortedUsers.begin(), sortedUsers.end(), [](const Use &left, const Use &right) {
                if (left.instruction != right.instruction) return left.instruction < right.instruction;
                return left.operandIndex < right.operandIndex;
            });
            for (size_t i = 0; i < sortedUsers.size(); ++i) {
                const Use &use = sortedUsers[i];
                if (i != 0 && use == sortedUsers[i - 1]) {
                    fail(1, context(function, "duplicate use-def edge"));
                }
                if (use.instruction >= function.instructionArena.size()) fail(1, context(function, "invalid user"));
                const Instruction &instruction = function.instructionArena[use.instruction];
                if (use.operandIndex >= instruction.operands.size() || instruction.operands[use.operandIndex] != value.id) {
                    fail(instruction.line, context(function, "stale use-def edge"));
                }
            }
        }
    }

    static vector<bool> reachableBlocks(const IRFunction &function) {
        vector<bool> reachable(function.blocks.size(), false);
        queue<BlockId> work;
        reachable[function.entry] = true;
        work.push(function.entry);
        while (!work.empty()) {
            BlockId block = work.front();
            work.pop();
            for (BlockId successor : function.blocks[block].successors) {
                if (!reachable[successor]) {
                    reachable[successor] = true;
                    work.push(successor);
                }
            }
        }
        return reachable;
    }

    static vector<vector<bool>> computeDominators(const IRFunction &function, const vector<bool> &reachable) {
        size_t countBlocks = function.blocks.size();
        vector<vector<bool>> dominators(countBlocks, vector<bool>(countBlocks, false));
        for (size_t block = 0; block < countBlocks; ++block) {
            if (!reachable[block]) continue;
            if (block == function.entry) dominators[block][block] = true;
            else {
                for (size_t candidate = 0; candidate < countBlocks; ++candidate) {
                    dominators[block][candidate] = reachable[candidate];
                }
            }
        }
        bool changed = true;
        while (changed) {
            changed = false;
            for (size_t block = 0; block < countBlocks; ++block) {
                if (!reachable[block] || block == function.entry) continue;
                vector<bool> next(countBlocks, true);
                bool sawPredecessor = false;
                for (BlockId predecessor : function.blocks[block].predecessors) {
                    if (!reachable[predecessor]) continue;
                    if (!sawPredecessor) {
                        next = dominators[predecessor];
                        sawPredecessor = true;
                    } else {
                        for (size_t i = 0; i < countBlocks; ++i) next[i] = next[i] && dominators[predecessor][i];
                    }
                }
                if (!sawPredecessor) fill(next.begin(), next.end(), false);
                next[block] = true;
                if (next != dominators[block]) {
                    dominators[block] = std::move(next);
                    changed = true;
                }
            }
        }
        return dominators;
    }

    static void verifyDominance(const IRFunction &function) {
        vector<bool> reachable = reachableBlocks(function);
        vector<vector<bool>> dominators = computeDominators(function, reachable);
        vector<int> position(function.instructionArena.size(), -1);
        for (const BasicBlock &block : function.blocks) {
            int current = 0;
            for (InstructionId phi : block.phis) position[phi] = current++;
            for (InstructionId instruction : block.instructions) position[instruction] = current++;
            position[block.terminator] = current;
        }
        for (const Instruction &instruction : function.instructionArena) {
            if (instruction.removed || !reachable[instruction.parent]) continue;
            for (size_t i = 0; i < instruction.operands.size(); ++i) {
                const Value &value = function.values[instruction.operands[i]];
                if (value.definition == InvalidInstruction) continue;
                const Instruction &definition = function.instructionArena[value.definition];
                if (instruction.opcode == Opcode::Phi) {
                    BlockId incomingBlock = instruction.incomingBlocks[i];
                    if (!dominators[incomingBlock][definition.parent]) {
                        fail(instruction.line, context(function, "phi operand definition does not dominate incoming edge"));
                    }
                } else {
                    if (!dominators[instruction.parent][definition.parent]) {
                        fail(instruction.line, context(function, "definition does not dominate use"));
                    }
                    if (definition.parent == instruction.parent && position[definition.id] >= position[instruction.id]) {
                        fail(instruction.line, context(function, "definition appears after use"));
                    }
                }
            }
        }
    }

    static void verifyMemorySSA(const IRFunction &function) {
        for (const MemoryAccess &access : function.memoryAccesses) {
            if (access.block >= function.blocks.size()) fail(1, context(function, "invalid MemorySSA block"));
            if (access.kind == MemoryAccessKind::Use || access.kind == MemoryAccessKind::Def) {
                if (access.instruction >= function.instructionArena.size() ||
                    function.instructionArena[access.instruction].memoryAccess != access.id ||
                    access.definingAccess >= function.memoryAccesses.size()) {
                    fail(1, context(function, "malformed MemorySSA use/def"));
                }
            } else if (access.kind == MemoryAccessKind::Phi) {
                const BasicBlock &block = function.blocks[access.block];
                if (access.incoming.size() != block.predecessors.size()) {
                    fail(1, context(function, "MemoryPhi predecessor count mismatch"));
                }
            }
            for (MemoryAccessId user : access.users) {
                if (user >= function.memoryAccesses.size()) fail(1, context(function, "invalid MemorySSA user"));
                const MemoryAccess &userAccess = function.memoryAccesses[user];
                bool found = userAccess.definingAccess == access.id;
                for (const MemoryIncoming &incoming : userAccess.incoming) found = found || incoming.access == access.id;
                if (!found) fail(1, context(function, "stale MemorySSA user edge"));
            }
        }
    }
};

} // namespace sysyc::ir

// ===== Sealed-block SSA builder =====
namespace sysyc::ir {

class IRBuilder {
public:
    IRBuilder(const Program &program, const SemanticProgram &semantics)
        : program_(program), semantics_(semantics) {}

    IRModule build() {
        buildGlobals();
        for (const TopLevel &item : program_.items) {
            if (!item.isFunc) continue;
            FunctionId functionId = semantics_.functionDefinitions.at(item.func.get());
            IRFunction &function = module_.createFunction(semantics_.function(functionId));
            FunctionBuilder builder(semantics_, *item.func, function, nextAliasClass_);
            builder.build();
        }
        return std::move(module_);
    }

private:
    const Program &program_;
    const SemanticProgram &semantics_;
    IRModule module_;
    AliasClassId nextAliasClass_ = 1;

    void buildGlobals() {
        for (const SemSymbol &symbol : semantics_.symbols) {
            if (!symbol.isGlobal) continue;
            module_.createGlobal(symbol, nextAliasClass_++);
        }
    }

    class FunctionBuilder {
    public:
        FunctionBuilder(const SemanticProgram &semantics, const FuncDef &ast,
                        IRFunction &function, AliasClassId &nextAliasClass)
            : semantics_(semantics), ast_(ast), function_(function),
              semanticFunction_(semantics.function(semantics.functionDefinitions.at(&ast))),
              nextAliasClass_(nextAliasClass) {}

        void build() {
            currentBlock_ = createBlock("entry");
            createParameters();
            createAddressableObjects(ast_.block);
            sealBlock(currentBlock_);
            emitStatement(ast_.block, false);
            finishFunction();
        }

    private:
        const SemanticProgram &semantics_;
        const FuncDef &ast_;
        IRFunction &function_;
        const SemFunction &semanticFunction_;
        AliasClassId &nextAliasClass_;
        BlockId currentBlock_ = InvalidBlock;
        uint32_t blockCounter_ = 0;
        vector<unordered_map<SymbolId, ValueId>> currentDefinitions_;
        vector<unordered_map<SymbolId, InstructionId>> incompletePhis_;
        vector<MemoryAccessId> currentMemory_;
        vector<MemoryAccessId> incompleteMemoryPhis_;
        unordered_map<SymbolId, ValueId> objectPointers_;
        unordered_map<SymbolId, AliasClassId> objectAliases_;
        vector<BlockId> breakTargets_;
        vector<BlockId> continueTargets_;

        static int productTail(const vector<int> &dims, size_t start) {
            int64_t result = 1;
            for (size_t i = start; i < dims.size(); ++i) result *= dims[i];
            return static_cast<int>(result);
        }

        BlockId createBlock(const string &hint) {
            BlockId id = function_.createBlock(hint + "." + to_string(blockCounter_++));
            currentDefinitions_.resize(function_.blocks.size());
            incompletePhis_.resize(function_.blocks.size());
            currentMemory_.resize(function_.blocks.size(), InvalidMemoryAccess);
            incompleteMemoryPhis_.resize(function_.blocks.size(), InvalidMemoryAccess);
            return id;
        }

        bool isTerminated(BlockId block) const {
            return function_.blocks.at(block).terminator != InvalidInstruction;
        }

        void createParameters() {
            for (size_t i = 0; i < semanticFunction_.parameters.size(); ++i) {
                SymbolId symbolId = semanticFunction_.parameters[i];
                const SemSymbol &symbol = semantics_.symbol(symbolId);
                IRType type = symbol.type.isArray() ? IRType::pointer() : lowerScalarType(symbol.type.base);
                ValueId value = function_.createParameter(type, static_cast<uint32_t>(i), symbol.name);
                if (symbol.type.isArray()) {
                    objectPointers_[symbolId] = value;
                    objectAliases_[symbolId] = UnknownAliasClass;
                } else {
                    writeVariable(symbolId, currentBlock_, value);
                }
            }
        }

        void createAddressableObjects(const StmtPtr &statement) {
            if (!statement) return;
            switch (statement->kind) {
                case StmtKind::Block:
                    for (const BlockItem &item : statement->items) {
                        if (item.isDecl) {
                            for (const VarDef &definition : item.decl->defs) {
                                SymbolId symbolId = semantics_.variableDefinitions.at(&definition);
                                const SemSymbol &symbol = semantics_.symbol(symbolId);
                                if (!symbol.type.isArray()) continue;
                                InstructionId alloca = function_.appendInstruction(
                                    function_.entry, Opcode::Alloca, IRType::pointer(), {}, definition.line);
                                Instruction &instruction = function_.instructionArena[alloca];
                                instruction.object = symbolId;
                                instruction.aliasClass = nextAliasClass_++;
                                objectPointers_[symbolId] = instruction.result;
                                objectAliases_[symbolId] = instruction.aliasClass;
                            }
                        } else {
                            createAddressableObjects(item.stmt);
                        }
                    }
                    break;
                case StmtKind::If:
                    createAddressableObjects(statement->thenStmt);
                    createAddressableObjects(statement->elseStmt);
                    break;
                case StmtKind::While:
                    createAddressableObjects(statement->body);
                    break;
                case StmtKind::Assign:
                case StmtKind::Expr:
                case StmtKind::Break:
                case StmtKind::Continue:
                case StmtKind::Return:
                    break;
            }
        }

        IRType variableType(SymbolId symbolId) const {
            const SemSymbol &symbol = semantics_.symbol(symbolId);
            if (!symbol.type.isScalar()) fail(symbol.line, "internal error: non-scalar SSA variable");
            return lowerScalarType(symbol.type.base);
        }

        void writeVariable(SymbolId symbol, BlockId block, ValueId value) {
            currentDefinitions_.at(block)[symbol] = value;
        }

        ValueId readVariable(SymbolId symbol, BlockId block) {
            auto found = currentDefinitions_.at(block).find(symbol);
            if (found != currentDefinitions_.at(block).end()) return found->second;
            ValueId value = readVariableRecursive(symbol, block);
            currentDefinitions_.at(block)[symbol] = value;
            return value;
        }

        ValueId readVariableRecursive(SymbolId symbol, BlockId block) {
            IRType type = variableType(symbol);
            BasicBlock &basicBlock = function_.blocks.at(block);
            if (!basicBlock.sealed) {
                InstructionId phi = function_.appendPhi(block, type, semantics_.symbol(symbol).line);
                incompletePhis_[block][symbol] = phi;
                ValueId result = function_.instructionArena[phi].result;
                writeVariable(symbol, block, result);
                return result;
            }
            if (basicBlock.predecessors.empty()) return function_.undef(type);
            if (basicBlock.predecessors.size() == 1) {
                return readVariable(symbol, basicBlock.predecessors.front());
            }
            InstructionId phi = function_.appendPhi(block, type, semantics_.symbol(symbol).line);
            ValueId result = function_.instructionArena[phi].result;
            writeVariable(symbol, block, result);
            for (BlockId predecessor : basicBlock.predecessors) {
                function_.addPhiIncoming(phi, predecessor, readVariable(symbol, predecessor));
            }
            return removeTrivialPhi(phi, symbol);
        }

        ValueId removeTrivialPhi(InstructionId phiId, SymbolId symbol) {
            Instruction &phi = function_.instructionArena.at(phiId);
            ValueId phiValue = phi.result;
            ValueId replacement = InvalidValue;
            for (ValueId operand : phi.operands) {
                if (operand == phiValue) continue;
                if (replacement == InvalidValue) replacement = operand;
                else if (replacement != operand) return phiValue;
            }
            if (replacement == InvalidValue) replacement = function_.undef(phi.resultType);
            function_.replaceAllUses(phiValue, replacement);
            for (auto &definitions : currentDefinitions_) {
                for (auto &entry : definitions) {
                    if (entry.second == phiValue) entry.second = replacement;
                }
            }
            for (ValueId operand : phi.operands) {
                auto &users = function_.values[operand].users;
                users.erase(remove(users.begin(), users.end(), Use{phiId, 0}), users.end());
                for (uint32_t i = 0; i < phi.operands.size(); ++i) {
                    users.erase(remove(users.begin(), users.end(), Use{phiId, i}), users.end());
                }
            }
            auto &phis = function_.blocks[phi.parent].phis;
            phis.erase(remove(phis.begin(), phis.end(), phiId), phis.end());
            phi.removed = true;
            incompletePhis_[phi.parent].erase(symbol);
            return replacement;
        }

        MemoryAccessId readMemory(BlockId block) {
            if (currentMemory_[block] != InvalidMemoryAccess) return currentMemory_[block];
            BasicBlock &basicBlock = function_.blocks[block];
            if (!basicBlock.sealed) {
                MemoryAccessId phi = function_.createMemoryAccess(
                    MemoryAccessKind::Phi, UnknownAliasClass, block);
                incompleteMemoryPhis_[block] = phi;
                currentMemory_[block] = phi;
                return phi;
            }
            if (basicBlock.predecessors.empty()) {
                currentMemory_[block] = function_.getLiveOnEntry(UnknownAliasClass);
            } else if (basicBlock.predecessors.size() == 1) {
                currentMemory_[block] = readMemory(basicBlock.predecessors.front());
            } else {
                MemoryAccessId phi = function_.createMemoryAccess(
                    MemoryAccessKind::Phi, UnknownAliasClass, block);
                currentMemory_[block] = phi;
                for (BlockId predecessor : basicBlock.predecessors) {
                    function_.addMemoryPhiIncoming(phi, predecessor, readMemory(predecessor));
                }
            }
            return currentMemory_[block];
        }

        void sealBlock(BlockId block) {
            BasicBlock &basicBlock = function_.blocks.at(block);
            if (basicBlock.sealed) return;
            basicBlock.sealed = true;
            vector<pair<SymbolId, InstructionId>> phis(
                incompletePhis_[block].begin(), incompletePhis_[block].end());
            for (const auto &[symbol, phi] : phis) {
                for (BlockId predecessor : basicBlock.predecessors) {
                    function_.addPhiIncoming(phi, predecessor, readVariable(symbol, predecessor));
                }
                removeTrivialPhi(phi, symbol);
            }
            incompletePhis_[block].clear();
            MemoryAccessId memoryPhi = incompleteMemoryPhis_[block];
            if (memoryPhi != InvalidMemoryAccess) {
                for (BlockId predecessor : basicBlock.predecessors) {
                    function_.addMemoryPhiIncoming(memoryPhi, predecessor, readMemory(predecessor));
                }
                incompleteMemoryPhis_[block] = InvalidMemoryAccess;
            }
        }

        ValueId emitValue(Opcode opcode, IRType type, vector<ValueId> operands, int line) {
            InstructionId instruction = function_.appendInstruction(
                currentBlock_, opcode, type, std::move(operands), line);
            return function_.instructionArena[instruction].result;
        }

        InstructionId emitVoid(Opcode opcode, vector<ValueId> operands, int line) {
            return function_.appendInstruction(
                currentBlock_, opcode, IRType::voidTy(), std::move(operands), line);
        }

        void emitBranch(BlockId target, int line) {
            InstructionId branch = emitVoid(Opcode::Branch, {}, line);
            function_.setTargets(branch, {target});
        }

        void emitCondBranch(ValueId condition, BlockId trueTarget, BlockId falseTarget, int line) {
            InstructionId branch = emitVoid(Opcode::CondBranch, {condition}, line);
            function_.setTargets(branch, {trueTarget, falseTarget});
        }

        ValueId constant(ConstValue value, BaseType target) {
            value = convertConst(value, target);
            if (target == BaseType::Float) return function_.constantFloat(floatBits(value.f));
            return function_.constantInt(IRType::i32(), value.i);
        }

        ValueId zero(IRType type) {
            if (type.kind == TypeKind::F32) return function_.constantFloat(0);
            return function_.constantInt(type, 0);
        }

        ValueId convert(ValueId value, IRType target, int line) {
            if (value == InvalidValue) fail(line, "internal error: conversion of void value");
            IRType source = function_.values[value].type;
            if (source == target) return value;
            if (source.kind == TypeKind::I1 && target.kind == TypeKind::I32) {
                return emitValue(Opcode::ZExt, target, {value}, line);
            }
            if (source.kind == TypeKind::I32 && target.kind == TypeKind::I1) return toBoolean(value, line);
            if (source.kind == TypeKind::I32 && target.kind == TypeKind::F32) {
                return emitValue(Opcode::SIToFP, target, {value}, line);
            }
            if (source.kind == TypeKind::F32 && target.kind == TypeKind::I32) {
                return emitValue(Opcode::FPToSI, target, {value}, line);
            }
            fail(line, "internal error: unsupported IR conversion");
        }

        ValueId toBoolean(ValueId value, int line) {
            IRType type = function_.values.at(value).type;
            if (type.kind == TypeKind::I1) return value;
            if (type.kind == TypeKind::I32) {
                ValueId comparison = emitValue(Opcode::ICmp, IRType::i1(), {value, zero(type)}, line);
                function_.instructionArena[function_.values[comparison].definition].predicate = ComparePredicate::NE;
                return comparison;
            }
            if (type.kind == TypeKind::F32) {
                ValueId comparison = emitValue(Opcode::FCmp, IRType::i1(), {value, zero(type)}, line);
                function_.instructionArena[function_.values[comparison].definition].predicate = ComparePredicate::ONE;
                return comparison;
            }
            fail(line, "internal error: non-scalar condition");
        }

        AliasClassId aliasFor(SymbolId symbol) const {
            auto local = objectAliases_.find(symbol);
            if (local != objectAliases_.end()) return local->second;
            return static_cast<AliasClassId>(symbol + 1);
        }

        ValueId baseAddress(SymbolId symbolId) {
            auto local = objectPointers_.find(symbolId);
            if (local != objectPointers_.end()) return local->second;
            const SemSymbol &symbol = semantics_.symbol(symbolId);
            if (!symbol.isGlobal) fail(symbol.line, "internal error: missing object address");
            ValueId address = function_.globalAddress(symbolId, symbol.name);
            objectPointers_[symbolId] = address;
            objectAliases_[symbolId] = static_cast<AliasClassId>(symbolId + 1);
            return address;
        }

        void attachMemoryAccess(InstructionId instructionId, MemoryAccessKind kind,
                                AliasClassId aliasClass) {
            MemoryAccessId defining = readMemory(currentBlock_);
            MemoryAccessId access = function_.createMemoryAccess(
                kind, aliasClass, currentBlock_, instructionId);
            function_.setDefiningAccess(access, defining);
            if (kind == MemoryAccessKind::Def) currentMemory_[currentBlock_] = access;
        }

        ValueId emitLoad(ValueId address, IRType type, SymbolId object, int line) {
            InstructionId instruction = function_.appendInstruction(
                currentBlock_, Opcode::Load, type, {address}, line);
            Instruction &load = function_.instructionArena[instruction];
            load.object = object;
            load.aliasClass = aliasFor(object);
            attachMemoryAccess(instruction, MemoryAccessKind::Use, load.aliasClass);
            return load.result;
        }

        void emitStore(ValueId address, ValueId value, SymbolId object, int line) {
            InstructionId instruction = emitVoid(Opcode::Store, {address, value}, line);
            Instruction &store = function_.instructionArena[instruction];
            store.object = object;
            store.aliasClass = aliasFor(object);
            attachMemoryAccess(instruction, MemoryAccessKind::Def, store.aliasClass);
        }

        ValueId emitAddress(const ExprPtr &expression,
                            const unordered_map<const Expr *, ValueId> &computedIndices) {
            const ExprSemantics &semantics = semantics_.expression(expression);
            SymbolId symbolId = semantics.symbol;
            const SemSymbol &symbol = semantics_.symbol(symbolId);
            ValueId address = baseAddress(symbolId);
            if (expression->indices.empty()) return address;

            ValueId elementOffset = function_.constantInt(IRType::i32(), 0);
            for (size_t i = 0; i < expression->indices.size(); ++i) {
                ValueId index = computedIndices.at(expression->indices[i].get());
                index = convert(index, IRType::i32(), expression->indices[i]->line);
                int stride = productTail(symbol.type.dims, i + 1);
                ValueId term = index;
                if (stride != 1) {
                    term = emitValue(Opcode::Mul, IRType::i32(),
                                     {index, function_.constantInt(IRType::i32(), stride)}, expression->line);
                }
                elementOffset = emitValue(Opcode::Add, IRType::i32(), {elementOffset, term}, expression->line);
            }
            ValueId byteOffset = emitValue(
                Opcode::Mul, IRType::i32(),
                {elementOffset, function_.constantInt(IRType::i32(), 4)}, expression->line);
            ValueId result = emitValue(Opcode::GetElementPtr, IRType::pointer(),
                                       {address, byteOffset}, expression->line);
            Instruction &gep = function_.instructionArena[function_.values[result].definition];
            gep.object = symbolId;
            gep.aliasClass = aliasFor(symbolId);
            return result;
        }

        ValueId emitLValue(const ExprPtr &expression,
                           const unordered_map<const Expr *, ValueId> &computedIndices) {
            const ExprSemantics &expressionSemantics = semantics_.expression(expression);
            const SemSymbol &symbol = semantics_.symbol(expressionSemantics.symbol);
            if (expression->indices.empty() && symbol.type.isScalar() && symbol.isConst &&
                !symbol.constElements.empty()) {
                return constant(symbol.constElements.front(), symbol.type.base);
            }
            if (expression->indices.empty() && symbol.type.isScalar() && !symbol.isGlobal) {
                return readVariable(symbol.id, currentBlock_);
            }
            ValueId address = emitAddress(expression, computedIndices);
            if (expressionSemantics.type.isArray()) return address;
            return emitLoad(address, lowerScalarType(expressionSemantics.type.base), symbol.id, expression->line);
        }

        ValueId emitExpression(const ExprPtr &root) {
            if (!root) return InvalidValue;
            unordered_map<const Expr *, ValueId> computed;
            vector<pair<ExprPtr, bool>> work;
            work.push_back({root, false});
            while (!work.empty()) {
                ExprPtr expression = work.back().first;
                bool ready = work.back().second;
                work.pop_back();
                if (computed.count(expression.get())) continue;
                if (!ready) {
                    work.push_back({expression, true});
                    switch (expression->kind) {
                        case ExprKind::LVal:
                            for (auto it = expression->indices.rbegin(); it != expression->indices.rend(); ++it) {
                                if (!computed.count(it->get())) work.push_back({*it, false});
                            }
                            break;
                        case ExprKind::Unary:
                            if (!computed.count(expression->lhs.get())) work.push_back({expression->lhs, false});
                            break;
                        case ExprKind::Binary:
                            if (expression->text != "&&" && expression->text != "||") {
                                if (!computed.count(expression->rhs.get())) work.push_back({expression->rhs, false});
                                if (!computed.count(expression->lhs.get())) work.push_back({expression->lhs, false});
                            }
                            break;
                        case ExprKind::Call:
                        case ExprKind::IntLit:
                        case ExprKind::FloatLit:
                        case ExprKind::StringLit:
                            break;
                    }
                    continue;
                }
                computed[expression.get()] = emitExpressionReady(expression, computed);
            }
            return computed.at(root.get());
        }

        ValueId emitExpressionReady(const ExprPtr &expression,
                                    const unordered_map<const Expr *, ValueId> &computed) {
            switch (expression->kind) {
                case ExprKind::IntLit:
                    return function_.constantInt(IRType::i32(), static_cast<int>(expression->intVal));
                case ExprKind::FloatLit:
                    return function_.constantFloat(floatBits(expression->floatVal));
                case ExprKind::StringLit:
                    fail(expression->line, "string literal is only valid as putf format");
                case ExprKind::LVal:
                    return emitLValue(expression, computed);
                case ExprKind::Unary:
                    return emitUnary(expression, computed.at(expression->lhs.get()));
                case ExprKind::Binary:
                    if (expression->text == "&&" || expression->text == "||") return emitLogical(expression);
                    return emitBinary(expression,
                                      computed.at(expression->lhs.get()),
                                      computed.at(expression->rhs.get()));
                case ExprKind::Call:
                    return emitCall(expression);
            }
            fail(expression->line, "internal error: invalid expression kind");
        }

        ValueId emitUnary(const ExprPtr &expression, ValueId operand) {
            if (expression->text == "+") return operand;
            if (expression->text == "-") {
                IRType type = function_.values[operand].type;
                Opcode opcode = type.kind == TypeKind::F32 ? Opcode::FNeg : Opcode::Neg;
                return emitValue(opcode, type, {operand}, expression->line);
            }
            if (expression->text == "!") {
                ValueId boolean = toBoolean(operand, expression->line);
                ValueId inverted = emitValue(
                    Opcode::Xor, IRType::i1(),
                    {boolean, function_.constantInt(IRType::i1(), 1)}, expression->line);
                return convert(inverted, IRType::i32(), expression->line);
            }
            fail(expression->line, "internal error: invalid unary operator");
        }

        ValueId emitBinary(const ExprPtr &expression, ValueId left, ValueId right) {
            const string &op = expression->text;
            bool comparison = op == "==" || op == "!=" || op == "<" || op == ">" ||
                              op == "<=" || op == ">=";
            const ExprSemantics &resultSemantics = semantics_.expression(expression);
            bool floating = semantics_.expression(expression->lhs).type.base == BaseType::Float ||
                            semantics_.expression(expression->rhs).type.base == BaseType::Float;
            if (comparison) {
                IRType operandType = floating ? IRType::f32() : IRType::i32();
                left = convert(left, operandType, expression->line);
                right = convert(right, operandType, expression->line);
                Opcode opcode = floating ? Opcode::FCmp : Opcode::ICmp;
                ValueId result = emitValue(opcode, IRType::i1(), {left, right}, expression->line);
                Instruction &instruction = function_.instructionArena[function_.values[result].definition];
                instruction.predicate = comparisonPredicate(op, floating);
                return convert(result, IRType::i32(), expression->line);
            }

            IRType type = lowerScalarType(resultSemantics.type.base);
            left = convert(left, type, expression->line);
            right = convert(right, type, expression->line);
            Opcode opcode;
            if (type.kind == TypeKind::F32) {
                if (op == "+") opcode = Opcode::FAdd;
                else if (op == "-") opcode = Opcode::FSub;
                else if (op == "*") opcode = Opcode::FMul;
                else if (op == "/") opcode = Opcode::FDiv;
                else fail(expression->line, "internal error: invalid float operator");
            } else {
                if (op == "+") opcode = Opcode::Add;
                else if (op == "-") opcode = Opcode::Sub;
                else if (op == "*") opcode = Opcode::Mul;
                else if (op == "/") opcode = Opcode::SDiv;
                else if (op == "%") opcode = Opcode::SRem;
                else fail(expression->line, "internal error: invalid integer operator");
            }
            return emitValue(opcode, type, {left, right}, expression->line);
        }

        static ComparePredicate comparisonPredicate(const string &op, bool floating) {
            if (floating) {
                if (op == "==") return ComparePredicate::OEQ;
                if (op == "!=") return ComparePredicate::ONE;
                if (op == "<") return ComparePredicate::OLT;
                if (op == "<=") return ComparePredicate::OLE;
                if (op == ">") return ComparePredicate::OGT;
                return ComparePredicate::OGE;
            }
            if (op == "==") return ComparePredicate::EQ;
            if (op == "!=") return ComparePredicate::NE;
            if (op == "<") return ComparePredicate::SLT;
            if (op == "<=") return ComparePredicate::SLE;
            if (op == ">") return ComparePredicate::SGT;
            return ComparePredicate::SGE;
        }

        ValueId emitLogical(const ExprPtr &expression) {
            bool isAnd = expression->text == "&&";
            ValueId left = toBoolean(emitExpression(expression->lhs), expression->line);
            BlockId leftBlock = currentBlock_;
            BlockId rightBlock = createBlock(isAnd ? "land.rhs" : "lor.rhs");
            BlockId mergeBlock = createBlock(isAnd ? "land.merge" : "lor.merge");
            if (isAnd) emitCondBranch(left, rightBlock, mergeBlock, expression->line);
            else emitCondBranch(left, mergeBlock, rightBlock, expression->line);
            sealBlock(rightBlock);

            currentBlock_ = rightBlock;
            ValueId right = toBoolean(emitExpression(expression->rhs), expression->line);
            BlockId rightExit = currentBlock_;
            emitBranch(mergeBlock, expression->line);
            sealBlock(mergeBlock);
            currentBlock_ = mergeBlock;

            InstructionId phi = function_.appendPhi(mergeBlock, IRType::i1(), expression->line);
            function_.addPhiIncoming(phi, leftBlock,
                                     function_.constantInt(IRType::i1(), isAnd ? 0 : 1));
            function_.addPhiIncoming(phi, rightExit, right);
            return convert(function_.instructionArena[phi].result, IRType::i32(), expression->line);
        }

        ValueId emitCall(const ExprPtr &expression) {
            const ExprSemantics &expressionSemantics = semantics_.expression(expression);
            const SemFunction &callee = semantics_.function(expressionSemantics.function);
            if (callee.name == "putf") return emitPutf(expression);
            vector<ValueId> arguments;
            if (callee.name == "starttime" || callee.name == "stoptime") {
                arguments.push_back(function_.constantInt(IRType::i32(), expression->line));
            } else {
                for (size_t i = 0; i < expression->args.size(); ++i) {
                    ValueId argument = emitExpression(expression->args[i]);
                    if (i < callee.parameterTypes.size() && callee.parameterTypes[i].isScalar()) {
                        argument = convert(argument, lowerScalarType(callee.parameterTypes[i].base),
                                           expression->args[i]->line);
                    }
                    arguments.push_back(argument);
                }
            }
            return emitCallInstruction(callee, std::move(arguments), expression->line);
        }

        ValueId emitCallInstruction(const SemFunction &callee, vector<ValueId> arguments, int line) {
            IRType returnType = lowerScalarType(callee.returnType);
            InstructionId call = function_.appendInstruction(
                currentBlock_, Opcode::Call, returnType, std::move(arguments), line);
            function_.instructionArena[call].callee = callee.id;
            if (callee.effect == BuiltinEffect::ReadsMemory) {
                attachMemoryAccess(call, MemoryAccessKind::Use, UnknownAliasClass);
            } else if (callee.effect == BuiltinEffect::WritesMemory ||
                       callee.effect == BuiltinEffect::ReadsAndWritesMemory) {
                attachMemoryAccess(call, MemoryAccessKind::Def, UnknownAliasClass);
            }
            return function_.instructionArena[call].result;
        }

        ValueId emitPutf(const ExprPtr &expression) {
            const string &format = expression->args.front()->text;
            size_t argumentIndex = 1;
            const SemFunction &putint = semantics_.function(
                semantics_.functionsByName.at("putint"));
            const SemFunction &putch = semantics_.function(
                semantics_.functionsByName.at("putch"));
            const SemFunction &putfloat = semantics_.function(
                semantics_.functionsByName.at("putfloat"));
            for (size_t i = 0; i < format.size(); ++i) {
                if (format[i] == '%' && i + 1 < format.size()) {
                    char specifier = format[++i];
                    if (specifier == '%') {
                        emitCallInstruction(putch, {function_.constantInt(IRType::i32(), '%')}, expression->line);
                    } else if (specifier == 'd' || specifier == 'c' || specifier == 'f') {
                        if (argumentIndex >= expression->args.size()) fail(expression->line, "not enough putf arguments");
                        ValueId argument = emitExpression(expression->args[argumentIndex++]);
                        if (specifier == 'f') {
                            argument = convert(argument, IRType::f32(), expression->line);
                            emitCallInstruction(putfloat, {argument}, expression->line);
                        } else {
                            argument = convert(argument, IRType::i32(), expression->line);
                            emitCallInstruction(specifier == 'd' ? putint : putch, {argument}, expression->line);
                        }
                    } else {
                        emitCallInstruction(putch,
                                            {function_.constantInt(IRType::i32(),
                                                                   static_cast<unsigned char>(specifier))},
                                            expression->line);
                    }
                } else {
                    emitCallInstruction(putch,
                                        {function_.constantInt(IRType::i32(),
                                                               static_cast<unsigned char>(format[i]))},
                                        expression->line);
                }
            }
            return InvalidValue;
        }

        void emitStatement(const StmtPtr &statement, bool createScope = true) {
            (void)createScope;
            if (!statement || isTerminated(currentBlock_)) return;
            switch (statement->kind) {
                case StmtKind::Block:
                    for (const BlockItem &item : statement->items) {
                        if (isTerminated(currentBlock_)) break;
                        if (item.isDecl) emitDeclaration(*item.decl);
                        else emitStatement(item.stmt);
                    }
                    return;
                case StmtKind::Assign:
                    emitAssignment(statement);
                    return;
                case StmtKind::Expr:
                    if (statement->expr) emitExpression(statement->expr);
                    return;
                case StmtKind::If:
                    emitIf(statement);
                    return;
                case StmtKind::While:
                    emitWhile(statement);
                    return;
                case StmtKind::Break:
                    if (breakTargets_.empty()) fail(statement->line, "internal error: break target missing");
                    emitBranch(breakTargets_.back(), statement->line);
                    return;
                case StmtKind::Continue:
                    if (continueTargets_.empty()) fail(statement->line, "internal error: continue target missing");
                    emitBranch(continueTargets_.back(), statement->line);
                    return;
                case StmtKind::Return:
                    emitReturn(statement);
                    return;
            }
        }

        void emitDeclaration(const Decl &declaration) {
            for (const VarDef &definition : declaration.defs) {
                SymbolId symbolId = semantics_.variableDefinitions.at(&definition);
                const SemSymbol &symbol = semantics_.symbol(symbolId);
                const auto &flattened = semantics_.flattenedInitializers.at(&definition);
                if (!symbol.type.isArray()) {
                    ValueId value;
                    if (definition.init) {
                        ExprPtr initializer = flattened.empty() ? nullptr : flattened.front();
                        value = initializer ? emitExpression(initializer)
                                            : zero(lowerScalarType(symbol.type.base));
                        value = convert(value, lowerScalarType(symbol.type.base), definition.line);
                    } else {
                        value = function_.undef(lowerScalarType(symbol.type.base));
                    }
                    writeVariable(symbolId, currentBlock_, value);
                    continue;
                }
                if (!definition.init) continue;
                ValueId base = baseAddress(symbolId);
                int count = productTail(symbol.type.dims, 0);
                for (int i = 0; i < count; ++i) {
                    ValueId value = flattened[static_cast<size_t>(i)]
                                        ? emitExpression(flattened[static_cast<size_t>(i)])
                                        : zero(lowerScalarType(symbol.type.base));
                    value = convert(value, lowerScalarType(symbol.type.base), definition.line);
                    ValueId byteOffset = function_.constantInt(IRType::i32(), i * 4);
                    ValueId address = emitValue(Opcode::GetElementPtr, IRType::pointer(),
                                                {base, byteOffset}, definition.line);
                    Instruction &gep = function_.instructionArena[function_.values[address].definition];
                    gep.object = symbolId;
                    gep.aliasClass = aliasFor(symbolId);
                    emitStore(address, value, symbolId, definition.line);
                }
            }
        }

        void emitAssignment(const StmtPtr &statement) {
            const ExprSemantics &lhsSemantics = semantics_.expression(statement->lhs);
            const SemSymbol &symbol = semantics_.symbol(lhsSemantics.symbol);
            ValueId value = emitExpression(statement->expr);
            value = convert(value, lowerScalarType(lhsSemantics.type.base), statement->line);
            if (statement->lhs->indices.empty() && symbol.type.isScalar() && !symbol.isGlobal) {
                writeVariable(symbol.id, currentBlock_, value);
                return;
            }
            unordered_map<const Expr *, ValueId> indices;
            for (const ExprPtr &index : statement->lhs->indices) indices[index.get()] = emitExpression(index);
            ValueId address = emitAddress(statement->lhs, indices);
            emitStore(address, value, symbol.id, statement->line);
        }

        void emitIf(const StmtPtr &statement) {
            ValueId condition = toBoolean(emitExpression(statement->cond), statement->line);
            BlockId thenBlock = createBlock("if.then");
            BlockId elseBlock = statement->elseStmt ? createBlock("if.else") : InvalidBlock;
            BlockId mergeBlock = createBlock("if.end");
            emitCondBranch(condition, thenBlock,
                           statement->elseStmt ? elseBlock : mergeBlock, statement->line);
            sealBlock(thenBlock);
            if (statement->elseStmt) sealBlock(elseBlock);

            currentBlock_ = thenBlock;
            emitStatement(statement->thenStmt);
            if (!isTerminated(currentBlock_)) emitBranch(mergeBlock, statement->line);

            if (statement->elseStmt) {
                currentBlock_ = elseBlock;
                emitStatement(statement->elseStmt);
                if (!isTerminated(currentBlock_)) emitBranch(mergeBlock, statement->line);
            }

            sealBlock(mergeBlock);
            currentBlock_ = mergeBlock;
            if (function_.blocks[mergeBlock].predecessors.empty()) {
                emitVoid(Opcode::Unreachable, {}, statement->line);
            }
        }

        void emitWhile(const StmtPtr &statement) {
            BlockId header = createBlock("while.header");
            BlockId body = createBlock("while.body");
            BlockId exit = createBlock("while.exit");
            emitBranch(header, statement->line);

            currentBlock_ = header;
            ValueId condition = toBoolean(emitExpression(statement->cond), statement->line);
            emitCondBranch(condition, body, exit, statement->line);
            sealBlock(body);

            breakTargets_.push_back(exit);
            continueTargets_.push_back(header);
            currentBlock_ = body;
            emitStatement(statement->body);
            if (!isTerminated(currentBlock_)) emitBranch(header, statement->line);
            continueTargets_.pop_back();
            breakTargets_.pop_back();

            sealBlock(header);
            sealBlock(exit);
            currentBlock_ = exit;
        }

        void emitReturn(const StmtPtr &statement) {
            vector<ValueId> operands;
            if (statement->expr) {
                ValueId value = emitExpression(statement->expr);
                value = convert(value, function_.returnType, statement->line);
                operands.push_back(value);
            }
            emitVoid(Opcode::Return, std::move(operands), statement->line);
        }

        void finishFunction() {
            if (!isTerminated(currentBlock_)) {
                vector<ValueId> operands;
                if (!function_.returnType.isVoid()) operands.push_back(zero(function_.returnType));
                emitVoid(Opcode::Return, std::move(operands), ast_.line);
            }
            for (BasicBlock &block : function_.blocks) {
                if (!block.sealed) sealBlock(block.id);
                if (block.terminator == InvalidInstruction) {
                    BlockId saved = currentBlock_;
                    currentBlock_ = block.id;
                    emitVoid(Opcode::Unreachable, {}, ast_.line);
                    currentBlock_ = saved;
                }
            }
        }
    };
};

} // namespace sysyc::ir


struct Symbol {
    string name;
    Type type;
    bool isConst = false;
    bool isGlobal = false;
    bool isParamArray = false;
    int offset = 0;
    int reg = -1;
    string label;
    vector<ConstValue> constElems;
};

struct FuncInfo {
    BaseType ret = BaseType::Int;
    vector<Type> params;
    bool builtin = false;
};

struct GlobalData {
    string label;
    Type type;
    vector<ConstValue> init;
};

struct ExprValue {
    Type type;
    bool knownBool = false;
};

class CodeGen {
public:
    explicit CodeGen(Program p) : prog_(std::move(p)) {
        installBuiltins();
    }

    string generate() {
        collectGlobalsAndFunctions();
        ostringstream os;
        os << "    .option nopic\n";
        emitGlobals(os);
        os << "    .text\n";
        for (auto &fn : functions_) emitFunction(os, *fn);
        return os.str();
    }

private:
    Program prog_;
    unordered_map<string, Symbol> globals_;
    unordered_map<string, FuncInfo> funcs_;
    vector<GlobalData> globalsOut_;
    vector<shared_ptr<FuncDef>> functions_;
    vector<unordered_map<string, Symbol>> scopes_;
    vector<string> code_;
    int localSize_ = 16;
    int labelId_ = 0;
    string curFunc_;
    BaseType curRet_ = BaseType::Int;
    string retLabel_;
    string selfTailEntryLabel_;
    vector<Symbol> selfTailParams_;
    bool selfTailUsed_ = false;
    vector<string> breakLabels_;
    vector<string> contLabels_;
    int nextLocalReg_ = 1;
    vector<int> usedLocalRegs_;
    int tempRegDepth_ = 0;
    int floatTempRegDepth_ = 0;
    unordered_map<string, int> globalBaseRegs_;
    map<int, int> constIntRegs_;
    unordered_map<const Param*, int> plannedParamRegs_;
    unordered_map<const VarDef*, int> plannedLocalRegs_;
    struct ActiveLoopPtr {
        string key;
        ExprPtr lval;
        string reg;
        int byteStep = 0;
        int count = 0;
        int readCount = 0;
        int writeCount = 0;
    };
    struct ActiveLoopBase {
        string key;
        ExprPtr lval;
        string reg;
        string ivReg;
        int ivByteCoeff = 0;
        int count = 0;
    };
    struct ActiveLoadCSE {
        string key;
        ExprPtr lval;
        string reg;
        Type type;
        bool loaded = false;
        bool loading = false;
    };
    struct KnownIntValue {
        bool known = false;
        int value = 0;
    };
    vector<vector<ActiveLoopPtr>> activeLoopPtrs_;
    vector<vector<ActiveLoopBase>> activeLoopBases_;
    vector<ActiveLoadCSE> activeLoadCSE_;
    vector<unordered_map<string, KnownIntValue>> knownIntScopes_;

    static int elemSize(BaseType) { return 4; }

    static int typeSize(const Type &t) {
        if (t.dims.empty()) return elemSize(t.base);
        long long n = 1;
        for (int d : t.dims) {
            if (d > 0) n *= d;
        }
        return static_cast<int>(n * elemSize(t.base));
    }

    static int productTail(const vector<int> &dims, size_t start) {
        long long p = 1;
        for (size_t i = start; i < dims.size(); ++i) {
            if (dims[i] > 0) p *= dims[i];
        }
        return static_cast<int>(p);
    }

    static bool isSimm12(long long x) { return x >= -2048 && x <= 2047; }

    static bool isPow2(long long x) { return x > 0 && (x & (x - 1)) == 0; }

    static int log2Int(long long x) {
        int s = 0;
        while (x > 1) {
            x >>= 1;
            ++s;
        }
        return s;
    }

    static vector<int> setBitPositions(int x) {
        vector<int> bits;
        int pos = 0;
        while (x > 0) {
            if (x & 1) bits.push_back(pos);
            x >>= 1;
            ++pos;
        }
        return bits;
    }

    struct DivMagic {
        int m = 0;
        int shift = 0;
    };

    static DivMagic signedDivMagicPositive(int d) {
        const uint64_t two31 = 1ull << 31;
        uint64_t ad = static_cast<uint64_t>(d);
        uint64_t anc = two31 - 1 - (two31 % ad);
        int p = 31;
        uint64_t q1 = two31 / anc, r1 = two31 - q1 * anc;
        uint64_t q2 = two31 / ad, r2 = two31 - q2 * ad;
        while (true) {
            ++p;
            q1 <<= 1;
            r1 <<= 1;
            if (r1 >= anc) {
                ++q1;
                r1 -= anc;
            }
            q2 <<= 1;
            r2 <<= 1;
            if (r2 >= ad) {
                ++q2;
                r2 -= ad;
            }
            uint64_t delta = ad - r2;
            if (!(q1 < delta || (q1 == delta && r1 == 0))) break;
        }
        long long raw = static_cast<long long>(q2 + 1);
        if (raw >= (1ll << 31)) raw -= (1ll << 32);
        DivMagic mg;
        mg.m = static_cast<int>(raw);
        mg.shift = p - 32;
        return mg;
    }

    static string trimAsm(string s) {
        size_t b = 0;
        while (b < s.size() && isspace(static_cast<unsigned char>(s[b]))) ++b;
        size_t e = s.size();
        while (e > b && isspace(static_cast<unsigned char>(s[e - 1]))) --e;
        return s.substr(b, e - b);
    }

    string newLabel(const string &hint) {
        return ".L_" + curFunc_ + "_" + hint + "_" + to_string(labelId_++);
    }

    void emit(const string &s) { code_.push_back("    " + s); }
    void emitLabel(const string &s) { code_.push_back(s + ":"); }

    void addSp(int n) {
        if (n == 0) return;
        if (isSimm12(n)) emit("addi sp, sp, " + to_string(n));
        else {
            emit("li t6, " + to_string(n));
            emit("add sp, sp, t6");
        }
    }

    void addrOffset(const string &dst, const string &base, int off) {
        if (isSimm12(off)) emit("addi " + dst + ", " + base + ", " + to_string(off));
        else {
            string tmp = (dst == base) ? "t6" : dst;
            emit("li " + tmp + ", " + to_string(off));
            emit("add " + dst + ", " + base + ", " + tmp);
        }
    }

    string localRegName(int reg) { return "s" + to_string(reg); }

    int allocLocalReg(const Type &t) {
        if (!t.dims.empty() || t.base != BaseType::Int) return -1;
        while (nextLocalReg_ <= 11 &&
               find(usedLocalRegs_.begin(), usedLocalRegs_.end(), nextLocalReg_) != usedLocalRegs_.end()) {
            ++nextLocalReg_;
        }
        if (nextLocalReg_ > 11) return -1;
        int r = nextLocalReg_++;
        usedLocalRegs_.push_back(r);
        return r;
    }

    int reserveSpecificLocalReg(int reg) {
        if (reg < 1 || reg > 11) return -1;
        if (find(usedLocalRegs_.begin(), usedLocalRegs_.end(), reg) != usedLocalRegs_.end()) return -1;
        usedLocalRegs_.push_back(reg);
        return reg;
    }

    void scaleIndexReg(const string &reg, int stride) {
        if (stride == 1) return;
        if (isPow2(stride)) emit("slli " + reg + ", " + reg + ", " + to_string(log2Int(stride)));
        else if (auto bits = setBitPositions(stride); bits.size() == 2) {
            int lo = bits[0], hi = bits[1];
            emit("mv t2, " + reg);
            emit("slli " + reg + ", " + reg + ", " + to_string(hi));
            if (lo == 0) emit("add " + reg + ", " + reg + ", t2");
            else {
                emit("slli t2, t2, " + to_string(lo));
                emit("add " + reg + ", " + reg + ", t2");
            }
        }
        else {
            emit("li t2, " + to_string(stride));
            emit("mul " + reg + ", " + reg + ", t2");
        }
    }

    void addScaledIndexToBase(const string &baseReg, int stride) {
        scaleIndexReg("t0", stride);
        emit("add t0, " + baseReg + ", t0");
    }

    bool exprHasCall(const ExprPtr &e) {
        if (!e) return false;
        if (e->kind == ExprKind::Call) return true;
        if (e->kind == ExprKind::Unary) return exprHasCall(e->lhs);
        if (e->kind == ExprKind::Binary) return exprHasCall(e->lhs) || exprHasCall(e->rhs);
        if (e->kind == ExprKind::LVal) {
            if (!e->indices.empty()) return true;
            for (auto &idx : e->indices) {
                if (exprHasCall(idx)) return true;
            }
        }
        return false;
    }

    bool exprMayCall(const ExprPtr &e) {
        if (!e) return false;
        if (e->kind == ExprKind::Call) return true;
        if (e->kind == ExprKind::Unary) return exprMayCall(e->lhs);
        if (e->kind == ExprKind::Binary) return exprMayCall(e->lhs) || exprMayCall(e->rhs);
        if (e->kind == ExprKind::LVal) {
            for (auto &idx : e->indices) {
                if (exprMayCall(idx)) return true;
            }
        }
        return false;
    }

    string acquireTempReg() {
        static const char *regs[] = {"t4", "t5"};
        if (tempRegDepth_ >= 2) return "";
        return regs[tempRegDepth_++];
    }

    void releaseTempReg() {
        if (tempRegDepth_ > 0) --tempRegDepth_;
    }

    string acquireFloatTempReg() {
        static const char *regs[] = {"ft2", "ft3", "ft4", "ft5", "ft6", "ft7"};
        if (floatTempRegDepth_ >= 6) return "";
        return regs[floatTempRegDepth_++];
    }

    void releaseFloatTempReg() {
        if (floatTempRegDepth_ > 0) --floatTempRegDepth_;
    }

    void loadInt(const string &dst, const string &base, int off) {
        if (isSimm12(off)) emit("lw " + dst + ", " + to_string(off) + "(" + base + ")");
        else {
            emit("li t6, " + to_string(off));
            emit("add t6, " + base + ", t6");
            emit("lw " + dst + ", 0(t6)");
        }
    }

    void loadWord64(const string &dst, const string &base, int off) {
        if (isSimm12(off)) emit("ld " + dst + ", " + to_string(off) + "(" + base + ")");
        else {
            emit("li t6, " + to_string(off));
            emit("add t6, " + base + ", t6");
            emit("ld " + dst + ", 0(t6)");
        }
    }

    void storeInt(const string &src, const string &base, int off) {
        if (isSimm12(off)) emit("sw " + src + ", " + to_string(off) + "(" + base + ")");
        else {
            emit("li t6, " + to_string(off));
            emit("add t6, " + base + ", t6");
            emit("sw " + src + ", 0(t6)");
        }
    }

    void storeWord64(const string &src, const string &base, int off) {
        if (isSimm12(off)) emit("sd " + src + ", " + to_string(off) + "(" + base + ")");
        else {
            emit("li t6, " + to_string(off));
            emit("add t6, " + base + ", t6");
            emit("sd " + src + ", 0(t6)");
        }
    }

    void loadFloat(const string &dst, const string &base, int off) {
        if (isSimm12(off)) emit("flw " + dst + ", " + to_string(off) + "(" + base + ")");
        else {
            emit("li t6, " + to_string(off));
            emit("add t6, " + base + ", t6");
            emit("flw " + dst + ", 0(t6)");
        }
    }

    void storeFloat(const string &src, const string &base, int off) {
        if (isSimm12(off)) emit("fsw " + src + ", " + to_string(off) + "(" + base + ")");
        else {
            emit("li t6, " + to_string(off));
            emit("add t6, " + base + ", t6");
            emit("fsw " + src + ", 0(t6)");
        }
    }

    void pushInt(const string &reg = "t0") {
        addSp(-16);
        storeWord64(reg, "sp", 8);
    }

    void popInt(const string &reg = "t0") {
        loadWord64(reg, "sp", 8);
        addSp(16);
    }

    void pushFloat(const string &reg = "ft0") {
        addSp(-16);
        storeFloat(reg, "sp", 8);
    }

    void popFloat(const string &reg = "ft0") {
        loadFloat(reg, "sp", 8);
        addSp(16);
    }

    void pushValue(const ExprValue &v) {
        if (v.type.isScalar() && v.type.base == BaseType::Float) pushFloat();
        else pushInt();
    }

    int allocLocal(int size, int align) {
        localSize_ = alignTo(localSize_, align);
        localSize_ += size;
        return -localSize_;
    }

    Symbol *findVar(const string &name) {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto f = it->find(name);
            if (f != it->end()) return &f->second;
        }
        auto g = globals_.find(name);
        if (g != globals_.end()) return &g->second;
        return nullptr;
    }

    void addLocal(Symbol sym) {
        auto &scope = scopes_.back();
        if (scope.count(sym.name)) fail(1, "redefinition of " + sym.name);
        scope[sym.name] = std::move(sym);
    }

    bool getKnownInt(const string &name, int &out) const {
        for (auto it = knownIntScopes_.rbegin(); it != knownIntScopes_.rend(); ++it) {
            auto f = it->find(name);
            if (f != it->end()) {
                if (!f->second.known) return false;
                out = f->second.value;
                return true;
            }
        }
        return false;
    }

    void setKnownInt(const string &name, int value) {
        for (auto it = knownIntScopes_.rbegin(); it != knownIntScopes_.rend(); ++it) {
            auto f = it->find(name);
            if (f != it->end()) {
                f->second = {true, value};
                return;
            }
        }
        if (!knownIntScopes_.empty()) knownIntScopes_.back()[name] = {true, value};
    }

    void forgetKnownInt(const string &name) {
        for (auto it = knownIntScopes_.rbegin(); it != knownIntScopes_.rend(); ++it) {
            auto f = it->find(name);
            if (f != it->end()) {
                f->second = {false, 0};
                return;
            }
        }
    }

    void declareKnownIntSlot(const string &name) {
        if (!knownIntScopes_.empty()) knownIntScopes_.back()[name] = {false, 0};
    }

    void installBuiltins() {
        auto scalar = [](BaseType b) {
            Type t; t.base = b; return t;
        };
        auto arr = [](BaseType b) {
            Type t; t.base = b; t.dims.push_back(-1); return t;
        };
        auto add = [&](const string &name, BaseType ret, vector<Type> ps) {
            FuncInfo f; f.ret = ret; f.params = std::move(ps); f.builtin = true; funcs_[name] = f;
        };
        add("getint", BaseType::Int, {});
        add("getch", BaseType::Int, {});
        add("getfloat", BaseType::Float, {});
        add("getarray", BaseType::Int, {arr(BaseType::Int)});
        add("getfarray", BaseType::Int, {arr(BaseType::Float)});
        add("putint", BaseType::Void, {scalar(BaseType::Int)});
        add("putch", BaseType::Void, {scalar(BaseType::Int)});
        add("putfloat", BaseType::Void, {scalar(BaseType::Float)});
        add("putarray", BaseType::Void, {scalar(BaseType::Int), arr(BaseType::Int)});
        add("putfarray", BaseType::Void, {scalar(BaseType::Int), arr(BaseType::Float)});
        add("_sysy_starttime", BaseType::Void, {scalar(BaseType::Int)});
        add("_sysy_stoptime", BaseType::Void, {scalar(BaseType::Int)});
    }

    string globalLabel(const string &name) { return ".Lglob_" + name; }

    Type paramType(const Param &p) {
        Type t;
        t.base = p.base;
        if (!p.dims.empty()) {
            t.dims.push_back(-1);
            for (size_t i = 1; i < p.dims.size(); ++i) t.dims.push_back(evalConstInt(p.dims[i]));
        }
        return t;
    }

    void collectGlobalsAndFunctions() {
        scopes_.clear();
        scopes_.push_back({});
        for (auto &item : prog_.items) {
            if (item.isFunc) {
                if (globals_.count(item.func->name)) fail(item.func->line, "top-level redefinition of " + item.func->name);
                auto old = funcs_.find(item.func->name);
                if (old != funcs_.end() && !old->second.builtin) fail(item.func->line, "function redefinition of " + item.func->name);
                FuncInfo fi;
                fi.ret = item.func->ret;
                fi.builtin = false;
                for (auto &p : item.func->params) fi.params.push_back(paramType(p));
                funcs_[item.func->name] = fi;
                functions_.push_back(item.func);
            } else {
                handleGlobalDecl(*item.decl);
            }
        }
        scopes_.clear();
    }

    void handleGlobalDecl(const Decl &d) {
        for (auto &vd : d.defs) {
            Type t;
            t.base = d.base;
            for (auto &dim : vd.dims) t.dims.push_back(evalConstInt(dim));
            int count = max(1, productTail(t.dims, 0));
            vector<ConstValue> init(count, zeroValue(t.base));
            if (vd.init) {
                if (t.dims.empty()) init[0] = convertConst(evalScalarInitConst(vd.init), t.base);
                else flattenConstInit(vd.init, t, 0, 0, count, init);
                for (auto &v : init) v = convertConst(v, t.base);
            }
            Symbol s;
            s.name = vd.name;
            s.type = t;
            s.isConst = d.isConst;
            s.isGlobal = true;
            s.label = globalLabel(vd.name);
            if (d.isConst) s.constElems = init;
            if (globals_.count(vd.name) || funcs_.count(vd.name)) fail(vd.line, "top-level redefinition of " + vd.name);
            globals_[vd.name] = s;
            globalsOut_.push_back({s.label, t, init});
        }
    }

    ConstValue evalScalarInitConst(const InitPtr &init) {
        if (!init) return zeroValue(BaseType::Int);
        if (!init->isList) return evalConst(init->expr);
        if (init->items.empty()) return zeroValue(BaseType::Int);
        return evalScalarInitConst(init->items.front());
    }

    static int initChildDepth(const Type &t, int depth, int start, int pos) {
        int n = static_cast<int>(t.dims.size());
        int rel = pos - start;
        for (int childDepth = depth + 1; childDepth <= n; ++childDepth) {
            int block = productTail(t.dims, static_cast<size_t>(childDepth));
            if (block <= 0) block = 1;
            if (rel % block == 0) return childDepth;
        }
        return n;
    }

    void flattenConstInit(const InitPtr &init, const Type &t, int depth, int start, int limit, vector<ConstValue> &out) {
        if (!init) return;
        if (!init->isList) {
            if (start < limit) out[start] = convertConst(evalConst(init->expr), t.base);
            return;
        }
        int pos = start;
        for (auto &child : init->items) {
            if (pos >= limit) fail(child->line, "too many initializer elements");
            if (child->isList && depth < static_cast<int>(t.dims.size())) {
                int childDepth = initChildDepth(t, depth, start, pos);
                int subSize = productTail(t.dims, static_cast<size_t>(childDepth));
                if (subSize <= 0) subSize = 1;
                int subStart = pos;
                if (subStart + subSize > limit) fail(child->line, "too many initializer elements");
                flattenConstInit(child, t, childDepth, subStart, subStart + subSize, out);
                pos = subStart + subSize;
            } else {
                flattenConstInit(child, t, static_cast<int>(t.dims.size()), pos, pos + 1, out);
                ++pos;
            }
        }
    }

    void flattenExprInit(const InitPtr &init, const Type &t, int depth, int start, int limit, vector<ExprPtr> &out) {
        if (!init) return;
        if (!init->isList) {
            if (start < limit) out[start] = init->expr;
            return;
        }
        int pos = start;
        for (auto &child : init->items) {
            if (pos >= limit) fail(child->line, "too many initializer elements");
            if (child->isList && depth < static_cast<int>(t.dims.size())) {
                int childDepth = initChildDepth(t, depth, start, pos);
                int subSize = productTail(t.dims, static_cast<size_t>(childDepth));
                if (subSize <= 0) subSize = 1;
                int subStart = pos;
                if (subStart + subSize > limit) fail(child->line, "too many initializer elements");
                flattenExprInit(child, t, childDepth, subStart, subStart + subSize, out);
                pos = subStart + subSize;
            } else {
                flattenExprInit(child, t, static_cast<int>(t.dims.size()), pos, pos + 1, out);
                ++pos;
            }
        }
    }

    ConstValue evalConst(const ExprPtr &e) {
        if (!e) fail(1, "constant expression expected");
        switch (e->kind) {
            case ExprKind::IntLit: {
                ConstValue v; v.base = BaseType::Int; v.i = static_cast<int>(e->intVal); return v;
            }
            case ExprKind::FloatLit: {
                ConstValue v; v.base = BaseType::Float; v.f = e->floatVal; return v;
            }
            case ExprKind::LVal: {
                Symbol *s = findVar(e->text);
                if (!s || !s->isConst || s->constElems.empty()) fail(e->line, "not a constant: " + e->text);
                int idx = 0;
                for (size_t i = 0; i < e->indices.size(); ++i) {
                    int iv = evalConstInt(e->indices[i]);
                    idx += iv * productTail(s->type.dims, i + 1);
                }
                if (idx < 0 || idx >= static_cast<int>(s->constElems.size())) fail(e->line, "constant index out of range");
                return s->constElems[idx];
            }
            case ExprKind::Unary: {
                ConstValue a = evalConst(e->lhs);
                if (e->text == "+") return a;
                if (e->text == "-") {
                    if (a.base == BaseType::Float) a.f = -a.f;
                    else a.i = -a.i;
                    return a;
                }
                if (e->text == "!") {
                    ConstValue r; r.base = BaseType::Int;
                    r.i = (a.base == BaseType::Float ? a.f == 0.0f : a.i == 0);
                    return r;
                }
                break;
            }
            case ExprKind::Binary: return evalConstBinary(e);
            default: break;
        }
        fail(e->line, "constant expression expected");
    }

    ConstValue evalConstBinary(const ExprPtr &e) {
        ConstValue a = evalConst(e->lhs), b = evalConst(e->rhs);
        const string &op = e->text;
        auto truth = [](ConstValue v) { return v.base == BaseType::Float ? v.f != 0.0f : v.i != 0; };
        if (op == "&&" || op == "||") {
            ConstValue r; r.base = BaseType::Int;
            r.i = (op == "&&") ? (truth(a) && truth(b)) : (truth(a) || truth(b));
            return r;
        }
        bool fp = a.base == BaseType::Float || b.base == BaseType::Float;
        if (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=") {
            float af = a.base == BaseType::Float ? a.f : static_cast<float>(a.i);
            float bf = b.base == BaseType::Float ? b.f : static_cast<float>(b.i);
            int ai = a.base == BaseType::Float ? static_cast<int>(a.f) : a.i;
            int bi = b.base == BaseType::Float ? static_cast<int>(b.f) : b.i;
            bool ok = false;
            if (fp) {
                if (op == "==") ok = af == bf; else if (op == "!=") ok = af != bf;
                else if (op == "<") ok = af < bf; else if (op == ">") ok = af > bf;
                else if (op == "<=") ok = af <= bf; else ok = af >= bf;
            } else {
                if (op == "==") ok = ai == bi; else if (op == "!=") ok = ai != bi;
                else if (op == "<") ok = ai < bi; else if (op == ">") ok = ai > bi;
                else if (op == "<=") ok = ai <= bi; else ok = ai >= bi;
            }
            ConstValue r; r.base = BaseType::Int; r.i = ok ? 1 : 0; return r;
        }
        if (fp) {
            float af = a.base == BaseType::Float ? a.f : static_cast<float>(a.i);
            float bf = b.base == BaseType::Float ? b.f : static_cast<float>(b.i);
            ConstValue r; r.base = BaseType::Float;
            if (op == "+") r.f = af + bf; else if (op == "-") r.f = af - bf;
            else if (op == "*") r.f = af * bf; else if (op == "/") r.f = af / bf;
            else fail(e->line, "invalid float operator " + op);
            return r;
        }
        ConstValue r; r.base = BaseType::Int;
        if (op == "+") r.i = a.i + b.i; else if (op == "-") r.i = a.i - b.i;
        else if (op == "*") r.i = a.i * b.i; else if (op == "/") r.i = b.i ? a.i / b.i : 0;
        else if (op == "%") r.i = b.i ? a.i % b.i : 0;
        else fail(e->line, "invalid operator " + op);
        return r;
    }

    int evalConstInt(const ExprPtr &e) {
        ConstValue v = evalConst(e);
        return v.base == BaseType::Float ? static_cast<int>(v.f) : v.i;
    }

    bool canEvalConstExprLimited(const ExprPtr &e, int &budget) {
        if (!e) return false;
        if (--budget <= 0) return false;
        switch (e->kind) {
            case ExprKind::IntLit:
            case ExprKind::FloatLit:
                return true;
            case ExprKind::StringLit:
            case ExprKind::Call:
                return false;
            case ExprKind::LVal: {
                Symbol *s = findVar(e->text);
                if (!s || !s->isConst || s->constElems.empty()) return false;
                if (e->indices.size() != s->type.dims.size()) return false;
                for (auto &idx : e->indices) {
                    if (!canEvalConstExprLimited(idx, budget)) return false;
                }
                return true;
            }
            case ExprKind::Unary:
                return canEvalConstExprLimited(e->lhs, budget);
            case ExprKind::Binary:
                return canEvalConstExprLimited(e->lhs, budget) && canEvalConstExprLimited(e->rhs, budget);
        }
        return false;
    }

    bool canEvalConstExpr(const ExprPtr &e) {
        int budget = 128;
        return canEvalConstExprLimited(e, budget);
    }

    bool tryEvalConstExpr(const ExprPtr &e, ConstValue &out) {
        if (!canEvalConstExpr(e)) return false;
        try {
            out = evalConst(e);
            return true;
        } catch (const CompileError &) {
            return false;
        }
    }

    ExprValue emitConstValue(ConstValue v) {
        Type t;
        t.base = v.base;
        if (v.base == BaseType::Float) {
            emit("li t0, " + to_string(floatBits(v.f)));
            emit("fmv.w.x ft0, t0");
        } else {
            loadImm("t0", v.i);
        }
        return {t, v.base == BaseType::Int && (v.i == 0 || v.i == 1)};
    }

    Type scalarType(BaseType b) {
        Type t;
        t.base = b;
        return t;
    }

    Type exprStaticType(const ExprPtr &e) {
        if (!e) return scalarType(BaseType::Int);
        switch (e->kind) {
            case ExprKind::IntLit: return scalarType(BaseType::Int);
            case ExprKind::FloatLit: return scalarType(BaseType::Float);
            case ExprKind::StringLit: return scalarType(BaseType::Void);
            case ExprKind::LVal: return lvalType(e);
            case ExprKind::Unary: {
                if (e->text == "!") return scalarType(BaseType::Int);
                return exprStaticType(e->lhs);
            }
            case ExprKind::Binary: {
                const string &op = e->text;
                if (op == "&&" || op == "||" || op == "==" || op == "!=" ||
                    op == "<" || op == ">" || op == "<=" || op == ">=") {
                    return scalarType(BaseType::Int);
                }
                Type a = exprStaticType(e->lhs), b = exprStaticType(e->rhs);
                if ((a.isScalar() && a.base == BaseType::Float) ||
                    (b.isScalar() && b.base == BaseType::Float)) {
                    return scalarType(BaseType::Float);
                }
                return scalarType(BaseType::Int);
            }
            case ExprKind::Call: {
                auto it = funcs_.find(e->text);
                if (it == funcs_.end()) fail(e->line, "undefined function " + e->text);
                return scalarType(it->second.ret);
            }
        }
        return scalarType(BaseType::Int);
    }

    bool tryExprStaticTypeLimited(const ExprPtr &e, Type &out, int &budget) {
        if (!e) return false;
        if (--budget <= 0) return false;
        switch (e->kind) {
            case ExprKind::IntLit:
                out = scalarType(BaseType::Int);
                return true;
            case ExprKind::FloatLit:
                out = scalarType(BaseType::Float);
                return true;
            case ExprKind::StringLit:
                out = scalarType(BaseType::Void);
                return true;
            case ExprKind::LVal:
                out = lvalType(e);
                return true;
            case ExprKind::Unary:
                if (e->text == "!") {
                    out = scalarType(BaseType::Int);
                    return true;
                }
                return tryExprStaticTypeLimited(e->lhs, out, budget);
            case ExprKind::Binary: {
                const string &op = e->text;
                if (op == "&&" || op == "||" || op == "==" || op == "!=" ||
                    op == "<" || op == ">" || op == "<=" || op == ">=") {
                    out = scalarType(BaseType::Int);
                    return true;
                }
                Type a, b;
                if (!tryExprStaticTypeLimited(e->lhs, a, budget)) return false;
                if (!tryExprStaticTypeLimited(e->rhs, b, budget)) return false;
                if ((a.isScalar() && a.base == BaseType::Float) ||
                    (b.isScalar() && b.base == BaseType::Float)) {
                    out = scalarType(BaseType::Float);
                } else {
                    out = scalarType(BaseType::Int);
                }
                return true;
            }
            case ExprKind::Call: {
                auto it = funcs_.find(e->text);
                if (it == funcs_.end()) fail(e->line, "undefined function " + e->text);
                out = scalarType(it->second.ret);
                return true;
            }
        }
        return false;
    }

    bool tryExprStaticType(const ExprPtr &e, Type &out) {
        int budget = 128;
        return tryExprStaticTypeLimited(e, out, budget);
    }

    bool cacheableLoadCSELVal(const ExprPtr &e, Type *out = nullptr) {
        if (!e || e->kind != ExprKind::LVal || exprMayCall(e)) return false;
        Type t = lvalType(e);
        if (!t.isScalar() || t.base != BaseType::Int) return false;
        Symbol *s = findVar(e->text);
        if (!s) return false;
        if (s->reg >= 0 && e->indices.empty()) return false;
        if (out) *out = t;
        return true;
    }

    int exprLoadCSECostLimited(const ExprPtr &e, int &budget) {
        if (!e || --budget <= 0) return 1;
        int cost = 1;
        if (e->kind == ExprKind::LVal) {
            cost += 4 + static_cast<int>(e->indices.size()) * 4;
            for (auto &idx : e->indices) cost += exprLoadCSECostLimited(idx, budget);
        } else if (e->kind == ExprKind::Unary) {
            cost += exprLoadCSECostLimited(e->lhs, budget);
        } else if (e->kind == ExprKind::Binary) {
            cost += exprLoadCSECostLimited(e->lhs, budget);
            cost += exprLoadCSECostLimited(e->rhs, budget);
        } else if (e->kind == ExprKind::Call) {
            for (auto &arg : e->args) cost += exprLoadCSECostLimited(arg, budget);
        }
        return cost;
    }

    int lvalLoadCSECost(const ExprPtr &e) {
        int budget = 96;
        int cost = 8 + static_cast<int>(e->indices.size()) * 8;
        for (auto &idx : e->indices) cost += exprLoadCSECostLimited(idx, budget);
        return cost;
    }

    struct LoadCSECandidate {
        ExprPtr lval;
        Type type;
        int count = 0;
        int cost = 0;
    };

    void collectLoadCSECandidatesExpr(const ExprPtr &e, unordered_map<string, LoadCSECandidate> &cands) {
        if (!e) return;
        if (e->kind == ExprKind::LVal) {
            Type t;
            if (cacheableLoadCSELVal(e, &t)) {
                string key = lvalKey(e);
                auto &c = cands[key];
                if (c.count == 0) {
                    c.lval = e;
                    c.type = t;
                    c.cost = lvalLoadCSECost(e);
                }
                ++c.count;
            }
            for (auto &idx : e->indices) collectLoadCSECandidatesExpr(idx, cands);
            return;
        }
        if (e->kind == ExprKind::Unary) {
            collectLoadCSECandidatesExpr(e->lhs, cands);
            return;
        }
        if (e->kind == ExprKind::Binary) {
            collectLoadCSECandidatesExpr(e->lhs, cands);
            collectLoadCSECandidatesExpr(e->rhs, cands);
            return;
        }
        if (e->kind == ExprKind::Call) {
            for (auto &arg : e->args) collectLoadCSECandidatesExpr(arg, cands);
        }
    }

    bool planExprLoadCSE(const ExprPtr &e, ActiveLoadCSE &out) {
        if (!e || exprMayCall(e)) return false;
        unordered_map<string, LoadCSECandidate> cands;
        collectLoadCSECandidatesExpr(e, cands);
        string bestKey;
        long long bestScore = 0;
        int bestCount = 0;
        int bestCost = 0;
        for (auto &kv : cands) {
            const LoadCSECandidate &c = kv.second;
            if (c.count < 2) continue;
            long long score = static_cast<long long>(c.count - 1) * c.cost;
            if (score > bestScore ||
                (score == bestScore && c.count > bestCount) ||
                (score == bestScore && c.count == bestCount && c.cost > bestCost) ||
                (score == bestScore && c.count == bestCount && c.cost == bestCost && kv.first < bestKey)) {
                bestKey = kv.first;
                bestScore = score;
                bestCount = c.count;
                bestCost = c.cost;
                out.key = kv.first;
                out.lval = c.lval;
                out.type = c.type;
            }
        }
        return bestScore > 0;
    }

    void loadImm(const string &reg, int v) {
        auto cached = constIntRegs_.find(v);
        if (cached != constIntRegs_.end()) {
            emit("mv " + reg + ", " + localRegName(cached->second));
            return;
        }
        emit("li " + reg + ", " + to_string(v));
    }

    string intConstOperandReg(int v, const string &tmp) {
        auto cached = constIntRegs_.find(v);
        if (cached != constIntRegs_.end()) return localRegName(cached->second);
        loadImm(tmp, v);
        return tmp;
    }

    void emitSignedDivPositiveConst(int d) {
        if (isPow2(d)) {
            int sh = log2Int(d);
            if (sh == 0) return;
            emit("sraiw t1, t0, 31");
            int bias = (1 << sh) - 1;
            if (isSimm12(bias)) emit("andi t1, t1, " + to_string(bias));
            else {
                loadImm("t2", bias);
                emit("and t1, t1, t2");
            }
            emit("addw t0, t0, t1");
            emit("sraiw t0, t0, " + to_string(sh));
            return;
        }
        DivMagic mg = signedDivMagicPositive(d);
        string mr = intConstOperandReg(mg.m, "t1");
        emit("mul t1, t0, " + mr);
        emit("srai t1, t1, 32");
        if (mg.m < 0) emit("addw t1, t1, t0");
        if (mg.shift > 0) emit("sraiw t1, t1, " + to_string(mg.shift));
        emit("sraiw t2, t0, 31");
        emit("subw t0, t1, t2");
    }

    bool tryEmitSignedDivConst(int c) {
        if (c == 0 || c == numeric_limits<int>::min()) return false;
        if (c == 1) return true;
        if (c == -1) {
            emit("subw t0, zero, t0");
            return true;
        }
        int d = c < 0 ? -c : c;
        emitSignedDivPositiveConst(d);
        if (c < 0) emit("subw t0, zero, t0");
        return true;
    }

    void emitIntRightConstOp(const string &op, int c) {
        if (op == "+") {
            if (c == 0) return;
            if (isSimm12(c)) emit("addiw t0, t0, " + to_string(c));
            else { string r = intConstOperandReg(c, "t1"); emit("addw t0, t0, " + r); }
        } else if (op == "-") {
            long long neg = -static_cast<long long>(c);
            if (neg == 0) return;
            if (isSimm12(neg)) emit("addiw t0, t0, " + to_string(neg));
            else { string r = intConstOperandReg(c, "t1"); emit("subw t0, t0, " + r); }
        } else if (op == "*") {
            if (c == 0) emit("li t0, 0");
            else if (c == 1) return;
            else if (c == -1) emit("subw t0, zero, t0");
            else {
                long long mag = c < 0 ? -static_cast<long long>(c) : static_cast<long long>(c);
                if (isPow2(mag) && log2Int(mag) < 32) {
                    emit("slliw t0, t0, " + to_string(log2Int(mag)));
                    if (c < 0) emit("subw t0, zero, t0");
                } else {
                    string r = intConstOperandReg(c, "t1");
                    emit("mulw t0, t0, " + r);
                }
            }
        } else if (op == "/") {
            if (c == 1) return;
            if (c == -1) emit("subw t0, zero, t0");
            else if (!tryEmitSignedDivConst(c)) { string r = intConstOperandReg(c, "t1"); emit("divw t0, t0, " + r); }
        } else if (op == "%") {
            if (c == 1 || c == -1) emit("li t0, 0");
            else if (c != 0 && c != numeric_limits<int>::min()) {
                emit("mv t6, t0");
                tryEmitSignedDivConst(c);
                string r = intConstOperandReg(c, "t1");
                emit("mulw t0, t0, " + r);
                emit("subw t0, t6, t0");
            }
            else { string r = intConstOperandReg(c, "t1"); emit("remw t0, t0, " + r); }
        } else if (op == "<") {
            if (isSimm12(c)) emit("slti t0, t0, " + to_string(c));
            else { string r = intConstOperandReg(c, "t1"); emit("slt t0, t0, " + r); }
        } else if (op == ">") {
            string r = intConstOperandReg(c, "t1");
            emit("slt t0, " + r + ", t0");
        } else if (op == "<=") {
            string r = intConstOperandReg(c, "t1");
            emit("slt t0, " + r + ", t0");
            emit("seqz t0, t0");
        } else if (op == ">=") {
            if (isSimm12(c)) emit("slti t0, t0, " + to_string(c));
            else { string r = intConstOperandReg(c, "t1"); emit("slt t0, t0, " + r); }
            emit("seqz t0, t0");
        } else if (op == "==") {
            if (c == 0) emit("seqz t0, t0");
            else { string r = intConstOperandReg(c, "t1"); emit("sub t0, t0, " + r); emit("seqz t0, t0"); }
        } else if (op == "!=") {
            if (c == 0) emit("snez t0, t0");
            else { string r = intConstOperandReg(c, "t1"); emit("sub t0, t0, " + r); emit("snez t0, t0"); }
        }
    }

    void emitIntLeftConstOp(const string &op, int c) {
        if (op == "+") {
            emitIntRightConstOp("+", c);
        } else if (op == "*") {
            emitIntRightConstOp("*", c);
        } else if (op == "-") {
            string r = intConstOperandReg(c, "t1");
            emit("subw t0, " + r + ", t0");
        } else if (op == "/") {
            string r = intConstOperandReg(c, "t1");
            emit("divw t0, " + r + ", t0");
        } else if (op == "%") {
            string r = intConstOperandReg(c, "t1");
            emit("remw t0, " + r + ", t0");
        } else if (op == "<") {
            string r = intConstOperandReg(c, "t1");
            emit("slt t0, " + r + ", t0");
        } else if (op == ">") {
            emitIntRightConstOp("<", c);
        } else if (op == "<=") {
            emitIntRightConstOp(">=", c);
        } else if (op == ">=") {
            emitIntRightConstOp("<=", c);
        } else if (op == "==") {
            emitIntRightConstOp("==", c);
        } else if (op == "!=") {
            emitIntRightConstOp("!=", c);
        }
    }

    void emitIntRegOp(const string &op, const string &lhsReg) {
        if (op == "<") emit("slt t0, " + lhsReg + ", t0");
        else if (op == ">") emit("slt t0, t0, " + lhsReg);
        else if (op == "<=") { emit("slt t0, t0, " + lhsReg); emit("seqz t0, t0"); }
        else if (op == ">=") { emit("slt t0, " + lhsReg + ", t0"); emit("seqz t0, t0"); }
        else if (op == "==") { emit("sub t0, " + lhsReg + ", t0"); emit("seqz t0, t0"); }
        else if (op == "!=") { emit("sub t0, " + lhsReg + ", t0"); emit("snez t0, t0"); }
        else if (op == "+") emit("addw t0, " + lhsReg + ", t0");
        else if (op == "-") emit("subw t0, " + lhsReg + ", t0");
        else if (op == "*") emit("mulw t0, " + lhsReg + ", t0");
        else if (op == "/") emit("divw t0, " + lhsReg + ", t0");
        else if (op == "%") emit("remw t0, " + lhsReg + ", t0");
    }

    bool collectAddSubChain(const ExprPtr &e, vector<pair<int, ExprPtr>> &terms) {
        if (!e) return false;
        vector<pair<int, ExprPtr>> st;
        st.push_back({1, e});
        while (!st.empty()) {
            auto cur = st.back();
            st.pop_back();
            ExprPtr n = cur.second;
            int sign = cur.first;
            if (n && n->kind == ExprKind::Binary && (n->text == "+" || n->text == "-")) {
                if (n->text == "+") {
                    st.push_back({sign, n->rhs});
                    st.push_back({sign, n->lhs});
                } else {
                    st.push_back({-sign, n->rhs});
                    st.push_back({sign, n->lhs});
                }
            } else {
                terms.push_back({sign, n});
            }
        }
        return terms.size() >= 3;
    }

    bool emitFlatAddSubExpr(const ExprPtr &e, ExprValue &out) {
        vector<pair<int, ExprPtr>> terms;
        if (!collectAddSubChain(e, terms)) return false;
        Type tt;
        for (auto &term : terms) {
            if (!tryExprStaticType(term.second, tt) || !tt.isScalar() || tt.base != BaseType::Int) {
                return false;
            }
            if (exprMayCall(term.second)) return false;
        }
        string tmp = acquireTempReg();
        if (tmp.empty()) return false;
        emit("li " + tmp + ", 0");
        for (auto &term : terms) {
            ConstValue cv;
            string reg;
            if (tryEvalConstExpr(term.second, cv) && cv.base == BaseType::Int) {
                long long val = term.first >= 0 ? cv.i : -static_cast<long long>(cv.i);
                if (val == 0) continue;
                if (isSimm12(val)) emit("addiw " + tmp + ", " + tmp + ", " + to_string(val));
                else {
                    string r = intConstOperandReg(static_cast<int>(cv.i), "t0");
                    if (term.first >= 0) emit("addw " + tmp + ", " + tmp + ", " + r);
                    else emit("subw " + tmp + ", " + tmp + ", " + r);
                }
                continue;
            }
            if (regScalarIntLVal(term.second, reg)) {
                if (term.first >= 0) emit("addw " + tmp + ", " + tmp + ", " + reg);
                else emit("subw " + tmp + ", " + tmp + ", " + reg);
                continue;
            }
            if (tryEmitActiveCachedMulTerm(term.second)) {
                if (term.first >= 0) emit("addw " + tmp + ", " + tmp + ", t0");
                else emit("subw " + tmp + ", " + tmp + ", t0");
                continue;
            }
            if (ensureActiveCachedIntLVal(term.second, reg)) {
                if (term.first >= 0) emit("addw " + tmp + ", " + tmp + ", " + reg);
                else emit("subw " + tmp + ", " + tmp + ", " + reg);
                continue;
            }
            ExprValue tv = emitExpr(term.second);
            convertReg(tv.type.base, BaseType::Int);
            if (term.first >= 0) emit("addw " + tmp + ", " + tmp + ", t0");
            else emit("subw " + tmp + ", " + tmp + ", t0");
        }
        emit("mv t0, " + tmp);
        releaseTempReg();
        out.type = scalarType(BaseType::Int);
        return true;
    }

    void emitGlobals(ostringstream &os) {
        enum class Section { None, Data, Bss };
        Section cur = Section::None;
        for (auto &g : globalsOut_) {
            bool allZero = true;
            for (auto &v : g.init) {
                if ((v.base == BaseType::Int && v.i != 0) || (v.base == BaseType::Float && floatBits(v.f) != 0)) {
                    allZero = false;
                    break;
                }
            }
            if (allZero) {
                if (cur != Section::Bss) {
                    os << "    .bss\n";
                    cur = Section::Bss;
                }
                os << "    .align 2\n" << g.label << ":\n";
                os << "    .zero " << max(4, typeSize(g.type)) << "\n";
            } else {
                if (cur != Section::Data) {
                    os << "    .data\n";
                    cur = Section::Data;
                }
                os << "    .align 2\n" << g.label << ":\n";
                for (auto v : g.init) {
                    v = convertConst(v, g.type.base);
                    if (g.type.base == BaseType::Float) {
                        os << "    .word 0x" << hex << setw(8) << setfill('0') << floatBits(v.f) << dec << setfill(' ') << "\n";
                    } else {
                        os << "    .word " << v.i << "\n";
                    }
                }
            }
        }
    }

    struct AbiLoc {
        bool inReg = true;
        bool isFloat = false;
        int idx = 0;
        int stackOff = 0;
    };

    vector<AbiLoc> abiLayout(const vector<Type> &types, int &stackBytes) {
        int ir = 0, fr = 0, so = 0;
        vector<AbiLoc> locs;
        for (auto &t : types) {
            AbiLoc l;
            bool fp = t.isScalar() && t.base == BaseType::Float;
            l.isFloat = fp;
            if (fp && fr < 8) {
                l.inReg = true; l.idx = fr++;
            } else if (!fp && ir < 8) {
                l.inReg = true; l.idx = ir++;
            } else {
                l.inReg = false; l.stackOff = so; so += 8;
            }
            locs.push_back(l);
        }
        stackBytes = alignTo(so, 16);
        return locs;
    }

    void countGlobalRefsExpr(const ExprPtr &e, unordered_map<string, int> &counts, int weight) {
        if (!e) return;
        if (e->kind == ExprKind::LVal) {
            Symbol *s = findVar(e->text);
            if (s && s->isGlobal && (!s->isConst || s->type.isArray())) counts[e->text] += weight;
            for (auto &idx : e->indices) countGlobalRefsExpr(idx, counts, weight);
            return;
        }
        if (e->kind == ExprKind::Unary) {
            countGlobalRefsExpr(e->lhs, counts, weight);
            return;
        }
        if (e->kind == ExprKind::Binary) {
            countGlobalRefsExpr(e->lhs, counts, weight);
            countGlobalRefsExpr(e->rhs, counts, weight);
            return;
        }
        if (e->kind == ExprKind::Call) {
            for (auto &arg : e->args) countGlobalRefsExpr(arg, counts, weight);
        }
    }

    void countGlobalRefsInit(const InitPtr &init, unordered_map<string, int> &counts, int weight) {
        if (!init) return;
        if (!init->isList) {
            countGlobalRefsExpr(init->expr, counts, weight);
            return;
        }
        for (auto &item : init->items) countGlobalRefsInit(item, counts, weight);
    }

    void countGlobalRefsDecl(const DeclPtr &decl, unordered_map<string, int> &counts, int weight) {
        if (!decl) return;
        for (auto &vd : decl->defs) {
            for (auto &dim : vd.dims) countGlobalRefsExpr(dim, counts, weight);
            countGlobalRefsInit(vd.init, counts, weight);
        }
    }

    void countGlobalRefsStmt(const StmtPtr &s, unordered_map<string, int> &counts, int weight = 1) {
        if (!s) return;
        switch (s->kind) {
            case StmtKind::Block:
                for (auto &it : s->items) {
                    if (it.isDecl) countGlobalRefsDecl(it.decl, counts, weight);
                    else countGlobalRefsStmt(it.stmt, counts, weight);
                }
                break;
            case StmtKind::Assign:
                countGlobalRefsExpr(s->lhs, counts, weight);
                countGlobalRefsExpr(s->expr, counts, weight);
                break;
            case StmtKind::Expr:
            case StmtKind::Return:
                countGlobalRefsExpr(s->expr, counts, weight);
                break;
            case StmtKind::If:
                countGlobalRefsExpr(s->cond, counts, weight);
                countGlobalRefsStmt(s->thenStmt, counts, weight);
                countGlobalRefsStmt(s->elseStmt, counts, weight);
                break;
            case StmtKind::While:
                countGlobalRefsExpr(s->cond, counts, weight);
                countGlobalRefsStmt(s->body, counts, min(weight * 8, 512));
                break;
            case StmtKind::Break:
            case StmtKind::Continue:
                break;
        }
    }

    void countDivMagicConstRef(const string &op, int c, unordered_map<int, int> &counts, int weight) {
        if (!(op == "/" || op == "%")) return;
        if (weight < 128) return;
        if (c == 0 || c == numeric_limits<int>::min() || c == 1 || c == -1) return;
        int d = c < 0 ? -c : c;
        if (isPow2(d)) return;
        DivMagic mg = signedDivMagicPositive(d);
        if (!isSimm12(mg.m) && mg.m != 0) counts[mg.m] += weight;
    }

    void countConstRefsExpr(const ExprPtr &e, unordered_map<int, int> &counts, int weight) {
        if (!e) return;
        ConstValue cv;
        if (tryEvalConstExpr(e, cv) && cv.base == BaseType::Int) {
            if (!isSimm12(cv.i) && cv.i != 0) counts[cv.i] += weight;
            return;
        }
        switch (e->kind) {
            case ExprKind::Unary:
                countConstRefsExpr(e->lhs, counts, weight);
                break;
            case ExprKind::Binary: {
                ConstValue lc, rc;
                bool lConst = tryEvalConstExpr(e->lhs, lc) && lc.base == BaseType::Int;
                bool rConst = tryEvalConstExpr(e->rhs, rc) && rc.base == BaseType::Int;
                if (lConst) {
                    if (!isSimm12(lc.i) && lc.i != 0) counts[lc.i] += weight;
                } else {
                    countConstRefsExpr(e->lhs, counts, weight);
                }
                if (rConst) {
                    if (!isSimm12(rc.i) && rc.i != 0) counts[rc.i] += weight;
                    countDivMagicConstRef(e->text, rc.i, counts, weight);
                } else {
                    countConstRefsExpr(e->rhs, counts, weight);
                }
                break;
            }
            case ExprKind::Call:
                for (auto &arg : e->args) countConstRefsExpr(arg, counts, weight);
                break;
            case ExprKind::LVal:
                for (auto &idx : e->indices) countConstRefsExpr(idx, counts, weight);
                break;
            case ExprKind::IntLit:
            case ExprKind::FloatLit:
            case ExprKind::StringLit:
                break;
        }
    }

    void countConstRefsInit(const InitPtr &init, unordered_map<int, int> &counts, int weight) {
        if (!init) return;
        if (!init->isList) {
            countConstRefsExpr(init->expr, counts, weight);
            return;
        }
        for (auto &item : init->items) countConstRefsInit(item, counts, weight);
    }

    void countConstRefsDecl(const DeclPtr &decl, unordered_map<int, int> &counts, int weight) {
        if (!decl) return;
        for (auto &vd : decl->defs) {
            for (auto &dim : vd.dims) countConstRefsExpr(dim, counts, weight);
            countConstRefsInit(vd.init, counts, weight);
        }
    }

    void countConstRefsStmt(const StmtPtr &s, unordered_map<int, int> &counts, int weight = 1) {
        if (!s) return;
        switch (s->kind) {
            case StmtKind::Block:
                for (auto &it : s->items) {
                    if (it.isDecl) countConstRefsDecl(it.decl, counts, weight);
                    else countConstRefsStmt(it.stmt, counts, weight);
                }
                break;
            case StmtKind::Assign:
                countConstRefsExpr(s->lhs, counts, weight);
                countConstRefsExpr(s->expr, counts, weight);
                break;
            case StmtKind::Expr:
            case StmtKind::Return:
                countConstRefsExpr(s->expr, counts, weight);
                break;
            case StmtKind::If:
                countConstRefsExpr(s->cond, counts, weight);
                countConstRefsStmt(s->thenStmt, counts, weight);
                countConstRefsStmt(s->elseStmt, counts, weight);
                break;
            case StmtKind::While:
                countConstRefsExpr(s->cond, counts, weight);
                countConstRefsStmt(s->body, counts, min(weight * 8, 512));
                break;
            case StmtKind::Break:
            case StmtKind::Continue:
                break;
        }
    }

    bool typeIsRegScalarInt(BaseType base, const vector<ExprPtr> &dims) {
        return base == BaseType::Int && dims.empty();
    }

    void collectLocalRegDefsDecl(const DeclPtr &decl, vector<pair<int, const VarDef*>> &defs) {
        if (!decl) return;
        for (auto &vd : decl->defs) {
            if (!decl->isConst && typeIsRegScalarInt(decl->base, vd.dims)) {
                defs.push_back({0, &vd});
            }
        }
    }

    void collectLocalRegDefsStmt(const StmtPtr &s, vector<pair<int, const VarDef*>> &defs) {
        if (!s) return;
        switch (s->kind) {
            case StmtKind::Block:
                for (auto &it : s->items) {
                    if (it.isDecl) collectLocalRegDefsDecl(it.decl, defs);
                    else collectLocalRegDefsStmt(it.stmt, defs);
                }
                break;
            case StmtKind::If:
                collectLocalRegDefsStmt(s->thenStmt, defs);
                collectLocalRegDefsStmt(s->elseStmt, defs);
                break;
            case StmtKind::While:
                collectLocalRegDefsStmt(s->body, defs);
                break;
            case StmtKind::Assign:
            case StmtKind::Expr:
            case StmtKind::Return:
            case StmtKind::Break:
            case StmtKind::Continue:
                break;
        }
    }

    void countLocalRegRefsExpr(const ExprPtr &e,
                               vector<unordered_map<string, const void*>> &scopeIds,
                               unordered_map<const void*, int> &counts,
                               int weight) {
        if (!e) return;
        switch (e->kind) {
            case ExprKind::LVal: {
                for (auto it = scopeIds.rbegin(); it != scopeIds.rend(); ++it) {
                    auto found = it->find(e->text);
                    if (found != it->end()) {
                        if (e->indices.empty()) counts[found->second] += weight;
                        break;
                    }
                }
                for (auto &idx : e->indices) countLocalRegRefsExpr(idx, scopeIds, counts, weight);
                break;
            }
            case ExprKind::Unary:
                countLocalRegRefsExpr(e->lhs, scopeIds, counts, weight);
                break;
            case ExprKind::Binary:
                countLocalRegRefsExpr(e->lhs, scopeIds, counts, weight);
                countLocalRegRefsExpr(e->rhs, scopeIds, counts, weight);
                break;
            case ExprKind::Call:
                for (auto &arg : e->args) countLocalRegRefsExpr(arg, scopeIds, counts, weight);
                break;
            case ExprKind::IntLit:
            case ExprKind::FloatLit:
            case ExprKind::StringLit:
                break;
        }
    }

    void countLocalRegRefsInit(const InitPtr &init,
                               vector<unordered_map<string, const void*>> &scopeIds,
                               unordered_map<const void*, int> &counts,
                               int weight) {
        if (!init) return;
        if (!init->isList) {
            countLocalRegRefsExpr(init->expr, scopeIds, counts, weight);
            return;
        }
        for (auto &item : init->items) countLocalRegRefsInit(item, scopeIds, counts, weight);
    }

    void countLocalRegRefsDecl(const DeclPtr &decl,
                               vector<unordered_map<string, const void*>> &scopeIds,
                               unordered_map<const void*, int> &counts,
                               int weight) {
        if (!decl) return;
        for (auto &vd : decl->defs) {
            for (auto &dim : vd.dims) countLocalRegRefsExpr(dim, scopeIds, counts, weight);
            countLocalRegRefsInit(vd.init, scopeIds, counts, weight);
            if (!decl->isConst && typeIsRegScalarInt(decl->base, vd.dims)) {
                scopeIds.back()[vd.name] = &vd;
            }
        }
    }

    void countLocalRegRefsStmt(const StmtPtr &s,
                               vector<unordered_map<string, const void*>> &scopeIds,
                               unordered_map<const void*, int> &counts,
                               int weight = 1) {
        if (!s) return;
        switch (s->kind) {
            case StmtKind::Block:
                scopeIds.push_back({});
                for (auto &it : s->items) {
                    if (it.isDecl) countLocalRegRefsDecl(it.decl, scopeIds, counts, weight);
                    else countLocalRegRefsStmt(it.stmt, scopeIds, counts, weight);
                }
                scopeIds.pop_back();
                break;
            case StmtKind::Assign:
                countLocalRegRefsExpr(s->lhs, scopeIds, counts, weight);
                countLocalRegRefsExpr(s->expr, scopeIds, counts, weight);
                break;
            case StmtKind::Expr:
            case StmtKind::Return:
                countLocalRegRefsExpr(s->expr, scopeIds, counts, weight);
                break;
            case StmtKind::If:
                countLocalRegRefsExpr(s->cond, scopeIds, counts, weight);
                countLocalRegRefsStmt(s->thenStmt, scopeIds, counts, weight);
                countLocalRegRefsStmt(s->elseStmt, scopeIds, counts, weight);
                break;
            case StmtKind::While:
                countLocalRegRefsExpr(s->cond, scopeIds, counts, weight);
                countLocalRegRefsStmt(s->body, scopeIds, counts, min(weight * 8, 512));
                break;
            case StmtKind::Break:
            case StmtKind::Continue:
                break;
        }
    }

    void planLocalScalarRegs(const FuncDef &fn) {
        plannedParamRegs_.clear();
        plannedLocalRegs_.clear();
        vector<pair<int, const Param*>> paramDefs;
        vector<pair<int, const VarDef*>> localDefs;
        for (auto &p : fn.params) {
            if (typeIsRegScalarInt(p.base, p.dims)) paramDefs.push_back({0, &p});
        }
        collectLocalRegDefsStmt(fn.block, localDefs);

        unordered_map<const void*, int> counts;
        vector<unordered_map<string, const void*>> scopeIds;
        scopeIds.push_back({});
        for (auto &item : paramDefs) scopeIds.back()[item.second->name] = item.second;
        if (fn.block && fn.block->kind == StmtKind::Block) {
            for (auto &it : fn.block->items) {
                if (it.isDecl) countLocalRegRefsDecl(it.decl, scopeIds, counts, 1);
                else countLocalRegRefsStmt(it.stmt, scopeIds, counts, 1);
            }
        } else {
            countLocalRegRefsStmt(fn.block, scopeIds, counts, 1);
        }

        struct Candidate {
            int count = 0;
            int order = 0;
            bool isParam = false;
            const Param *param = nullptr;
            const VarDef *var = nullptr;
        };
        vector<Candidate> ranked;
        int order = 0;
        for (auto &item : paramDefs) {
            Candidate c;
            c.count = counts[item.second];
            c.order = order++;
            c.isParam = true;
            c.param = item.second;
            ranked.push_back(c);
        }
        for (auto &item : localDefs) {
            Candidate c;
            c.count = counts[item.second];
            c.order = order++;
            c.var = item.second;
            ranked.push_back(c);
        }
        sort(ranked.begin(), ranked.end(), [](const Candidate &a, const Candidate &b) {
            if (a.count != b.count) return a.count > b.count;
            return a.order < b.order;
        });
        int assigned = 0;
        for (auto &c : ranked) {
            if (c.count < 3) continue;
            if (assigned >= 6) break;
            int got = reserveSpecificLocalReg(1 + assigned);
            if (got < 0) continue;
            if (c.isParam) plannedParamRegs_[c.param] = got;
            else plannedLocalRegs_[c.var] = got;
            ++assigned;
        }
    }

    void planGlobalBaseRegs(const FuncDef &fn) {
        globalBaseRegs_.clear();
        unordered_map<string, int> counts;
        countGlobalRefsStmt(fn.block, counts);
        vector<pair<int, string>> ranked;
        for (auto &kv : counts) {
            if (kv.second >= 3) ranked.push_back({kv.second, kv.first});
        }
        sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) {
            if (a.first != b.first) return a.first > b.first;
            return a.second < b.second;
        });
        for (auto &item : ranked) {
            for (int r = 11; r >= 1; --r) {
                if (reserveSpecificLocalReg(r) >= 0) {
                    globalBaseRegs_[item.second] = r;
                    break;
                }
            }
            if (globalBaseRegs_.size() >= 3) break;
        }
    }

    void planConstIntRegs(const FuncDef &fn) {
        constIntRegs_.clear();
        unordered_map<int, int> counts;
        countConstRefsStmt(fn.block, counts);
        vector<pair<int, int>> ranked;
        for (auto &kv : counts) {
            if (kv.second >= 8) ranked.push_back({kv.second, kv.first});
        }
        sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) {
            if (a.first != b.first) return a.first > b.first;
            return a.second < b.second;
        });
        for (auto &item : ranked) {
            for (int r = 11; r >= 1; --r) {
                if (reserveSpecificLocalReg(r) >= 0) {
                    constIntRegs_[item.second] = r;
                    break;
                }
            }
            if (constIntRegs_.size() >= 2) break;
        }
    }

    static bool isAsmTokenChar(char c) {
        unsigned char uc = static_cast<unsigned char>(c);
        return isalnum(uc) || c == '_' || c == '.';
    }

    static bool asmHasRegisterToken(const string &text, const string &reg) {
        size_t pos = 0;
        while ((pos = text.find(reg, pos)) != string::npos) {
            bool left = pos == 0 || !isAsmTokenChar(text[pos - 1]);
            size_t end = pos + reg.size();
            bool right = end == text.size() || !isAsmTokenChar(text[end]);
            if (left && right) return true;
            pos = end;
        }
        return false;
    }

    static string replaceAsmRegisterToken(string text, const string &from, const string &to) {
        size_t pos = 0;
        while ((pos = text.find(from, pos)) != string::npos) {
            bool left = pos == 0 || !isAsmTokenChar(text[pos - 1]);
            size_t end = pos + from.size();
            bool right = end == text.size() || !isAsmTokenChar(text[end]);
            if (left && right) {
                text.replace(pos, from.size(), to);
                pos += to.size();
            } else {
                pos = end;
            }
        }
        return text;
    }

    static string asmOpcode(const string &line) {
        string cur = trimAsm(line);
        size_t end = cur.find_first_of(" \t");
        return end == string::npos ? cur : cur.substr(0, end);
    }

    static bool asmWritesFirstOperand(const string &op) {
        static const unordered_set<string> ops = {
            "li", "la", "mv", "neg", "negw", "not", "sext.w", "zext.w",
            "add", "addw", "addi", "addiw", "sub", "subw", "mul", "mulw",
            "mulh", "mulhu", "mulhsu", "div", "divu", "divw", "divuw",
            "rem", "remu", "remw", "remuw", "and", "andi", "or", "ori",
            "xor", "xori", "sll", "sllw", "slli", "slliw", "srl", "srlw",
            "srli", "srliw", "sra", "sraw", "srai", "sraiw", "slt", "sltu",
            "slti", "sltiu", "seqz", "snez", "sltz", "sgtz", "lw", "lwu",
            "ld", "lh", "lhu", "lb", "lbu", "flw", "fld", "fadd.s", "fsub.s",
            "fmul.s", "fdiv.s", "fneg.s", "fmv.s", "fmv.w.x", "fmv.x.w",
            "fcvt.s.w", "fcvt.w.s", "feq.s", "flt.s", "fle.s"
        };
        return ops.count(op) != 0;
    }

    static bool asmHasNoDestination(const string &op) {
        static const unordered_set<string> ops = {
            "sb", "sh", "sw", "sd", "fsw", "fsd", "nop"
        };
        return ops.count(op) != 0;
    }

    static bool asmIsBlockBoundary(const string &line) {
        string cur = trimAsm(line);
        if (cur.empty() || cur.back() == ':' || cur.front() == '.') return true;
        string op = asmOpcode(cur);
        return (!op.empty() && op.front() == 'b') || op == "j" || op == "jr" ||
               op == "jal" || op == "jalr" || op == "ret" || op == "call" ||
               op == "tail" || op == "ecall" || op == "ebreak";
    }

    static string asmOperandText(const string &line) {
        string cur = trimAsm(line);
        size_t start = cur.find_first_of(" \t");
        if (start == string::npos) return "";
        while (start < cur.size() && isspace(static_cast<unsigned char>(cur[start]))) ++start;
        return cur.substr(start);
    }

    static bool asmWritesRegister(const string &line, const string &reg) {
        string op = asmOpcode(line);
        if (!asmWritesFirstOperand(op)) return false;
        string operands = asmOperandText(line);
        size_t comma = operands.find(',');
        string dst = trimAsm(operands.substr(0, comma));
        return dst == reg;
    }

    static bool asmUsesRegister(const string &line, const string &reg) {
        string op = asmOpcode(line);
        string operands = asmOperandText(line);
        if (asmWritesFirstOperand(op)) {
            size_t comma = operands.find(',');
            if (comma == string::npos) return false;
            operands = operands.substr(comma + 1);
        } else if (!asmHasNoDestination(op)) {
            return false;
        }
        return asmHasRegisterToken(operands, reg);
    }

    static string replaceAsmRegisterUses(const string &line, const string &from,
                                         const string &to) {
        string cur = trimAsm(line);
        string op = asmOpcode(cur);
        size_t operandStart = cur.find_first_of(" \t");
        if (operandStart == string::npos) return line;
        size_t replaceStart = operandStart;
        if (asmWritesFirstOperand(op)) {
            size_t comma = cur.find(',', operandStart);
            if (comma == string::npos) return line;
            replaceStart = comma + 1;
        } else if (!asmHasNoDestination(op)) {
            return line;
        }
        string rewritten = cur.substr(0, replaceStart) +
                           replaceAsmRegisterToken(cur.substr(replaceStart), from, to);
        size_t indent = line.find_first_not_of(" \t");
        return (indent == string::npos ? string() : line.substr(0, indent)) + rewritten;
    }

    static bool parseAsmMove(const string &line, string &dst, string &src) {
        if (asmOpcode(line) != "mv") return false;
        string operands = asmOperandText(line);
        size_t comma = operands.find(',');
        if (comma == string::npos || operands.find(',', comma + 1) != string::npos) return false;
        dst = trimAsm(operands.substr(0, comma));
        src = trimAsm(operands.substr(comma + 1));
        return !dst.empty() && !src.empty();
    }

    void propagateStraightLineMoves() {
        vector<bool> remove(code_.size(), false);
        for (size_t i = 0; i < code_.size(); ++i) {
            string dst, src;
            if (!parseAsmMove(code_[i], dst, src) || dst == src) continue;

            vector<pair<size_t, string>> rewrites;
            bool canRemove = false;
            for (size_t j = i + 1; j < code_.size(); ++j) {
                string cur = trimAsm(code_[j]);
                if (asmIsBlockBoundary(cur)) break;
                string op = asmOpcode(cur);
                bool known = asmWritesFirstOperand(op) || asmHasNoDestination(op);
                if (!known && (asmHasRegisterToken(cur, dst) ||
                               asmHasRegisterToken(cur, src))) {
                    break;
                }

                bool dstUsed = asmUsesRegister(cur, dst);
                bool dstWritten = asmWritesRegister(cur, dst);
                bool srcWritten = asmWritesRegister(cur, src);
                if (srcWritten) break;
                if (dstUsed) {
                    rewrites.push_back({j, replaceAsmRegisterUses(code_[j], dst, src)});
                }
                if (dstWritten) {
                    canRemove = true;
                    break;
                }
            }
            if (!canRemove) continue;
            remove[i] = true;
            for (auto &rewrite : rewrites) code_[rewrite.first] = std::move(rewrite.second);
        }

        vector<string> out;
        out.reserve(code_.size());
        for (size_t i = 0; i < code_.size(); ++i) {
            if (!remove[i]) out.push_back(std::move(code_[i]));
        }
        code_.swap(out);
    }

    void optimizeCode() {
        vector<string> out;
        out.reserve(code_.size());
        for (size_t i = 0; i < code_.size(); ++i) {
            string cur = trimAsm(code_[i]);
            if (cur.rfind("mv ", 0) == 0) {
                string rest = cur.substr(3);
                size_t comma = rest.find(',');
                if (comma != string::npos) {
                    string a = trimAsm(rest.substr(0, comma));
                    string b = trimAsm(rest.substr(comma + 1));
                    if (a == b) continue;
                }
            }
            if (cur == "addi sp, sp, 0") continue;
            if (cur.rfind("j ", 0) == 0 && i + 1 < code_.size()) {
                string label = cur.substr(2);
                if (trimAsm(code_[i + 1]) == label + ":") continue;
            }
            if (cur == "li a0, 0" && i + 1 < code_.size() &&
                trimAsm(code_[i + 1]) == "j " + retLabel_) {
                bool prevReturns = !out.empty() && trimAsm(out.back()) == "j " + retLabel_;
                bool nextIsReturnLabel = i + 2 < code_.size() && trimAsm(code_[i + 2]) == retLabel_ + ":";
                if (prevReturns || nextIsReturnLabel) {
                    ++i;
                    continue;
                }
            }
            out.push_back(code_[i]);
        }
        code_.swap(out);
        propagateStraightLineMoves();

        out.clear();
        out.reserve(code_.size());
        for (auto &line : code_) {
            string cur = trimAsm(line);
            string dst, src;
            if (parseAsmMove(cur, dst, src) && dst == src) continue;
            out.push_back(std::move(line));
        }
        code_.swap(out);
    }

    void emitFunction(ostringstream &os, const FuncDef &fn) {
        code_.clear();
        scopes_.clear();
        scopes_.push_back({});
        localSize_ = 16;
        labelId_ = 0;
        curFunc_ = fn.name;
        curRet_ = fn.ret;
        retLabel_ = ".L_" + curFunc_ + "_return";
        selfTailEntryLabel_ = ".L_" + curFunc_ + "_tail_entry";
        selfTailParams_.clear();
        selfTailUsed_ = false;
        nextLocalReg_ = 1;
        usedLocalRegs_.clear();
        tempRegDepth_ = 0;
        floatTempRegDepth_ = 0;
        globalBaseRegs_.clear();
        constIntRegs_.clear();
        plannedParamRegs_.clear();
        plannedLocalRegs_.clear();
        knownIntScopes_.clear();
        knownIntScopes_.push_back({});
        breakLabels_.clear();
        contLabels_.clear();
        activeLoadCSE_.clear();
        planGlobalBaseRegs(fn);
        planConstIntRegs(fn);
        planLocalScalarRegs(fn);
        for (auto &kv : globalBaseRegs_) {
            Symbol *s = findVar(kv.first);
            if (s && s->isGlobal) emit("la " + localRegName(kv.second) + ", " + s->label);
        }
        for (auto &kv : constIntRegs_) {
            emit("li " + localRegName(kv.second) + ", " + to_string(kv.first));
        }
        emitParamSetup(fn);
        emitLabel(selfTailEntryLabel_);
        genStmt(fn.block, false);
        if (!selfTailUsed_) {
            auto entry = find(code_.begin(), code_.end(), selfTailEntryLabel_ + ":");
            if (entry != code_.end()) code_.erase(entry);
        }
        if (fn.ret == BaseType::Int) emit("li a0, 0");
        else if (fn.ret == BaseType::Float) {
            emit("li t0, 0");
            emit("fmv.w.x fa0, t0");
        }
        emit("j " + retLabel_);
        optimizeCode();
        bool hasCall = false;
        for (auto &l : code_) {
            if (trimAsm(l).rfind("call ", 0) == 0) {
                hasCall = true;
                break;
            }
        }
        bool bodyUsesS0 = false;
        for (auto &l : code_) {
            if (trimAsm(l).find("s0") != string::npos) {
                bodyUsesS0 = true;
                break;
            }
        }
        bool omitFramePointer = !bodyUsesS0;
        int saveBase = omitFramePointer ? (hasCall ? 8 : 0) : alignTo(localSize_, 8);
        int frame = alignTo(saveBase + static_cast<int>(usedLocalRegs_.size()) * 8, 16);
        os << "    .align 1\n";
        os << "    .globl " << fn.name << "\n";
        os << "    .type " << fn.name << ", @function\n";
        os << fn.name << ":\n";
        if (frame != 0) {
            if (isSimm12(-frame)) os << "    addi sp, sp, " << -frame << "\n";
            else os << "    li t6, " << -frame << "\n    add sp, sp, t6\n";
        }
        if (hasCall) {
            if (isSimm12(frame - 8)) os << "    sd ra, " << frame - 8 << "(sp)\n";
            else os << "    li t6, " << frame - 8 << "\n    add t6, sp, t6\n    sd ra, 0(t6)\n";
        }
        if (!omitFramePointer) {
            if (isSimm12(frame - 16)) os << "    sd s0, " << frame - 16 << "(sp)\n";
            else os << "    li t6, " << frame - 16 << "\n    add t6, sp, t6\n    sd s0, 0(t6)\n";
            if (isSimm12(frame)) os << "    addi s0, sp, " << frame << "\n";
            else os << "    li s0, " << frame << "\n    add s0, sp, s0\n";
        }
        for (size_t i = 0; i < usedLocalRegs_.size(); ++i) {
            int off = -(saveBase + static_cast<int>(i + 1) * 8);
            if (omitFramePointer) {
                int spOff = frame + off;
                if (isSimm12(spOff)) os << "    sd s" << usedLocalRegs_[i] << ", " << spOff << "(sp)\n";
                else os << "    li t6, " << spOff << "\n    add t6, sp, t6\n    sd s" << usedLocalRegs_[i] << ", 0(t6)\n";
            } else {
                if (isSimm12(off)) os << "    sd s" << usedLocalRegs_[i] << ", " << off << "(s0)\n";
                else os << "    li t6, " << off << "\n    add t6, s0, t6\n    sd s" << usedLocalRegs_[i] << ", 0(t6)\n";
            }
        }
        for (auto &l : code_) os << l << "\n";
        os << retLabel_ << ":\n";
        for (size_t i = 0; i < usedLocalRegs_.size(); ++i) {
            int off = -(saveBase + static_cast<int>(i + 1) * 8);
            if (omitFramePointer) {
                int spOff = frame + off;
                if (isSimm12(spOff)) os << "    ld s" << usedLocalRegs_[i] << ", " << spOff << "(sp)\n";
                else os << "    li t6, " << spOff << "\n    add t6, sp, t6\n    ld s" << usedLocalRegs_[i] << ", 0(t6)\n";
            } else {
                if (isSimm12(off)) os << "    ld s" << usedLocalRegs_[i] << ", " << off << "(s0)\n";
                else os << "    li t6, " << off << "\n    add t6, s0, t6\n    ld s" << usedLocalRegs_[i] << ", 0(t6)\n";
            }
        }
        if (omitFramePointer) {
            if (hasCall) os << "    ld ra, " << frame - 8 << "(sp)\n";
            if (frame != 0) {
                if (isSimm12(frame)) os << "    addi sp, sp, " << frame << "\n";
                else os << "    li t6, " << frame << "\n    add sp, sp, t6\n";
            }
        } else {
            if (hasCall) os << "    ld ra, -8(s0)\n";
            os << "    ld t6, -16(s0)\n";
            os << "    mv sp, s0\n";
            os << "    mv s0, t6\n";
        }
        os << "    ret\n";
        os << "    .size " << fn.name << ", .-" << fn.name << "\n";
        scopes_.clear();
        knownIntScopes_.clear();
    }

    void emitParamSetup(const FuncDef &fn) {
        FuncInfo &fi = funcs_.at(fn.name);
        int stackBytes = 0;
        vector<AbiLoc> locs = abiLayout(fi.params, stackBytes);
        for (size_t i = 0; i < fn.params.size(); ++i) {
            Symbol s;
            s.name = fn.params[i].name;
            s.type = fi.params[i];
            s.isParamArray = s.type.isArray();
            auto planned = plannedParamRegs_.find(&fn.params[i]);
            if (planned != plannedParamRegs_.end()) s.reg = planned->second;
            else if (!s.isParamArray) s.reg = allocLocalReg(s.type);
            int sz = s.isParamArray ? 8 : elemSize(s.type.base);
            int al = s.isParamArray ? 8 : 4;
            if (s.reg < 0) s.offset = allocLocal(sz, al);
            addLocal(s);
            declareKnownIntSlot(s.name);
            Symbol *sym = findVar(fn.params[i].name);
            if (!sym) fail(fn.params[i].line, "internal error: missing parameter symbol");
            selfTailParams_.push_back(*sym);
            AbiLoc l = locs[i];
            if (sym && sym->reg >= 0) {
                string r = localRegName(sym->reg);
                if (l.inReg) emit("mv " + r + ", a" + to_string(l.idx));
                else loadWord64(r, "s0", l.stackOff);
            } else if (s.isParamArray) {
                if (l.inReg) storeWord64("a" + to_string(l.idx), "s0", sym->offset);
                else {
                    loadWord64("t0", "s0", l.stackOff);
                    storeWord64("t0", "s0", sym->offset);
                }
            } else if (s.type.base == BaseType::Float) {
                if (l.inReg && l.isFloat) storeFloat("fa" + to_string(l.idx), "s0", sym->offset);
                else {
                    loadFloat("ft0", "s0", l.stackOff);
                    storeFloat("ft0", "s0", sym->offset);
                }
            } else {
                if (l.inReg) storeInt("a" + to_string(l.idx), "s0", sym->offset);
                else {
                    loadWord64("t0", "s0", l.stackOff);
                    storeInt("t0", "s0", sym->offset);
                }
            }
        }
    }

    bool tryEmitSelfTailReturn(const ExprPtr &expr) {
        if (!expr || expr->kind != ExprKind::Call || expr->text != curFunc_) return false;
        auto fit = funcs_.find(curFunc_);
        if (fit == funcs_.end() || fit->second.params.size() != expr->args.size() ||
            selfTailParams_.size() != expr->args.size()) {
            return false;
        }
        for (const Symbol &param : selfTailParams_) {
            if (!param.type.isArray() &&
                (!param.type.isScalar() || param.type.base != BaseType::Int)) {
                return false;
            }
        }

        auto writeParam = [&](size_t i, const string &src) {
            const Symbol &param = selfTailParams_[i];
            if (param.reg >= 0) {
                emit("mv " + localRegName(param.reg) + ", " + src);
            } else if (param.isParamArray) {
                storeWord64(src, "s0", param.offset);
            } else {
                storeInt(src, "s0", param.offset);
            }
        };

        auto readParam = [&](size_t i, const string &dst) {
            const Symbol &param = selfTailParams_[i];
            if (param.reg >= 0) {
                emit("mv " + dst + ", " + localRegName(param.reg));
            } else if (param.isParamArray) {
                loadWord64(dst, "s0", param.offset);
            } else {
                loadInt(dst, "s0", param.offset);
            }
        };
        auto sameParamStorage = [&](const Symbol &candidate, const Symbol &param) {
            if (candidate.isGlobal || candidate.name != param.name ||
                candidate.isParamArray != param.isParamArray ||
                candidate.type.base != param.type.base ||
                candidate.type.dims != param.type.dims || candidate.reg != param.reg) {
                return false;
            }
            return candidate.reg >= 0 || candidate.offset == param.offset;
        };
        auto directParamIndex = [&](const ExprPtr &arg) {
            if (!arg || arg->kind != ExprKind::LVal || !arg->indices.empty()) return -1;
            Symbol *source = findVar(arg->text);
            if (!source) return -1;
            for (size_t i = 0; i < selfTailParams_.size(); ++i) {
                if (sameParamStorage(*source, selfTailParams_[i])) return static_cast<int>(i);
            }
            return -1;
        };

        struct TailParamMove {
            size_t dst = 0;
            int src = -1;
            bool fromScratch = false;
        };
        vector<TailParamMove> directMoves;
        vector<size_t> complexTargets;
        int directCount = 0;
        for (size_t i = 0; i < expr->args.size(); ++i) {
            int source = directParamIndex(expr->args[i]);
            if (source < 0) {
                complexTargets.push_back(i);
                continue;
            }
            ++directCount;
            if (source != static_cast<int>(i)) directMoves.push_back({i, source, false});
        }

        // Large all-computed argument lists are not reliably cheaper than a normal call.
        if (complexTargets.size() > 2 && directCount == 0) return false;

        auto movesNeedScratch = [](vector<TailParamMove> pending) {
            while (!pending.empty()) {
                unordered_set<int> sources;
                for (const TailParamMove &move : pending) sources.insert(move.src);
                auto ready = find_if(pending.begin(), pending.end(), [&](const TailParamMove &move) {
                    return !sources.count(static_cast<int>(move.dst));
                });
                if (ready == pending.end()) return true;
                pending.erase(ready);
            }
            return false;
        };
        bool needCopyScratch = movesNeedScratch(directMoves);
        bool complexCallFree = true;
        for (size_t target : complexTargets) {
            if (exprMayCall(expr->args[target])) {
                complexCallFree = false;
                break;
            }
        }
        bool holdComplexInRegs = complexCallFree &&
            complexTargets.size() + static_cast<size_t>(needCopyScratch) <= 2;

        vector<string> complexRegs;
        string copyScratch;
        int reservedTempRegs = 0;
        if (holdComplexInRegs) {
            for (size_t i = 0; i < complexTargets.size(); ++i) {
                string reg = acquireTempReg();
                if (reg.empty()) {
                    while (reservedTempRegs-- > 0) releaseTempReg();
                    return false;
                }
                ++reservedTempRegs;
                complexRegs.push_back(reg);
            }
        }
        if (needCopyScratch) {
            copyScratch = acquireTempReg();
            if (copyScratch.empty()) {
                while (reservedTempRegs-- > 0) releaseTempReg();
                return false;
            }
            ++reservedTempRegs;
        }

        int tempBytes = holdComplexInRegs ? 0 :
            alignTo(static_cast<int>(complexTargets.size()) * 8, 16);
        addSp(-tempBytes);
        for (size_t slot = 0; slot < complexTargets.size(); ++slot) {
            size_t target = complexTargets[slot];
            ExprValue value = emitExpr(expr->args[target]);
            const Type &want = fit->second.params[target];
            if (want.isScalar() && value.type.isScalar()) {
                convertReg(value.type.base, want.base);
            }
            if (holdComplexInRegs) emit("mv " + complexRegs[slot] + ", t0");
            else storeWord64("t0", "sp", static_cast<int>(slot) * 8);
        }

        vector<TailParamMove> pending = directMoves;
        while (!pending.empty()) {
            unordered_set<int> sources;
            for (const TailParamMove &move : pending) {
                if (!move.fromScratch) sources.insert(move.src);
            }
            auto ready = find_if(pending.begin(), pending.end(), [&](const TailParamMove &move) {
                return !sources.count(static_cast<int>(move.dst));
            });
            if (ready != pending.end()) {
                if (ready->fromScratch) writeParam(ready->dst, copyScratch);
                else {
                    readParam(static_cast<size_t>(ready->src), "t0");
                    writeParam(ready->dst, "t0");
                }
                pending.erase(ready);
                continue;
            }

            int saved = static_cast<int>(pending.front().dst);
            readParam(static_cast<size_t>(saved), copyScratch);
            for (TailParamMove &move : pending) {
                if (!move.fromScratch && move.src == saved) move.fromScratch = true;
            }
        }

        for (size_t slot = 0; slot < complexTargets.size(); ++slot) {
            size_t target = complexTargets[slot];
            if (holdComplexInRegs) writeParam(target, complexRegs[slot]);
            else {
                loadWord64("t0", "sp", static_cast<int>(slot) * 8);
                writeParam(target, "t0");
            }
        }
        addSp(tempBytes);
        while (reservedTempRegs-- > 0) releaseTempReg();
        emit("j " + selfTailEntryLabel_);
        selfTailUsed_ = true;
        return true;
    }

    void genStmt(const StmtPtr &s, bool newScope = true) {
        if (!s) return;
        switch (s->kind) {
            case StmtKind::Block:
                if (newScope) {
                    scopes_.push_back({});
                    knownIntScopes_.push_back({});
                }
                for (auto &it : s->items) {
                    if (it.isDecl) genLocalDecl(*it.decl);
                    else genStmt(it.stmt);
                }
                if (newScope) {
                    knownIntScopes_.pop_back();
                    scopes_.pop_back();
                }
                break;
            case StmtKind::Assign: {
                Symbol *sym = findVar(s->lhs->text);
                if (sym && sym->isConst) fail(s->line, "cannot assign to const " + s->lhs->text);
                Type lt = lvalType(s->lhs);
                if (sym && sym->reg >= 0 && s->lhs->indices.empty()) {
                    int oldKnown = 0;
                    int stepVarDelta = 0;
                    bool hadKnown = getKnownInt(sym->name, oldKnown);
                    bool isStep = false;
                    string stepVar;
                    if (isSimpleInductionStep(s, stepVar, stepVarDelta) && stepVar == sym->name) {
                        isStep = true;
                    }
                    if (tryEmitRegSelfUpdate(sym, s->expr)) {
                        if (hadKnown && isStep) setKnownInt(sym->name, oldKnown + stepVarDelta);
                        else forgetKnownInt(sym->name);
                        break;
                    }
                    ExprValue rv = emitExpr(s->expr);
                    if (!lt.isScalar()) fail(s->line, "assignment target must be scalar");
                    convertReg(rv.type.base, lt.base);
                    emit("mv " + localRegName(sym->reg) + ", t0");
                    updateKnownAfterScalarAssign(s->lhs, s->expr, lt);
                    break;
                }
                emitLValAddress(s->lhs);
                if (tryEmitLValSelfUpdateAtAddress(lt, s->lhs, s->expr)) {
                    updateKnownAfterScalarAssign(s->lhs, s->expr, lt);
                    break;
                }
                if (tryStoreSimpleRhsAtAddress(lt, s->expr)) {
                    updateKnownAfterScalarAssign(s->lhs, s->expr, lt);
                    break;
                }
                string addrTmp;
                if (!exprMayCall(s->expr)) addrTmp = acquireTempReg();
                if (!addrTmp.empty()) {
                    emit("mv " + addrTmp + ", t0");
                } else {
                    pushInt();
                }
                ExprValue rv = emitExpr(s->expr);
                if (!lt.isScalar()) fail(s->line, "assignment target must be scalar");
                convertReg(rv.type.base, lt.base);
                if (!addrTmp.empty()) {
                    emit("mv t1, " + addrTmp);
                    releaseTempReg();
                } else {
                    popInt("t1");
                }
                if (lt.base == BaseType::Float) storeFloat("ft0", "t1", 0);
                else storeInt("t0", "t1", 0);
                updateKnownAfterScalarAssign(s->lhs, s->expr, lt);
                break;
            }
            case StmtKind::Expr:
                if (s->expr) emitExpr(s->expr);
                break;
            case StmtKind::If: {
                string endL = newLabel("endif");
                if (!s->elseStmt) {
                    if (!tryEmitBranchIfFalse(s->cond, endL)) {
                        ExprValue c = emitExpr(s->cond);
                        toBool(c);
                        emit("beqz t0, " + endL);
                    }
                    auto beforeIfKnown = knownIntScopes_;
                    genStmt(s->thenStmt);
                    emitLabel(endL);
                    knownIntScopes_ = beforeIfKnown;
                    clearKnownInts();
                    break;
                }
                string elseL = newLabel("else");
                if (!tryEmitBranchIfFalse(s->cond, elseL)) {
                    ExprValue c = emitExpr(s->cond);
                    toBool(c);
                    emit("beqz t0, " + elseL);
                }
                auto beforeIfKnown = knownIntScopes_;
                genStmt(s->thenStmt);
                emit("j " + endL);
                emitLabel(elseL);
                knownIntScopes_ = beforeIfKnown;
                if (s->elseStmt) genStmt(s->elseStmt);
                emitLabel(endL);
                knownIntScopes_ = beforeIfKnown;
                clearKnownInts();
                break;
            }
            case StmtKind::While: {
                if (tryEmitSmallConstWhileUnroll(s)) break;
                if (tryEmitPartialConstWhileUnroll(s)) break;
                if (tryEmitRuntimeCountedWhileUnroll(s)) break;
                string bodyL = newLabel("while_body"), condL = newLabel("while_cond");
                string endL = newLabel("endwhile");
                vector<ActiveLoopPtr> loopPtrs;
                planSimpleLoopPointers(s, loopPtrs);
                vector<ActiveLoopBase> loopBases;
                planCountedLoopBases(s, loopPtrs, loopBases);
                vector<ActiveLoadCSE> invariantLoads;
                planLoopInvariantLoads(s, invariantLoads);

                if (!invariantLoads.empty()) {
                    if (!tryEmitBranchIfFalse(s->cond, endL)) {
                        ExprValue condition = emitExpr(s->cond);
                        toBool(condition);
                        emit("beqz t0, " + endL);
                    }
                    for (auto &pointer : loopPtrs) {
                        emitLValAddressNoLoopPtr(pointer.lval);
                        emit("mv " + pointer.reg + ", t0");
                    }
                    for (auto &base : loopBases) emitLoopBaseInit(base);

                    size_t invariantDepth = activeLoadCSE_.size();
                    for (auto &load : invariantLoads) {
                        ExprValue value = emitExpr(load.lval);
                        convertReg(value.type.base, BaseType::Int);
                        emit("mv " + load.reg + ", t0");
                        load.loaded = true;
                        activeLoadCSE_.push_back(load);
                    }

                    activeLoopPtrs_.push_back(loopPtrs);
                    activeLoopBases_.push_back(loopBases);
                    contLabels_.push_back(condL);
                    breakLabels_.push_back(endL);
                    emitLabel(bodyL);
                    genStmt(s->body);
                    for (auto &pointer : loopPtrs) {
                        addrOffset(pointer.reg, pointer.reg, pointer.byteStep);
                    }
                    emitLabel(condL);
                    if (!tryEmitBranchIfTrue(s->cond, bodyL)) {
                        ExprValue condition = emitExpr(s->cond);
                        toBool(condition);
                        emit("bnez t0, " + bodyL);
                    }
                    breakLabels_.pop_back();
                    contLabels_.pop_back();
                    activeLoopBases_.pop_back();
                    activeLoopPtrs_.pop_back();
                    activeLoadCSE_.resize(invariantDepth);
                    emitLabel(endL);
                    forgetAssignedKnownInts(s->body);
                    break;
                }

                for (auto &p : loopPtrs) {
                    emitLValAddressNoLoopPtr(p.lval);
                    emit("mv " + p.reg + ", t0");
                }
                for (auto &b : loopBases) {
                    emitLoopBaseInit(b);
                }
                activeLoopPtrs_.push_back(loopPtrs);
                activeLoopBases_.push_back(loopBases);
                contLabels_.push_back(condL);
                breakLabels_.push_back(endL);
                emit("j " + condL);
                emitLabel(bodyL);
                genStmt(s->body);
                for (auto &p : loopPtrs) {
                    addrOffset(p.reg, p.reg, p.byteStep);
                }
                emitLabel(condL);
                if (!tryEmitBranchIfTrue(s->cond, bodyL)) {
                    ExprValue c = emitExpr(s->cond);
                    toBool(c);
                    emit("bnez t0, " + bodyL);
                }
                emitLabel(endL);
                contLabels_.pop_back();
                breakLabels_.pop_back();
                activeLoopBases_.pop_back();
                activeLoopPtrs_.pop_back();
                forgetAssignedKnownInts(s->body);
                break;
            }
            case StmtKind::Break:
                if (breakLabels_.empty()) fail(s->line, "break outside loop");
                emit("j " + breakLabels_.back());
                break;
            case StmtKind::Continue:
                if (contLabels_.empty()) fail(s->line, "continue outside loop");
                emit("j " + contLabels_.back());
                break;
            case StmtKind::Return:
                if (s->expr) {
                    if (tryEmitSelfTailReturn(s->expr)) break;
                    ExprValue v = emitExpr(s->expr);
                    if (curRet_ == BaseType::Void) fail(s->line, "void function cannot return a value");
                    convertReg(v.type.base, curRet_);
                    if (curRet_ == BaseType::Float) emit("fmv.s fa0, ft0");
                    else emit("mv a0, t0");
                } else if (curRet_ != BaseType::Void) {
                    emit("li a0, 0");
                }
                emit("j " + retLabel_);
                break;
        }
    }

    ExprPtr initFirstExpr(const InitPtr &init) {
        if (!init->isList) return init->expr;
        if (init->items.empty()) {
            auto z = make_shared<Expr>();
            z->kind = ExprKind::IntLit;
            z->line = init->line;
            z->intVal = 0;
            return z;
        }
        return initFirstExpr(init->items.front());
    }

    void genLocalDecl(const Decl &d) {
        for (auto &vd : d.defs) {
            Type t;
            t.base = d.base;
            for (auto &dim : vd.dims) t.dims.push_back(evalConstInt(dim));
            Symbol s;
            s.name = vd.name;
            s.type = t;
            s.isConst = d.isConst;
            int size = t.isArray() ? typeSize(t) : elemSize(t.base);
            auto planned = plannedLocalRegs_.find(&vd);
            if (planned != plannedLocalRegs_.end()) s.reg = planned->second;
            else if (!t.isArray()) s.reg = allocLocalReg(t);
            if (s.reg < 0) s.offset = allocLocal(size, t.isArray() ? 8 : 4);
            if (d.isConst && vd.init) {
                int count = max(1, productTail(t.dims, 0));
                s.constElems.assign(count, zeroValue(t.base));
                if (t.dims.empty()) s.constElems[0] = convertConst(evalScalarInitConst(vd.init), t.base);
                else flattenConstInit(vd.init, t, 0, 0, count, s.constElems);
                for (auto &v : s.constElems) v = convertConst(v, t.base);
            }
            addLocal(s);
            if (!t.isArray() && t.base == BaseType::Int) declareKnownIntSlot(vd.name);
            Symbol *sym = findVar(vd.name);
            if (!vd.init) continue;
            if (!t.isArray()) {
                ExprValue v = emitExpr(initFirstExpr(vd.init));
                convertReg(v.type.base, t.base);
                if (sym->reg >= 0) emit("mv " + localRegName(sym->reg) + ", t0");
                else if (t.base == BaseType::Float) storeFloat("ft0", "s0", sym->offset);
                else storeInt("t0", "s0", sym->offset);
                ConstValue cv;
                if (t.base == BaseType::Int && tryEvalConstExpr(initFirstExpr(vd.init), cv) && cv.base == BaseType::Int) {
                    setKnownInt(vd.name, cv.i);
                } else if (t.base == BaseType::Int) {
                    forgetKnownInt(vd.name);
                }
            } else {
                int count = productTail(t.dims, 0);
                for (int i = 0; i < count; ++i) {
                    if (t.base == BaseType::Float) {
                        emit("li t0, 0");
                        emit("fmv.w.x ft0, t0");
                        storeFloat("ft0", "s0", sym->offset + i * 4);
                    } else {
                        emit("li t0, 0");
                        storeInt("t0", "s0", sym->offset + i * 4);
                    }
                }
                vector<ExprPtr> elems(count);
                flattenExprInit(vd.init, t, 0, 0, count, elems);
                for (int i = 0; i < count; ++i) {
                    if (!elems[i]) continue;
                    ExprValue v = emitExpr(elems[i]);
                    convertReg(v.type.base, t.base);
                    if (t.base == BaseType::Float) storeFloat("ft0", "s0", sym->offset + i * 4);
                    else storeInt("t0", "s0", sym->offset + i * 4);
                }
            }
        }
    }

    Type lvalType(const ExprPtr &e) {
        Symbol *s = findVar(e->text);
        if (!s) fail(e->line, "undefined variable " + e->text);
        Type t = s->type;
        if (e->indices.size() > t.dims.size()) fail(e->line, "too many indices for " + e->text);
        t.dims.erase(t.dims.begin(), t.dims.begin() + static_cast<ptrdiff_t>(e->indices.size()));
        return t;
    }

    bool exprContainsVar(const ExprPtr &e, const string &name) {
        if (!e) return false;
        if (e->kind == ExprKind::LVal) {
            if (e->text == name && e->indices.empty()) return true;
            for (auto &idx : e->indices) {
                if (exprContainsVar(idx, name)) return true;
            }
            return false;
        }
        if (e->kind == ExprKind::Unary) return exprContainsVar(e->lhs, name);
        if (e->kind == ExprKind::Binary) return exprContainsVar(e->lhs, name) || exprContainsVar(e->rhs, name);
        if (e->kind == ExprKind::Call) {
            for (auto &arg : e->args) {
                if (exprContainsVar(arg, name)) return true;
            }
        }
        return false;
    }

    bool exprIsVarPlusConst(const ExprPtr &e, const string &name, int wantDelta) {
        if (!e || e->kind != ExprKind::Binary) return false;
        ConstValue cv;
        if (e->text == "+" &&
            isSameScalarLVal(e->lhs, findVar(name)) &&
            tryEvalConstExpr(e->rhs, cv) && cv.base == BaseType::Int && cv.i == wantDelta) {
            return true;
        }
        if (e->text == "+" &&
            isSameScalarLVal(e->rhs, findVar(name)) &&
            tryEvalConstExpr(e->lhs, cv) && cv.base == BaseType::Int && cv.i == wantDelta) {
            return true;
        }
        if (e->text == "-" &&
            isSameScalarLVal(e->lhs, findVar(name)) &&
            tryEvalConstExpr(e->rhs, cv) && cv.base == BaseType::Int && -cv.i == wantDelta) {
            return true;
        }
        return false;
    }

    bool isSimpleInductionStep(const StmtPtr &s, string &var, int &delta) {
        if (!s || s->kind != StmtKind::Assign || !s->lhs ||
            s->lhs->kind != ExprKind::LVal || !s->lhs->indices.empty()) {
            return false;
        }
        Symbol *sym = findVar(s->lhs->text);
        if (!sym || !sym->type.isScalar() || sym->type.base != BaseType::Int) return false;
        var = s->lhs->text;
        if (exprIsVarPlusConst(s->expr, var, 1)) {
            delta = 1;
            return true;
        }
        if (exprIsVarPlusConst(s->expr, var, -1)) {
            delta = -1;
            return true;
        }
        return false;
    }

    bool blockItemsContainUnsafeLoopControlOrCall(const vector<BlockItem> &items, const string &iv) {
        for (auto &it : items) {
            if (it.isDecl) {
                return true;
            }
            if (stmtHasUnsafeForLoopPtr(it.stmt, iv)) return true;
        }
        return false;
    }

    bool stmtHasUnsafeForLoopPtr(const StmtPtr &s, const string &iv) {
        if (!s) return false;
        switch (s->kind) {
            case StmtKind::Block:
                return blockItemsContainUnsafeLoopControlOrCall(s->items, iv);
            case StmtKind::Assign:
                if (s->lhs && s->lhs->kind == ExprKind::LVal && s->lhs->text == iv && s->lhs->indices.empty()) return true;
                return exprMayCall(s->lhs) || exprMayCall(s->expr);
            case StmtKind::Expr:
            case StmtKind::Return:
                return exprMayCall(s->expr);
            case StmtKind::If:
                return exprMayCall(s->cond) ||
                       stmtHasUnsafeForLoopPtr(s->thenStmt, iv) ||
                       stmtHasUnsafeForLoopPtr(s->elseStmt, iv);
            case StmtKind::While:
                return true;
            case StmtKind::Break:
            case StmtKind::Continue:
                return true;
        }
        return true;
    }

    bool simpleWhileInduction(const StmtPtr &s, string &iv, int &delta) {
        if (!s || s->kind != StmtKind::While || !s->body || s->body->kind != StmtKind::Block) return false;
        if (s->cond && exprMayCall(s->cond)) return false;
        auto &items = s->body->items;
        if (items.empty() || items.back().isDecl) return false;
        string stepVar;
        int stepDelta = 0;
        if (!isSimpleInductionStep(items.back().stmt, stepVar, stepDelta)) return false;
        for (size_t i = 0; i + 1 < items.size(); ++i) {
            if (items[i].isDecl) return false;
            if (stmtHasUnsafeForLoopPtr(items[i].stmt, stepVar)) return false;
        }
        iv = stepVar;
        delta = stepDelta;
        return true;
    }

    bool condVarConstTripCount(const ExprPtr &cond, const string &iv, int start,
                              int delta, int &trips, int maxTrips = 8) {
        if (!cond || cond->kind != ExprKind::Binary) return false;
        const string &op = cond->text;
        bool rel = op == "<" || op == "<=" || op == ">" || op == ">=" || op == "==" || op == "!=";
        if (!rel) return false;
        ConstValue cv;
        bool ivLeft = isSameScalarLVal(cond->lhs, findVar(iv)) &&
                      tryEvalConstExpr(cond->rhs, cv) && cv.base == BaseType::Int;
        bool ivRight = !ivLeft &&
                       isSameScalarLVal(cond->rhs, findVar(iv)) &&
                       tryEvalConstExpr(cond->lhs, cv) && cv.base == BaseType::Int;
        if (!ivLeft && !ivRight) return false;
        auto cmp = [&](long long v) {
            long long a = ivLeft ? v : cv.i;
            long long b = ivLeft ? cv.i : v;
            if (op == "<") return a < b;
            if (op == "<=") return a <= b;
            if (op == ">") return a > b;
            if (op == ">=") return a >= b;
            if (op == "==") return a == b;
            return a != b;
        };
        long long v = start;
        for (int n = 0; n <= maxTrips; ++n) {
            if (!cmp(v)) {
                trips = n;
                return trips >= 2;
            }
            if (v + delta < numeric_limits<int>::min() ||
                v + delta > numeric_limits<int>::max()) {
                return false;
            }
            v += delta;
        }
        return false;
    }

    bool smallConstWhileTripCount(const StmtPtr &s, int &trips) {
        string iv;
        int delta = 0;
        if (!simpleWhileInduction(s, iv, delta)) return false;
        if (delta != 1 && delta != -1) return false;
        int start = 0;
        if (!getKnownInt(iv, start)) return false;
        return condVarConstTripCount(s->cond, iv, start, delta, trips);
    }

    void clearKnownInts() {
        for (auto &scope : knownIntScopes_) {
            for (auto &kv : scope) kv.second = {false, 0};
        }
    }

    void forgetAssignedKnownInts(const StmtPtr &s) {
        unordered_map<string, bool> assigned;
        collectAssignedScalarsStmt(s, assigned);
        for (auto &kv : assigned) forgetKnownInt(kv.first);
    }

    void updateKnownAfterScalarAssign(const ExprPtr &lhs, const ExprPtr &rhs, const Type &target) {
        if (!lhs || lhs->kind != ExprKind::LVal || !lhs->indices.empty()) return;
        if (!target.isScalar() || target.base != BaseType::Int) return;
        ConstValue cv;
        if (tryEvalConstExpr(rhs, cv) && cv.base == BaseType::Int) {
            setKnownInt(lhs->text, cv.i);
            return;
        }
        int old = 0;
        if (getKnownInt(lhs->text, old)) {
            if (exprIsVarPlusConst(rhs, lhs->text, 1)) {
                setKnownInt(lhs->text, old + 1);
                return;
            }
            if (exprIsVarPlusConst(rhs, lhs->text, -1)) {
                setKnownInt(lhs->text, old - 1);
                return;
            }
        }
        forgetKnownInt(lhs->text);
    }

    bool exprCanSkipUnrolledIvStep(const ExprPtr &e, const string &iv,
                                   const unordered_map<string, bool> &ptrKeys) {
        if (!e) return true;
        if (e->kind == ExprKind::LVal) {
            if (e->indices.empty()) return e->text != iv;
            bool usesIvInIndex = false;
            for (auto &idx : e->indices) {
                if (exprContainsVar(idx, iv)) usesIvInIndex = true;
            }
            if (usesIvInIndex && !ptrKeys.count(lvalKey(e))) return false;
            if (usesIvInIndex) return true;
            for (auto &idx : e->indices) {
                if (!exprCanSkipUnrolledIvStep(idx, iv, ptrKeys)) return false;
            }
            return true;
        }
        if (e->kind == ExprKind::Unary) return exprCanSkipUnrolledIvStep(e->lhs, iv, ptrKeys);
        if (e->kind == ExprKind::Binary) {
            return exprCanSkipUnrolledIvStep(e->lhs, iv, ptrKeys) &&
                   exprCanSkipUnrolledIvStep(e->rhs, iv, ptrKeys);
        }
        if (e->kind == ExprKind::Call) {
            for (auto &arg : e->args) {
                if (!exprCanSkipUnrolledIvStep(arg, iv, ptrKeys)) return false;
            }
        }
        return true;
    }

    bool stmtCanSkipUnrolledIvStepImpl(const StmtPtr &s, const string &iv,
                                       const unordered_map<string, bool> &ptrKeys,
                                       bool skipTrailingStep) {
        if (!s) return true;
        switch (s->kind) {
            case StmtKind::Block: {
                size_t count = s->items.size();
                if (skipTrailingStep && count > 0) --count;
                for (size_t i = 0; i < count; ++i) {
                    auto &it = s->items[i];
                    if (it.isDecl) return false;
                    if (!stmtCanSkipUnrolledIvStepImpl(it.stmt, iv, ptrKeys, false)) return false;
                }
                return true;
            }
            case StmtKind::Assign:
                return exprCanSkipUnrolledIvStep(s->lhs, iv, ptrKeys) &&
                       exprCanSkipUnrolledIvStep(s->expr, iv, ptrKeys);
            case StmtKind::Expr:
            case StmtKind::Return:
                return exprCanSkipUnrolledIvStep(s->expr, iv, ptrKeys);
            case StmtKind::If:
                return exprCanSkipUnrolledIvStep(s->cond, iv, ptrKeys) &&
                       stmtCanSkipUnrolledIvStepImpl(s->thenStmt, iv, ptrKeys, false) &&
                       stmtCanSkipUnrolledIvStepImpl(s->elseStmt, iv, ptrKeys, false);
            case StmtKind::While:
            case StmtKind::Break:
            case StmtKind::Continue:
                return false;
        }
        return false;
    }

    bool stmtCanSkipUnrolledIvStep(const StmtPtr &s, const string &iv,
                                   const unordered_map<string, bool> &ptrKeys) {
        return stmtCanSkipUnrolledIvStepImpl(s, iv, ptrKeys, true);
    }

    void genUnrolledLoopBodyWithoutStep(const StmtPtr &body) {
        if (!body || body->kind != StmtKind::Block || body->items.empty()) return;
        for (size_t i = 0; i + 1 < body->items.size(); ++i) {
            if (!body->items[i].isDecl) genStmt(body->items[i].stmt);
        }
    }

    void emitIvAdvance(const string &iv, int delta) {
        Symbol *sym = findVar(iv);
        if (!sym || !sym->type.isScalar() || sym->type.base != BaseType::Int) return;
        if (sym->reg >= 0) {
            string r = localRegName(sym->reg);
            if (delta == 0) return;
            if (isSimm12(delta)) emit("addiw " + r + ", " + r + ", " + to_string(delta));
            else {
                loadImm("t0", delta);
                emit("addw " + r + ", " + r + ", t0");
            }
        } else {
            loadInt("t0", "s0", sym->offset);
            if (delta != 0) emitIntRightConstOp("+", delta);
            storeInt("t0", "s0", sym->offset);
        }
    }

    bool tryEmitSmallConstWhileUnroll(const StmtPtr &s) {
        string iv;
        int delta = 0;
        if (!simpleWhileInduction(s, iv, delta)) return false;
        int start = 0;
        if (!getKnownInt(iv, start)) return false;
        int trips = 0;
        if (!condVarConstTripCount(s->cond, iv, start, delta, trips)) return false;
        vector<ActiveLoopPtr> loopPtrs;
        planSimpleLoopPointers(s, loopPtrs);
        vector<ActiveLoopBase> loopBases;
        planCountedLoopBases(s, loopPtrs, loopBases);
        unordered_map<string, bool> ptrKeys;
        for (auto &p : loopPtrs) ptrKeys[p.key] = true;
        bool skipStep = stmtCanSkipUnrolledIvStep(s->body, iv, ptrKeys);
        for (auto &p : loopPtrs) {
            emitLValAddressNoLoopPtr(p.lval);
            emit("mv " + p.reg + ", t0");
        }
        for (auto &b : loopBases) emitLoopBaseInit(b);
        activeLoopPtrs_.push_back(loopPtrs);
        activeLoopBases_.push_back(loopBases);
        for (int i = 0; i < trips; ++i) {
            if (skipStep) genUnrolledLoopBodyWithoutStep(s->body);
            else genStmt(s->body);
            if (i + 1 < trips) {
                for (auto &p : loopPtrs) addrOffset(p.reg, p.reg, p.byteStep);
            }
        }
        activeLoopBases_.pop_back();
        activeLoopPtrs_.pop_back();
        if (skipStep) {
            emitIvAdvance(iv, delta * trips);
            setKnownInt(iv, start + delta * trips);
        }
        return true;
    }

    int estimateExprTreeCost(const ExprPtr &e, int limit = 128) {
        if (!e || limit <= 0) return 0;
        int cost = 1;
        if (e->kind == ExprKind::Unary) {
            cost += estimateExprTreeCost(e->lhs, limit - cost);
        } else if (e->kind == ExprKind::Binary) {
            cost += estimateExprTreeCost(e->lhs, limit - cost);
            if (cost < limit) cost += estimateExprTreeCost(e->rhs, limit - cost);
        } else if (e->kind == ExprKind::LVal) {
            for (auto &index : e->indices) {
                if (cost >= limit) break;
                cost += estimateExprTreeCost(index, limit - cost);
            }
        } else if (e->kind == ExprKind::Call) {
            for (auto &argument : e->args) {
                if (cost >= limit) break;
                cost += estimateExprTreeCost(argument, limit - cost);
            }
        }
        return min(cost, limit);
    }

    int estimateStmtTreeCost(const StmtPtr &s, int limit = 128) {
        if (!s || limit <= 0) return 0;
        int cost = 1;
        switch (s->kind) {
            case StmtKind::Block:
                for (auto &item : s->items) {
                    if (cost >= limit) break;
                    if (item.isDecl) {
                        cost = limit;
                        break;
                    }
                    cost += estimateStmtTreeCost(item.stmt, limit - cost);
                }
                break;
            case StmtKind::Assign:
                cost += estimateExprTreeCost(s->lhs, limit - cost);
                if (cost < limit) cost += estimateExprTreeCost(s->expr, limit - cost);
                break;
            case StmtKind::Expr:
            case StmtKind::Return:
                cost += estimateExprTreeCost(s->expr, limit - cost);
                break;
            case StmtKind::If:
                cost += estimateExprTreeCost(s->cond, limit - cost);
                if (cost < limit) cost += estimateStmtTreeCost(s->thenStmt, limit - cost);
                if (cost < limit) cost += estimateStmtTreeCost(s->elseStmt, limit - cost);
                break;
            case StmtKind::While:
            case StmtKind::Break:
            case StmtKind::Continue:
                cost = limit;
                break;
        }
        return min(cost, limit);
    }

    bool tryEmitPartialConstWhileUnroll(const StmtPtr &s) {
        string iv;
        int delta = 0;
        if (!simpleWhileInduction(s, iv, delta) || (delta != 1 && delta != -1)) return false;
        int start = 0;
        if (!getKnownInt(iv, start)) return false;
        int trips = 0;
        if (!condVarConstTripCount(s->cond, iv, start, delta, trips, 1 << 20) || trips <= 8) {
            return false;
        }

        int bodyCost = estimateStmtTreeCost(s->body);
        int factor = 0;
        if (trips % 4 == 0 && bodyCost <= 32) factor = 4;
        else if (trips % 2 == 0 && bodyCost <= 64) factor = 2;
        if (factor == 0) return false;

        vector<ActiveLoopPtr> loopPtrs;
        planSimpleLoopPointers(s, loopPtrs);
        vector<ActiveLoopBase> loopBases;
        planCountedLoopBases(s, loopPtrs, loopBases);
        vector<ActiveLoadCSE> invariantLoads;
        planLoopInvariantLoads(s, invariantLoads);
        unordered_map<string, bool> ptrKeys;
        for (auto &pointer : loopPtrs) ptrKeys[pointer.key] = true;
        bool skipStep = stmtCanSkipUnrolledIvStep(s->body, iv, ptrKeys);

        for (auto &pointer : loopPtrs) {
            emitLValAddressNoLoopPtr(pointer.lval);
            emit("mv " + pointer.reg + ", t0");
        }
        for (auto &base : loopBases) emitLoopBaseInit(base);

        size_t invariantDepth = activeLoadCSE_.size();
        for (auto &load : invariantLoads) {
            ExprValue value = emitExpr(load.lval);
            convertReg(value.type.base, BaseType::Int);
            emit("mv " + load.reg + ", t0");
            load.loaded = true;
            activeLoadCSE_.push_back(load);
        }

        activeLoopPtrs_.push_back(loopPtrs);
        activeLoopBases_.push_back(loopBases);
        string bodyLabel = newLabel("partial_unroll_body");
        emitLabel(bodyLabel);
        for (int copy = 0; copy < factor; ++copy) {
            if (skipStep) genUnrolledLoopBodyWithoutStep(s->body);
            else genStmt(s->body);
            for (auto &pointer : loopPtrs) {
                addrOffset(pointer.reg, pointer.reg, pointer.byteStep);
            }
        }
        if (skipStep) emitIvAdvance(iv, delta * factor);
        if (!tryEmitBranchIfTrue(s->cond, bodyLabel)) {
            ExprValue condition = emitExpr(s->cond);
            toBool(condition);
            emit("bnez t0, " + bodyLabel);
        }

        activeLoopBases_.pop_back();
        activeLoopPtrs_.pop_back();
        activeLoadCSE_.resize(invariantDepth);
        forgetAssignedKnownInts(s->body);
        setKnownInt(iv, start + delta * trips);
        return true;
    }

    bool tryEmitRuntimeCountedWhileUnroll(const StmtPtr &s) {
        string iv;
        int delta = 0;
        if (!simpleWhileInduction(s, iv, delta) || (delta != 1 && delta != -1) ||
            !s->cond || s->cond->kind != ExprKind::Binary) {
            return false;
        }

        string op = s->cond->text;
        ExprPtr bound;
        Symbol *ivSymbol = findVar(iv);
        if (isSameScalarLVal(s->cond->lhs, ivSymbol)) {
            bound = s->cond->rhs;
        } else if (isSameScalarLVal(s->cond->rhs, ivSymbol)) {
            bound = s->cond->lhs;
            if (op == "<") op = ">";
            else if (op == "<=") op = ">=";
            else if (op == ">") op = "<";
            else if (op == ">=") op = "<=";
            else return false;
        } else {
            return false;
        }
        bool inclusive = op == "<=" || op == ">=";
        if ((delta > 0 && op != "<" && op != "<=") ||
            (delta < 0 && op != ">" && op != ">=")) {
            return false;
        }

        unordered_map<string, bool> assigned;
        collectAssignedScalarsStmt(s->body, assigned);
        function<bool(const ExprPtr &)> safeBound = [&](const ExprPtr &e) {
            if (!e) return false;
            if (e->kind == ExprKind::IntLit) return true;
            if (e->kind == ExprKind::LVal) {
                if (!e->indices.empty() || e->text == iv) return false;
                Symbol *symbol = findVar(e->text);
                auto written = assigned.find(e->text);
                return symbol && symbol->type.isScalar() && symbol->type.base == BaseType::Int &&
                       (written == assigned.end() || !written->second);
            }
            if (e->kind == ExprKind::Unary) {
                return (e->text == "+" || e->text == "-") && safeBound(e->lhs);
            }
            if (e->kind == ExprKind::Binary) {
                return (e->text == "+" || e->text == "-" || e->text == "*" ||
                        e->text == "/" || e->text == "%") &&
                       safeBound(e->lhs) && safeBound(e->rhs);
            }
            return false;
        };
        if (!safeBound(bound)) return false;

        int bodyCost = estimateStmtTreeCost(s->body);
        int factor = bodyCost <= 32 ? 4 : (bodyCost <= 64 ? 2 : 0);
        if (factor == 0) return false;
        int minTrips = factor == 4 ? 12 : 8;

        vector<ActiveLoopPtr> loopPtrs;
        planSimpleLoopPointers(s, loopPtrs);
        vector<ActiveLoopBase> loopBases;
        planCountedLoopBases(s, loopPtrs, loopBases);
        unordered_map<string, bool> ptrKeys;
        for (auto &pointer : loopPtrs) ptrKeys[pointer.key] = true;
        bool skipStep = stmtCanSkipUnrolledIvStep(s->body, iv, ptrKeys);

        string shortInit = newLabel("runtime_unroll_short_init");
        string groupBody = newLabel("runtime_unroll_body");
        string groupCheck = newLabel("runtime_unroll_check");
        string cleanupBody = newLabel("runtime_unroll_cleanup_body");
        string cleanupCond = newLabel("runtime_unroll_cleanup_cond");
        string endLabel = newLabel("runtime_unroll_end");

        auto emitRemainingTrips = [&]() {
            ExprValue boundValue = emitExpr(bound);
            convertReg(boundValue.type.base, BaseType::Int);
            if (ivSymbol->reg >= 0) {
                string ivReg = localRegName(ivSymbol->reg);
                if (delta > 0) emit("sub t0, t0, " + ivReg);
                else emit("sub t0, " + ivReg + ", t0");
            } else {
                loadInt("t1", "s0", ivSymbol->offset);
                if (delta > 0) emit("sub t0, t0, t1");
                else emit("sub t0, t1, t0");
            }
            if (inclusive) emit("addi t0, t0, 1");
        };

        emitRemainingTrips();
        loadImm("t1", minTrips);
        emit("blt t0, t1, " + shortInit);

        for (auto &pointer : loopPtrs) {
            emitLValAddressNoLoopPtr(pointer.lval);
            emit("mv " + pointer.reg + ", t0");
        }
        for (auto &base : loopBases) emitLoopBaseInit(base);

        activeLoopPtrs_.push_back(loopPtrs);
        activeLoopBases_.push_back(loopBases);
        forgetAssignedKnownInts(s->body);

        emitLabel(groupBody);
        for (int copy = 0; copy < factor; ++copy) {
            if (skipStep) genUnrolledLoopBodyWithoutStep(s->body);
            else genStmt(s->body);
            for (auto &pointer : loopPtrs) {
                addrOffset(pointer.reg, pointer.reg, pointer.byteStep);
            }
        }
        if (skipStep) emitIvAdvance(iv, delta * factor);

        emitLabel(groupCheck);
        emitRemainingTrips();
        loadImm("t1", factor);
        emit("bge t0, t1, " + groupBody);
        emit("blez t0, " + endLabel);

        emitLabel(cleanupBody);
        genStmt(s->body);
        for (auto &pointer : loopPtrs) {
            addrOffset(pointer.reg, pointer.reg, pointer.byteStep);
        }
        emitLabel(cleanupCond);
        if (!tryEmitBranchIfTrue(s->cond, cleanupBody)) {
            ExprValue condition = emitExpr(s->cond);
            toBool(condition);
            emit("bnez t0, " + cleanupBody);
        }
        emit("j " + endLabel);

        activeLoopBases_.pop_back();
        activeLoopPtrs_.pop_back();
        forgetAssignedKnownInts(s->body);

        emitLabel(shortInit);
        emit("blez t0, " + endLabel);
        for (auto &pointer : loopPtrs) {
            emitLValAddressNoLoopPtr(pointer.lval);
            emit("mv " + pointer.reg + ", t0");
        }
        for (auto &base : loopBases) emitLoopBaseInit(base);

        activeLoopPtrs_.push_back(loopPtrs);
        activeLoopBases_.push_back(loopBases);
        forgetAssignedKnownInts(s->body);

        string shortBody = newLabel("runtime_unroll_short_body");
        string shortCond = newLabel("runtime_unroll_short_cond");
        emitLabel(shortBody);
        genStmt(s->body);
        for (auto &pointer : loopPtrs) {
            addrOffset(pointer.reg, pointer.reg, pointer.byteStep);
        }
        emitLabel(shortCond);
        if (!tryEmitBranchIfTrue(s->cond, shortBody)) {
            ExprValue condition = emitExpr(s->cond);
            toBool(condition);
            emit("bnez t0, " + shortBody);
        }

        activeLoopBases_.pop_back();
        activeLoopPtrs_.pop_back();
        forgetAssignedKnownInts(s->body);
        emitLabel(endLabel);
        return true;
    }

    bool collectLinearIndexCoeff(const ExprPtr &e, const string &iv, int sign, int &coeff, long long &constant) {
        if (!e) return false;
        ConstValue cv;
        if (tryEvalConstExpr(e, cv) && cv.base == BaseType::Int) {
            constant += static_cast<long long>(sign) * cv.i;
            return true;
        }
        if (e->kind == ExprKind::LVal && e->text == iv && e->indices.empty()) {
            coeff += sign;
            return true;
        }
        if (e->kind == ExprKind::LVal) {
            Symbol *s = findVar(e->text);
            if (s && s->reg >= 0 && e->indices.empty()) return true;
            return false;
        }
        if (e->kind == ExprKind::Unary && e->text == "+") {
            return collectLinearIndexCoeff(e->lhs, iv, sign, coeff, constant);
        }
        if (e->kind == ExprKind::Unary && e->text == "-") {
            return collectLinearIndexCoeff(e->lhs, iv, -sign, coeff, constant);
        }
        if (e->kind == ExprKind::Binary && e->text == "+") {
            return collectLinearIndexCoeff(e->lhs, iv, sign, coeff, constant) &&
                   collectLinearIndexCoeff(e->rhs, iv, sign, coeff, constant);
        }
        if (e->kind == ExprKind::Binary && e->text == "-") {
            return collectLinearIndexCoeff(e->lhs, iv, sign, coeff, constant) &&
                   collectLinearIndexCoeff(e->rhs, iv, -sign, coeff, constant);
        }
        return false;
    }

    bool lvalLoopByteStep(const ExprPtr &e, const string &iv, int loopDelta, int &byteStep) {
        if (!e || e->kind != ExprKind::LVal || e->indices.empty() || exprMayCall(e)) return false;
        Symbol *s = findVar(e->text);
        if (!s || s->type.dims.size() < e->indices.size()) return false;
        int totalCoeff = 0;
        for (size_t i = 0; i < e->indices.size(); ++i) {
            int coeff = 0;
            long long constant = 0;
            if (!collectLinearIndexCoeff(e->indices[i], iv, 1, coeff, constant)) return false;
            if (coeff != 0) {
                int stride = productTail(s->type.dims, i + 1) * elemSize(s->type.base);
                totalCoeff += coeff * stride;
            }
        }
        if (totalCoeff == 0) return false;
        byteStep = totalCoeff * loopDelta;
        return true;
    }

    bool exprIsLoopPtrInvariant(const ExprPtr &e, const string &iv,
                                const unordered_map<string, bool> &assigned,
                                const unordered_set<string> &written,
                                bool hasUnknownArrayWrite) {
        if (!e) return true;
        if (e->kind == ExprKind::LVal) {
            auto assignedIt = assigned.find(e->text);
            if (assignedIt != assigned.end() && assignedIt->second) return false;
            if (e->indices.empty()) {
                return e->text != iv;
            }
            Symbol *symbol = findVar(e->text);
            if (!symbol || written.count(e->text)) return false;
            if (symbol->isParamArray) {
                for (const string &name : written) {
                    Symbol *writtenSymbol = findVar(name);
                    if (writtenSymbol && writtenSymbol->type.isArray()) return false;
                }
            }
            if (symbol->type.isArray() && hasUnknownArrayWrite &&
                (symbol->isGlobal || symbol->isParamArray)) {
                return false;
            }
            for (auto &index : e->indices) {
                if (!exprIsLoopPtrInvariant(index, iv, assigned, written,
                                            hasUnknownArrayWrite)) {
                    return false;
                }
            }
            return true;
        }
        if (e->kind == ExprKind::Unary) {
            return exprIsLoopPtrInvariant(e->lhs, iv, assigned, written,
                                          hasUnknownArrayWrite);
        }
        if (e->kind == ExprKind::Binary) {
            return exprIsLoopPtrInvariant(e->lhs, iv, assigned, written,
                                          hasUnknownArrayWrite) &&
                   exprIsLoopPtrInvariant(e->rhs, iv, assigned, written,
                                          hasUnknownArrayWrite);
        }
        if (e->kind == ExprKind::Call) return false;
        return true;
    }

    bool collectInvariantLoopIndexCoeff(const ExprPtr &e, const string &iv, int sign,
                                        const unordered_map<string, bool> &assigned,
                                        const unordered_set<string> &written,
                                        bool hasUnknownArrayWrite, int &coeff) {
        if (!e) return false;
        if (e->kind == ExprKind::LVal && e->text == iv && e->indices.empty()) {
            coeff += sign;
            return true;
        }
        if (!exprContainsVar(e, iv)) {
            return exprIsLoopPtrInvariant(e, iv, assigned, written,
                                          hasUnknownArrayWrite);
        }
        if (e->kind == ExprKind::Unary && e->text == "+") {
            return collectInvariantLoopIndexCoeff(e->lhs, iv, sign, assigned,
                                                  written, hasUnknownArrayWrite, coeff);
        }
        if (e->kind == ExprKind::Unary && e->text == "-") {
            return collectInvariantLoopIndexCoeff(e->lhs, iv, -sign, assigned,
                                                  written, hasUnknownArrayWrite, coeff);
        }
        if (e->kind == ExprKind::Binary && e->text == "+") {
            return collectInvariantLoopIndexCoeff(e->lhs, iv, sign, assigned,
                                                  written, hasUnknownArrayWrite, coeff) &&
                   collectInvariantLoopIndexCoeff(e->rhs, iv, sign, assigned,
                                                  written, hasUnknownArrayWrite, coeff);
        }
        if (e->kind == ExprKind::Binary && e->text == "-") {
            return collectInvariantLoopIndexCoeff(e->lhs, iv, sign, assigned,
                                                  written, hasUnknownArrayWrite, coeff) &&
                   collectInvariantLoopIndexCoeff(e->rhs, iv, -sign, assigned,
                                                  written, hasUnknownArrayWrite, coeff);
        }
        return false;
    }

    bool lvalInvariantLoopByteStep(const ExprPtr &e, const string &iv, int loopDelta,
                                   const unordered_map<string, bool> &assigned,
                                   const unordered_set<string> &written,
                                   bool hasUnknownArrayWrite, int &byteStep) {
        if (!e || e->kind != ExprKind::LVal || e->indices.empty() || exprMayCall(e)) return false;
        Symbol *symbol = findVar(e->text);
        if (!symbol || symbol->type.dims.size() < e->indices.size()) return false;
        int totalCoeff = 0;
        for (size_t i = 0; i < e->indices.size(); ++i) {
            int coeff = 0;
            if (!collectInvariantLoopIndexCoeff(e->indices[i], iv, 1, assigned,
                                                written, hasUnknownArrayWrite, coeff)) {
                return false;
            }
            if (coeff != 0) {
                int stride = productTail(symbol->type.dims, i + 1) * elemSize(symbol->type.base);
                totalCoeff += coeff * stride;
            }
        }
        if (totalCoeff == 0) return false;
        byteStep = totalCoeff * loopDelta;
        return true;
    }

    void collectAssignedScalarsStmt(const StmtPtr &s, unordered_map<string, bool> &assigned) {
        if (!s) return;
        switch (s->kind) {
            case StmtKind::Block:
                for (auto &it : s->items) {
                    if (!it.isDecl) collectAssignedScalarsStmt(it.stmt, assigned);
                }
                break;
            case StmtKind::Assign:
                if (s->lhs && s->lhs->kind == ExprKind::LVal && s->lhs->indices.empty()) {
                    assigned[s->lhs->text] = true;
                }
                break;
            case StmtKind::If:
                collectAssignedScalarsStmt(s->thenStmt, assigned);
                collectAssignedScalarsStmt(s->elseStmt, assigned);
                break;
            case StmtKind::While:
            case StmtKind::Expr:
            case StmtKind::Break:
            case StmtKind::Continue:
            case StmtKind::Return:
                break;
        }
    }

    bool exprUsesAssignedNonIvScalar(const ExprPtr &e, const string &iv, const unordered_map<string, bool> &assigned) {
        if (!e) return false;
        if (e->kind == ExprKind::LVal) {
            if (e->indices.empty()) {
                auto it = assigned.find(e->text);
                return e->text != iv && it != assigned.end() && it->second;
            }
            for (auto &idx : e->indices) {
                if (exprUsesAssignedNonIvScalar(idx, iv, assigned)) return true;
            }
            return false;
        }
        if (e->kind == ExprKind::Unary) return exprUsesAssignedNonIvScalar(e->lhs, iv, assigned);
        if (e->kind == ExprKind::Binary) {
            return exprUsesAssignedNonIvScalar(e->lhs, iv, assigned) ||
                   exprUsesAssignedNonIvScalar(e->rhs, iv, assigned);
        }
        if (e->kind == ExprKind::Call) {
            for (auto &arg : e->args) {
                if (exprUsesAssignedNonIvScalar(arg, iv, assigned)) return true;
            }
        }
        return false;
    }

    bool lvalUsesAssignedNonIvScalar(const ExprPtr &e, const string &iv, const unordered_map<string, bool> &assigned) {
        if (!e || e->kind != ExprKind::LVal) return true;
        for (auto &idx : e->indices) {
            if (exprUsesAssignedNonIvScalar(idx, iv, assigned)) return true;
        }
        return false;
    }

    void collectWrittenObjectsStmt(const StmtPtr &s, unordered_set<string> &written,
                                   bool &hasUnknownArrayWrite) {
        if (!s) return;
        switch (s->kind) {
            case StmtKind::Block:
                for (auto &item : s->items) {
                    if (!item.isDecl) collectWrittenObjectsStmt(item.stmt, written, hasUnknownArrayWrite);
                }
                break;
            case StmtKind::Assign:
                if (s->lhs && s->lhs->kind == ExprKind::LVal) {
                    written.insert(s->lhs->text);
                    Symbol *symbol = findVar(s->lhs->text);
                    if (symbol && symbol->isParamArray) hasUnknownArrayWrite = true;
                }
                break;
            case StmtKind::If:
                collectWrittenObjectsStmt(s->thenStmt, written, hasUnknownArrayWrite);
                collectWrittenObjectsStmt(s->elseStmt, written, hasUnknownArrayWrite);
                break;
            case StmtKind::While:
                collectWrittenObjectsStmt(s->body, written, hasUnknownArrayWrite);
                break;
            case StmtKind::Expr:
            case StmtKind::Break:
            case StmtKind::Continue:
            case StmtKind::Return:
                break;
        }
    }

    bool isLoopInvariantLoad(const ExprPtr &e, const string &iv,
                             const unordered_map<string, bool> &assigned,
                             const unordered_set<string> &written,
                             bool hasUnknownArrayWrite, Type &type) {
        Symbol *symbol = findVar(e->text);
        if (!symbol) return false;
        if (symbol->isConst && !symbol->type.isArray()) return false;
        if (!cacheableLoadCSELVal(e, &type) || e->text == iv || written.count(e->text)) return false;
        if (symbol->isParamArray) {
            for (const string &name : written) {
                Symbol *writtenSymbol = findVar(name);
                if (writtenSymbol && writtenSymbol->type.isArray()) return false;
            }
        }
        if (symbol->type.isArray() && hasUnknownArrayWrite &&
            (symbol->isGlobal || symbol->isParamArray)) {
            return false;
        }
        for (auto &index : e->indices) {
            if (exprContainsVar(index, iv) || exprUsesAssignedNonIvScalar(index, iv, assigned)) return false;
        }
        return true;
    }

    void collectLoopInvariantLoadsExpr(const ExprPtr &e, const string &iv,
                                       const unordered_map<string, bool> &assigned,
                                       const unordered_set<string> &written,
                                       bool hasUnknownArrayWrite,
                                       unordered_map<string, LoadCSECandidate> &candidates) {
        if (!e) return;
        if (e->kind == ExprKind::LVal) {
            Type type;
            if (isLoopInvariantLoad(e, iv, assigned, written, hasUnknownArrayWrite, type)) {
                string key = lvalKey(e);
                auto &candidate = candidates[key];
                if (candidate.count == 0) {
                    candidate.lval = e;
                    candidate.type = type;
                    candidate.cost = lvalLoadCSECost(e);
                }
                ++candidate.count;
            }
            for (auto &index : e->indices) {
                collectLoopInvariantLoadsExpr(index, iv, assigned, written,
                                              hasUnknownArrayWrite, candidates);
            }
            return;
        }
        if (e->kind == ExprKind::Unary) {
            collectLoopInvariantLoadsExpr(e->lhs, iv, assigned, written, hasUnknownArrayWrite, candidates);
            return;
        }
        if (e->kind == ExprKind::Binary) {
            collectLoopInvariantLoadsExpr(e->lhs, iv, assigned, written, hasUnknownArrayWrite, candidates);
            collectLoopInvariantLoadsExpr(e->rhs, iv, assigned, written, hasUnknownArrayWrite, candidates);
            return;
        }
        if (e->kind == ExprKind::Call) {
            for (auto &argument : e->args) {
                collectLoopInvariantLoadsExpr(argument, iv, assigned, written,
                                              hasUnknownArrayWrite, candidates);
            }
        }
    }

    void collectLoopInvariantLoadsInit(const InitPtr &init, const string &iv,
                                       const unordered_map<string, bool> &assigned,
                                       const unordered_set<string> &written,
                                       bool hasUnknownArrayWrite,
                                       unordered_map<string, LoadCSECandidate> &candidates) {
        if (!init) return;
        if (!init->isList) {
            collectLoopInvariantLoadsExpr(init->expr, iv, assigned, written,
                                          hasUnknownArrayWrite, candidates);
            return;
        }
        for (auto &item : init->items) {
            collectLoopInvariantLoadsInit(item, iv, assigned, written,
                                          hasUnknownArrayWrite, candidates);
        }
    }

    void collectLoopInvariantLoadsStmt(const StmtPtr &s, const string &iv,
                                       const unordered_map<string, bool> &assigned,
                                       const unordered_set<string> &written,
                                       bool hasUnknownArrayWrite,
                                       unordered_map<string, LoadCSECandidate> &candidates) {
        if (!s) return;
        switch (s->kind) {
            case StmtKind::Block:
                for (auto &item : s->items) {
                    if (item.isDecl) {
                        for (auto &definition : item.decl->defs) {
                            for (auto &dimension : definition.dims) {
                                collectLoopInvariantLoadsExpr(dimension, iv, assigned, written,
                                                              hasUnknownArrayWrite, candidates);
                            }
                            collectLoopInvariantLoadsInit(definition.init, iv, assigned, written,
                                                          hasUnknownArrayWrite, candidates);
                        }
                    } else {
                        collectLoopInvariantLoadsStmt(item.stmt, iv, assigned, written,
                                                      hasUnknownArrayWrite, candidates);
                    }
                }
                break;
            case StmtKind::Assign:
                if (s->lhs && s->lhs->kind == ExprKind::LVal) {
                    for (auto &index : s->lhs->indices) {
                        collectLoopInvariantLoadsExpr(index, iv, assigned, written,
                                                      hasUnknownArrayWrite, candidates);
                    }
                }
                collectLoopInvariantLoadsExpr(s->expr, iv, assigned, written,
                                              hasUnknownArrayWrite, candidates);
                break;
            case StmtKind::Expr:
            case StmtKind::Return:
                collectLoopInvariantLoadsExpr(s->expr, iv, assigned, written,
                                              hasUnknownArrayWrite, candidates);
                break;
            case StmtKind::If:
                collectLoopInvariantLoadsExpr(s->cond, iv, assigned, written,
                                              hasUnknownArrayWrite, candidates);
                collectLoopInvariantLoadsStmt(s->thenStmt, iv, assigned, written,
                                              hasUnknownArrayWrite, candidates);
                collectLoopInvariantLoadsStmt(s->elseStmt, iv, assigned, written,
                                              hasUnknownArrayWrite, candidates);
                break;
            case StmtKind::While:
            case StmtKind::Break:
            case StmtKind::Continue:
                break;
        }
    }

    bool initializerMayCall(const InitPtr &init) {
        if (!init) return false;
        if (!init->isList) return exprMayCall(init->expr);
        for (auto &item : init->items) {
            if (initializerMayCall(item)) return true;
        }
        return false;
    }

    bool declarationMayCall(const DeclPtr &declaration) {
        if (!declaration) return false;
        for (auto &definition : declaration->defs) {
            for (auto &dimension : definition.dims) {
                if (exprMayCall(dimension)) return true;
            }
            if (initializerMayCall(definition.init)) return true;
        }
        return false;
    }

    bool invariantLoadLoopInduction(const StmtPtr &s, string &iv, int &delta,
                                    unordered_set<string> &declaredNames) {
        if (!s || s->kind != StmtKind::While || !s->body || s->body->kind != StmtKind::Block) return false;
        if (s->cond && exprMayCall(s->cond)) return false;
        auto &items = s->body->items;
        if (items.empty() || items.back().isDecl) return false;
        string stepVar;
        int stepDelta = 0;
        if (!isSimpleInductionStep(items.back().stmt, stepVar, stepDelta)) return false;
        for (size_t i = 0; i + 1 < items.size(); ++i) {
            if (items[i].isDecl) {
                if (declarationMayCall(items[i].decl)) return false;
                for (auto &definition : items[i].decl->defs) declaredNames.insert(definition.name);
            } else if (stmtHasUnsafeForLoopPtr(items[i].stmt, stepVar)) {
                return false;
            }
        }
        if (declaredNames.count(stepVar)) return false;
        iv = stepVar;
        delta = stepDelta;
        return true;
    }

    void planLoopInvariantLoads(const StmtPtr &s, vector<ActiveLoadCSE> &out) {
        string iv;
        int loopDelta = 0;
        unordered_set<string> declaredNames;
        if (!invariantLoadLoopInduction(s, iv, loopDelta, declaredNames)) return;

        unordered_map<string, bool> assigned;
        collectAssignedScalarsStmt(s->body, assigned);
        unordered_set<string> written;
        bool hasUnknownArrayWrite = false;
        collectWrittenObjectsStmt(s->body, written, hasUnknownArrayWrite);
        for (const string &name : declaredNames) {
            assigned[name] = true;
            written.insert(name);
        }

        unordered_map<string, LoadCSECandidate> candidates;
        collectLoopInvariantLoadsExpr(s->cond, iv, assigned, written,
                                      hasUnknownArrayWrite, candidates);
        collectLoopInvariantLoadsStmt(s->body, iv, assigned, written,
                                      hasUnknownArrayWrite, candidates);
        vector<pair<string, LoadCSECandidate>> ranked;
        bool hasRepeatedCandidate = false;
        for (auto &entry : candidates) {
            if (entry.second.count >= 2) hasRepeatedCandidate = true;
        }
        for (auto &entry : candidates) {
            bool profitableCompanion = hasRepeatedCandidate && entry.second.count == 1 &&
                                       !entry.second.lval->indices.empty();
            if (entry.second.count >= 2 || profitableCompanion) ranked.push_back(entry);
        }
        sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) {
            long long aScore = static_cast<long long>(a.second.count - 1) * a.second.cost;
            long long bScore = static_cast<long long>(b.second.count - 1) * b.second.cost;
            if (aScore != bScore) return aScore > bScore;
            if (a.second.count != b.second.count) return a.second.count > b.second.count;
            return a.first < b.first;
        });

        for (auto &entry : ranked) {
            int reg = -1;
            for (int candidate = 11; candidate >= 1; --candidate) {
                reg = reserveSpecificLocalReg(candidate);
                if (reg >= 0) break;
            }
            if (reg < 0) break;
            ActiveLoadCSE load;
            load.key = entry.first;
            load.lval = entry.second.lval;
            load.type = entry.second.type;
            load.reg = localRegName(reg);
            out.push_back(std::move(load));
            if (out.size() >= 2) break;
        }
    }

    bool lvalIvByteCoeff(const ExprPtr &e, const string &iv, int &coeffBytes) {
        if (!e || e->kind != ExprKind::LVal || e->indices.empty() || exprMayCall(e)) return false;
        Symbol *s = findVar(e->text);
        if (!s || s->type.dims.size() < e->indices.size()) return false;
        int totalCoeff = 0;
        for (size_t i = 0; i < e->indices.size(); ++i) {
            int coeff = 0;
            long long constant = 0;
            if (!collectLinearIndexCoeff(e->indices[i], iv, 1, coeff, constant)) return false;
            if (coeff != 0) {
                int stride = productTail(s->type.dims, i + 1) * elemSize(s->type.base);
                totalCoeff += coeff * stride;
            }
        }
        if (totalCoeff == 0) return false;
        coeffBytes = totalCoeff;
        return true;
    }

    void emitLoopBaseInit(const ActiveLoopBase &b) {
        emitLValAddressNoLoopPtr(b.lval);
        int mag = abs(b.ivByteCoeff);
        if (mag == 1) {
            emit("mv t2, " + b.ivReg);
        } else if (isPow2(mag)) {
            emit("slli t2, " + b.ivReg + ", " + to_string(log2Int(mag)));
        } else {
            emit("li t2, " + to_string(mag));
            emit("mul t2, " + b.ivReg + ", t2");
        }
        if (b.ivByteCoeff >= 0) emit("sub " + b.reg + ", t0, t2");
        else emit("add " + b.reg + ", t0, t2");
    }

    bool tryEmitLoopBaseAddress(const ExprPtr &e) {
        if (!e || e->kind != ExprKind::LVal || activeLoopBases_.empty()) return false;
        string key = lvalKey(e);
        for (auto bit = activeLoopBases_.rbegin(); bit != activeLoopBases_.rend(); ++bit) {
            for (auto &b : *bit) {
                if (b.key != key) continue;
                int mag = abs(b.ivByteCoeff);
                if (mag == 1) {
                    if (b.ivByteCoeff >= 0) emit("add t0, " + b.reg + ", " + b.ivReg);
                    else emit("sub t0, " + b.reg + ", " + b.ivReg);
                } else if (isPow2(mag)) {
                    emit("slli t2, " + b.ivReg + ", " + to_string(log2Int(mag)));
                    if (b.ivByteCoeff >= 0) emit("add t0, " + b.reg + ", t2");
                    else emit("sub t0, " + b.reg + ", t2");
                } else {
                    emit("li t2, " + to_string(mag));
                    emit("mul t2, " + b.ivReg + ", t2");
                    if (b.ivByteCoeff >= 0) emit("add t0, " + b.reg + ", t2");
                    else emit("sub t0, " + b.reg + ", t2");
                }
                return true;
            }
        }
        return false;
    }

    void collectLoopPtrCandidatesExpr(const ExprPtr &e, const string &iv, int loopDelta,
                                      const unordered_map<string, bool> &assigned,
                                      const unordered_set<string> &written,
                                      bool hasUnknownArrayWrite,
                                      unordered_map<string, ActiveLoopPtr> &cands,
                                      bool asWrite = false) {
        if (!e) return;
        if (e->kind == ExprKind::LVal) {
            auto assignedIt = assigned.find(e->text);
            bool baseAssigned = assignedIt != assigned.end() && assignedIt->second;
            if (!baseAssigned && !e->indices.empty() &&
                !lvalUsesAssignedNonIvScalar(e, iv, assigned)) {
                Type t = lvalType(e);
                int step = 0;
                if (t.isScalar() &&
                    lvalInvariantLoopByteStep(e, iv, loopDelta, assigned, written,
                                              hasUnknownArrayWrite, step)) {
                    string key = lvalKey(e);
                    auto &p = cands[key];
                    if (p.count == 0) {
                        p.key = key;
                        p.lval = e;
                        p.byteStep = step;
                    }
                    ++p.count;
                    if (asWrite) ++p.writeCount;
                    else ++p.readCount;
                }
            }
            for (auto &idx : e->indices) {
                collectLoopPtrCandidatesExpr(idx, iv, loopDelta, assigned, written,
                                             hasUnknownArrayWrite, cands, false);
            }
            return;
        }
        if (e->kind == ExprKind::Unary) {
            collectLoopPtrCandidatesExpr(e->lhs, iv, loopDelta, assigned, written,
                                         hasUnknownArrayWrite, cands, false);
            return;
        }
        if (e->kind == ExprKind::Binary) {
            collectLoopPtrCandidatesExpr(e->lhs, iv, loopDelta, assigned, written,
                                         hasUnknownArrayWrite, cands, false);
            collectLoopPtrCandidatesExpr(e->rhs, iv, loopDelta, assigned, written,
                                         hasUnknownArrayWrite, cands, false);
            return;
        }
        if (e->kind == ExprKind::Call) {
            for (auto &arg : e->args) {
                collectLoopPtrCandidatesExpr(arg, iv, loopDelta, assigned, written,
                                             hasUnknownArrayWrite, cands, false);
            }
        }
    }

    void collectLoopPtrCandidatesInit(const InitPtr &init, const string &iv, int loopDelta,
                                      const unordered_map<string, bool> &assigned,
                                      const unordered_set<string> &written,
                                      bool hasUnknownArrayWrite,
                                      unordered_map<string, ActiveLoopPtr> &cands) {
        if (!init) return;
        if (!init->isList) {
            collectLoopPtrCandidatesExpr(init->expr, iv, loopDelta, assigned, written,
                                         hasUnknownArrayWrite, cands, false);
            return;
        }
        for (auto &item : init->items) {
            collectLoopPtrCandidatesInit(item, iv, loopDelta, assigned, written,
                                         hasUnknownArrayWrite, cands);
        }
    }

    void collectLoopPtrCandidatesStmt(const StmtPtr &s, const string &iv, int loopDelta,
                                      const unordered_map<string, bool> &assigned,
                                      const unordered_set<string> &written,
                                      bool hasUnknownArrayWrite,
                                      unordered_map<string, ActiveLoopPtr> &cands) {
        if (!s) return;
        switch (s->kind) {
            case StmtKind::Block:
                for (auto &it : s->items) {
                    if (it.isDecl) {
                        for (auto &definition : it.decl->defs) {
                            for (auto &dimension : definition.dims) {
                                collectLoopPtrCandidatesExpr(dimension, iv, loopDelta,
                                                             assigned, written,
                                                             hasUnknownArrayWrite, cands, false);
                            }
                            collectLoopPtrCandidatesInit(definition.init, iv, loopDelta,
                                                         assigned, written,
                                                         hasUnknownArrayWrite, cands);
                        }
                    } else {
                        collectLoopPtrCandidatesStmt(it.stmt, iv, loopDelta, assigned,
                                                     written, hasUnknownArrayWrite, cands);
                    }
                }
                break;
            case StmtKind::Assign:
                collectLoopPtrCandidatesExpr(s->lhs, iv, loopDelta, assigned, written,
                                             hasUnknownArrayWrite, cands, true);
                collectLoopPtrCandidatesExpr(s->expr, iv, loopDelta, assigned, written,
                                             hasUnknownArrayWrite, cands, false);
                break;
            case StmtKind::Expr:
            case StmtKind::Return:
                collectLoopPtrCandidatesExpr(s->expr, iv, loopDelta, assigned, written,
                                             hasUnknownArrayWrite, cands, false);
                break;
            case StmtKind::If:
                collectLoopPtrCandidatesExpr(s->cond, iv, loopDelta, assigned, written,
                                             hasUnknownArrayWrite, cands, false);
                collectLoopPtrCandidatesStmt(s->thenStmt, iv, loopDelta, assigned,
                                             written, hasUnknownArrayWrite, cands);
                collectLoopPtrCandidatesStmt(s->elseStmt, iv, loopDelta, assigned,
                                             written, hasUnknownArrayWrite, cands);
                break;
            case StmtKind::While:
            case StmtKind::Break:
            case StmtKind::Continue:
                break;
        }
    }

    void collectLoopBaseCountsExpr(const ExprPtr &e, const string &iv, const string &ivReg,
                                   const unordered_map<string, bool> &assigned,
                                   const unordered_map<string, bool> &skipKeys,
                                   unordered_map<string, ActiveLoopBase> &cands) {
        if (!e) return;
        if (e->kind == ExprKind::LVal) {
            if (!e->indices.empty() && !lvalUsesAssignedNonIvScalar(e, iv, assigned)) {
                Type t = lvalType(e);
                int coeff = 0;
                string key = lvalKey(e);
                if (t.isScalar() && !skipKeys.count(key) &&
                    lvalIvByteCoeff(e, iv, coeff)) {
                    auto &b = cands[key];
                    if (b.count == 0) {
                        b.key = key;
                        b.lval = e;
                        b.ivReg = ivReg;
                        b.ivByteCoeff = coeff;
                    }
                    ++b.count;
                }
            }
            for (auto &idx : e->indices) collectLoopBaseCountsExpr(idx, iv, ivReg, assigned, skipKeys, cands);
            return;
        }
        if (e->kind == ExprKind::Unary) {
            collectLoopBaseCountsExpr(e->lhs, iv, ivReg, assigned, skipKeys, cands);
            return;
        }
        if (e->kind == ExprKind::Binary) {
            collectLoopBaseCountsExpr(e->lhs, iv, ivReg, assigned, skipKeys, cands);
            collectLoopBaseCountsExpr(e->rhs, iv, ivReg, assigned, skipKeys, cands);
            return;
        }
        if (e->kind == ExprKind::Call) {
            for (auto &arg : e->args) collectLoopBaseCountsExpr(arg, iv, ivReg, assigned, skipKeys, cands);
        }
    }

    void collectLoopBaseCountsStmt(const StmtPtr &s, const string &iv, const string &ivReg,
                                   const unordered_map<string, bool> &assigned,
                                   const unordered_map<string, bool> &skipKeys,
                                   unordered_map<string, ActiveLoopBase> &cands) {
        if (!s) return;
        switch (s->kind) {
            case StmtKind::Block:
                for (auto &it : s->items) {
                    if (!it.isDecl) collectLoopBaseCountsStmt(it.stmt, iv, ivReg, assigned, skipKeys, cands);
                }
                break;
            case StmtKind::Assign:
                collectLoopBaseCountsExpr(s->lhs, iv, ivReg, assigned, skipKeys, cands);
                collectLoopBaseCountsExpr(s->expr, iv, ivReg, assigned, skipKeys, cands);
                break;
            case StmtKind::Expr:
            case StmtKind::Return:
                collectLoopBaseCountsExpr(s->expr, iv, ivReg, assigned, skipKeys, cands);
                break;
            case StmtKind::If:
                collectLoopBaseCountsExpr(s->cond, iv, ivReg, assigned, skipKeys, cands);
                collectLoopBaseCountsStmt(s->thenStmt, iv, ivReg, assigned, skipKeys, cands);
                collectLoopBaseCountsStmt(s->elseStmt, iv, ivReg, assigned, skipKeys, cands);
                break;
            case StmtKind::While:
            case StmtKind::Break:
            case StmtKind::Continue:
                break;
        }
    }

    void planSimpleLoopPointers(const StmtPtr &s, vector<ActiveLoopPtr> &out) {
        string iv;
        int loopDelta = 0;
        unordered_set<string> declaredNames;
        if (!invariantLoadLoopInduction(s, iv, loopDelta, declaredNames)) return;
        unordered_map<string, bool> assigned;
        collectAssignedScalarsStmt(s->body, assigned);
        for (const string &name : declaredNames) assigned[name] = true;
        unordered_set<string> written;
        bool hasUnknownArrayWrite = false;
        collectWrittenObjectsStmt(s->body, written, hasUnknownArrayWrite);
        unordered_map<string, ActiveLoopPtr> cands;
        collectLoopPtrCandidatesStmt(s->body, iv, loopDelta, assigned, written,
                                     hasUnknownArrayWrite, cands);
        vector<ActiveLoopPtr> ranked;
        for (auto &kv : cands) ranked.push_back(kv.second);
        sort(ranked.begin(), ranked.end(), [](const ActiveLoopPtr &a, const ActiveLoopPtr &b) {
            int aScore = a.readCount * 3 + a.writeCount * 2;
            int bScore = b.readCount * 3 + b.writeCount * 2;
            if (aScore != bScore) return aScore > bScore;
            if (a.count != b.count) return a.count > b.count;
            if (a.readCount != b.readCount) return a.readCount > b.readCount;
            if (abs(a.byteStep) != abs(b.byteStep)) return abs(a.byteStep) < abs(b.byteStep);
            return a.key < b.key;
        });
        for (auto &p : ranked) {
            if (out.size() >= 2 && p.count < 2) break;
            if (out.size() >= 3) {
                int score = p.readCount * 3 + p.writeCount * 2;
                if (p.count < 2 || score < 5) break;
            }
            int got = -1;
            for (int r = 11; r >= 1; --r) {
                got = reserveSpecificLocalReg(r);
                if (got >= 0) break;
            }
            if (got < 0) break;
            p.reg = localRegName(got);
            out.push_back(p);
            if (out.size() >= 4) break;
        }
    }

    void planCountedLoopBases(const StmtPtr &s, const vector<ActiveLoopPtr> &ptrs, vector<ActiveLoopBase> &out) {
        string iv;
        int loopDelta = 0;
        unordered_set<string> declaredNames;
        if (!invariantLoadLoopInduction(s, iv, loopDelta, declaredNames)) return;
        if (loopDelta != 1 && loopDelta != -1) return;
        Symbol *ivSym = findVar(iv);
        if (!ivSym || ivSym->reg < 0) return;
        unordered_map<string, bool> assigned;
        collectAssignedScalarsStmt(s->body, assigned);
        for (const string &name : declaredNames) assigned[name] = true;
        unordered_map<string, bool> skipKeys;
        for (auto &p : ptrs) skipKeys[p.key] = true;
        unordered_map<string, ActiveLoopBase> cands;
        collectLoopBaseCountsStmt(s->body, iv, localRegName(ivSym->reg), assigned, skipKeys, cands);
        vector<ActiveLoopBase> ranked;
        for (auto &kv : cands) {
            if (kv.second.count >= 2) ranked.push_back(kv.second);
        }
        sort(ranked.begin(), ranked.end(), [](const ActiveLoopBase &a, const ActiveLoopBase &b) {
            if (a.count != b.count) return a.count > b.count;
            if (abs(a.ivByteCoeff) != abs(b.ivByteCoeff)) return abs(a.ivByteCoeff) < abs(b.ivByteCoeff);
            return a.key < b.key;
        });
        for (auto &b : ranked) {
            int got = -1;
            for (int r = 11; r >= 1; --r) {
                got = reserveSpecificLocalReg(r);
                if (got >= 0) break;
            }
            if (got < 0) break;
            b.reg = localRegName(got);
            out.push_back(b);
            break;
        }
    }

    bool sameExprLimited(const ExprPtr &a, const ExprPtr &b, int &budget) {
        if (a.get() == b.get()) return true;
        if (!a || !b) return false;
        if (--budget <= 0) return false;
        if (a->kind != b->kind || a->text != b->text) return false;
        switch (a->kind) {
            case ExprKind::IntLit:
                return a->intVal == b->intVal;
            case ExprKind::FloatLit:
                return floatBits(a->floatVal) == floatBits(b->floatVal);
            case ExprKind::StringLit:
                return a->text == b->text;
            case ExprKind::LVal:
                if (a->indices.size() != b->indices.size()) return false;
                for (size_t i = 0; i < a->indices.size(); ++i) {
                    if (!sameExprLimited(a->indices[i], b->indices[i], budget)) return false;
                }
                return true;
            case ExprKind::Unary:
                return sameExprLimited(a->lhs, b->lhs, budget);
            case ExprKind::Binary:
                return sameExprLimited(a->lhs, b->lhs, budget) &&
                       sameExprLimited(a->rhs, b->rhs, budget);
            case ExprKind::Call:
                if (a->args.size() != b->args.size()) return false;
                for (size_t i = 0; i < a->args.size(); ++i) {
                    if (!sameExprLimited(a->args[i], b->args[i], budget)) return false;
                }
                return true;
        }
        return false;
    }

    bool sameExpr(const ExprPtr &a, const ExprPtr &b) {
        int budget = 128;
        return sameExprLimited(a, b, budget);
    }

    string exprKey(const ExprPtr &e) {
        if (!e) return "#";
        switch (e->kind) {
            case ExprKind::IntLit:
                return "i" + to_string(e->intVal);
            case ExprKind::FloatLit:
                return "f" + to_string(floatBits(e->floatVal));
            case ExprKind::StringLit:
                return "s" + e->text;
            case ExprKind::LVal: {
                string k = "v" + e->text;
                for (auto &idx : e->indices) k += "[" + exprKey(idx) + "]";
                return k;
            }
            case ExprKind::Unary:
                return "u" + e->text + "(" + exprKey(e->lhs) + ")";
            case ExprKind::Binary:
                return "b" + e->text + "(" + exprKey(e->lhs) + "," + exprKey(e->rhs) + ")";
            case ExprKind::Call: {
                string k = "c" + e->text + "(";
                for (auto &arg : e->args) k += exprKey(arg) + ",";
                k += ")";
                return k;
            }
        }
        return "#";
    }

    string lvalKey(const ExprPtr &e) {
        return exprKey(e);
    }

    bool sameScalarLValExpr(const ExprPtr &a, const ExprPtr &b, Type *out = nullptr) {
        if (!a || !b || a->kind != ExprKind::LVal || b->kind != ExprKind::LVal) return false;
        if (a->text != b->text || a->indices.size() != b->indices.size()) return false;
        if (exprMayCall(a) || exprMayCall(b)) return false;
        Type ta = lvalType(a);
        Type tb = lvalType(b);
        if (!ta.isScalar() || !tb.isScalar() || ta.base != tb.base) return false;
        for (size_t i = 0; i < a->indices.size(); ++i) {
            if (!sameExpr(a->indices[i], b->indices[i])) return false;
        }
        if (out) *out = ta;
        return true;
    }

    ActiveLoadCSE *findActiveLoadCSE(const ExprPtr &e) {
        if (!e || e->kind != ExprKind::LVal || activeLoadCSE_.empty()) return nullptr;
        string key = lvalKey(e);
        for (auto it = activeLoadCSE_.rbegin(); it != activeLoadCSE_.rend(); ++it) {
            if (it->key == key && !it->loading) return &*it;
        }
        return nullptr;
    }

    bool ensureActiveCachedIntLVal(const ExprPtr &e, string &reg) {
        ActiveLoadCSE *c = findActiveLoadCSE(e);
        if (!c || c->type.base != BaseType::Int) return false;
        if (!c->loaded) {
            ExprValue v = emitLVal(e);
            if (!v.type.isScalar() || v.type.base != BaseType::Int) return false;
        }
        c = findActiveLoadCSE(e);
        if (!c || !c->loaded) return false;
        reg = c->reg;
        return true;
    }

    bool tryEmitActiveCachedMulTerm(const ExprPtr &e) {
        if (!e || e->kind != ExprKind::Binary || e->text != "*") return false;
        ExprPtr cached;
        ExprPtr other;
        string cachedReg;
        if (findActiveLoadCSE(e->lhs)) {
            cached = e->lhs;
            other = e->rhs;
        } else if (findActiveLoadCSE(e->rhs)) {
            cached = e->rhs;
            other = e->lhs;
        } else {
            return false;
        }
        if (exprMayCall(other)) return false;
        Type ot;
        if (!tryExprStaticType(other, ot) || !ot.isScalar() || ot.base != BaseType::Int) return false;
        if (!ensureActiveCachedIntLVal(cached, cachedReg)) return false;
        ExprValue ov = emitExpr(other);
        convertReg(ov.type.base, BaseType::Int);
        emit("mulw t0, " + cachedReg + ", t0");
        return true;
    }

    bool isSameScalarLVal(const ExprPtr &e, const Symbol *s) {
        return e && s && e->kind == ExprKind::LVal && e->text == s->name && e->indices.empty();
    }

    bool tryEmitRegSelfUpdate(Symbol *sym, const ExprPtr &expr) {
        if (!sym || sym->reg < 0 || !expr || expr->kind != ExprKind::Binary) return false;
        Type target = sym->type;
        if (!target.isScalar() || target.base != BaseType::Int) return false;
        if (tryEmitRegRemainderUpdate(sym, expr)) return true;
        ConstValue cv;
        string r = localRegName(sym->reg);
        if ((expr->text == "+" || expr->text == "-") &&
            isSameScalarLVal(expr->lhs, sym) &&
            tryEvalConstExpr(expr->rhs, cv) && cv.base == BaseType::Int) {
            long long delta = expr->text == "+" ? cv.i : -static_cast<long long>(cv.i);
            if (delta == 0) return true;
            if (isSimm12(delta)) emit("addiw " + r + ", " + r + ", " + to_string(delta));
            else {
                loadImm("t0", static_cast<int>(cv.i));
                if (expr->text == "+") emit("addw " + r + ", " + r + ", t0");
                else emit("subw " + r + ", " + r + ", t0");
            }
            return true;
        }
        if (expr->text == "+" &&
            isSameScalarLVal(expr->rhs, sym) &&
            tryEvalConstExpr(expr->lhs, cv) && cv.base == BaseType::Int) {
            if (cv.i == 0) return true;
            if (isSimm12(cv.i)) emit("addiw " + r + ", " + r + ", " + to_string(cv.i));
            else {
                loadImm("t0", cv.i);
                emit("addw " + r + ", " + r + ", t0");
            }
            return true;
        }

        const string &op = expr->text;
        if (!(op == "+" || op == "-" || op == "*" || op == "/" || op == "%")) {
            return false;
        }
        bool leftSame = isSameScalarLVal(expr->lhs, sym);
        bool rightSame = isSameScalarLVal(expr->rhs, sym);
        if (!leftSame && !rightSame) return false;

        ExprPtr other = leftSame ? expr->rhs : expr->lhs;
        if (!(leftSame && rightSame) && exprMayCall(other)) return false;

        if (leftSame && rightSame) {
            if (op == "+") emit("addw " + r + ", " + r + ", " + r);
            else if (op == "-") emit("subw " + r + ", " + r + ", " + r);
            else if (op == "*") emit("mulw " + r + ", " + r + ", " + r);
            else if (op == "/") emit("divw " + r + ", " + r + ", " + r);
            else emit("remw " + r + ", " + r + ", " + r);
            return true;
        }

        Type otherType;
        if (!tryExprStaticType(other, otherType) || !otherType.isScalar() ||
            otherType.base != BaseType::Int) {
            return false;
        }

        if (op == "*" && tryEvalConstExpr(other, cv) && cv.base == BaseType::Int) {
            emitMulConstInReg(r, cv.i);
            return true;
        }

        if (!tryEmitDirectIntMulTerm(other)) {
            ExprValue value = emitExpr(other);
            convertReg(value.type.base, BaseType::Int);
        }
        if (leftSame) {
            if (op == "+") emit("addw " + r + ", " + r + ", t0");
            else if (op == "-") emit("subw " + r + ", " + r + ", t0");
            else if (op == "*") emit("mulw " + r + ", " + r + ", t0");
            else if (op == "/") emit("divw " + r + ", " + r + ", t0");
            else emit("remw " + r + ", " + r + ", t0");
        } else {
            if (op == "+") emit("addw " + r + ", t0, " + r);
            else if (op == "-") emit("subw " + r + ", t0, " + r);
            else if (op == "*") emit("mulw " + r + ", t0, " + r);
            else if (op == "/") emit("divw " + r + ", t0, " + r);
            else emit("remw " + r + ", t0, " + r);
        }
        return true;
    }

    bool regScalarIntLVal(const ExprPtr &e, string &reg) {
        if (!e || e->kind != ExprKind::LVal || !e->indices.empty()) return false;
        Symbol *s = findVar(e->text);
        if (!s || s->reg < 0) return false;
        Type t = lvalType(e);
        if (!t.isScalar() || t.base != BaseType::Int) return false;
        reg = localRegName(s->reg);
        return true;
    }

    int countScalarSymbolRefs(const ExprPtr &e, const Symbol *sym) {
        if (!e || !sym) return 0;
        int count = 0;
        if (e->kind == ExprKind::LVal) {
            if (e->text == sym->name && e->indices.empty()) ++count;
            for (auto &index : e->indices) count += countScalarSymbolRefs(index, sym);
            return count;
        }
        if (e->kind == ExprKind::Unary) return countScalarSymbolRefs(e->lhs, sym);
        if (e->kind == ExprKind::Binary) {
            return countScalarSymbolRefs(e->lhs, sym) + countScalarSymbolRefs(e->rhs, sym);
        }
        if (e->kind == ExprKind::Call) {
            for (auto &argument : e->args) count += countScalarSymbolRefs(argument, sym);
        }
        return count;
    }

    bool targetSeedConstMultiplier(const ExprPtr &e, const Symbol *sym, int &multiplier) {
        if (!e || e->kind != ExprKind::Binary || e->text != "*") return false;
        ConstValue cv;
        if (isSameScalarLVal(e->lhs, sym) &&
            tryEvalConstExpr(e->rhs, cv) && cv.base == BaseType::Int) {
            multiplier = cv.i;
            return true;
        }
        if (isSameScalarLVal(e->rhs, sym) &&
            tryEvalConstExpr(e->lhs, cv) && cv.base == BaseType::Int) {
            multiplier = cv.i;
            return true;
        }
        return false;
    }

    void emitMulConstInReg(const string &reg, int c) {
        if (c == 0) {
            emit("li " + reg + ", 0");
            return;
        }
        if (c == 1) return;
        if (c == -1) {
            emit("subw " + reg + ", zero, " + reg);
            return;
        }
        long long magnitude = c < 0 ? -static_cast<long long>(c) : static_cast<long long>(c);
        if (isPow2(magnitude) && log2Int(magnitude) < 32) {
            emit("slliw " + reg + ", " + reg + ", " + to_string(log2Int(magnitude)));
            if (c < 0) emit("subw " + reg + ", zero, " + reg);
            return;
        }
        string operand = intConstOperandReg(c, "t1");
        emit("mulw " + reg + ", " + reg + ", " + operand);
    }

    void emitSignedRemainderConstInReg(const string &reg, int c) {
        int d = c < 0 ? -c : c;
        if (d == 1) {
            emit("li " + reg + ", 0");
            return;
        }
        if (isPow2(d)) {
            int shift = log2Int(d);
            emit("sraiw t1, " + reg + ", 31");
            int bias = (1 << shift) - 1;
            if (isSimm12(bias)) emit("andi t1, t1, " + to_string(bias));
            else {
                loadImm("t2", bias);
                emit("and t1, t1, t2");
            }
            emit("addw t0, " + reg + ", t1");
            emit("sraiw t0, t0, " + to_string(shift));
            emit("slliw t0, t0, " + to_string(shift));
            emit("subw " + reg + ", " + reg + ", t0");
            return;
        }

        DivMagic magic = signedDivMagicPositive(d);
        string magicReg = intConstOperandReg(magic.m, "t1");
        emit("mul t1, " + reg + ", " + magicReg);
        emit("srai t1, t1, 32");
        if (magic.m < 0) emit("addw t1, t1, " + reg);
        if (magic.shift > 0) emit("sraiw t1, t1, " + to_string(magic.shift));
        emit("sraiw t2, " + reg + ", 31");
        emit("subw t0, t1, t2");
        string divisorReg = intConstOperandReg(d, "t1");
        emit("mulw t0, t0, " + divisorReg);
        emit("subw " + reg + ", " + reg + ", t0");
    }

    bool canLoadIntLValDirectly(const ExprPtr &e) {
        if (!e || e->kind != ExprKind::LVal || exprMayCall(e)) return false;
        Type type;
        if (!tryExprStaticType(e, type) || !type.isScalar() || type.base != BaseType::Int) return false;
        if (ActiveLoadCSE *cached = findActiveLoadCSE(e)) return cached->loaded;
        return true;
    }

    void emitIntLValIntoReg(const ExprPtr &e, const string &dst) {
        if (ActiveLoadCSE *cached = findActiveLoadCSE(e)) {
            emit("mv " + dst + ", " + cached->reg);
            return;
        }
        Symbol *symbol = findVar(e->text);
        if (symbol && symbol->reg >= 0 && e->indices.empty()) {
            emit("mv " + dst + ", " + localRegName(symbol->reg));
            return;
        }
        emitLValAddress(e);
        loadInt(dst, "t0", 0);
    }

    bool tryEmitDirectIntMulTerm(const ExprPtr &e) {
        if (!e || e->kind != ExprKind::Binary || e->text != "*" || exprMayCall(e)) return false;
        Type leftType, rightType;
        if (!tryExprStaticType(e->lhs, leftType) || !tryExprStaticType(e->rhs, rightType) ||
            !leftType.isScalar() || leftType.base != BaseType::Int ||
            !rightType.isScalar() || rightType.base != BaseType::Int ||
            !canLoadIntLValDirectly(e->lhs)) {
            return false;
        }
        string existingReg;
        if (regScalarIntLVal(e->lhs, existingReg)) return false;

        string saved = acquireTempReg();
        if (saved.empty()) return false;
        emitIntLValIntoReg(e->lhs, saved);
        ExprValue right = emitExpr(e->rhs);
        convertReg(right.type.base, BaseType::Int);
        emit("mulw t0, " + saved + ", t0");
        releaseTempReg();
        return true;
    }

    bool validateRegRemainderUpdate(Symbol *sym, const ExprPtr &expr,
                                    int &divisor, vector<pair<int, ExprPtr>> &terms,
                                    int &seedMultiplier, bool &seedIsMultiply) {
        if (!sym || !expr || expr->kind != ExprKind::Binary || expr->text != "%" ||
            exprMayCall(expr->lhs)) {
            return false;
        }
        ConstValue cv;
        if (!tryEvalConstExpr(expr->rhs, cv) || cv.base != BaseType::Int ||
            cv.i == 0 || cv.i == numeric_limits<int>::min()) {
            return false;
        }
        Type numeratorType;
        if (!tryExprStaticType(expr->lhs, numeratorType) || !numeratorType.isScalar() ||
            numeratorType.base != BaseType::Int || countScalarSymbolRefs(expr->lhs, sym) != 1) {
            return false;
        }

        collectAddSubChain(expr->lhs, terms);
        if (terms.empty()) return false;
        for (auto &term : terms) {
            Type type;
            if (!tryExprStaticType(term.second, type) || !type.isScalar() ||
                type.base != BaseType::Int || exprMayCall(term.second)) {
                return false;
            }
        }

        seedIsMultiply = false;
        seedMultiplier = 1;
        if (!isSameScalarLVal(terms.front().second, sym)) {
            if (!targetSeedConstMultiplier(terms.front().second, sym, seedMultiplier)) return false;
            seedIsMultiply = true;
        }
        divisor = cv.i;
        return true;
    }

    bool tryEmitRegRemainderUpdate(Symbol *sym, const ExprPtr &expr) {
        int divisor = 0;
        int seedMultiplier = 1;
        bool seedIsMultiply = false;
        vector<pair<int, ExprPtr>> terms;
        if (!validateRegRemainderUpdate(sym, expr, divisor, terms,
                                        seedMultiplier, seedIsMultiply)) {
            return false;
        }

        string dst = localRegName(sym->reg);
        if (seedIsMultiply) emitMulConstInReg(dst, seedMultiplier);
        if (terms.front().first < 0) emit("subw " + dst + ", zero, " + dst);

        for (size_t i = 1; i < terms.size(); ++i) {
            int sign = terms[i].first;
            ExprPtr term = terms[i].second;
            ConstValue cv;
            string reg;
            if (tryEvalConstExpr(term, cv) && cv.base == BaseType::Int) {
                long long delta = sign > 0 ? cv.i : -static_cast<long long>(cv.i);
                if (delta == 0) continue;
                if (isSimm12(delta)) {
                    emit("addiw " + dst + ", " + dst + ", " + to_string(delta));
                } else {
                    string operand = intConstOperandReg(cv.i, "t1");
                    if (sign > 0) emit("addw " + dst + ", " + dst + ", " + operand);
                    else emit("subw " + dst + ", " + dst + ", " + operand);
                }
                continue;
            }
            if (regScalarIntLVal(term, reg)) {
                if (sign > 0) emit("addw " + dst + ", " + dst + ", " + reg);
                else emit("subw " + dst + ", " + dst + ", " + reg);
                continue;
            }
            if (!tryEmitDirectIntMulTerm(term)) {
                ExprValue value = emitExpr(term);
                convertReg(value.type.base, BaseType::Int);
            }
            if (sign > 0) emit("addw " + dst + ", " + dst + ", t0");
            else emit("subw " + dst + ", " + dst + ", t0");
        }

        emitSignedRemainderConstInReg(dst, divisor);
        return true;
    }

    bool tryStoreSimpleRhsAtAddress(const Type &target, const ExprPtr &rhs) {
        if (!target.isScalar() || !rhs) return false;

        string reg;
        if (regScalarIntLVal(rhs, reg)) {
            if (target.base == BaseType::Int) {
                storeInt(reg, "t0", 0);
            } else if (target.base == BaseType::Float) {
                emit("fcvt.s.w ft0, " + reg);
                storeFloat("ft0", "t0", 0);
            } else {
                return false;
            }
            return true;
        }

        ConstValue cv;
        if (!tryEvalConstExpr(rhs, cv)) return false;
        cv = convertConst(cv, target.base);
        if (target.base == BaseType::Int) {
            if (cv.i == 0) storeInt("zero", "t0", 0);
            else {
                loadImm("t1", cv.i);
                storeInt("t1", "t0", 0);
            }
            return true;
        }
        if (target.base == BaseType::Float) {
            emit("li t1, " + to_string(floatBits(cv.f)));
            emit("fmv.w.x ft0, t1");
            storeFloat("ft0", "t0", 0);
            return true;
        }
        return false;
    }

    void emitIntSameRegBinary(const string &op, const string &reg) {
        if (op == "+") emit("addw t0, " + reg + ", " + reg);
        else if (op == "-") emit("subw t0, " + reg + ", " + reg);
        else if (op == "*") emit("mulw t0, " + reg + ", " + reg);
        else if (op == "/") emit("divw t0, " + reg + ", " + reg);
        else if (op == "%") emit("remw t0, " + reg + ", " + reg);
        else if (op == "<" || op == ">") emit("slt t0, " + reg + ", " + reg);
        else if (op == "<=" || op == ">=") {
            emit("slt t0, " + reg + ", " + reg);
            emit("seqz t0, t0");
        } else if (op == "==") {
            emit("sub t0, " + reg + ", " + reg);
            emit("seqz t0, t0");
        } else if (op == "!=") {
            emit("sub t0, " + reg + ", " + reg);
            emit("snez t0, t0");
        }
    }

    void emitIntBinaryWithSavedLhs(const string &op, const string &lhsReg) {
        if (op == "+") emit("addw t0, " + lhsReg + ", t0");
        else if (op == "-") emit("subw t0, " + lhsReg + ", t0");
        else if (op == "*") emit("mulw t0, " + lhsReg + ", t0");
        else if (op == "/") emit("divw t0, " + lhsReg + ", t0");
        else if (op == "%") emit("remw t0, " + lhsReg + ", t0");
    }

    void emitIntBinaryWithSavedRhs(const string &op, const string &rhsReg) {
        if (op == "+") emit("addw t0, t0, " + rhsReg);
        else if (op == "-") emit("subw t0, t0, " + rhsReg);
        else if (op == "*") emit("mulw t0, t0, " + rhsReg);
        else if (op == "/") emit("divw t0, t0, " + rhsReg);
        else if (op == "%") emit("remw t0, t0, " + rhsReg);
    }

    bool tryEmitLValSelfUpdateAtAddress(const Type &target, const ExprPtr &lhs, const ExprPtr &rhs) {
        if (!target.isScalar() || !lhs || !rhs || rhs->kind != ExprKind::Binary) return false;
        const string &op = rhs->text;
        bool intOp = target.base == BaseType::Int &&
                     (op == "+" || op == "-" || op == "*" || op == "/" || op == "%");
        bool floatOp = target.base == BaseType::Float &&
                       (op == "+" || op == "-" || op == "*" || op == "/");
        if (!intOp && !floatOp) return false;

        Type rhsType;
        if (!tryExprStaticType(rhs, rhsType) || !rhsType.isScalar() || rhsType.base != target.base) return false;

        Type sameType;
        bool leftSame = sameScalarLValExpr(rhs->lhs, lhs, &sameType);
        bool rightSame = sameScalarLValExpr(rhs->rhs, lhs, &sameType);
        if (!leftSame && !rightSame) return false;
        if (sameType.base != target.base) return false;

        ExprPtr other = leftSame ? rhs->rhs : rhs->lhs;
        if (!(leftSame && rightSame) && exprMayCall(other)) return false;

        string addrTmp = acquireTempReg();
        if (addrTmp.empty()) return false;
        string valTmp = acquireTempReg();
        if (valTmp.empty()) {
            releaseTempReg();
            return false;
        }

        emit("mv " + addrTmp + ", t0");
        if (target.base == BaseType::Int) {
            loadInt(valTmp, addrTmp, 0);
            if (leftSame && rightSame) {
                emitIntSameRegBinary(op, valTmp);
            } else {
                ExprValue ov = emitExpr(other);
                convertReg(ov.type.base, BaseType::Int);
                if (leftSame) emitIntBinaryWithSavedLhs(op, valTmp);
                else emitIntBinaryWithSavedRhs(op, valTmp);
            }
            storeInt("t0", addrTmp, 0);
        } else {
            loadFloat("ft0", addrTmp, 0);
            emit("fmv.x.w " + valTmp + ", ft0");
            if (leftSame && rightSame) {
                emit("fmv.w.x ft0, " + valTmp);
                if (op == "+") emit("fadd.s ft0, ft0, ft0");
                else if (op == "-") emit("fsub.s ft0, ft0, ft0");
                else if (op == "*") emit("fmul.s ft0, ft0, ft0");
                else if (op == "/") emit("fdiv.s ft0, ft0, ft0");
            } else {
                ExprValue ov = emitExpr(other);
                convertReg(ov.type.base, BaseType::Float);
                emit("fmv.w.x ft1, " + valTmp);
                if (leftSame) {
                    if (op == "+") emit("fadd.s ft0, ft1, ft0");
                    else if (op == "-") emit("fsub.s ft0, ft1, ft0");
                    else if (op == "*") emit("fmul.s ft0, ft1, ft0");
                    else if (op == "/") emit("fdiv.s ft0, ft1, ft0");
                } else {
                    if (op == "+") emit("fadd.s ft0, ft0, ft1");
                    else if (op == "-") emit("fsub.s ft0, ft0, ft1");
                    else if (op == "*") emit("fmul.s ft0, ft0, ft1");
                    else if (op == "/") emit("fdiv.s ft0, ft0, ft1");
                }
            }
            storeFloat("ft0", addrTmp, 0);
        }

        releaseTempReg();
        releaseTempReg();
        return true;
    }

    void addAddressOffset(long long off) {
        if (off == 0) return;
        if (isSimm12(off)) emit("addi t0, t0, " + to_string(off));
        else {
            emit("li t6, " + to_string(off));
            emit("add t0, t0, t6");
        }
    }

    void scaleRegInPlace(const string &reg, const string &scratch, int stride) {
        if (stride == 1) return;
        if (isPow2(stride)) {
            emit("slli " + reg + ", " + reg + ", " + to_string(log2Int(stride)));
        } else if (auto bits = setBitPositions(stride); bits.size() == 2) {
            int lo = bits[0], hi = bits[1];
            emit("mv " + scratch + ", " + reg);
            emit("slli " + reg + ", " + reg + ", " + to_string(hi));
            if (lo == 0) emit("add " + reg + ", " + reg + ", " + scratch);
            else {
                emit("slli " + scratch + ", " + scratch + ", " + to_string(lo));
                emit("add " + reg + ", " + reg + ", " + scratch);
            }
        } else {
            emit("li " + scratch + ", " + to_string(stride));
            emit("mul " + reg + ", " + reg + ", " + scratch);
        }
    }

    void addScaledRegToAddress(const string &reg, int stride) {
        if (stride == 1) {
            emit("add t0, t0, " + reg);
        } else if (isPow2(stride)) {
            emit("slli t2, " + reg + ", " + to_string(log2Int(stride)));
            emit("add t0, t0, t2");
        } else if (auto bits = setBitPositions(stride); bits.size() == 2) {
            int lo = bits[0], hi = bits[1];
            emit("slli t2, " + reg + ", " + to_string(hi));
            if (lo == 0) emit("add t2, t2, " + reg);
            else {
                emit("slli t6, " + reg + ", " + to_string(lo));
                emit("add t2, t2, t6");
            }
            emit("add t0, t0, t2");
        } else {
            emit("li t2, " + to_string(stride));
            emit("mul t2, " + reg + ", t2");
            emit("add t0, t0, t2");
        }
    }

    bool collectLinearIndex(const ExprPtr &e, int sign, vector<pair<int, string>> &regs, long long &constant) {
        if (!e) return false;
        ConstValue cv;
        if (tryEvalConstExpr(e, cv) && cv.base == BaseType::Int) {
            constant += static_cast<long long>(sign) * cv.i;
            return true;
        }
        string reg;
        if (regScalarIntLVal(e, reg)) {
            regs.push_back({sign, reg});
            return true;
        }
        if (e->kind == ExprKind::Unary && e->text == "+") {
            return collectLinearIndex(e->lhs, sign, regs, constant);
        }
        if (e->kind == ExprKind::Unary && e->text == "-") {
            return collectLinearIndex(e->lhs, -sign, regs, constant);
        }
        if (e->kind == ExprKind::Binary && e->text == "+") {
            return collectLinearIndex(e->lhs, sign, regs, constant) &&
                   collectLinearIndex(e->rhs, sign, regs, constant);
        }
        if (e->kind == ExprKind::Binary && e->text == "-") {
            return collectLinearIndex(e->lhs, sign, regs, constant) &&
                   collectLinearIndex(e->rhs, -sign, regs, constant);
        }
        return false;
    }

    bool tryAddLinearIndexToAddress(const ExprPtr &idx, int stride) {
        vector<pair<int, string>> regs;
        long long constant = 0;
        if (!collectLinearIndex(idx, 1, regs, constant)) return false;
        if (regs.empty()) {
            addAddressOffset(constant * static_cast<long long>(stride));
            return true;
        }
        if (regs.size() == 1 && regs[0].first == 1 && constant == 0) {
            addScaledRegToAddress(regs[0].second, stride);
            return true;
        }

        bool have = false;
        for (auto &term : regs) {
            if (!have) {
                if (term.first >= 0) emit("mv t2, " + term.second);
                else emit("subw t2, zero, " + term.second);
                have = true;
            } else {
                if (term.first >= 0) emit("addw t2, t2, " + term.second);
                else emit("subw t2, t2, " + term.second);
            }
        }
        if (constant != 0) {
            if (isSimm12(constant)) emit("addiw t2, t2, " + to_string(constant));
            else {
                emit("li t6, " + to_string(constant));
                emit("addw t2, t2, t6");
            }
        }
        scaleRegInPlace("t2", "t6", stride);
        emit("add t0, t0, t2");
        return true;
    }

    string loadBranchOperandConst(int v, const string &tmp) {
        if (v == 0) return "zero";
        auto cached = constIntRegs_.find(v);
        if (cached != constIntRegs_.end()) return localRegName(cached->second);
        loadImm(tmp, v);
        return tmp;
    }

    void emitRelBranchFalse(const string &lhs, const string &op, const string &rhs, const string &falseL) {
        if (op == "<") emit("bge " + lhs + ", " + rhs + ", " + falseL);
        else if (op == ">") emit("bge " + rhs + ", " + lhs + ", " + falseL);
        else if (op == "<=") emit("blt " + rhs + ", " + lhs + ", " + falseL);
        else if (op == ">=") emit("blt " + lhs + ", " + rhs + ", " + falseL);
        else if (op == "==") emit("bne " + lhs + ", " + rhs + ", " + falseL);
        else if (op == "!=") emit("beq " + lhs + ", " + rhs + ", " + falseL);
        else fail(1, "bad relational branch operator");
    }

    void emitRelBranchTrue(const string &lhs, const string &op, const string &rhs, const string &trueL) {
        if (op == "<") emit("blt " + lhs + ", " + rhs + ", " + trueL);
        else if (op == ">") emit("blt " + rhs + ", " + lhs + ", " + trueL);
        else if (op == "<=") emit("bge " + rhs + ", " + lhs + ", " + trueL);
        else if (op == ">=") emit("bge " + lhs + ", " + rhs + ", " + trueL);
        else if (op == "==") emit("beq " + lhs + ", " + rhs + ", " + trueL);
        else if (op == "!=") emit("bne " + lhs + ", " + rhs + ", " + trueL);
        else fail(1, "bad relational branch operator");
    }

    bool matchPowerOfTwoRemainderZeroTest(const ExprPtr &cond, ExprPtr &value,
                                          int &mask, bool &trueOnZero) {
        if (!cond || cond->kind != ExprKind::Binary ||
            (cond->text != "==" && cond->text != "!=")) {
            return false;
        }
        auto match = [&](const ExprPtr &rem, const ExprPtr &zero) {
            ConstValue zeroValue, divisor;
            if (!rem || rem->kind != ExprKind::Binary || rem->text != "%" ||
                !tryEvalConstExpr(zero, zeroValue) || zeroValue.base != BaseType::Int || zeroValue.i != 0 ||
                !tryEvalConstExpr(rem->rhs, divisor) || divisor.base != BaseType::Int) {
                return false;
            }
            long long magnitude = divisor.i < 0 ? -static_cast<long long>(divisor.i)
                                                : static_cast<long long>(divisor.i);
            if (magnitude < 2 || magnitude > (1ll << 31) ||
                (magnitude & (magnitude - 1)) != 0) {
                return false;
            }
            value = rem->lhs;
            mask = static_cast<int>(magnitude - 1);
            trueOnZero = cond->text == "==";
            return true;
        };
        return match(cond->lhs, cond->rhs) || match(cond->rhs, cond->lhs);
    }

    bool tryEmitPowerOfTwoRemainderBranch(const ExprPtr &cond, const string &label,
                                          bool branchOnTrue) {
        ExprPtr value;
        int mask = 0;
        bool trueOnZero = false;
        if (!matchPowerOfTwoRemainderZeroTest(cond, value, mask, trueOnZero)) return false;
        ExprValue v = emitExpr(value);
        convertReg(v.type.base, BaseType::Int);
        if (isSimm12(mask)) emit("andi t0, t0, " + to_string(mask));
        else {
            loadImm("t1", mask);
            emit("and t0, t0, t1");
        }
        bool branchOnZero = trueOnZero == branchOnTrue;
        emit(string(branchOnZero ? "beqz t0, " : "bnez t0, ") + label);
        return true;
    }

    bool tryEmitBranchIfFalse(const ExprPtr &cond, const string &falseL) {
        ConstValue cv;
        if (tryEvalConstExpr(cond, cv)) {
            bool truth = cv.base == BaseType::Float ? cv.f != 0.0f : cv.i != 0;
            if (!truth) emit("j " + falseL);
            return true;
        }
        string reg;
        if (regScalarIntLVal(cond, reg)) {
            emit("beqz " + reg + ", " + falseL);
            return true;
        }
        if (cond && cond->kind == ExprKind::Unary && cond->text == "!" && regScalarIntLVal(cond->lhs, reg)) {
            emit("bnez " + reg + ", " + falseL);
            return true;
        }
        if (tryEmitPowerOfTwoRemainderBranch(cond, falseL, false)) return true;
        if (!cond || cond->kind != ExprKind::Binary) return false;
        const string &op = cond->text;
        bool rel = op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=";
        if (!rel) return false;
        Type lt, rt;
        if (!tryExprStaticType(cond->lhs, lt) || !tryExprStaticType(cond->rhs, rt)) return false;
        if (!lt.isScalar() || lt.base != BaseType::Int || !rt.isScalar() || rt.base != BaseType::Int) return false;

        string lhsReg, rhsReg;
        ConstValue rc, lc;
        if (regScalarIntLVal(cond->lhs, lhsReg) && tryEvalConstExpr(cond->rhs, rc) && rc.base == BaseType::Int) {
            rhsReg = loadBranchOperandConst(rc.i, "t0");
            emitRelBranchFalse(lhsReg, op, rhsReg, falseL);
            return true;
        }
        if (tryEvalConstExpr(cond->lhs, lc) && lc.base == BaseType::Int && regScalarIntLVal(cond->rhs, rhsReg)) {
            lhsReg = loadBranchOperandConst(lc.i, "t0");
            emitRelBranchFalse(lhsReg, op, rhsReg, falseL);
            return true;
        }
        if (regScalarIntLVal(cond->lhs, lhsReg) && regScalarIntLVal(cond->rhs, rhsReg)) {
            emitRelBranchFalse(lhsReg, op, rhsReg, falseL);
            return true;
        }
        if (regScalarIntLVal(cond->lhs, lhsReg) && !exprMayCall(cond->rhs)) {
            ExprValue rhs = emitExpr(cond->rhs);
            convertReg(rhs.type.base, BaseType::Int);
            emitRelBranchFalse(lhsReg, op, "t0", falseL);
            return true;
        }
        if (!exprMayCall(cond->lhs) && regScalarIntLVal(cond->rhs, rhsReg)) {
            ExprValue lhs = emitExpr(cond->lhs);
            convertReg(lhs.type.base, BaseType::Int);
            emitRelBranchFalse("t0", op, rhsReg, falseL);
            return true;
        }
        return false;
    }

    bool tryEmitBranchIfTrue(const ExprPtr &cond, const string &trueL) {
        ConstValue cv;
        if (tryEvalConstExpr(cond, cv)) {
            bool truth = cv.base == BaseType::Float ? cv.f != 0.0f : cv.i != 0;
            if (truth) emit("j " + trueL);
            return true;
        }
        string reg;
        if (regScalarIntLVal(cond, reg)) {
            emit("bnez " + reg + ", " + trueL);
            return true;
        }
        if (cond && cond->kind == ExprKind::Unary && cond->text == "!" && regScalarIntLVal(cond->lhs, reg)) {
            emit("beqz " + reg + ", " + trueL);
            return true;
        }
        if (tryEmitPowerOfTwoRemainderBranch(cond, trueL, true)) return true;
        if (!cond || cond->kind != ExprKind::Binary) return false;
        const string &op = cond->text;
        bool rel = op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=";
        if (!rel) return false;
        Type lt, rt;
        if (!tryExprStaticType(cond->lhs, lt) || !tryExprStaticType(cond->rhs, rt)) return false;
        if (!lt.isScalar() || lt.base != BaseType::Int || !rt.isScalar() || rt.base != BaseType::Int) return false;

        string lhsReg, rhsReg;
        ConstValue rc, lc;
        if (regScalarIntLVal(cond->lhs, lhsReg) && tryEvalConstExpr(cond->rhs, rc) && rc.base == BaseType::Int) {
            rhsReg = loadBranchOperandConst(rc.i, "t0");
            emitRelBranchTrue(lhsReg, op, rhsReg, trueL);
            return true;
        }
        if (tryEvalConstExpr(cond->lhs, lc) && lc.base == BaseType::Int && regScalarIntLVal(cond->rhs, rhsReg)) {
            lhsReg = loadBranchOperandConst(lc.i, "t0");
            emitRelBranchTrue(lhsReg, op, rhsReg, trueL);
            return true;
        }
        if (regScalarIntLVal(cond->lhs, lhsReg) && regScalarIntLVal(cond->rhs, rhsReg)) {
            emitRelBranchTrue(lhsReg, op, rhsReg, trueL);
            return true;
        }
        if (regScalarIntLVal(cond->lhs, lhsReg) && !exprMayCall(cond->rhs)) {
            ExprValue rhs = emitExpr(cond->rhs);
            convertReg(rhs.type.base, BaseType::Int);
            emitRelBranchTrue(lhsReg, op, "t0", trueL);
            return true;
        }
        if (!exprMayCall(cond->lhs) && regScalarIntLVal(cond->rhs, rhsReg)) {
            ExprValue lhs = emitExpr(cond->lhs);
            convertReg(lhs.type.base, BaseType::Int);
            emitRelBranchTrue("t0", op, rhsReg, trueL);
            return true;
        }
        return false;
    }

    bool tryEmitLoopPtrAddress(const ExprPtr &e) {
        if (!e || e->kind != ExprKind::LVal || activeLoopPtrs_.empty()) return false;
        string key = lvalKey(e);
        for (auto pit = activeLoopPtrs_.rbegin(); pit != activeLoopPtrs_.rend(); ++pit) {
            for (auto &p : *pit) {
                if (p.key == key) {
                    emit("mv t0, " + p.reg);
                    return true;
                }
            }
        }
        return false;
    }

    void emitLValAddressNoLoopPtr(const ExprPtr &e) {
        Symbol *s = findVar(e->text);
        if (!s) fail(e->line, "undefined variable " + e->text);
        if (s->reg >= 0 && e->indices.empty()) fail(e->line, "cannot take register scalar address");
        if (s->isGlobal) {
            auto cached = globalBaseRegs_.find(s->name);
            if (cached != globalBaseRegs_.end()) emit("mv t0, " + localRegName(cached->second));
            else emit("la t0, " + s->label);
        }
        else if (s->isParamArray) loadWord64("t0", "s0", s->offset);
        else addrOffset("t0", "s0", s->offset);

        long long constByteOff = 0;
        size_t firstDynamic = 0;
        for (; firstDynamic < e->indices.size(); ++firstDynamic) {
            ConstValue cv;
            if (!tryEvalConstExpr(e->indices[firstDynamic], cv) || cv.base != BaseType::Int) break;
            int stride = productTail(s->type.dims, firstDynamic + 1) * elemSize(s->type.base);
            constByteOff += static_cast<long long>(cv.i) * stride;
        }
        if (constByteOff != 0) addrOffset("t0", "t0", static_cast<int>(constByteOff));
        if (firstDynamic == e->indices.size()) return;

        for (size_t i = 0; i < e->indices.size(); ++i) {
            if (i < firstDynamic) continue;
            int stride = productTail(s->type.dims, i + 1) * elemSize(s->type.base);
            if (tryAddLinearIndexToAddress(e->indices[i], stride)) continue;
            string baseReg;
            bool acquiredBaseReg = false;
            if (!exprMayCall(e->indices[i])) {
                baseReg = acquireTempReg();
                acquiredBaseReg = !baseReg.empty();
                if (baseReg.empty() && !exprHasCall(e->indices[i])) baseReg = "t3";
            }
            if (!baseReg.empty()) emit("mv " + baseReg + ", t0");
            else pushInt("t0");
            ExprValue idx = emitExpr(e->indices[i]);
            convertReg(idx.type.base, BaseType::Int);
            if (!baseReg.empty()) {
                addScaledIndexToBase(baseReg, stride);
                if (acquiredBaseReg) releaseTempReg();
            }
            else {
                popInt("t1");
                addScaledIndexToBase("t1", stride);
            }
        }
    }

    void emitLValAddress(const ExprPtr &e) {
        if (tryEmitLoopPtrAddress(e)) return;
        if (tryEmitLoopBaseAddress(e)) return;
        emitLValAddressNoLoopPtr(e);
    }

    ExprValue emitExpr(const ExprPtr &e) {
        ConstValue cv;
        if (tryEvalConstExpr(e, cv)) return emitConstValue(cv);
        if (tempRegDepth_ == 0 && activeLoadCSE_.empty() && !exprMayCall(e)) {
            ActiveLoadCSE c;
            if (planExprLoadCSE(e, c)) {
                string reg = acquireTempReg();
                if (!reg.empty()) {
                    c.reg = reg;
                    activeLoadCSE_.push_back(c);
                    ExprValue v = emitExpr(e);
                    activeLoadCSE_.pop_back();
                    releaseTempReg();
                    return v;
                }
            }
        }
        ExprValue flat;
        if (emitFlatAddSubExpr(e, flat)) return flat;
        switch (e->kind) {
            case ExprKind::IntLit: {
                emit("li t0, " + to_string(static_cast<int>(e->intVal)));
                Type t; t.base = BaseType::Int; return {t};
            }
            case ExprKind::FloatLit: {
                emit("li t0, " + to_string(floatBits(e->floatVal)));
                emit("fmv.w.x ft0, t0");
                Type t; t.base = BaseType::Float; return {t};
            }
            case ExprKind::StringLit:
                fail(e->line, "string literal is only supported as putf's first argument");
            case ExprKind::LVal: return emitLVal(e);
            case ExprKind::Unary: return emitUnary(e);
            case ExprKind::Binary: return emitBinary(e);
            case ExprKind::Call: return emitCall(e->text, e->args, e->line);
        }
        fail(e->line, "bad expression");
    }

    ExprValue emitLVal(const ExprPtr &e) {
        Type t = lvalType(e);
        if (ActiveLoadCSE *c = findActiveLoadCSE(e)) {
            if (c->loaded) {
                emit("mv t0, " + c->reg);
                return {t};
            }
            c->loading = true;
            emitLValAddress(e);
            if (t.isArray()) {
                c->loading = false;
                return {t};
            }
            if (t.base == BaseType::Float) loadFloat("ft0", "t0", 0);
            else loadInt("t0", "t0", 0);
            if (t.base == BaseType::Int) {
                emit("mv " + c->reg + ", t0");
                c->loaded = true;
            }
            c->loading = false;
            return {t};
        }
        Symbol *s = findVar(e->text);
        if (s && s->reg >= 0 && e->indices.empty()) {
            emit("mv t0, " + localRegName(s->reg));
            return {t};
        }
        emitLValAddress(e);
        if (t.isArray()) return {t};
        if (t.base == BaseType::Float) loadFloat("ft0", "t0", 0);
        else loadInt("t0", "t0", 0);
        return {t};
    }

    ExprValue emitUnary(const ExprPtr &e) {
        ExprValue v = emitExpr(e->lhs);
        if (e->text == "+") return v;
        if (e->text == "-") {
            if (v.type.base == BaseType::Float) emit("fneg.s ft0, ft0");
            else emit("subw t0, zero, t0");
            return v;
        }
        if (e->text == "!") {
            toBool(v);
            emit("seqz t0, t0");
            Type t; t.base = BaseType::Int; return {t, true};
        }
        fail(e->line, "bad unary operator");
    }

    bool tryEmitRegRegIntBinary(const ExprPtr &e, bool rel, ExprValue &out) {
        string lhsReg, rhsReg;
        if (!regScalarIntLVal(e->lhs, lhsReg) || !regScalarIntLVal(e->rhs, rhsReg)) return false;
        const string &op = e->text;
        if (op == "+") emit("addw t0, " + lhsReg + ", " + rhsReg);
        else if (op == "-") emit("subw t0, " + lhsReg + ", " + rhsReg);
        else if (op == "*") emit("mulw t0, " + lhsReg + ", " + rhsReg);
        else if (op == "/") emit("divw t0, " + lhsReg + ", " + rhsReg);
        else if (op == "%") emit("remw t0, " + lhsReg + ", " + rhsReg);
        else if (op == "<") emit("slt t0, " + lhsReg + ", " + rhsReg);
        else if (op == ">") emit("slt t0, " + rhsReg + ", " + lhsReg);
        else if (op == "<=") { emit("slt t0, " + rhsReg + ", " + lhsReg); emit("seqz t0, t0"); }
        else if (op == ">=") { emit("slt t0, " + lhsReg + ", " + rhsReg); emit("seqz t0, t0"); }
        else if (op == "==") { emit("sub t0, " + lhsReg + ", " + rhsReg); emit("seqz t0, t0"); }
        else if (op == "!=") { emit("sub t0, " + lhsReg + ", " + rhsReg); emit("snez t0, t0"); }
        else return false;
        Type t; t.base = BaseType::Int;
        out = {t, rel};
        return true;
    }

    bool tryEmitSameLValBinary(const ExprPtr &e, bool rel, ExprValue &out) {
        Type t;
        if (!sameScalarLValExpr(e->lhs, e->rhs, &t)) return false;
        const string &op = e->text;
        if (t.base == BaseType::Int) {
            if (!(op == "+" || op == "-" || op == "*" || op == "/" || op == "%" ||
                  op == "<" || op == ">" || op == "<=" || op == ">=" ||
                  op == "==" || op == "!=")) {
                return false;
            }
            ExprValue v = emitLVal(e->lhs);
            convertReg(v.type.base, BaseType::Int);
            emitIntSameRegBinary(op, "t0");
            Type rt; rt.base = rel ? BaseType::Int : BaseType::Int;
            out = {rt, rel};
            return true;
        }
        if (t.base == BaseType::Float) {
            if (!(op == "+" || op == "-" || op == "*" || op == "/" ||
                  op == "<" || op == ">" || op == "<=" || op == ">=" ||
                  op == "==" || op == "!=")) {
                return false;
            }
            emitLVal(e->lhs);
            if (op == "+") emit("fadd.s ft0, ft0, ft0");
            else if (op == "-") emit("fsub.s ft0, ft0, ft0");
            else if (op == "*") emit("fmul.s ft0, ft0, ft0");
            else if (op == "/") emit("fdiv.s ft0, ft0, ft0");
            else if (op == "<" || op == ">") emit("flt.s t0, ft0, ft0");
            else if (op == "<=" || op == ">=") emit("fle.s t0, ft0, ft0");
            else if (op == "==") emit("feq.s t0, ft0, ft0");
            else if (op == "!=") {
                emit("feq.s t0, ft0, ft0");
                emit("seqz t0, t0");
            }
            Type rt;
            rt.base = rel ? BaseType::Int : BaseType::Float;
            out = {rt, rel};
            return true;
        }
        return false;
    }

    ExprValue emitBinary(const ExprPtr &e) {
        if (e->text == "&&") return emitLogicalAnd(e);
        if (e->text == "||") return emitLogicalOr(e);
        bool rel = e->text == "==" || e->text == "!=" || e->text == "<" || e->text == ">" || e->text == "<=" || e->text == ">=";
        ConstValue cv;
        Type lt, rt;
        ExprValue fast;
        if (tryEmitRegRegIntBinary(e, rel, fast)) return fast;
        if (tryEmitSameLValBinary(e, rel, fast)) return fast;
        if (tryEvalConstExpr(e->rhs, cv) && cv.base == BaseType::Int) {
            if (tryExprStaticType(e->lhs, lt) && lt.isScalar() && lt.base == BaseType::Int) {
                ExprValue lv = emitExpr(e->lhs);
                convertReg(lv.type.base, BaseType::Int);
                emitIntRightConstOp(e->text, cv.i);
                Type t; t.base = BaseType::Int; return {t, rel};
            }
        }
        if (tryEvalConstExpr(e->lhs, cv) && cv.base == BaseType::Int) {
            if (tryExprStaticType(e->rhs, rt) && rt.isScalar() && rt.base == BaseType::Int) {
                ExprValue rv = emitExpr(e->rhs);
                convertReg(rv.type.base, BaseType::Int);
                emitIntLeftConstOp(e->text, cv.i);
                Type t; t.base = BaseType::Int; return {t, rel};
            }
        }
        string lhsReg;
        if (regScalarIntLVal(e->lhs, lhsReg) &&
            tryExprStaticType(e->rhs, rt) && rt.isScalar() && rt.base == BaseType::Int &&
            !exprMayCall(e->rhs)) {
            ExprValue rv = emitExpr(e->rhs);
            convertReg(rv.type.base, BaseType::Int);
            emitIntRegOp(e->text, lhsReg);
            Type t; t.base = BaseType::Int; return {t, rel};
        }
        if (tryExprStaticType(e->lhs, lt) && tryExprStaticType(e->rhs, rt) &&
            lt.isScalar() && lt.base == BaseType::Int &&
            rt.isScalar() && rt.base == BaseType::Int &&
            !exprMayCall(e->rhs)) {
            string tmp = acquireTempReg();
            if (!tmp.empty()) {
                ExprValue lv = emitExpr(e->lhs);
                convertReg(lv.type.base, BaseType::Int);
                emit("mv " + tmp + ", t0");
                ExprValue rv = emitExpr(e->rhs);
                convertReg(rv.type.base, BaseType::Int);
                emitIntRegOp(e->text, tmp);
                releaseTempReg();
                Type t; t.base = BaseType::Int; return {t, rel};
            }
        }
        if (tryExprStaticType(e->lhs, lt) && tryExprStaticType(e->rhs, rt) &&
            lt.isScalar() && rt.isScalar() &&
            (lt.base == BaseType::Float || rt.base == BaseType::Float) &&
            !exprMayCall(e->rhs)) {
            string tmp = acquireFloatTempReg();
            if (!tmp.empty()) {
                ExprValue lv = emitExpr(e->lhs);
                convertReg(lv.type.base, BaseType::Float);
                emit("fmv.s " + tmp + ", ft0");
                ExprValue rv = emitExpr(e->rhs);
                convertReg(rv.type.base, BaseType::Float);
                if (rel) {
                    if (e->text == "<") emit("flt.s t0, " + tmp + ", ft0");
                    else if (e->text == ">") emit("flt.s t0, ft0, " + tmp);
                    else if (e->text == "<=") emit("fle.s t0, " + tmp + ", ft0");
                    else if (e->text == ">=") emit("fle.s t0, ft0, " + tmp);
                    else if (e->text == "==") emit("feq.s t0, " + tmp + ", ft0");
                    else if (e->text == "!=") { emit("feq.s t0, " + tmp + ", ft0"); emit("seqz t0, t0"); }
                    else {
                        releaseFloatTempReg();
                        fail(e->line, "bad float relational operator " + e->text);
                    }
                    releaseFloatTempReg();
                    Type t; t.base = BaseType::Int; return {t, true};
                }
                if (e->text == "+") emit("fadd.s ft0, " + tmp + ", ft0");
                else if (e->text == "-") emit("fsub.s ft0, " + tmp + ", ft0");
                else if (e->text == "*") emit("fmul.s ft0, " + tmp + ", ft0");
                else if (e->text == "/") emit("fdiv.s ft0, " + tmp + ", ft0");
                else {
                    releaseFloatTempReg();
                    fail(e->line, "invalid float operator " + e->text);
                }
                releaseFloatTempReg();
                Type t; t.base = BaseType::Float; return {t};
            }
        }
        ExprValue lv = emitExpr(e->lhs);
        pushValue(lv);
        ExprValue rv = emitExpr(e->rhs);
        bool fp = (lv.type.isScalar() && lv.type.base == BaseType::Float) || (rv.type.isScalar() && rv.type.base == BaseType::Float);
        if (fp) {
            if (rv.type.base != BaseType::Float) convertReg(rv.type.base, BaseType::Float);
            if (lv.type.base == BaseType::Float) popFloat("ft1");
            else {
                popInt("t1");
                emit("fcvt.s.w ft1, t1");
            }
            if (rel) {
                if (e->text == "<") emit("flt.s t0, ft1, ft0");
                else if (e->text == ">") emit("flt.s t0, ft0, ft1");
                else if (e->text == "<=") emit("fle.s t0, ft1, ft0");
                else if (e->text == ">=") emit("fle.s t0, ft0, ft1");
                else if (e->text == "==") emit("feq.s t0, ft1, ft0");
                else { emit("feq.s t0, ft1, ft0"); emit("seqz t0, t0"); }
                Type t; t.base = BaseType::Int; return {t, true};
            }
            if (e->text == "+") emit("fadd.s ft0, ft1, ft0");
            else if (e->text == "-") emit("fsub.s ft0, ft1, ft0");
            else if (e->text == "*") emit("fmul.s ft0, ft1, ft0");
            else if (e->text == "/") emit("fdiv.s ft0, ft1, ft0");
            else fail(e->line, "invalid float operator " + e->text);
            Type t; t.base = BaseType::Float; return {t};
        }
        popInt("t1");
        if (rel) {
            if (e->text == "<") emit("slt t0, t1, t0");
            else if (e->text == ">") emit("slt t0, t0, t1");
            else if (e->text == "<=") { emit("slt t0, t0, t1"); emit("seqz t0, t0"); }
            else if (e->text == ">=") { emit("slt t0, t1, t0"); emit("seqz t0, t0"); }
            else if (e->text == "==") { emit("sub t0, t1, t0"); emit("seqz t0, t0"); }
            else { emit("sub t0, t1, t0"); emit("snez t0, t0"); }
            Type t; t.base = BaseType::Int; return {t, true};
        }
        if (e->text == "+") emit("addw t0, t1, t0");
        else if (e->text == "-") emit("subw t0, t1, t0");
        else if (e->text == "*") emit("mulw t0, t1, t0");
        else if (e->text == "/") emit("divw t0, t1, t0");
        else if (e->text == "%") emit("remw t0, t1, t0");
        else fail(e->line, "bad binary operator " + e->text);
        Type t; t.base = BaseType::Int; return {t};
    }

    ExprValue emitLogicalAnd(const ExprPtr &e) {
        string falseL = newLabel("land_false"), endL = newLabel("land_end");
        ExprValue a = emitExpr(e->lhs);
        toBool(a);
        emit("beqz t0, " + falseL);
        ExprValue b = emitExpr(e->rhs);
        toBool(b);
        emit("beqz t0, " + falseL);
        emit("li t0, 1");
        emit("j " + endL);
        emitLabel(falseL);
        emit("li t0, 0");
        emitLabel(endL);
        Type t; t.base = BaseType::Int; return {t, true};
    }

    ExprValue emitLogicalOr(const ExprPtr &e) {
        string trueL = newLabel("lor_true"), endL = newLabel("lor_end");
        ExprValue a = emitExpr(e->lhs);
        toBool(a);
        emit("bnez t0, " + trueL);
        ExprValue b = emitExpr(e->rhs);
        toBool(b);
        emit("bnez t0, " + trueL);
        emit("li t0, 0");
        emit("j " + endL);
        emitLabel(trueL);
        emit("li t0, 1");
        emitLabel(endL);
        Type t; t.base = BaseType::Int; return {t, true};
    }

    void toBool(const ExprValue &v) {
        if (v.knownBool && v.type.isScalar() && v.type.base == BaseType::Int) {
            return;
        }
        if (v.type.isScalar() && v.type.base == BaseType::Float) {
            emit("li t0, 0");
            emit("fmv.w.x ft1, t0");
            emit("feq.s t0, ft0, ft1");
            emit("seqz t0, t0");
        } else {
            emit("snez t0, t0");
        }
    }

    void convertReg(BaseType from, BaseType to) {
        if (from == to) return;
        if (from == BaseType::Int && to == BaseType::Float) emit("fcvt.s.w ft0, t0");
        else if (from == BaseType::Float && to == BaseType::Int) emit("fcvt.w.s t0, ft0, rtz");
    }

    ExprValue emitCall(const string &name, const vector<ExprPtr> &args, int line) {
        if (name == "putf") return emitPutf(args, line);
        if (name == "starttime" || name == "stoptime") {
            string real = name == "starttime" ? "_sysy_starttime" : "_sysy_stoptime";
            auto lit = make_shared<Expr>();
            lit->kind = ExprKind::IntLit;
            lit->line = line;
            lit->intVal = line;
            vector<ExprPtr> a{lit};
            return emitNormalCall(real, a, line);
        }
        return emitNormalCall(name, args, line);
    }

    bool tryEmitDirectNoCallArgs(const string &name, const vector<ExprPtr> &args, int line,
                                 FuncInfo &fi, ExprValue &out) {
        vector<Type> argTypes;
        vector<Type> wants;
        argTypes.reserve(args.size());
        wants.reserve(args.size());
        for (size_t i = 0; i < args.size(); ++i) {
            if (exprMayCall(args[i])) return false;
            Type vt;
            if (!tryExprStaticType(args[i], vt)) return false;
            Type want = i < fi.params.size() ? fi.params[i] : vt;
            Type finalType = vt;
            if (want.isScalar() && finalType.isScalar()) finalType.base = want.base;
            if (finalType.isVoid()) return false;
            wants.push_back(want);
            argTypes.push_back(finalType);
        }

        int stackBytes = 0;
        vector<AbiLoc> locs = abiLayout(argTypes, stackBytes);
        addSp(-stackBytes);

        for (size_t i = 0; i < args.size(); ++i) {
            ExprValue v = emitExpr(args[i]);
            Type want = wants[i];
            if (want.isScalar() && v.type.isScalar()) {
                convertReg(v.type.base, want.base);
                v.type.base = want.base;
            }
            AbiLoc l = locs[i];
            bool fp = v.type.isScalar() && v.type.base == BaseType::Float;
            if (l.inReg) {
                if (fp) emit("fmv.s fa" + to_string(l.idx) + ", ft0");
                else emit("mv a" + to_string(l.idx) + ", t0");
            } else {
                if (fp) storeFloat("ft0", "sp", l.stackOff);
                else storeWord64("t0", "sp", l.stackOff);
            }
        }

        emit("call " + name);
        addSp(stackBytes);
        Type ret;
        ret.base = fi.ret;
        if (fi.ret == BaseType::Float) emit("fmv.s ft0, fa0");
        else if (fi.ret == BaseType::Int) emit("mv t0, a0");
        out = {ret};
        return true;
    }

    ExprValue emitNormalCall(const string &name, const vector<ExprPtr> &args, int line) {
        auto fit = funcs_.find(name);
        if (fit == funcs_.end()) fail(line, "undefined function " + name);
        FuncInfo &fi = fit->second;
        ExprValue fast;
        if (tryEmitDirectNoCallArgs(name, args, line, fi, fast)) return fast;
        vector<Type> argTypes;
        for (size_t i = 0; i < args.size(); ++i) {
            ExprValue v = emitExpr(args[i]);
            Type want = i < fi.params.size() ? fi.params[i] : v.type;
            if (want.isScalar() && v.type.isScalar()) {
                convertReg(v.type.base, want.base);
                v.type.base = want.base;
            }
            argTypes.push_back(v.type);
            pushValue(v);
        }
        int stackBytes = 0;
        vector<AbiLoc> locs = abiLayout(argTypes, stackBytes);
        addSp(-stackBytes);
        int n = static_cast<int>(args.size());
        for (int i = n - 1; i >= 0; --i) {
            int tempOff = stackBytes + 16 * (n - 1 - i) + 8;
            AbiLoc l = locs[i];
            bool fp = argTypes[i].isScalar() && argTypes[i].base == BaseType::Float;
            if (l.inReg) {
                if (fp) loadFloat("fa" + to_string(l.idx), "sp", tempOff);
                else loadWord64("a" + to_string(l.idx), "sp", tempOff);
            } else {
                if (fp) {
                    loadFloat("ft0", "sp", tempOff);
                    storeFloat("ft0", "sp", l.stackOff);
                } else {
                    loadWord64("t0", "sp", tempOff);
                    storeWord64("t0", "sp", l.stackOff);
                }
            }
        }
        emit("call " + name);
        addSp(stackBytes + 16 * n);
        Type ret;
        ret.base = fi.ret;
        if (fi.ret == BaseType::Float) emit("fmv.s ft0, fa0");
        else if (fi.ret == BaseType::Int) emit("mv t0, a0");
        return {ret};
    }

    ExprValue emitPutf(const vector<ExprPtr> &args, int line) {
        if (args.empty() || args[0]->kind != ExprKind::StringLit) fail(line, "putf requires a format string");
        const string &fmt = args[0]->text;
        size_t ai = 1;
        for (size_t i = 0; i < fmt.size(); ++i) {
            if (fmt[i] == '%' && i + 1 < fmt.size()) {
                char spec = fmt[++i];
                if (ai >= args.size() && spec != '%') fail(line, "not enough putf arguments");
                if (spec == 'd') {
                    ExprValue v = emitExpr(args[ai++]);
                    convertReg(v.type.base, BaseType::Int);
                    emit("mv a0, t0");
                    emit("call putint");
                } else if (spec == 'c') {
                    ExprValue v = emitExpr(args[ai++]);
                    convertReg(v.type.base, BaseType::Int);
                    emit("mv a0, t0");
                    emit("call putch");
                } else if (spec == 'f') {
                    ExprValue v = emitExpr(args[ai++]);
                    convertReg(v.type.base, BaseType::Float);
                    emit("fmv.s fa0, ft0");
                    emit("call putfloat");
                } else if (spec == '%') {
                    emit("li a0, 37");
                    emit("call putch");
                } else {
                    emit("li a0, " + to_string(static_cast<unsigned char>(spec)));
                    emit("call putch");
                }
            } else {
                emit("li a0, " + to_string(static_cast<unsigned char>(fmt[i])));
                emit("call putch");
            }
        }
        Type t; t.base = BaseType::Void; return {t};
    }
};

static string readFile(const string &path) {
    ifstream in(path, ios::binary);
    if (!in) throw runtime_error("cannot open input file: " + path);
    ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void writeFile(const string &path, const string &data) {
    ofstream out(path, ios::binary);
    if (!out) throw runtime_error("cannot open output file: " + path);
    out << data;
}

int main(int argc, char **argv) {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    string input, output;
    for (int i = 1; i < argc; ++i) {
        string a = argv[i];
        if (a == "-o") {
            if (i + 1 >= argc) {
                cerr << "missing argument after -o\n";
                return 1;
            }
            output = argv[++i];
        } else if (a == "-S" || a == "-O1" || a == "-O0") {
            continue;
        } else if (!a.empty() && a[0] == '-') {
            continue;
        } else {
            input = a;
        }
    }
    if (input.empty() || output.empty()) {
        cerr << "usage: compiler -S -o output.s input.sy [-O1]\n";
        return 1;
    }
    try {
        bool traceStages = getenv("SYSYC_TRACE_STAGES") != nullptr;
        auto stageStart = chrono::steady_clock::now();
        auto traceStage = [&](const char *name) {
            if (!traceStages) return;
            auto now = chrono::steady_clock::now();
            double seconds = chrono::duration<double>(now - stageStart).count();
            cerr << "[stage] " << name << ": " << fixed << setprecision(6) << seconds << "s\n";
            stageStart = now;
        };
        string src = readFile(input);
        Lexer lex(src);
        Parser parser(lex.lexAll());
        Program prog = parser.parseProgram();
        traceStage("parse");
        sysyc::SemanticProgram semantics = sysyc::SemanticAnalyzer(prog).analyze();
        traceStage("semantic");
        sysyc::ir::IRModule irModule = sysyc::ir::IRBuilder(prog, semantics).build();
        traceStage("ir-build");
        sysyc::ir::IRVerifier::verify(irModule);
        traceStage("ir-verify");
        CodeGen cg(std::move(prog));
        writeFile(output, cg.generate());
        traceStage("legacy-codegen");
    } catch (const exception &e) {
        cerr << "compiler error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
