#include "syntax.h"
#include "text_renderer.h"
#include <string.h>
#include <ctype.h>

// ---- Colors -----------------------------------------------------------------
#define CLR_DEFAULT_R  224/255.0f
#define CLR_DEFAULT_G  224/255.0f
#define CLR_DEFAULT_B  224/255.0f

#define CLR_KEYWORD_R   86/255.0f
#define CLR_KEYWORD_G  156/255.0f
#define CLR_KEYWORD_B  214/255.0f

#define CLR_TYPE_R      78/255.0f
#define CLR_TYPE_G     201/255.0f
#define CLR_TYPE_B     176/255.0f

#define CLR_STRING_R   209/255.0f
#define CLR_STRING_G   154/255.0f
#define CLR_STRING_B   102/255.0f

#define CLR_NUMBER_R   209/255.0f
#define CLR_NUMBER_G   154/255.0f
#define CLR_NUMBER_B   102/255.0f

#define CLR_COMMENT_R  106/255.0f
#define CLR_COMMENT_G  106/255.0f
#define CLR_COMMENT_B  106/255.0f

#define CLR_PREPROC_R  198/255.0f
#define CLR_PREPROC_G  120/255.0f
#define CLR_PREPROC_B  221/255.0f

#define CLR_OPERATOR_R  86/255.0f
#define CLR_OPERATOR_G 156/255.0f
#define CLR_OPERATOR_B 214/255.0f

// ---- Keywords ---------------------------------------------------------------
static const char *c_keywords[] = {
    "if", "else", "for", "while", "do", "switch", "case", "default",
    "break", "continue", "return", "goto",
    "sizeof", "typedef", "struct", "union", "enum", "extern", "static",
    "const", "volatile", "register", "auto",
    "void", "NULL", "true", "false",
    NULL
};

static const char *c_types[] = {
    "int", "char", "short", "long", "float", "double", "unsigned", "signed",
    "bool", "size_t", "ssize_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "int8_t", "int16_t", "int32_t", "int64_t",
    "GLuint", "GLint", "GLfloat", "GLsizei", "GLchar", "GLenum", "GLboolean",
    "HANDLE", "DWORD", "BOOL", "LPVOID", "HRESULT",
    NULL
};

static int match_word(const char *word, int len, const char **list) {
    for (int i = 0; list[i]; i++) {
        int klen = (int)strlen(list[i]);
        if (len == klen && memcmp(word, list[i], len) == 0) return 1;
    }
    return 0;
}

// ---- Tokenizer --------------------------------------------------------------
int syntax_tokenize(const char *line, Token *out, int max_tokens, SyntaxState *state) {
    int count = 0;
    const char *p = line;

    if (state->in_block_comment) {
        const char *end = strstr(p, "*/");
        if (end) {
            if (count < max_tokens) {
                out[count].start = p;
                out[count].len   = (int)(end + 2 - p);
                out[count].type  = TOK_COMMENT;
                count++;
            }
            p = end + 2;
            state->in_block_comment = 0;
        } else {
            if (count < max_tokens) {
                int slen = (int)strlen(p);
                out[count].start = p;
                out[count].len   = slen;
                out[count].type  = TOK_COMMENT;
                count++;
            }
            return count;
        }
    }

    while (*p && count < max_tokens) {
        if (*p == ' ' || *p == '\t') {
            const char *ws = p;
            while (*p == ' ' || *p == '\t') p++;
            if (count < max_tokens) {
                out[count].start = ws;
                out[count].len   = (int)(p - ws);
                out[count].type  = TOK_DEFAULT;
                count++;
            }
            continue;
        }

        if (*p == '#' && (p == line || *(p-1) == '\n')) {
            const char *start = p;
            while (*p && *p != '\n') p++;
            if (count < max_tokens) {
                out[count].start = start;
                out[count].len   = (int)(p - start);
                out[count].type  = TOK_PREPROC;
                count++;
            }
            continue;
        }

        if (p[0] == '/' && p[1] == '/') {
            if (count < max_tokens) {
                out[count].start = p;
                out[count].len   = (int)strlen(p);
                out[count].type  = TOK_COMMENT;
                count++;
            }
            p = p + strlen(p);
            continue;
        }

        if (p[0] == '/' && p[1] == '*') {
            const char *start = p;
            const char *end = strstr(p + 2, "*/");
            if (end) {
                if (count < max_tokens) {
                    out[count].start = start;
                    out[count].len   = (int)(end + 2 - start);
                    out[count].type  = TOK_COMMENT;
                    count++;
                }
                p = end + 2;
            } else {
                if (count < max_tokens) {
                    out[count].start = p;
                    out[count].len   = (int)strlen(p);
                    out[count].type  = TOK_COMMENT;
                    count++;
                }
                state->in_block_comment = 1;
                p = p + strlen(p);
            }
            continue;
        }

        if (*p == '"' || *p == '\'') {
            char quote = *p;
            const char *start = p;
            p++;
            while (*p && *p != '\n') {
                if (*p == '\\') { p += 2; continue; }
                if (*p == quote) { p++; break; }
                p++;
            }
            if (count < max_tokens) {
                out[count].start = start;
                out[count].len   = (int)(p - start);
                out[count].type  = TOK_STRING;
                count++;
            }
            continue;
        }

        if (isdigit((unsigned char)*p) || (*p == '.' && isdigit((unsigned char)p[1]))) {
            const char *start = p;
            if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
                p += 2;
                while (isxdigit((unsigned char)*p)) p++;
            } else {
                while (isdigit((unsigned char)*p)) p++;
                if (*p == '.') { p++; while (isdigit((unsigned char)*p)) p++; }
                if (*p == 'e' || *p == 'E') {
                    p++;
                    if (*p == '+' || *p == '-') p++;
                    while (isdigit((unsigned char)*p)) p++;
                }
            }
            while (*p == 'u' || *p == 'U' || *p == 'l' || *p == 'L' || *p == 'f' || *p == 'F') p++;
            if (count < max_tokens) {
                out[count].start = start;
                out[count].len   = (int)(p - start);
                out[count].type  = TOK_NUMBER;
                count++;
            }
            continue;
        }

        if (isalpha((unsigned char)*p) || *p == '_') {
            const char *start = p;
            while (isalnum((unsigned char)*p) || *p == '_') p++;
            int len = (int)(p - start);
            TokenType type = TOK_DEFAULT;
            if (match_word(start, len, c_keywords))   type = TOK_KEYWORD;
            else if (match_word(start, len, c_types)) type = TOK_TYPE;
            if (count < max_tokens) {
                out[count].start = start;
                out[count].len   = len;
                out[count].type  = type;
                count++;
            }
            continue;
        }

        {
            const char *start = p;
            p++;
            if (count < max_tokens) {
                out[count].start = start;
                out[count].len   = 1;
                out[count].type  = TOK_OPERATOR;
                count++;
            }
        }
    }

    return count;
}

// ---- Drawing ----------------------------------------------------------------
static void token_color(TokenType type, float *cr, float *cg, float *cb) {
    switch (type) {
        case TOK_KEYWORD:  *cr = CLR_KEYWORD_R;  *cg = CLR_KEYWORD_G;  *cb = CLR_KEYWORD_B;  return;
        case TOK_TYPE:     *cr = CLR_TYPE_R;     *cg = CLR_TYPE_G;     *cb = CLR_TYPE_B;     return;
        case TOK_STRING:   *cr = CLR_STRING_R;   *cg = CLR_STRING_G;   *cb = CLR_STRING_B;   return;
        case TOK_NUMBER:   *cr = CLR_NUMBER_R;   *cg = CLR_NUMBER_G;   *cb = CLR_NUMBER_B;   return;
        case TOK_COMMENT:  *cr = CLR_COMMENT_R;  *cg = CLR_COMMENT_G;  *cb = CLR_COMMENT_B;  return;
        case TOK_PREPROC:  *cr = CLR_PREPROC_R;  *cg = CLR_PREPROC_G;  *cb = CLR_PREPROC_B;  return;
        case TOK_OPERATOR: *cr = CLR_OPERATOR_R; *cg = CLR_OPERATOR_G; *cb = CLR_OPERATOR_B; return;
        default:           *cr = CLR_DEFAULT_R;  *cg = CLR_DEFAULT_G;  *cb = CLR_DEFAULT_B;  return;
    }
}

void draw_text_highlighted(const char *line, float x, float y,
                           SyntaxState *state) {
    Token tokens[512];
    int n = syntax_tokenize(line, tokens, 512, state);

    for (int i = 0; i < n; i++) {
        Token *t = &tokens[i];
        float cr, cg, cb;
        token_color(t->type, &cr, &cg, &cb);

        char buf[512];
        int len = t->len < 511 ? t->len : 511;
        memcpy(buf, t->start, len);
        buf[len] = '\0';

        draw_text(buf, x, y, cr, cg, cb);
        x += (float)len * text_char_width();
    }
}
