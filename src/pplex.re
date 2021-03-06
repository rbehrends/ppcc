// vim:set ft=cpp:

#include "adlib/lib.h"
#include "adlib/set.h"
// "fixstr.h" must be included before "map.h"
#include "fixstr.h"
#include "adlib/map.h"

#include "pplex.h"

const char *SymbolNames[] = {
  "SymNone",
  "SymClass",
  "SymNamespace",
  "SymExtern",
  "SymVAR",
  "SymEXTERN_VAR",
  "SymSTATIC_VAR",
  "SymINST_VAR",
  "SymEXTERN_INST_VAR",
  "SymSTATIC_INST_VAR",
  "SymIdent",
  "SymLiteral",
  "SymOp",
  "SymSemicolon",
  "SymComma",
  "SymEqual",
  "SymAst",
  "SymAnd",
  "SymAndAnd",
  "SymLPar",
  "SymRPar",
  "SymLBrkt",
  "SymRBrkt",
  "SymLBrace",
  "SymRBrace",
  "SymWS",
  "SymEOL",
  "SymComment",
  "SymBAD",
  "SymLineDir",
  "SymEOF",
  "SymGen",
};

typedef Map<FixStr, Str *> InternMap;

Str *Intern(const char *ptr, Int len) {
  static InternMap *map = NULL;
  if (!map) {
    GCVar(map, new InternMap());
  }
  FixStr fs;
  fs.str = ptr;
  fs.len = len;
  Str *result = map->get(fs, NULL);
  if (!result) {
    result = new Str(ptr, len);
    // `ptr` above is an interior pointer whose contents may disappear
    // due to GC once the `SourceFile` object containing it is no
    // longer reachable.
    //
    // Therefore, we replace it with `result->c_str()`. This is not only
    // not an interior pointer, but is kept alive by the value that the
    // key is in use for.
    fs.str = result->c_str();
    map->add(fs, result);
  }
  return result;
}

#define PUSH_TOKEN(s) \
  token.sym = s; \
  goto pushtoken;

bool Tokenize(SourceFile *source) {
  Str *input = source->filedata;
  const char *cursor = input->c_str();
  const char *marker = NULL;
  const char *ctxmarker = NULL;
  bool done = false;
  bool error = false;
  TokenList *result = new TokenList();
  Token token;
  while (!done) {
    const char *last = cursor;
    /*!re2c
    re2c:define:YYCTYPE = "unsigned char";
    re2c:yyfill:enable = 0;
    re2c:define:YYCURSOR = cursor;
    re2c:define:YYMARKER = marker;
    re2c:define:YYCTXMARKER = ctxmarker;

    alpha = [a-zA-Z_];
    digit = [0-9];
    oct = [0-7];
    hex = [0-9a-fA-F];
    floatsuffix = [fFlL]?;
    intsuffix = [uUlL]*;
    exp = 'e' [-+]? digit+;
    squote = ['];
    quote = ["];
    any = [^\000\r\n];
    anyunescaped = [^\000\r\n\\];
    sp = [ \t\f];
    eol = [\000\r\n];
    nl = "\r" | "\n" | "\r\n";
    postpparg = [^a-zA-Z0-9_\r\n\000];
    ppany = anyunescaped | ("\\" sp* nl);
    pparg = (postpparg ppany *)?;
    anystr = any \ ["\\];
    anych = any \ ['\\];
    longops = "..." | ">>=" | "<<=" | "+=" | "-=" | "*=" | "/=" | "%="
            | "&=" | "^=" | "|=" | ">>" | "<<" | "++" | "--" | "->"
            | "&&" | "||" | "<=" | ">=" | "==" | "!=";
    esc = "\\";

    "class"         { PUSH_TOKEN(SymClass); }
    "namespace"     { PUSH_TOKEN(SymNamespace); }
    "extern"        { PUSH_TOKEN(SymExtern); }
    "VAR"           { PUSH_TOKEN(SymVAR); }
    "EXTERN_VAR"    { PUSH_TOKEN(SymEXTERN_VAR); }
    "STATIC_VAR"    { PUSH_TOKEN(SymSTATIC_VAR); }
    "INST_VAR"      { PUSH_TOKEN(SymINST_VAR); }
    "EXTERN_INST_VAR" { PUSH_TOKEN(SymEXTERN_INST_VAR); }
    "STATIC_INST_VAR" { PUSH_TOKEN(SymSTATIC_INST_VAR); }
    alpha (alpha | digit)* { PUSH_TOKEN(SymIdent); }
    '0x' hex+ intsuffix { PUSH_TOKEN(SymLiteral); }
    '0' oct+ intsuffix { PUSH_TOKEN(SymLiteral); }
    digit+ intsuffix { PUSH_TOKEN(SymLiteral); }
    "L"? squote (esc any anych* | anych) squote { PUSH_TOKEN(SymLiteral); }
    "L"? quote (esc any | anystr)* quote { PUSH_TOKEN(SymLiteral); }
    digit+ exp floatsuffix { PUSH_TOKEN(SymLiteral); }
    digit* "." digit+ exp? floatsuffix { PUSH_TOKEN(SymLiteral); }
    digit+ "." digit* exp? floatsuffix { PUSH_TOKEN(SymLiteral); }
    "(" { PUSH_TOKEN(SymLPar); }
    ")" { PUSH_TOKEN(SymRPar); }
    "[" { PUSH_TOKEN(SymLBrkt); }
    "]" { PUSH_TOKEN(SymRBrkt); }
    "{" { PUSH_TOKEN(SymLBrace); }
    "}" { PUSH_TOKEN(SymRBrace); }
    "=" { PUSH_TOKEN(SymEqual); }
    "," { PUSH_TOKEN(SymComma); }
    ";" { PUSH_TOKEN(SymSemicolon); }
    "&" { PUSH_TOKEN(SymAnd); }
    "&&" { PUSH_TOKEN(SymAndAnd); }
    "*" { PUSH_TOKEN(SymAst); }
    [-.&!~+*%/<>^|?:=,] { PUSH_TOKEN(SymOp); }
    longops { PUSH_TOKEN(SymOp); }
    ";" { PUSH_TOKEN(SymSemicolon); }
    "//" any+ { PUSH_TOKEN(SymComment); }
    "/" "*" { goto comment; }
    nl { PUSH_TOKEN(SymEOL); }
    "\\" sp* / nl { PUSH_TOKEN(SymWS); }
    sp+ { PUSH_TOKEN(SymWS); }
    "#" sp* digit+ "\"" anystr* "\"" (sp | digit)* {
      PUSH_TOKEN(SymLineDir);
    }
    "\000" { done = true; continue; }
    any { error = true; PUSH_TOKEN(SymBAD); }
    * { done = true; continue; }
    */
    comment:
    /*!re2c
    "*" "/" { PUSH_TOKEN(SymComment); }
    [^\000] { goto comment; }
    "\000" { done = true; PUSH_TOKEN(SymComment); }
    */
    pushtoken:
      token.str = Intern(last, cursor - last);
      result->add(token);
  }
  token.sym = SymEOF;
  token.str = S("");
  result->add(token);
  source->tokens = result;
  return !error;
}

SourceFile *ReadSource(Str *filename, Str *filedata) {
  SourceFile *result = new SourceFile();
  result->filename = filename;
  Str *modulename = filename->clone();
  for (Int i = 0; i < modulename->len(); i++) {
    char ch = modulename->at(i);
    if (ch >= 'a' && ch <= 'z') continue;
    if (ch >= 'A' && ch <= 'Z') continue;
    if (ch >= '0' && ch <= '9') continue;
    if (ch == '_') continue;
    modulename->at(i) = '_';
  }
  result->modulename = modulename;
  if (!filedata)
    filedata = ReadFile(filename);
  result->filedata = filedata;
  if (!result->filedata)
    return NULL;
  Tokenize(result);
  return result;
}
