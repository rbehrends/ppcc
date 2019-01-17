#include "adlib/lib.h"
#include "adlib/map.h"
#include "pplex.h"

#ifdef GCC
#define noinline __attribute__((noinlinex))
#else
#define noinline
#endif

typedef Map<Str *, Str *> Dict;

static Token space, semicolon, eof, static_token, asterisk;
static Str *decl_var;
static Str *decl_extern_var;
static Str *decl_static_var;
static StrArr *init_list;
static Dict *class_vars;
static Dict *class_types;

INIT(DeclParser, {
  GCVar(space, Token(SymWS, S(" ")));
  GCVar(semicolon, Token(SymWS, S(";\n")));
  GCVar(eof, Token(SymEOF, S("")));
  GCVar(asterisk, Token(SymAst, S("*")));
  GCVar(static_token, Token(SymIdent, S("static")));
  GCVar(decl_var, S("__thread"));
  GCVar(decl_extern_var, S("extern __thread"));
  GCVar(decl_static_var, S("static __thread"));
  GCVar(init_list, A());
  GCVar(class_vars, new Dict());
  GCVar(class_types, new Dict());
});

struct State {
  Int pos, marker;
};

class Parser : public GC {
public:
  TokenList *input, *output;
  Int pos, marker;
  noinline Parser(TokenList *_input) {
    if (_input->len() == 0 || _input->last().sym != SymEOF)
      _input->add(eof);
    input = _input;
    output = new TokenList();
    pos = 0;
    marker = 0;
  }
  void skip_until(Word64 syms) {
    syms |= BIT(SymEOF);
    while (!TEST(syms, input->at(pos).sym)) {
      pos++;
    }
  }
  void skip_while(Word64 syms) {
    while (TEST(syms, input->at(pos).sym)) {
      pos++;
    }
  }
  noinline Int find_back_until(Word64 syms) {
    Int p = pos;
    while (p > marker) {
      p--;
      if (TEST(syms, input->at(p).sym))
        return p;
    }
    return p;
  }
  noinline void safe_skip_until(Word64 syms) {
    // handle parentheses and brackets properly.
    Int par_level = 0;
    Int brkt_level = 0;
    Int brace_level = 0;
    for (;;) {
      Symbol sym = input->at(pos).sym;
      switch (sym) {
        case SymEOF:
          return;
        case SymLPar:
          par_level++;
          break;
        case SymRPar:
          par_level--;
          break;
        case SymLBrkt:
          brkt_level++;
          break;
        case SymRBrkt:
          brkt_level--;
          break;
        case SymLBrace:
          brace_level++;
          break;
        case SymRBrace:
          brace_level--;
          break;
        default:
          if (par_level + brkt_level + brace_level > 0)
            break;
          if (TEST(syms, sym)) {
            return;
          }
          break;
      }
      advance();
    }
  }
  void skipWhiteSpace() {
    while (BIT(input->at(pos).sym) & SymsWS) {
      pos++;
    }
  }
  void advance() {
    if (pos < input->len())
      pos++;
  }
  Token &token(Int i) {
    return input->at(i);
  }
  TokenList *tokenRange(Int start, Int end) {
    return input->subarr(start, end - start);
  }
  Token &current() {
    return input->at(pos);
  }
  Symbol current_sym() {
    return input->at(pos).sym;
  }
  void emit(Token token) {
    output->add(token);
  }
  void mark() {
    marker = pos;
  }
  Int current_pos() {
    return pos;
  }
  Int markerPos() {
    return marker;
  }
  noinline void push_marked_until(Int p) {
    while (marker < p) {
      output->add(input->at(marker));
      marker++;
    }
  }
  noinline void emit_range(Int start, Int end) {
    while (start < end) {
      output->add(input->at(start));
      start++;
    }
  }
  void emit_tokens(TokenList *tokens) {
    output->add(tokens);
  }
  void push_marked() {
    push_marked_until(pos);
  }
  State save() {
    State result;
    result.pos = pos;
    result.marker = marker;
    return result;
  }
  void restore(State state) {
    pos = state.pos;
    marker = state.marker;
  }
};

void EmitDecl(Parser *parser, Str *storage_class,
    Int type_start, Int type_end,
    Int var_start, Int var_end,
    Int init_start, Int init_end, Int var_pos,
    bool is_class, bool is_extern) {
  parser->emit(Token(SymGen, storage_class));
  parser->emit(space);
  parser->emit_range(type_start, type_end);
  parser->emit(space);
  if (is_class)
    parser->emit(asterisk);
  Str *var_name = parser->token(var_pos).str;
  parser->emit_range(var_start, var_pos);
  parser->emit(Token(SymGen, var_name));
  parser->emit_range(var_pos+1, var_end);
  parser->emit(semicolon);
  if (is_class) {
    class_vars->add(var_name, S("(*")->add(var_name)->add(")"));
    class_types->add(var_name, parser->token(type_start).str);
  }
  if ((is_class && !is_extern) || init_start >= 0) {
    init_list->add(var_name);
    Token var_init = Token(SymGen, var_name->clone()->add("__INIT__"));
    parser->emit(static_token);
    parser->emit(space);
    parser->emit_range(type_start, type_end);
    parser->emit(space);
    parser->emit_range(var_start, var_pos);
    parser->emit(var_init);
    parser->emit_range(var_pos+1, var_end);
    if (init_start >= 0) {
      parser->emit(space);
      parser->emit_range(init_start, init_end);
    }
    parser->emit(semicolon);
  }
}

void EmitEpilogue(Parser *parser, SourceFile *source) {
  if (init_list->len() == 0)
    return;
  Str *modulename = source->modulename;
  int c_source = source->filename->ends_with(".c");
  TokenList *output = parser->output;
  for (Int i = 0; i < output->len(); i++) {
    Token &token = output->at(i);
    if (token.sym == SymIdent && class_vars->contains(token.str)) {
      token.str = class_vars->at(token.str);
    }
  }
  Str *init_part;
  if (c_source) {
    init_part = S("\n"
      "void pSingular_init_var(const void *s, const void *t, long n);\n"
      "void *pSingular_alloc_var(long n);\n"
      "void pSingular_register_init(void (*f)());\n"
      "static void pSingular_mod_init() {\n"
      );

  } else {
    init_part = S("\n"
      "extern \"C\" {\n"
      "void pSingular_init_var(const void *s, const void *t, long n);\n"
      "void *pSingular_alloc_var(long n);\n"
      "void pSingular_register_init(void (*f)());\n"
      "}\n"
      "static void pSingular_mod_init() {\n"
      );
  }
  for (Int i = 0; i < init_list->len(); i++) {
    Str *var_name = init_list->at(i);
    if (class_vars->contains(var_name)) {
      Str *type = class_types->at(var_name);
      init_part->add(S(
        "  %s = (%c *)pSingular_alloc_var((long)sizeof(%c));\n"
        "  pSingular_init_var(%s, &%s__INIT__, (long) sizeof(%s));\n"
      )->replace_all(S("%c"), type)->replace_all(S("%s"), var_name));
    } else {
      init_part->add(
        S("  pSingular_init_var(&%s, &%s__INIT__, (long) sizeof(%s));\n")
        ->replace_all(S("%s"), var_name)
      );
    }
  }
  init_part->add("}\n");
  parser->emit(Token(SymGen, init_part));
  Str *init_rest;
  if (c_source) {
    init_rest = S(
      "__attribute__((constructor))"
      "static void pSingular_init_%s(void) {\n"
      "  pSingular_register_init(pSingular_mod_init);\n"
      "}\n"
    );
  } else {
    init_rest = S(
      "static struct pSingular_Init_%s {\n"
      "  pSingular_Init_%s() {\n"
      "    pSingular_register_init(pSingular_mod_init);\n"
      "  }\n"
      "} pSingular_init_%s;\n"
    );
  }
  init_rest = init_rest->replace_all(S("%s"), modulename);
  parser->emit(Token(SymGen, init_rest));
}

void TransformVarDecl(Parser *parser, Str *storage_class,
    bool is_class = false, bool is_extern = false) {
  // We rewrite: VAR type a, b = init, c;
  // as:
  // storage_class type a;
  // storage_class type b;
  // static type b__INIT__;
  // storage_class type c;
  // Init(b, init);
  State saved = parser->save();
  Int special_pos = parser->current_pos();
  parser->current().str = storage_class; // rewrite contents
  parser->advance(); // skip past special token
  parser->skip_while(SymsWS);
  Int type_start = parser->current_pos();
  parser->advance();
  parser->skip_while(SymsTypePrefix | SymsWS);
  Int type_end = -1;
  for(;;) {
    Int var_pos = -1, var_start = -1, var_end = -1;
    if (TEST(SymsEndDecl | BIT(SymLBrkt), parser->current_sym())) {
      // We are one symbol past the initial variable identifier.
      var_pos = parser->find_back_until(BIT(SymIdent));
      var_start = var_pos;
      var_end = var_pos + 1;
    } else {
      // We have a function pointer declaration
      State tmp = parser->save();
      var_start = parser->current_pos();
      parser->skip_until(BIT(SymIdent));
      if (parser->current_sym() == SymIdent)
        var_pos = parser->current_pos();
      parser->restore(tmp);
      parser->skip_until(SymsEndDecl);
      var_end = parser->current_pos();
    }
    if (type_end < 0) type_end = var_start;
    Int init_start = -1, init_end = -1;
    if (parser->current_sym() == SymEqual) {
      init_start = parser->current_pos();
      parser->advance();
      parser->safe_skip_until(BIT(SymComma) | BIT(SymSemicolon));
      init_end = parser->current_pos();
    }
    switch (parser->current_sym()) {
      case SymComma:
        EmitDecl(parser, storage_class,
          type_start, type_end, var_start, var_end,
          init_start, init_end, var_pos, is_class, is_extern);
        parser->advance();
        break;
      case SymSemicolon:
        EmitDecl(parser, storage_class,
          type_start, type_end, var_start, var_end,
          init_start, init_end, var_pos, is_class, is_extern);
        parser->advance();
        parser->mark();
        return;
      case SymEOF:
        return; // error
      default:
        assert(0, "exhaustive switch hits default case");
        return;
    }
  }
}

TokenList *Transform(SourceFile *source) {
  Parser *parser = new Parser(source->tokens);
  while (parser->current().sym != SymEOF) {
    parser->skip_until(SymsSpecial);
    parser->push_marked();
    switch (parser->current().sym) {
      case SymVAR:
        TransformVarDecl(parser, decl_var);
        break;
      case SymEXTERN_VAR:
        parser->current().str = decl_extern_var;
        parser->advance();
        parser->push_marked();
        break;
      case SymSTATIC_VAR:
        TransformVarDecl(parser, decl_static_var);
        break;
      case SymINST_VAR:
        TransformVarDecl(parser, decl_var, true);
        break;
      case SymEXTERN_INST_VAR:
        TransformVarDecl(parser, decl_extern_var, true, true);
        break;
      case SymSTATIC_INST_VAR:
        TransformVarDecl(parser, decl_static_var, true);
        break;
      case SymEOF:
        break;
      default:
        assert(0, "exhaustive switch hits default case");
        return NULL;
    }
  }
  EmitEpilogue(parser, source);
  return parser->output;
}

Str *TestPreProcessor(Str *filename) {
  SourceFile *source = ReadSource(filename);
  if (!source) {
    return S("ERROR: File not found: ")->add(filename)->add("\n");
  }
  TokenList *tokens = Transform(source);
  Str *result = new Str();
  for (Int i = 0; i < tokens->len(); i++) {
    result->add(tokens->at(i).str);
  }
  return result;
}

Str *RunPreProcessor(Str *filename, Str *filedata) {
  SourceFile *source = ReadSource(filename, filedata);
  TokenList *tokens = Transform(source);
  Str *result = new Str();  for (Int i = 0; i < tokens->len(); i++) {
    result->add(tokens->at(i).str);
  }
  return result;
}