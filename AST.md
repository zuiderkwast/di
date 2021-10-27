Abstract syntax tree
====================

All syntactic elements which have an "op" key also have the location keys "line"
and "column" being the location of the first token in the syntactic element.

Expressions (expr)
------------------

    do ... end            {"op": "do", "seq": [expr-or-def]}
    if a then b else c    {"op": "if", "cond": a, "then": b, "else": c}
    f(a,b,c)              {"op": "apply", "func": f, "args": [a,b,c]}
    42                    {"op": "lit", "value": 42}
    x                     {"op": "var", "name": "x"}
    [...]                 {"op": "array", "elems": [...]}
    {k1: v1, ...}         {"op": "dict", "pairs": [dict-pair]}
    d{k1: v1, ...}        {"op": "dictup", "subj": d, "pairs": [dict-pair]}
    case a of ... end     {"op": "case", "subj": a, "alts": [alt]}

where the dict pairs (k1 and v1 being expressions) have the form

    k1: v1                {"key": k1, "value": v1}

the alternatives in case "alt" are on the form

    a -> b                {"pat": a, "expr": b}

Patterns
--------

The patterns of literals, variables, array, dict, dictup and the binary
operators "@" and "~" have the same syntax as expressions. Additionally:

    /[a-z]*\n/            {"op": "regex", "regex": "[a-z]*\\n"}

Definitions
-----------

    pattern = expr        {"op": "match", "pat": pattern, "expr": expr}
    f(..) = .. ; ..       {"op": "func", "name": "f", "alts": [func-alts]}

where a function alternative "func-alt" is

    f(params) = body      {"params": params, "body": body}

where params is an array of patterns and body is an expression.

Example of a function definition:

    f(0) = 42             {"op": "func", "name": "f", "alts": [
    f(n) = n-1                {"params": [{"op": "lit", "value": 0}],
                               "body": {"op": "lit", "value": 42}},
                              {"params": [{"op": "var", "name": "n"}],
                               "body": {"op": "-",
                                        "left": {"op": "var", "name": "n"},
                                        "right": {"op": "lit", "value": 1}}}]}

Annotated syntax tree
---------------------

Keys added to all the AST by the annotator:

* "varset": a dict with the accesses variable names as the keys. The values are
  unspecified.

The variables, i.e. "op": "var", have the additional "action" key with value
indicating the type of access:

* "bind" in the pattern where the variable is bound
* "discrd" in the pattern where the variable is bound, if the variable is never
  accessed.
* "access" where the variable is accessed, except for the last access
* "last" where the variables is accessed for the last time

Function definitions may optionally have the additional key "cloctx", the
closure context, which is present only if a function refers to variables outside
the function. The value is a dict where the variable names as keys and the
access types (aka actions) as the values. Most of the "varset" keys as well as
all "line" and "column" keys are omitted for brevity.

Example:

    y = 42                    {"op": "match",
                               "left": {"op": "var",
                                        "name": "y",
                                        "action": "bind"}},
                               "right": {"op": "lit", "value": 42},
                               "varset": {"y": null}}
    f(x) = x + y              {"op": "func",
                               "name": "f",
                               "params": [{"pat": {"op": "var",
                                                   "name": "x",
                                                   "action": "bind"}}],
                               "body": {"op": "+",
                                        "left": {"op": "var",
                                                 "name": "x",
                                                 "action": "last"},
                                        "right": {"op": "var",
                                                 "name": "y",
                                                 "action": "last"}},
                               "cloctx": {"y": "last"},
                               "varset": {"y": null}}
