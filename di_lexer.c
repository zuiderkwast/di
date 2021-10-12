/*
 * gcc -I/opt/local/include json-test.c json.c di.c -L/opt/local/lib -lyajl
 */

#include <pcre.h>
#include <assert.h>
#include <stdio.h>
#include "di.h"
#include "di.h"
#include "di_lexer.h"
#include "di_debug.h"

/* --------- probing code ---------- */

#define CHECK_PCRE_COMPILE_FLAG(what) do {              \
        int result;                                     \
        assert(pcre_config(what, (void*)&result) == 0); \
        assert(result);                                 \
    } while(0)

static void check_pcre_compile_flags(void) {
    CHECK_PCRE_COMPILE_FLAG(PCRE_CONFIG_UTF8);
    CHECK_PCRE_COMPILE_FLAG(PCRE_CONFIG_UNICODE_PROPERTIES);
    CHECK_PCRE_COMPILE_FLAG(PCRE_CONFIG_JIT);
}

/* --------- end of probing code ---------- */

static di_t parse_int(const char *subject, int length) {
    (void)length;
    int val;
    assert(sscanf(subject, "%d", &val));
    return di_from_int(val);
}

static di_t parse_double(const char *subject, int length) {
    (void)length;
    double val;
    assert(sscanf(subject, "%lf", &val));
    return di_from_double(val);
}

static di_t parse_number(const char *subject, int length) {
    for (int i = 0; i < length; i++)
        if (subject[i] == '.' || subject[i] == 'e' || subject[i] == 'E')
            return parse_double(subject, length);
    return parse_int(subject, length);
}

/* Code point <-> UTF-8 conversion
   First   Last     Byte 1   Byte 2   Byte 3   Byte 4
   U+0000  U+007F   0xxxxxxx
   U+0080  U+07FF   110xxxxx 10xxxxxx
   U+0800  U+FFFF   1110xxxx 10xxxxxx 10xxxxxx
   U+10000 U+10FFFF 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */

static int utf8_cp_length(unsigned codepoint) {
    if (codepoint <= 0x7f) return 1;
    if (codepoint <= 0x7ff) return 2;
    if (codepoint <= 0xffff) return 3;
    return 4;
}

// UTF-8 encodes a single code point. Writes the UTF-8 bytes to dst and returns
// the number of bytes written.
static int utf8_cp_encode(unsigned cp, char *dst) {
    if (cp <= 0x7f) {
        dst[0] = (char)cp;
        return 1;
    } else if (cp <= 0x7ff) {
        dst[0] = (char)(0xc0 | ((cp >> 6) & 0x1f));
        dst[1] = (char)(0x80 | (cp & 0x3f));
        return 2;
    } else if (cp <= 0xffff) {
        dst[0] = (char)(0xe0 | ((cp >> 12) & 0x0f));
        dst[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
        dst[2] = (char)(0x80 | (cp & 0x3f));
        return 3;
    } else {
        dst[0] = (char)(0xf0 | ((cp >> 18) & 0x07));
        dst[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
        dst[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
        dst[2] = (char)(0x80 | (cp & 0x3f));
        return 4;
    }
}

// Parses a string literal
// TODO: Replace assert with error report.
static di_t parse_string(const char *subject, int length) {
    // Get rid of surrounding quotes
    subject++;
    length -= 2;
    // Compute length
    int len = 0;
    int i = 0;
    while (i < length) {
        if (subject[i++] == '\\') {
            if (subject[i++] == 'u') {
                assert(i + 3 < length); // at least 4 chars left
                unsigned codepoint;
                int n;
                assert(sscanf(&subject[i], "%4x%n", &codepoint, &n) == 1);
                assert(n == 4);               /* exactly 4 bytes were read */
                assert(codepoint <= 0x10fff); /* basic validation */
                len += utf8_cp_length(codepoint);
                i += 4;                       /* skip the 4 hex chars */
                continue;
            }
        }
        len++;
    }

    // allocate and populate
    di_t str = di_string_create_presized(len);
    char *chars = di_string_chars(str);
    i = 0;
    int j = 0;
    while (i < length) {
        if (subject[i] == '\\') {
            // Escapes: \" \\ \/ \b \f \n \r \t \uHHHH
            switch (subject[++i]) {
            case 'u':
                i++; // skip the u
                unsigned codepoint;
                assert(sscanf(&subject[i], "%4x", &codepoint) == 1);
                j += utf8_cp_encode(codepoint, &chars[j]);
                i += 4; // skip the hex chars
                break;
            case 'b': chars[j++] = '\b'; i++; break;
            case 'f': chars[j++] = '\f'; i++; break;
            case 'n': chars[j++] = '\n'; i++; break;
            case 'r': chars[j++] = '\r'; i++; break;
            case 't': chars[j++] = '\t'; i++; break;
            default:
                // Actually only '"', '\\' and '/' for JSON
                chars[j++] = subject[i++];
            }
        } else {
            chars[j++] = subject[i++];
        }
    }
    assert(j == len);
    return str;
}

static pcre *word_re = NULL, *operator_re, *div_re, *regex_re, *string_re,
    *num_re, *nl_re, *spaces_re;
static di_t keyword_dict;


/* wrapper for pcre_compile */
static pcre *mk_re(const char *regex) {
    const char *error;
    pcre *re;
    int erroffset;
    re = pcre_compile(
                      regex,
                      PCRE_ANCHORED |    /* match start of subject only */
                      PCRE_UTF8 |
                      PCRE_NEWLINE_ANY | /* \n is any unicode newline */
                      PCRE_UCP,          /* \w all unicode letters, \d all numbers */
                      &error,
                      &erroffset,
                      NULL               /* use default character tables */
                      );
    if (re == NULL) {
        fprintf(
                stderr,
                "Error \"%s\" in regular expression /%s/ at offset %d\n",
                error, regex, erroffset
                );
    }
    return re;
}

#define str(arg) di_string_from_cstring(arg)

static void prepare_patterns(void) {
    if (word_re)
        return; /* already done */
    /* compile patterns */
    word_re       = mk_re("[[:alpha:]$][\\w$]*");  // Conservative: "[a-z]+"
    operator_re   = mk_re("->|<=|>=|≤|≥|==|!=|≠|[<>,:;=+*~@\\-{}\\[\\]()\\\\]");
    div_re        = mk_re("/");                    // Conflicts with regex_re
    regex_re      = mk_re("/(?:\\\\/|[^/\\n])*/"); // Conflicts with div_re
    string_re     = mk_re("\"(?:\\\\\"|[^\"\\n])*\"");
    num_re        = mk_re("-?(?:0|[1-9][0-9]*)(?:\\.[0-9]+)?(?:[eE][-+]?[0-9]+)?");
    nl_re         = mk_re("(?:\\#.*?)?\\R");       // Any unicode newline sequence
    spaces_re     = mk_re("\\h+");                 // Any horizontal space
    keyword_dict = di_dict_empty();
    /* create dict of keywords */
    const char *keywords[] = {
        "case", "of", "let", "in", "do", "end",
        "if", "then", "else",
        "and", "or", "not", "mod"
    };
    int i;
    int n = sizeof(keywords) / sizeof(char *);
    keyword_dict = di_dict_empty();
    for (i = 0; i < n; i++) {
        keyword_dict = di_dict_set(keyword_dict, str(keywords[i]), di_true());
    }
    assert(di_dict_contains(keyword_dict, str("case")));
}

di_t di_lexer_create(di_t source) {
    check_pcre_compile_flags();
    prepare_patterns();
    di_t lexer = di_dict_empty();
    lexer = di_dict_set(lexer, str("source"), source);
    lexer = di_dict_set(lexer, str("offset"), di_from_int(0));
    lexer = di_dict_set(lexer, str("line"), di_from_int(1));
    lexer = di_dict_set(lexer, str("column"), di_from_int(1));
    lexer = di_dict_set(lexer, str("layout"), di_array_empty());
    return lexer;
}

/* Utility pcre_exec wrapper. Om match, returns true and sets *match_start and
 * *match_end, if provided. */
static bool re_match(pcre *re, const char *subject, int len, int start,
                     int *match_start, int *match_end) {
    int ovector[3];
    int rc = pcre_exec(re, NULL, subject, len, start, 0, ovector, 3);
    if (rc > 0) {
        if (match_start != NULL) *match_start = ovector[0];
        if (match_end   != NULL) *match_end   = ovector[1];
        return true;
    } else if (rc >= PCRE_ERROR_NOMATCH) {
        return false;
    } else {
        fprintf(stderr, "PCRE error: %d\n", rc);
        exit(rc);
    }
}

/* Sets the fields in the provided token and returns the new one */
static di_t set_token_fields(di_t token, di_t op, di_t data,
                             int line, int column) {
    if (!di_is_dict(token)) {
        di_cleanup(token);
        token = di_dict_empty();
    }
    token = di_dict_set(token, str("op"), op);
    token = (di_is_null(data) ?
             di_dict_delete(token, str("data")) :
             di_dict_set(token, str("data"), data));
    token = di_dict_set(token, str("line"), di_from_int(line));
    token = di_dict_set(token, str("column"), di_from_int(column));
    return token;
}

di_t update_lexer_offsets(di_t lexer, int offset, int line, int column) {
    lexer = di_dict_set(lexer, str("offset"), di_from_int(offset));
    lexer = di_dict_set(lexer, str("line"), di_from_int(line));
    lexer = di_dict_set(lexer, str("column"), di_from_int(column));
    return lexer;
}

/* find a token */
di_t di_lex(di_t * lexer_ptr, di_t old_token) {
    di_t lexer = *lexer_ptr;
    bool accept_regex = true;
    if (di_is_dict(old_token)) {
        di_t old_op = di_dict_get(old_token, str("op"));
        if (di_equal(old_op, str("ident")) ||
            di_equal(old_op, str("lit")) ||
            di_equal(old_op, str(")")) ||
            di_equal(old_op, str("]")) ||
            di_equal(old_op, str("}"))) accept_regex = false; // division
    }
    di_t source = di_dict_get(lexer, str("source"));
    di_t offset = di_dict_get(lexer, str("offset"));
    di_t layout = di_dict_get(lexer, str("layout"));
    int line   = di_to_int(di_dict_get(lexer, str("line")));
    int column = di_to_int(di_dict_get(lexer, str("column")));
    di_size_t len = di_string_length(source);
    char * subject = di_string_chars(source);
    int start = di_to_int(offset);
    int match_start, match_end;
    di_t op;
    di_t data;
    di_t token = di_null();

    // Consume leading whitespace and update start, line and column.
    while (true) {
        // Consume newline
        if (re_match(nl_re, subject, len, start, NULL, &start)) {
            line++;
            column = 1;
            continue;
        }
        // Consume horizontal whitespace
        if (re_match(spaces_re, subject, len, start, &match_start, &match_end)) {
            start = match_end;
            int i;
            for (i = match_start; i < match_end; i++) {
                if (subject[i] == '\t') {
                    // Round up column to 8n + 1.
                    column += 8 - (column - 1) % 8;
                }
                else {
                    // Not a tab. Consider it 1 space wide.
                    column++;
                }
            }
            continue;
        }
        // Nothing consumed in this iteration. Done.
        break;
    }

    // check layout stack to see if we need to insert 'end' or ';'
    di_size_t layout_depth = di_array_length(layout);
    if (di_is_dict(old_token) && layout_depth > 0) {
        di_t layoutframe = di_array_get(layout, layout_depth - 1);
        di_t layoutcol   = di_dict_get(layoutframe, str("column"));
        assert(di_is_int(layoutcol));
        if (column < di_to_int(layoutcol)) {
            // Insert 'end' (or 'in' after 'let') and pop layout stack
            di_t layoutop = di_dict_get(layoutframe, str("op"));
            const char *endop = di_equal(str("let"), layoutop) ? "in" : "end";
            token = set_token_fields(old_token, str(endop), di_null(),
                                     line, column);
            // pop the frame from the stack
            di_array_pop(&layout);
            lexer = di_dict_set(lexer, str("layout"), layout);
        } else if (column == di_to_int(layoutcol)) {
            // insert ';' unless we inserted one just before (check oldtoken?)
            di_t old_op = di_dict_get(old_token, str("op"));
            if (!di_equal(old_op, str(";"))) {
                token = set_token_fields(old_token, str(";"), di_null(),
                                         line, column);
            }
        }
        // If we have set token above, update lexer state and return the token.
        if (!di_is_null(token)) {
            lexer = update_lexer_offsets(lexer, start, line, column);
            *lexer_ptr = lexer;
            return token;
        }
    }
    // Check for end of string

    // Match tokens
    if (re_match(operator_re, subject, len, start, &match_start, &match_end)) {
        op = di_string_from_chars(subject + start, match_end - match_start);
        data = di_null();
        goto found;
    }

    if (re_match(num_re, subject, len, start, &match_start, &match_end)) {
        op = str("lit");
        data = parse_number(subject + start, match_end - match_start);
        goto found;
    }

    if (re_match(string_re, subject, len, start, &match_start, &match_end)) {
        op = str("lit");
        data = parse_string(subject + start, match_end - match_start);
        goto found;
    }

    if (accept_regex) {
        if (re_match(regex_re, subject, len, start, &match_start, &match_end)) {
            op = str("regex");
            data = di_string_from_chars(subject + start + 1,
                                        match_end - match_start - 2);
            goto found;
        }
    }
    else {
        // Regex not allowed here => division is allowed.
        if (re_match(div_re, subject, len, start, &match_start, &match_end)) {
            op = str("/");
            data = di_null();
            goto found;
        }
    }

    // TODO: more token types

    if (re_match(word_re, subject, len, start, &match_start, &match_end)) {
        data = di_string_from_chars(subject + start, match_end - match_start);
        if (di_dict_contains(keyword_dict, data)) {
            op = data;
            data = di_null();
        }
        else if (di_equal(data, str("false"))) {
            op = str("lit");
            data = di_false();
        }
        else if (di_equal(data, str("true"))) {
            op = str("lit");
            data = di_true();
        }
        else if (di_equal(data, str("null"))) {
            op = str("lit");
            data = di_null();
        }
        else {
            op = str("ident");
        }
        goto found;
    }

    if (start >= len) {
        op = str("eof");
        data = di_null();
        goto found;
    }

    // Unmatched token
    fprintf(stderr,
            "Unmatched token on line %d, column %d\n",
            line, column);
    exit(-1);

 found:
    // If old_token is do/of/let/where, add the current column to the layout stack.
    if (di_is_dict(old_token)) {
        di_t old_op = di_dict_get(old_token, str("op"));
        if (di_equal(old_op, str("do")) || di_equal(old_op, str("of"))
            || di_equal(old_op, str("let")) || di_equal(old_op, str("where"))) {
            di_t frame = di_dict_empty();
            frame = di_dict_set(frame, str("op"), old_op);
            frame = di_dict_set(frame, str("column"), di_from_int(column));
            di_array_push(&layout, frame);
            lexer = di_dict_set(lexer, str("layout"), layout);
        }
    }

    // Create token dict
    token = set_token_fields(old_token, op, data, line, column);

    // Update lexer offsets
    start = match_end;
    column += match_end - match_start;
    lexer = update_lexer_offsets(lexer, start, line, column);
    lexer = di_dict_set(lexer, str("offset"), di_from_int(start));
    lexer = di_dict_set(lexer, str("line"), di_from_int(line));
    lexer = di_dict_set(lexer, str("column"), di_from_int(column));

    // Return lexer (by pointer) and token (normal return)
    *lexer_ptr = lexer;
    return token;
}
