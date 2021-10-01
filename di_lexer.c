/*
 * gcc -I/opt/local/include json-test.c json.c di.c -L/opt/local/lib -lyajl
 */

#include <pcre.h>
#include <assert.h>
#include <stdio.h>
#include "di.h"
#include "json.h"
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



static void prepare_patterns(void) {
    if (word_re)
        return; /* already done */
    /* compile patterns */
    word_re       = mk_re("[[:alpha:]$][\\w$]*"); // Conservative: "[a-z]+"
    operator_re   = mk_re("->|<=|>=|≤|≥|==|!=|≠|[<>,:;=+*~@\\-{}\\[\\]()]");
    div_re        = mk_re("/");                  // Conflicts with regex_re
    regex_re      = mk_re("/(?:\\\\/|[^/\\n])*/"); // Conflicts with div_re
    string_re     = mk_re("\"(?:\\\\\"|[^\"\\n])*\"");
    num_re        = mk_re("-?(?:0|[1-9][0-9]*)(?:\\.[0-9]+)?(?:[eE][-+]?[0-9]+)?");
    nl_re         = mk_re("\\R");               // Any unicode newline sequence
    spaces_re     = mk_re("\\h+");              // Any horizontal space
    keyword_dict = di_dict_empty();
    /* create dict of keywords */
    const char *keywords[] = {
        "case", "of", "fun", "do",
        "if", "then", "else",
        "and", "or", "not", "mod"
    };
    int i;
    int n = sizeof(keywords) / sizeof(char *);
    keyword_dict = di_dict_empty();
    for (i = 0; i < n; i++) {
        keyword_dict = di_dict_set(
                                   keyword_dict,
                                   di_string_from_cstring(keywords[i]),
                                   di_true()
                                   );
    }
    assert(di_dict_contains(keyword_dict, di_string_from_cstring("case")));
}

di_t di_lexer_create(di_t source) {
    check_pcre_compile_flags();
    prepare_patterns();
    di_t lexer = di_dict_empty();
    lexer = di_dict_set(lexer, di_string_from_cstring("source"), source);
    lexer = di_dict_set(lexer, di_string_from_cstring("offset"), di_from_int(0));
    lexer = di_dict_set(lexer, di_string_from_cstring("line"), di_from_int(1));
    lexer = di_dict_set(lexer, di_string_from_cstring("column"), di_from_int(1));
    return lexer;
}

#define die_on_prce_error(rc) do{                                       \
        if ((rc) < PCRE_ERROR_NOMATCH) {                                \
            fprintf(stderr, "PCRE error %d on line %d.\n", rc, __LINE__); \
            exit(rc);                                                   \
        }                                                               \
    }while(0)

/* find a token in the normal state */
di_t di_lex(di_t * lexer_ptr, di_t old_token, bool accept_regex) {
    di_t lexer = *lexer_ptr;
    di_t source_lit = di_string_from_cstring("source");
    di_t offset_lit = di_string_from_cstring("offset");
    di_t line_lit   = di_string_from_cstring("line");
    di_t column_lit = di_string_from_cstring("column");
    di_t op_lit     = di_string_from_cstring("op");
    di_t data_lit   = di_string_from_cstring("data");
    di_t source = di_dict_get(lexer, source_lit);
    di_t offset = di_dict_get(lexer, offset_lit);
    int line   = di_to_int(di_dict_get(lexer, line_lit));
    int column = di_to_int(di_dict_get(lexer, column_lit));
    di_size_t len = di_string_length(source);

    char * subject = di_string_chars(source);
    int start = di_to_int(offset);
    int rc;
    int ovector[3];
    di_t op;
    di_t data;
    di_t token;

    // Consume leading whitespace
    while (true) {
        // Consume newline
        rc = pcre_exec(nl_re, NULL, subject, len, start, 0, ovector, 3);
        die_on_prce_error(rc);
        if (rc > 0) {
            start = ovector[1];
            line++;
            column = 1;
            continue;
        }
        // Consume horizontal whitespace
        rc = pcre_exec(spaces_re, NULL, subject, len, start, 0, ovector, 3);
        die_on_prce_error(rc);
        if (rc > 0) {
            start = ovector[1];
            int i;
            for (i = ovector[0]; i < ovector[1]; i++) {
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

    // Check for end of string

    // Match tokens
    rc = pcre_exec(operator_re, NULL, subject, len, start, 0, ovector, 3);
    die_on_prce_error(rc);
    if (rc > 0) {
        op = di_string_from_chars(subject + start,
                                  ovector[1] - ovector[0]);
        data = di_undefined();
        goto found;
    }

    rc = pcre_exec(num_re, NULL, subject, len, start, 0, ovector, 3);
    die_on_prce_error(rc);
    if (rc > 0) {
        op = di_string_from_cstring("lit");
        data = di_string_from_chars(subject + start,
                                    ovector[1] - ovector[0]);
        data = json_decode(data);
        goto found;
    }

    rc = pcre_exec(string_re, NULL, subject, len, start, 0, ovector, 3);
    die_on_prce_error(rc);
    if (rc > 0) {
        op = di_string_from_cstring("lit");
        data = di_string_from_chars(subject + start,
                                    ovector[1] - ovector[0]);
        data = json_decode(data);
        goto found;
    }

    if (accept_regex) {
        rc = pcre_exec(regex_re, NULL, subject, len, start, 0, ovector, 3);
        die_on_prce_error(rc);
        if (rc > 0) {
            op = di_string_from_cstring("regex");
            data = di_string_from_chars(subject + start + 1,
                                        ovector[1] - ovector[0] - 2);
            goto found;
        }
    }
    else {
        // Regex not allowed here => division is allowed.
        rc = pcre_exec(div_re, NULL, subject, len, start, 0, ovector, 3);
        die_on_prce_error(rc);
        if (rc > 0) {
            op = di_string_from_cstring("/");
            data = di_undefined();
            goto found;
        }
    }

    // TODO: more token types

    rc = pcre_exec(word_re, NULL, subject, len, start, 0, ovector, 3);
    die_on_prce_error(rc);
    if (rc > 0) {
        data = di_string_from_chars(subject + start,
                                    ovector[1] - ovector[0]);
        if (di_dict_contains(keyword_dict, data)) {
            // it's a keyword
            op = data;
            data = di_undefined();
        }
        else if (di_equal(data, di_string_from_cstring("false"))) {
            // literal
            op = di_string_from_cstring("lit");
            data = di_false();
        }
        else if (di_equal(data, di_string_from_cstring("true"))) {
            // literal
            op = di_string_from_cstring("lit");
            data = di_true();
        }
        else if (di_equal(data, di_string_from_cstring("null"))) {
            // literal
            op = di_string_from_cstring("lit");
            data = di_null();
        }
        else {
            // it's an identifier
            op = di_string_from_cstring("ident");
        }
        goto found;
    }

    if (start >= len) {
        // eof
        op = di_string_from_cstring("eof");
        data = di_undefined();
        goto found;
    }

    // Unmatched token
    fprintf(stderr,
            "Unmatched token on line %d, column %d\n",
            line, column);
    exit(-1);

 found:
    // Create token dict
    if (di_is_dict(old_token)) {
        token = old_token;
    } else {
        di_cleanup(old_token);
        token = di_dict_empty();
    }
    token = di_dict_set(token, op_lit, op);
    token = di_is_undefined(data) ? di_dict_delete(token, data_lit)
        : di_dict_set(token, data_lit, data);
    token = di_dict_set(token, line_lit, di_from_int(line));
    token = di_dict_set(token, column_lit, di_from_int(column));

    // Update lexer offsets
    start = ovector[1];
    column += ovector[1] - ovector[0];
    lexer = di_dict_set(lexer, offset_lit, di_from_int(start));
    lexer = di_dict_set(lexer, line_lit, di_from_int(line));
    lexer = di_dict_set(lexer, column_lit, di_from_int(column));

    // Return lexer (by pointer) and token (normal return)
    *lexer_ptr = lexer;
    return token;
}

/* token, when the parser expects a pattern */
di_t di_lex_pattern(di_t lexer);
