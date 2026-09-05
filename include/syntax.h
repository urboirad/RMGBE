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

// Tracks whether we're inside a /* */ block comment across lines.
typedef struct SyntaxState {
    int in_block_comment;
} SyntaxState;

// Break a line into tokens (keywords, strings, comments, etc.).
// Writes up to max_tokens into `out`. Updates state for the next line.
int syntax_tokenize(const char *line, Token *out, int max_tokens, SyntaxState *state);

// Get theme color for a token type.
void token_color(TokenType type, float *r, float *g, float *b);

// Draw a line with syntax-highlighted colors at (x, y).
void draw_text_highlighted(const char *line, float x, float y,
                           SyntaxState *state);

#endif
