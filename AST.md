Abstract syntax tree
====================

All syntactic elements which have an "syntax" key also have the location keys
"line" and "column" being the location of the first token in the syntactic
element.

Expressions (expr)
------------------

    do ... end            {"syntax": "do", "seq": [expr-or-match], "defs": defs}
    if a then b else c    {"syntax": "if", "cond": a, "then": b, "else": c}
    f(a,b,c)              {"syntax": "apply", "func": f, "args": [a,b,c]}
    42                    {"syntax": "lit", "value": 42}
    x                     {"syntax": "var", "name": "x"}
    [...]                 {"syntax": "array", "elems": [...]}
    {k1: v1, ...}         {"syntax": "dict", "entries": [entry]}
    d{k1: v1, ...}        {"syntax": "dictup", "subj": d, "entries": [entry]}
    case a of ... end     {"syntax": "case", "subj": a, "clauses": [alt]}
    e1 + e2               {"syntax": "+", "left": e1, "right": e2}

where the dict entries (k1 and v1 being expressions) have the form

    k1: v1                {"syntax": "entry", "key": k1, "value": v1}

the case clauses are on the form

    a -> b                {"syntax": "clause", "pats": [a], "body": b}

Patterns
--------

The patterns of literals, variables, array, dict, dictup and the binary
operators "@" and "~" have the same syntax as expressions. Additionally, regex
pattern and "=" pattern are allowed, and the underscore variable:

    /[a-z]*\n/            {"syntax": "regex", "regex": "[a-z]*\\n"}
    [x] @ _ = xs          {"syntax": "=", "left": pattern, "right": pattern}

Match
-----

Free variables in LSH are bound in the current scope. Bound variables are
matched, causing an exception on mismatch. A match can occur anywhere in
patterns, but not in nested expressions as it would make it non-deterministic
whether the variables are bound or not. They can appear in "do" sequences
though.

    pattern = expr        {"syntax": "=", "left": pattern, "right": expr}
    pattern = pattern     {"syntax": "=", "left": pattern, "right": pattern}

Function definitions
--------------------

In a "do" block, the function definitions under "defs" is a dict, where the keys
are the function names. Each function can have multiple clauses under the key
"clauses". All clauses must have the same arity.

    f(..) = .. ; ..       {"syntax": "func", "name": "f", "clauses": [clauses],
                           "arity": n}

where a function clause is

    f(params) = body      {"syntax": "clause", "pats": params, "body": body}

where params is an array of patterns and body is an expression.

Example of a function definition:

    f(0) = 42             {"syntax": "func", "name": "f", "clauses": [
    f(n) = n-1                {"syntax": "clause",
                               "pats": [{"syntax": "lit", "value": 0}],
                               "body": {"syntax": "lit", "value": 42}},
                              {"syntax": "clause",
                               "pats": [{"syntax": "var", "name": "n"}],
                               "body": {"syntax": "-",
                                        "left": {"syntax": "var",
                                                 "name": "n"},
                                        "right": {"syntax": "lit",
                                                  "value": 1}}}]}

Annotated syntax tree
---------------------

Keys added to all the AST by the annotator:

* "varset": a dict with the accesses variable names as the keys. The values are
  "bind" or "access".

The variables, i.e. "syntax": "var", have the additional "action" key with value
indicating the type of access:

* "bind" in the pattern where the variable is bound;
* "discard" in the pattern where the variable is bound, if the variable is never
  accessed;
* "access" where the variable is accessed, except for the last access;
* "last" where the variables is accessed for the last time.

Function definitions may optionally have the additional key "env", the closure
environment, which is present only if a function refers to variables outside the
function. The value is a dict where the variable names as keys and the access
types (aka actions) as the values. Most of the "varset" keys as well as all
"line" and "column" keys are omitted for brevity.

Example:

    y = 42                    {"syntax": "=",
                               "left": {"syntax": "var",
                                        "name": "y",
                                        "action": "bind"}},
                               "right": {"syntax": "lit", "value": 42},
                               "varset": {"y": null}}
    f(x) = x + y              {"syntax": "func",
                               "name": "f",
                               "clauses": [
                                  {"syntax": "clause",
                                   "pats": [{"syntax": "var",
                                             "name": "x",
                                             "action": "bind"}],
                                   "body": {"syntax": "+",
                                            "left": {"syntax": "var",
                                                     "name": "x",
                                                     "action": "last"},
                                            "right": {"syntax": "var",
                                                     "name": "y",
                                                     "action": "last"}}}
                               ],
                               "env": {"y": "last"}}
