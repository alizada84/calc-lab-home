// Ibrahim Alizada <251ADB242>
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used: AI (Google Gemini)
//   - Prompt: "how to structure a recursive descent parser for correct associativity in C without flex/bison"
//   - Gemini provided the core function structure (expr/term/power/primary) and file buffer sizing approach.
//   - I adapted the token struct and manual float/integer math logic to strictly match grading rules.

#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

#define STUDENT_FIRST "ibrahim"
#define STUDENT_LAST "alizada"
#define STUDENT_ID "251adb242"

/* =====================================================================
 * 1. Tokenizer (Lexer)
 * =====================================================================
 * HOW POSITIONS ARE TRACKED: 
 * The file is loaded completely into memory as a flat character array (`text`).
 * `lex->pos` is a 0-based index. Whenever we identify a token, we set its 
 * 1-based position as `lex->pos + 1`. This naturally counts every \n, space, 
 * and comment character globally, perfectly satisfying the grading rule.
 */

typedef enum {
    TOK_INT, TOK_FLOAT, TOK_PLUS, TOK_MINUS, TOK_MUL, TOK_DIV, TOK_POW,
    TOK_LPAREN, TOK_RPAREN, TOK_NEWLINE, TOK_EOF, TOK_ERROR
} TokenType;

typedef struct {
    bool is_float;
    union {
        long long i;
        double f;
    };
} Value;

typedef struct {
    TokenType type;
    long long token_pos; // 1-based exact character position
    Value val;
} Token;

typedef struct {
    const char *text;
    long long len;
    long long pos;       // 0-based internal index
    Token current;
    long long error_pos; // If > 0, an error occurred here
} Lexer;

void set_error(Lexer *lex, long long pos) {
    if (lex->error_pos == 0) { // Only record the first error
        lex->error_pos = pos;
    }
}

void next_token(Lexer *lex) {
    if (lex->error_pos) return;

    // Skip whitespace, but don't skip newlines
    while (lex->pos < lex->len) {
        char c = lex->text[lex->pos];
        if (c == ' ' || c == '\t' || c == '\r') {
            lex->pos++;
        } else {
            break;
        }
    }

    lex->current.token_pos = lex->pos + 1;

    if (lex->pos >= lex->len) {
        lex->current.type = TOK_EOF;
        return;
    }

    char c = lex->text[lex->pos];

    // Handle comments (#)
    if (c == '#') {
        bool first_nonblank = true;
        for (long long i = lex->pos - 1; i >= 0; i--) {
            if (lex->text[i] == '\n') break;
            if (lex->text[i] != ' ' && lex->text[i] != '\t' && lex->text[i] != '\r') {
                first_nonblank = false;
                break;
            }
        }
        if (first_nonblank) {
            // Skip to newline or EOF
            while (lex->pos < lex->len && lex->text[lex->pos] != '\n') {
                lex->pos++;
            }
            // recursively get next token
            next_token(lex);
            return;
        } else {
            set_error(lex, lex->pos + 1); // # anywhere else is an invalid char
            return;
        }
    }

    if (c == '\n') {
        lex->current.type = TOK_NEWLINE;
        lex->pos++;
        return;
    }

    if (isdigit(c)) {
        long long start_pos = lex->pos;
        bool is_flt = false;
        
        while (lex->pos < lex->len && isdigit(lex->text[lex->pos])) lex->pos++;
        
        if (lex->pos < lex->len && lex->text[lex->pos] == '.') {
            is_flt = true;
            long long dot_pos = lex->pos;
            lex->pos++;
            
            if (lex->pos >= lex->len || !isdigit(lex->text[lex->pos])) {
                set_error(lex, dot_pos + 1); // Error at the '.' if no digits follow
                return;
            }
            while (lex->pos < lex->len && isdigit(lex->text[lex->pos])) lex->pos++;
        }
        
        if (lex->pos < lex->len && (lex->text[lex->pos] == 'e' || lex->text[lex->pos] == 'E')) {
            set_error(lex, lex->pos + 1); // e or E is strictly prohibited
            return;
        }

        long long len = lex->pos - start_pos;
        char buf[256];
        if (len > 255) len = 255;
        strncpy(buf, lex->text + start_pos, len);
        buf[len] = '\0';

        if (is_flt) {
            lex->current.type = TOK_FLOAT;
            lex->current.val.is_float = true;
            lex->current.val.f = strtod(buf, NULL);
        } else {
            lex->current.type = TOK_INT;
            lex->current.val.is_float = false;
            lex->current.val.i = strtoll(buf, NULL, 10);
        }
        return;
    }

    switch (c) {
        case '+': lex->current.type = TOK_PLUS; lex->pos++; break;
        case '-': lex->current.type = TOK_MINUS; lex->pos++; break;
        case '/': lex->current.type = TOK_DIV; lex->pos++; break;
        case '(': lex->current.type = TOK_LPAREN; lex->pos++; break;
        case ')': lex->current.type = TOK_RPAREN; lex->pos++; break;
        case '*':
            if (lex->pos + 1 < lex->len && lex->text[lex->pos + 1] == '*') {
                lex->current.type = TOK_POW;
                lex->pos += 2;
            } else {
                lex->current.type = TOK_MUL;
                lex->pos++;
            }
            break;
        default:
            set_error(lex, lex->pos + 1);
            lex->pos++;
            break;
    }
}

/* =====================================================================
 * 2. Math Operations
 * =====================================================================
 * INTEGER VS FLOAT ARITHMETIC:
 * A simple boolean (`is_float`) is passed alongside the union value. If either 
 * operand in a binary operation is marked float, we cast both to `double` and do 
 * float arithmetic. Otherwise, we do `long long` integer math.
 */

Value do_add(Value a, Value b, long long op_pos, Lexer *lex) {
    Value res;
    res.is_float = a.is_float || b.is_float;
    if (res.is_float) {
        double fa = a.is_float ? a.f : (double)a.i;
        double fb = b.is_float ? b.f : (double)b.i;
        res.f = fa + fb;
        if (isinf(res.f) || isnan(res.f)) set_error(lex, op_pos);
    } else {
        res.i = a.i + b.i;
    }
    return res;
}

Value do_sub(Value a, Value b, long long op_pos, Lexer *lex) {
    Value res;
    res.is_float = a.is_float || b.is_float;
    if (res.is_float) {
        double fa = a.is_float ? a.f : (double)a.i;
        double fb = b.is_float ? b.f : (double)b.i;
        res.f = fa - fb;
        if (isinf(res.f) || isnan(res.f)) set_error(lex, op_pos);
    } else {
        res.i = a.i - b.i;
    }
    return res;
}

Value do_mul(Value a, Value b, long long op_pos, Lexer *lex) {
    Value res;
    res.is_float = a.is_float || b.is_float;
    if (res.is_float) {
        double fa = a.is_float ? a.f : (double)a.i;
        double fb = b.is_float ? b.f : (double)b.i;
        res.f = fa * fb;
        if (isinf(res.f) || isnan(res.f)) set_error(lex, op_pos);
    } else {
        res.i = a.i * b.i;
    }
    return res;
}

Value do_div(Value a, Value b, long long op_pos, Lexer *lex) {
    Value res;
    res.is_float = a.is_float || b.is_float;
    if (res.is_float) {
        double fa = a.is_float ? a.f : (double)a.i;
        double fb = b.is_float ? b.f : (double)b.i;
        res.f = fa / fb;
        if (isinf(res.f) || isnan(res.f)) set_error(lex, op_pos);
    } else {
        res.i = a.i / b.i; // integer division naturally truncates towards zero in C
    }
    return res;
}

Value do_pow(Value a, Value b, long long op_pos, Lexer *lex) {
    Value res;
    res.is_float = a.is_float || b.is_float;
    if (res.is_float) {
        double fa = a.is_float ? a.f : (double)a.i;
        double fb = b.is_float ? b.f : (double)b.i;
        res.f = pow(fa, fb);
        if (isinf(res.f) || isnan(res.f)) set_error(lex, op_pos);
    } else {
        res.i = 1;
        long long base = a.i;
        long long exp = b.i;
        while (exp > 0) {
            if (exp % 2 == 1) res.i *= base;
            base *= base;
            exp /= 2;
        }
    }
    return res;
}

/* =====================================================================
 * 3. Parser (Recursive Descent)
 * =====================================================================
 * RIGHT PRECEDENCE & ASSOCIATIVITY: 
 * Left-associative rules (+, -, *, /) are handled using a `while` loop, allowing 
 * successive tokens to overwrite `left` linearly. 
 * Right-associative rules (**) are handled natively by recursively calling `parse_power` 
 * inside itself before executing the operation `do_pow`.
 * ERROR POSITIONS:
 * If an expected token is missing, the lexer naturally flags the unexpected token's position.
 */

Value parse_expr(Lexer *lex);

Value parse_primary(Lexer *lex) {
    Value val = {0, {0}};
    if (lex->error_pos) return val;

    if (lex->current.type == TOK_INT || lex->current.type == TOK_FLOAT) {
        val = lex->current.val;
        next_token(lex);
        return val;
    } else if (lex->current.type == TOK_LPAREN) {
        next_token(lex);
        val = parse_expr(lex);
        if (lex->error_pos) return val;
        
        if (lex->current.type != TOK_RPAREN) {
            set_error(lex, lex->current.token_pos); // missing or extra parenthesis
            return val;
        }
        next_token(lex);
        return val;
    } else {
        // Unexpected token. Captures unary -, double operators, and empty operands.
        set_error(lex, lex->current.token_pos);
        return val;
    }
}

Value parse_power(Lexer *lex) {
    Value left = parse_primary(lex);
    if (lex->error_pos) return left;

    if (lex->current.type == TOK_POW) {
        long long op_pos = lex->current.token_pos;
        next_token(lex);
        long long right_pos = lex->current.token_pos;
        
        Value right = parse_power(lex); // Right-associativity via recursion!
        if (lex->error_pos) return left;
        
        if (!right.is_float && right.i < 0) {
            set_error(lex, right_pos); // negative int exp is invalid
            return left;
        }
        left = do_pow(left, right, op_pos, lex);
    }
    return left;
}

Value parse_term(Lexer *lex) {
    Value left = parse_power(lex);
    if (lex->error_pos) return left;

    while (lex->current.type == TOK_MUL || lex->current.type == TOK_DIV) {
        int op = lex->current.type;
        long long op_pos = lex->current.token_pos;
        next_token(lex);
        long long right_pos = lex->current.token_pos;
        
        Value right = parse_power(lex);
        if (lex->error_pos) return left;
        
        if (op == TOK_DIV) {
            if ((!right.is_float && right.i == 0) || (right.is_float && right.f == 0.0)) {
                set_error(lex, right_pos); // Division by zero exactly at right operand start
                return left;
            }
            left = do_div(left, right, op_pos, lex);
        } else {
            left = do_mul(left, right, op_pos, lex);
        }
    }
    return left;
}

Value parse_expr(Lexer *lex) {
    Value left = parse_term(lex);
    if (lex->error_pos) return left;

    while (lex->current.type == TOK_PLUS || lex->current.type == TOK_MINUS) {
        int op = lex->current.type;
        long long op_pos = lex->current.token_pos;
        next_token(lex);
        
        Value right = parse_term(lex);
        if (lex->error_pos) return left;
        
        if (op == TOK_PLUS) left = do_add(left, right, op_pos, lex);
        else left = do_sub(left, right, op_pos, lex);
    }
    return left;
}

/* =====================================================================
 * 4. File Execution & I/O
 * =====================================================================
 * HANDLING LARGE FILES:
 * Bypasses `fgets` completely. The entire input is `fread` into memory at once, 
 * removing redundant looping allocations and speeding up operations for huge files.
 * Output writes strings strictly to a heap array `out_buf` that grows exponentially. 
 * Nothing is dumped to disk unless `lex->error_pos == 0`.
 */

void process_file(const char *inpath, const char *outpath) {
    FILE *fin = fopen(inpath, "rb");
    if (!fin) {
        fprintf(stderr, "Cannot open input file: %s\n", inpath);
        exit(1);
    }
    fseek(fin, 0, SEEK_END);
    long length = ftell(fin);
    fseek(fin, 0, SEEK_SET);
    
    char *text = malloc(length + 1);
    if (length > 0) {
        if (fread(text, 1, length, fin) != (size_t)length) {
            fprintf(stderr, "Warning: could not read entire file\n");
        }
    }
    text[length] = '\0';
    fclose(fin);

    Lexer lex = { .text = text, .len = length, .pos = 0, .error_pos = 0 };
    next_token(&lex);

    size_t out_cap = 65536;
    char *out_buf = malloc(out_cap);
    size_t out_len = 0;
    out_buf[0] = '\0';

    while (lex.current.type != TOK_EOF) {
        if (lex.current.type == TOK_NEWLINE) {
            next_token(&lex);
            continue; // Blank lines produce no output
        }

        Value v = parse_expr(&lex);
        if (lex.error_pos) break;

        if (lex.current.type != TOK_NEWLINE && lex.current.type != TOK_EOF) {
            set_error(&lex, lex.current.token_pos);
            break;
        }

        char line_res[128];
        if (v.is_float) snprintf(line_res, sizeof(line_res), "%.15g\n", v.f);
        else snprintf(line_res, sizeof(line_res), "%lld\n", v.i);

        size_t slen = strlen(line_res);
        if (out_len + slen + 1 > out_cap) {
            out_cap *= 2;
            out_buf = realloc(out_buf, out_cap);
        }
        strcpy(out_buf + out_len, line_res);
        out_len += slen;

        if (lex.current.type == TOK_NEWLINE) {
            next_token(&lex);
        }
    }

    FILE *fout = fopen(outpath, "w");
    if (!fout) {
        fprintf(stderr, "Cannot write to output file: %s\n", outpath);
        free(text);
        free(out_buf);
        exit(1);
    }

    if (lex.error_pos) {
        fprintf(fout, "ERROR:%lld\n", lex.error_pos);
    } else {
        fprintf(fout, "%s", out_buf);
    }

    fclose(fout);
    free(text);
    free(out_buf);
}

const char *get_base_name(const char *path) {
    const char *base = strrchr(path, '/');
    return base ? base + 1 : path;
}

void build_output_filename(const char *outdir, const char *infile, char *outpath) {
    char base[256];
    strncpy(base, get_base_name(infile), sizeof(base) - 1);
    base[sizeof(base) - 1] = '\0';
    char *dot = strrchr(base, '.');
    if (dot && strcmp(dot, ".txt") == 0) *dot = '\0';

    snprintf(outpath, 1024, "%s/%s_%s_%s_%s.txt", outdir, base, STUDENT_FIRST, STUDENT_LAST, STUDENT_ID);
}

int main(int argc, char **argv) {
    char *dir = NULL;
    char *outdir = NULL;
    char *input_file = NULL;

    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--dir") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "Missing dir arg\n"); return 1; }
            dir = argv[i + 1];
            i += 2;
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output-dir") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "Missing outdir arg\n"); return 1; }
            outdir = argv[i + 1];
            i += 2;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        } else {
            input_file = argv[i];
            i++;
        }
    }

    if (!dir && !input_file) {
        fprintf(stderr, "No input specified\n");
        return 1;
    }

    char default_outdir[512];
    if (!outdir) {
        const char *base_name = dir ? get_base_name(dir) : get_base_name(input_file);
        char base_no_ext[256];
        strncpy(base_no_ext, base_name, sizeof(base_no_ext) - 1);
        base_no_ext[sizeof(base_no_ext) - 1] = '\0';
        
        if (!dir) {
            char *dot = strrchr(base_no_ext, '.');
            if (dot && strcmp(dot, ".txt") == 0) *dot = '\0';
        }
        snprintf(default_outdir, sizeof(default_outdir), "%s_%s_%s", base_no_ext, STUDENT_FIRST, STUDENT_ID);
        outdir = default_outdir;
    }

    // Try to create directory if it doesn't exist
    if (mkdir(outdir, 0777) != 0 && errno != EEXIST) {
        fprintf(stderr, "Failed to create directory %s\n", outdir);
        return 1;
    }

    if (dir) {
        DIR *d = opendir(dir);
        if (!d) {
            fprintf(stderr, "Cannot open directory: %s\n", dir);
            return 1;
        }
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            size_t len = strlen(ent->d_name);
            if (len > 4 && strcmp(ent->d_name + len - 4, ".txt") == 0) {
                char inpath[1024];
                snprintf(inpath, sizeof(inpath), "%s/%s", dir, ent->d_name);
                
                struct stat st;
                if (stat(inpath, &st) == 0 && S_ISREG(st.st_mode)) {
                    char outpath[1024];
                    build_output_filename(outdir, inpath, outpath);
                    process_file(inpath, outpath);
                }
            }
        }
        closedir(d);
    } else {
        char outpath[1024];
        build_output_filename(outdir, input_file, outpath);
        process_file(input_file, outpath);
    }

    return 0; // Exit cleanly, even if files parsed had formatting errors internally
}