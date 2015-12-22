#include "di.h"

di_t di_lexer_create(di_t source);

/* find a token in the normal state */
di_t di_lex(di_t * lexer, di_t old_token, bool accept_regex);

/* token, when the parser expects a pattern */
di_t di_lex_pattern(di_t lexer);
