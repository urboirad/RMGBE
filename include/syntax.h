#ifndef SYNTAX_H
#define SYNTAX_H

typedef enum {
    TOK_DEFAULT,
    TOK_KEYWORD,
    TOK_TYPE,
    TOK_STRING,
    TOK_NUMBER,
    TOK_COMMENT,
    TOK_PREPROC,
    TOK_OPERATOR,
} TokenType;

typedef struct {
    const char *start;
    int         len;
    TokenType   type;
} Token;

// State persists across lines for multi-line comments
typedef struct {
    int in_block_comment;
} SyntaxState;

// Tokenize a line and write tokens into `out` (max `max_tokens`).
// Returns number of tokens written. Updates `state` across lines.
int syntax_tokenize(const char *line, Token *out, int max_tokens, SyntaxState *state);

// Draw a line with syntax highlighting at (x, y).
void draw_text_highlighted(const char *line, float x, float y,
                           SyntaxState *state);

#endif
