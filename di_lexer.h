#include "di.h"

di_t di_lexer_create(di_t source);

/* find a token */
di_t di_lex(di_t * lexer, di_t old_token);
