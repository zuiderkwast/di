#ifndef DI_PRETTYPRINT_H
#define DI_PRETTYPRINT_H

/* Pretty-prints a parse tree to stdout. */
void di_prettyprint(di_t tree);

/* Returns the source code of a value. Does *not* free value. (TODO: borrowed
 * pointers.) */
di_t di_to_source(di_t value, int indent);

#endif
