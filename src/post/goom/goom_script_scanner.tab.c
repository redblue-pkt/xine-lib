/* A Bison parser, made by GNU Bison 1.875a.  */

/* Skeleton parser for Yacc-like parsing with Bison,
   Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002, 2003 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

/* As a special exception, when this file is copied by Bison into a
   Bison output file, you may use that output file without restriction.
   This special exception was added by the Free Software Foundation
   in version 1.24 of Bison.  */

/* Written by Richard Stallman by simplifying the original so called
   ``semantic'' parser.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output.  */
#define YYBISON 1

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Using locations.  */
#define YYLSP_NEEDED 0



/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     TYPE_INTEGER = 258,
     TYPE_FLOAT = 259,
     TYPE_VAR = 260,
     TYPE_PARAM = 261,
     INT_TK = 262,
     FLOAT_TK = 263
   };
#endif
#define TYPE_INTEGER 258
#define TYPE_FLOAT 259
#define TYPE_VAR 260
#define TYPE_PARAM 261
#define INT_TK 262
#define FLOAT_TK 263




/* Copy the first part of user declarations.  */
#line 6 "goom_script_scanner.y"

    #include <stdio.h>
    #include <string.h>
    #include "goom_script_scanner.h"
    
    int yylex(void);
    void yyerror(char *);
    extern GoomScriptScanner *currentScanner;

    static NodeType *nodeNew(const char *str, int type);
    static void nodeFreeInternals(NodeType *node);
    static void nodeFree(NodeType *node);

    static void commit_node(NodeType *node);
    static void precommit_node(NodeType *node);

    static NodeType *new_constInt(const char *str);
    static NodeType *new_constFloat(const char *str);
    static NodeType *new_var(const char *str);
    static NodeType *new_param(const char *str);
    static NodeType *new_nop(const char *str);
    static NodeType *new_op(const char *str, int type, int nbOp);

    static int allocateLabel();
    #define allocateTemp allocateLabel

    /* SETTER */
    static NodeType *new_set(NodeType *lvalue, NodeType *expression) {
        NodeType *set = new_op("set", OPR_SET, 2);
        set->opr.op[0] = lvalue;
        set->opr.op[1] = expression;
        return set;
    }
    static void commit_set(NodeType *set) {
        precommit_node(set->opr.op[1]);
#ifdef VERBOSE
        printf("set.f %s %s\n", set->opr.op[0]->str, set->opr.op[1]->str);
#endif
        currentScanner->instr = instr_init(currentScanner, "set.f", INSTR_SETF, 2);
        commit_node(set->opr.op[0]);
        commit_node(set->opr.op[1]);
    }

    /* FLOAT */
    static NodeType *new_float_decl(NodeType *name) {
        NodeType *fld = new_op("float", OPR_DECLARE_FLOAT, 1);
        fld->opr.op[0] = name;
        return fld;
    }
    static void commit_float(NodeType *var) {
#ifdef VERBOSE
        printf("float %s\n", var->opr.op[0]->str);
#endif
        currentScanner->instr = instr_init(currentScanner, "float", INSTR_INT, 1);
        commit_node(var->opr.op[0]);
    }
    
    /* INT */
    static NodeType *new_int_decl(NodeType *name) {
        NodeType *intd = new_op("int", OPR_DECLARE_INT, 1);
        intd->opr.op[0] = name;
        return intd;
    }
    static void commit_int(NodeType *var) {
#ifdef VERBOSE
        printf("int %s\n", var->opr.op[0]->str);
#endif
        currentScanner->instr = instr_init(currentScanner, "int", INSTR_INT, 1);
        commit_node(var->opr.op[0]);
    }

    /* commodity method for add, mult, ... */

    static int is_tmp_expr(NodeType *node) {
        return node->str && !strncmp(node->str,"__tmp",5);
    }

    static void precommit_expr(NodeType *expr, const char *type, int instr_id) {

        char stmp[256];
        NodeType *tmp;
        int toAdd;

        /* compute "left" and "right" */
        precommit_node(expr->opr.op[0]);
        precommit_node(expr->opr.op[1]);

        if (is_tmp_expr(expr->opr.op[0])) {
            strcpy(stmp, expr->opr.op[0]->str);
            toAdd = 1;
        }
        else if (is_tmp_expr(expr->opr.op[1])) {
            strcpy(stmp,expr->opr.op[1]->str);
            toAdd = 0;
        }
        else {
            /* declare a float to store the result */
            sprintf(stmp,"__tmp%i",allocateTemp());
            commit_node(new_float_decl(new_var(stmp)));
            /* set the float to the value of "op1" */
            commit_node(new_set(new_var(stmp),expr->opr.op[0]));
            toAdd = 1;
        }

        /* add op2 to tmp */
#ifdef VERBOSE
        printf("%s %s %s\n", type, stmp, expr->opr.op[toAdd]->str);
#endif
        currentScanner->instr = instr_init(currentScanner, type, instr_id, 2);
        commit_node(new_var(stmp));
        commit_node(expr->opr.op[toAdd]);
    
        /* redefine the ADD node now as the computed variable */
        nodeFreeInternals(expr);
        tmp = new_var(stmp);
        *expr = *tmp;
        free(tmp);
    }

    static NodeType *new_expr2(const char *name, int id, NodeType *expr1, NodeType *expr2) {
        NodeType *add = new_op(name, id, 2);
        add->opr.op[0] = expr1;
        add->opr.op[1] = expr2;
        return add;
    }

    /* ADD */
    static NodeType *new_add(NodeType *expr1, NodeType *expr2) {
        return new_expr2("add.f", OPR_ADD, expr1, expr2);
    }
    static void precommit_add(NodeType *add) {
        precommit_expr(add,"add.f",INSTR_ADDF);
    }

    /* MUL */
    static NodeType *new_mul(NodeType *expr1, NodeType *expr2) {
        return new_expr2("mul.f", OPR_MUL, expr1, expr2);
    }
    static void precommit_mul(NodeType *mul) {
        precommit_expr(mul,"mul.f",INSTR_MULF);
    }
    
    /* EQU */
    static NodeType *new_equ(NodeType *expr1, NodeType *expr2) {
        return new_expr2("isequal.f", OPR_EQU, expr1, expr2);
    }
    static void precommit_equ(NodeType *mul) {
        precommit_expr(mul,"isequal.f",INSTR_ISEQUALF);
    }
    
    /* INF */
    static NodeType *new_low(NodeType *expr1, NodeType *expr2) {
        return new_expr2("islower.f", OPR_LOW, expr1, expr2);
    }
    static void precommit_low(NodeType *mul) {
        precommit_expr(mul,"islower.f",INSTR_ISLOWERF);
    }
    
    /* IF */
    static NodeType *new_if(NodeType *expression, NodeType *instr) {
        NodeType *node = new_op("if", OPR_IF, 2);
        node->opr.op[0] = expression;
        node->opr.op[1] = instr;
        return node;
    }
    static void commit_if(NodeType *node) {

        char slab[1024];
        precommit_node(node->opr.op[0]);

        /* jzero.i <expression> <endif> */
        sprintf(slab, "|eif%d|", allocateLabel());
#ifdef VERBOSE
        printf("jzero.i %s %s\n", node->opr.op[0]->str, slab);
#endif
        currentScanner->instr = instr_init(currentScanner, "jzero.i", INSTR_JZERO, 2);
        commit_node(node->opr.op[0]);
        instr_add_param(currentScanner->instr, slab, TYPE_LABEL);

        /* [... instrs of the if ...] */
        commit_node(node->opr.op[1]);
        /* label <endif> */
#ifdef VERBOSE
        printf("label %s\n", slab);
#endif
        currentScanner->instr = instr_init(currentScanner, "label", INSTR_LABEL, 1); 
        instr_add_param(currentScanner->instr, slab, TYPE_LABEL);
    }

    /* BLOCK */
    static NodeType *new_block(NodeType *lastNode) {
        NodeType *blk = new_op("block", OPR_BLOCK, 2);
        blk->opr.op[0] = new_nop("start_of_block");
        blk->opr.op[1] = lastNode;        
        return blk;
    }
    static void commit_block(NodeType *node) {
        commit_node(node->opr.op[0]->opr.next);
    }

    /** **/

    static NodeType *rootNode = 0; // TODO: reinitialiser a chaque compilation.
    static NodeType *lastNode = 0;
    static NodeType *gsl_append(NodeType *curNode) {
        if (lastNode)
            lastNode->opr.next = curNode;
        lastNode = curNode;
        while(lastNode->opr.next) lastNode = lastNode->opr.next;
        if (rootNode == 0)
            rootNode = curNode;
        return curNode;
    }

    static int lastLabel = 0;
    int allocateLabel() {
        return ++lastLabel;
    }
    void releaseLabel(int n) {
        if (n == lastLabel)
            lastLabel--;
    }

    void gsl_commit_compilation() {
        commit_node(rootNode);
        rootNode = 0;
        lastNode = 0;
    }
    
    void precommit_node(NodeType *node) {
        /* do here stuff for expression.. for exemple */
        switch(node->type) {
            case OPR_NODE:
                switch(node->opr.type) {
                    case OPR_ADD: precommit_add(node); break;
                    case OPR_MUL: precommit_mul(node); break;
                    case OPR_EQU: precommit_equ(node); break;
                    case OPR_LOW: precommit_low(node); break;
                }
        }
    }
    
    void commit_node(NodeType *node) {

        if (node == 0) return;
        
        switch(node->type) {
            case OPR_NODE:
                switch(node->opr.type) {
                    case OPR_DECLARE_FLOAT: commit_float(node); break;
                    case OPR_DECLARE_INT:   commit_int(node);   break;
                    case OPR_SET:           commit_set(node); break;
                    case OPR_IF:            commit_if(node); break;
                    case OPR_BLOCK:         commit_block(node); break;
#ifdef VERBOSE
                    case EMPTY_NODE:        printf("NOP\n"); break;
#endif
                }

                commit_node(node->opr.next); /* recursive for the moment, maybe better to do something iterative? */
                break;

            case PARAM_NODE:       instr_add_param(currentScanner->instr, node->str, TYPE_PARAM); break;
            case VAR_NODE:         instr_add_param(currentScanner->instr, node->str, TYPE_VAR); break;
            case CONST_INT_NODE:   instr_add_param(currentScanner->instr, node->str, TYPE_INTEGER); break;
            case CONST_FLOAT_NODE: instr_add_param(currentScanner->instr, node->str, TYPE_FLOAT); break;
        }
        nodeFree(node);
    }


/* Enabling traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 0
#endif

#if ! defined (YYSTYPE) && ! defined (YYSTYPE_IS_DECLARED)
#line 263 "goom_script_scanner.y"
typedef union YYSTYPE {
    int intValue;
    float floatValue;
    char charValue;
    char strValue[2048];
    NodeType *nPtr;
  } YYSTYPE;
/* Line 191 of yacc.c.  */
#line 357 "goom_script_scanner.tab.c"
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif



/* Copy the second part of user declarations.  */


/* Line 214 of yacc.c.  */
#line 369 "goom_script_scanner.tab.c"

#if ! defined (yyoverflow) || YYERROR_VERBOSE

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# if YYSTACK_USE_ALLOCA
#  define YYSTACK_ALLOC alloca
# else
#  ifndef YYSTACK_USE_ALLOCA
#   if defined (alloca) || defined (_ALLOCA_H)
#    define YYSTACK_ALLOC alloca
#   else
#    ifdef __GNUC__
#     define YYSTACK_ALLOC __builtin_alloca
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's `empty if-body' warning. */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
# else
#  if defined (__STDC__) || defined (__cplusplus)
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   define YYSIZE_T size_t
#  endif
#  define YYSTACK_ALLOC malloc
#  define YYSTACK_FREE free
# endif
#endif /* ! defined (yyoverflow) || YYERROR_VERBOSE */


#if (! defined (yyoverflow) \
     && (! defined (__cplusplus) \
	 || (YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  short yyss;
  YYSTYPE yyvs;
  };

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (sizeof (short) + sizeof (YYSTYPE))				\
      + YYSTACK_GAP_MAXIMUM)

/* Copy COUNT objects from FROM to TO.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if 1 < __GNUC__
#   define YYCOPY(To, From, Count) \
      __builtin_memcpy (To, From, (Count) * sizeof (*(From)))
#  else
#   define YYCOPY(To, From, Count)		\
      do					\
	{					\
	  register YYSIZE_T yyi;		\
	  for (yyi = 0; yyi < (Count); yyi++)	\
	    (To)[yyi] = (From)[yyi];		\
	}					\
      while (0)
#  endif
# endif

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack)					\
    do									\
      {									\
	YYSIZE_T yynewbytes;						\
	YYCOPY (&yyptr->Stack, Stack, yysize);				\
	Stack = &yyptr->Stack;						\
	yynewbytes = yystacksize * sizeof (*Stack) + YYSTACK_GAP_MAXIMUM; \
	yyptr += yynewbytes / sizeof (*yyptr);				\
      }									\
    while (0)

#endif

#if defined (__STDC__) || defined (__cplusplus)
   typedef signed char yysigned_char;
#else
   typedef short yysigned_char;
#endif

/* YYFINAL -- State number of the termination state. */
#define YYFINAL  2
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   79

/* YYNTOKENS -- Number of terminals. */
#define YYNTOKENS  19
/* YYNNTS -- Number of nonterminals. */
#define YYNNTS  7
/* YYNRULES -- Number of rules. */
#define YYNRULES  22
/* YYNRULES -- Number of states. */
#define YYNSTATES  44

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   263

#define YYTRANSLATE(YYX) 						\
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[YYLEX] -- Bison symbol number corresponding to YYLEX.  */
static const unsigned char yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
      14,    15,    10,     9,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,    13,
       8,     7,     2,    16,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    17,     2,    18,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,    11,    12
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const unsigned char yyprhs[] =
{
       0,     0,     3,     6,     7,    12,    18,    22,    26,    32,
      37,    39,    40,    42,    44,    48,    52,    56,    60,    64,
      66,    68,    70
};

/* YYRHS -- A `-1'-separated list of the rules' RHS. */
static const yysigned_char yyrhs[] =
{
      20,     0,    -1,    20,    21,    -1,    -1,    24,     7,    23,
      13,    -1,    12,     5,     7,    23,    13,    -1,    12,     5,
      13,    -1,    11,     5,    13,    -1,    14,    23,    15,    16,
      21,    -1,    17,    22,    20,    18,    -1,    13,    -1,    -1,
      24,    -1,    25,    -1,    23,    10,    23,    -1,    23,     9,
      23,    -1,    23,     7,    23,    -1,    23,     8,    23,    -1,
      14,    23,    15,    -1,     5,    -1,     6,    -1,     4,    -1,
       3,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const unsigned short yyrline[] =
{
       0,   284,   284,   285,   288,   289,   291,   292,   293,   294,
     295,   298,   301,   302,   303,   304,   305,   306,   307,   310,
     311,   314,   315
};
#endif

#if YYDEBUG || YYERROR_VERBOSE
/* YYTNME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals. */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "TYPE_INTEGER", "TYPE_FLOAT", "TYPE_VAR", 
  "TYPE_PARAM", "'='", "'<'", "'+'", "'*'", "INT_TK", "FLOAT_TK", "';'", 
  "'('", "')'", "'?'", "'{'", "'}'", "$accept", "gsl", "instruction", 
  "start_block", "expression", "variable", "constValue", 0
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[YYLEX-NUM] -- Internal token number corresponding to
   token YYLEX-NUM.  */
static const unsigned short yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,    61,    60,    43,
      42,   262,   263,    59,    40,    41,    63,   123,   125
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const unsigned char yyr1[] =
{
       0,    19,    20,    20,    21,    21,    21,    21,    21,    21,
      21,    22,    23,    23,    23,    23,    23,    23,    23,    24,
      24,    25,    25
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const unsigned char yyr2[] =
{
       0,     2,     2,     0,     4,     5,     3,     3,     5,     4,
       1,     0,     1,     1,     3,     3,     3,     3,     3,     1,
       1,     1,     1
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const unsigned char yydefact[] =
{
       3,     0,     1,    19,    20,     0,     0,    10,     0,    11,
       2,     0,     0,     0,    22,    21,     0,     0,    12,    13,
       3,     0,     7,     0,     6,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    18,    16,    17,    15,    14,     0,
       9,     4,     5,     8
};

/* YYDEFGOTO[NTERM-NUM]. */
static const yysigned_char yydefgoto[] =
{
      -1,     1,    10,    20,    17,    18,    19
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -8
static const yysigned_char yypact[] =
{
      -8,     1,    -8,    -8,    -8,     0,    22,    -8,     5,    -8,
      -8,    -3,     8,    13,    -8,    -8,     5,    46,    -8,    -8,
      -8,     5,    -8,     5,    -8,    50,     5,     5,     5,     5,
      15,    11,    59,    66,    -8,    -7,    -7,    23,    -8,    35,
      -8,    -8,    -8,    -8
};

/* YYPGOTO[NTERM-NUM].  */
static const yysigned_char yypgoto[] =
{
      -8,    14,    -4,    -8,    16,    -1,    -8
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -1
static const unsigned char yytable[] =
{
      11,     2,    28,    29,    21,    12,     3,     4,    14,    15,
       3,     4,     5,     6,     7,     8,     3,     4,     9,    16,
      23,    22,     5,     6,     7,     8,    24,    13,     9,    40,
      11,    39,    25,    29,    31,    43,     0,    32,    11,    33,
       3,     4,    35,    36,    37,    38,     5,     6,     7,     8,
       0,     0,     9,    26,    27,    28,    29,    26,    27,    28,
      29,    30,     0,     0,     0,    34,    26,    27,    28,    29,
       0,     0,    41,    26,    27,    28,    29,     0,     0,    42
};

static const yysigned_char yycheck[] =
{
       1,     0,     9,    10,     7,     5,     5,     6,     3,     4,
       5,     6,    11,    12,    13,    14,     5,     6,    17,    14,
       7,    13,    11,    12,    13,    14,    13,     5,    17,    18,
      31,    16,    16,    10,    20,    39,    -1,    21,    39,    23,
       5,     6,    26,    27,    28,    29,    11,    12,    13,    14,
      -1,    -1,    17,     7,     8,     9,    10,     7,     8,     9,
      10,    15,    -1,    -1,    -1,    15,     7,     8,     9,    10,
      -1,    -1,    13,     7,     8,     9,    10,    -1,    -1,    13
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const unsigned char yystos[] =
{
       0,    20,     0,     5,     6,    11,    12,    13,    14,    17,
      21,    24,     5,     5,     3,     4,    14,    23,    24,    25,
      22,     7,    13,     7,    13,    23,     7,     8,     9,    10,
      15,    20,    23,    23,    15,    23,    23,    23,    23,    16,
      18,    13,    13,    21
};

#if ! defined (YYSIZE_T) && defined (__SIZE_TYPE__)
# define YYSIZE_T __SIZE_TYPE__
#endif
#if ! defined (YYSIZE_T) && defined (size_t)
# define YYSIZE_T size_t
#endif
#if ! defined (YYSIZE_T)
# if defined (__STDC__) || defined (__cplusplus)
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# endif
#endif
#if ! defined (YYSIZE_T)
# define YYSIZE_T unsigned int
#endif

#define yyerrok		(yyerrstatus = 0)
#define yyclearin	(yychar = YYEMPTY)
#define YYEMPTY		(-2)
#define YYEOF		0

#define YYACCEPT	goto yyacceptlab
#define YYABORT		goto yyabortlab
#define YYERROR		goto yyerrlab1


/* Like YYERROR except do call yyerror.  This remains here temporarily
   to ease the transition to the new meaning of YYERROR, for GCC.
   Once GCC version 2 has supplanted version 1, this can go.  */

#define YYFAIL		goto yyerrlab

#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)					\
do								\
  if (yychar == YYEMPTY && yylen == 1)				\
    {								\
      yychar = (Token);						\
      yylval = (Value);						\
      yytoken = YYTRANSLATE (yychar);				\
      YYPOPSTACK;						\
      goto yybackup;						\
    }								\
  else								\
    { 								\
      yyerror ("syntax error: cannot back up");\
      YYERROR;							\
    }								\
while (0)

#define YYTERROR	1
#define YYERRCODE	256

/* YYLLOC_DEFAULT -- Compute the default location (before the actions
   are run).  */

#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)         \
  Current.first_line   = Rhs[1].first_line;      \
  Current.first_column = Rhs[1].first_column;    \
  Current.last_line    = Rhs[N].last_line;       \
  Current.last_column  = Rhs[N].last_column;
#endif

/* YYLEX -- calling `yylex' with the right arguments.  */

#ifdef YYLEX_PARAM
# define YYLEX yylex (YYLEX_PARAM)
#else
# define YYLEX yylex ()
#endif

/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)			\
do {						\
  if (yydebug)					\
    YYFPRINTF Args;				\
} while (0)

# define YYDSYMPRINT(Args)			\
do {						\
  if (yydebug)					\
    yysymprint Args;				\
} while (0)

# define YYDSYMPRINTF(Title, Token, Value, Location)		\
do {								\
  if (yydebug)							\
    {								\
      YYFPRINTF (stderr, "%s ", Title);				\
      yysymprint (stderr, 					\
                  Token, Value);	\
      YYFPRINTF (stderr, "\n");					\
    }								\
} while (0)

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (cinluded).                                                   |
`------------------------------------------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yy_stack_print (short *bottom, short *top)
#else
static void
yy_stack_print (bottom, top)
    short *bottom;
    short *top;
#endif
{
  YYFPRINTF (stderr, "Stack now");
  for (/* Nothing. */; bottom <= top; ++bottom)
    YYFPRINTF (stderr, " %d", *bottom);
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)				\
do {								\
  if (yydebug)							\
    yy_stack_print ((Bottom), (Top));				\
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yy_reduce_print (int yyrule)
#else
static void
yy_reduce_print (yyrule)
    int yyrule;
#endif
{
  int yyi;
  unsigned int yylineno = yyrline[yyrule];
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %u), ",
             yyrule - 1, yylineno);
  /* Print the symbols being reduced, and their result.  */
  for (yyi = yyprhs[yyrule]; 0 <= yyrhs[yyi]; yyi++)
    YYFPRINTF (stderr, "%s ", yytname [yyrhs[yyi]]);
  YYFPRINTF (stderr, "-> %s\n", yytname [yyr1[yyrule]]);
}

# define YY_REDUCE_PRINT(Rule)		\
do {					\
  if (yydebug)				\
    yy_reduce_print (Rule);		\
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args)
# define YYDSYMPRINT(Args)
# define YYDSYMPRINTF(Title, Token, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef	YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   SIZE_MAX < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#if YYMAXDEPTH == 0
# undef YYMAXDEPTH
#endif

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif



#if YYERROR_VERBOSE

# ifndef yystrlen
#  if defined (__GLIBC__) && defined (_STRING_H)
#   define yystrlen strlen
#  else
/* Return the length of YYSTR.  */
static YYSIZE_T
#   if defined (__STDC__) || defined (__cplusplus)
yystrlen (const char *yystr)
#   else
yystrlen (yystr)
     const char *yystr;
#   endif
{
  register const char *yys = yystr;

  while (*yys++ != '\0')
    continue;

  return yys - yystr - 1;
}
#  endif
# endif

# ifndef yystpcpy
#  if defined (__GLIBC__) && defined (_STRING_H) && defined (_GNU_SOURCE)
#   define yystpcpy stpcpy
#  else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
static char *
#   if defined (__STDC__) || defined (__cplusplus)
yystpcpy (char *yydest, const char *yysrc)
#   else
yystpcpy (yydest, yysrc)
     char *yydest;
     const char *yysrc;
#   endif
{
  register char *yyd = yydest;
  register const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
#  endif
# endif

#endif /* !YYERROR_VERBOSE */



#if YYDEBUG
/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yysymprint (FILE *yyoutput, int yytype, YYSTYPE *yyvaluep)
#else
static void
yysymprint (yyoutput, yytype, yyvaluep)
    FILE *yyoutput;
    int yytype;
    YYSTYPE *yyvaluep;
#endif
{
  /* Pacify ``unused variable'' warnings.  */
  (void) yyvaluep;

  if (yytype < YYNTOKENS)
    {
      YYFPRINTF (yyoutput, "token %s (", yytname[yytype]);
# ifdef YYPRINT
      YYPRINT (yyoutput, yytoknum[yytype], *yyvaluep);
# endif
    }
  else
    YYFPRINTF (yyoutput, "nterm %s (", yytname[yytype]);

  switch (yytype)
    {
      default:
        break;
    }
  YYFPRINTF (yyoutput, ")");
}

#endif /* ! YYDEBUG */
/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yydestruct (int yytype, YYSTYPE *yyvaluep)
#else
static void
yydestruct (yytype, yyvaluep)
    int yytype;
    YYSTYPE *yyvaluep;
#endif
{
  /* Pacify ``unused variable'' warnings.  */
  (void) yyvaluep;

  switch (yytype)
    {

      default:
        break;
    }
}


/* Prevent warnings from -Wmissing-prototypes.  */

#ifdef YYPARSE_PARAM
# if defined (__STDC__) || defined (__cplusplus)
int yyparse (void *YYPARSE_PARAM);
# else
int yyparse ();
# endif
#else /* ! YYPARSE_PARAM */
#if defined (__STDC__) || defined (__cplusplus)
int yyparse (void);
#else
int yyparse ();
#endif
#endif /* ! YYPARSE_PARAM */



/* The lookahead symbol.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;

/* Number of syntax errors so far.  */
int yynerrs;



/*----------.
| yyparse.  |
`----------*/

#ifdef YYPARSE_PARAM
# if defined (__STDC__) || defined (__cplusplus)
int yyparse (void *YYPARSE_PARAM)
# else
int yyparse (YYPARSE_PARAM)
  void *YYPARSE_PARAM;
# endif
#else /* ! YYPARSE_PARAM */
#if defined (__STDC__) || defined (__cplusplus)
int
yyparse (void)
#else
int
yyparse ()

#endif
#endif
{
  
  register int yystate;
  register int yyn;
  int yyresult;
  /* Number of tokens to shift before error messages enabled.  */
  int yyerrstatus;
  /* Lookahead token as an internal (translated) token number.  */
  int yytoken = 0;

  /* Three stacks and their tools:
     `yyss': related to states,
     `yyvs': related to semantic values,
     `yyls': related to locations.

     Refer to the stacks thru separate pointers, to allow yyoverflow
     to reallocate them elsewhere.  */

  /* The state stack.  */
  short	yyssa[YYINITDEPTH];
  short *yyss = yyssa;
  register short *yyssp;

  /* The semantic value stack.  */
  YYSTYPE yyvsa[YYINITDEPTH];
  YYSTYPE *yyvs = yyvsa;
  register YYSTYPE *yyvsp;



#define YYPOPSTACK   (yyvsp--, yyssp--)

  YYSIZE_T yystacksize = YYINITDEPTH;

  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;


  /* When reducing, the number of symbols on the RHS of the reduced
     rule.  */
  int yylen;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY;		/* Cause a token to be read.  */

  /* Initialize stack pointers.
     Waste one element of value and location stack
     so that they stay on the same level as the state stack.
     The wasted elements are never initialized.  */

  yyssp = yyss;
  yyvsp = yyvs;

  goto yysetstate;

/*------------------------------------------------------------.
| yynewstate -- Push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
 yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed. so pushing a state here evens the stacks.
     */
  yyssp++;

 yysetstate:
  *yyssp = yystate;

  if (yyss + yystacksize - 1 <= yyssp)
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYSIZE_T yysize = yyssp - yyss + 1;

#ifdef yyoverflow
      {
	/* Give user a chance to reallocate the stack. Use copies of
	   these so that the &'s don't force the real ones into
	   memory.  */
	YYSTYPE *yyvs1 = yyvs;
	short *yyss1 = yyss;


	/* Each stack pointer address is followed by the size of the
	   data in use in that stack, in bytes.  This used to be a
	   conditional around just the two extra args, but that might
	   be undefined if yyoverflow is a macro.  */
	yyoverflow ("parser stack overflow",
		    &yyss1, yysize * sizeof (*yyssp),
		    &yyvs1, yysize * sizeof (*yyvsp),

		    &yystacksize);

	yyss = yyss1;
	yyvs = yyvs1;
      }
#else /* no yyoverflow */
# ifndef YYSTACK_RELOCATE
      goto yyoverflowlab;
# else
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
	goto yyoverflowlab;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
	yystacksize = YYMAXDEPTH;

      {
	short *yyss1 = yyss;
	union yyalloc *yyptr =
	  (union yyalloc *) YYSTACK_ALLOC (YYSTACK_BYTES (yystacksize));
	if (! yyptr)
	  goto yyoverflowlab;
	YYSTACK_RELOCATE (yyss);
	YYSTACK_RELOCATE (yyvs);

#  undef YYSTACK_RELOCATE
	if (yyss1 != yyssa)
	  YYSTACK_FREE (yyss1);
      }
# endif
#endif /* no yyoverflow */

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;


      YYDPRINTF ((stderr, "Stack size increased to %lu\n",
		  (unsigned long int) yystacksize));

      if (yyss + yystacksize - 1 <= yyssp)
	YYABORT;
    }

  YYDPRINTF ((stderr, "Entering state %d\n", yystate));

  goto yybackup;

/*-----------.
| yybackup.  |
`-----------*/
yybackup:

/* Do appropriate processing given the current state.  */
/* Read a lookahead token if we need one and don't already have one.  */
/* yyresume: */

  /* First try to decide what to do without reference to lookahead token.  */

  yyn = yypact[yystate];
  if (yyn == YYPACT_NINF)
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either YYEMPTY or YYEOF or a valid lookahead symbol.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token: "));
      yychar = YYLEX;
    }

  if (yychar <= YYEOF)
    {
      yychar = yytoken = YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YYDSYMPRINTF ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yyn == 0 || yyn == YYTABLE_NINF)
	goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  /* Shift the lookahead token.  */
  YYDPRINTF ((stderr, "Shifting token %s, ", yytname[yytoken]));

  /* Discard the token being shifted unless it is eof.  */
  if (yychar != YYEOF)
    yychar = YYEMPTY;

  *++yyvsp = yylval;


  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  yystate = yyn;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- Do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     `$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
        case 2:
#line 284 "goom_script_scanner.y"
    { gsl_append(yyvsp[0].nPtr); }
    break;

  case 4:
#line 288 "goom_script_scanner.y"
    { yyval.nPtr = new_set(yyvsp[-3].nPtr,yyvsp[-1].nPtr); }
    break;

  case 5:
#line 289 "goom_script_scanner.y"
    { yyval.nPtr = new_float_decl(new_var(yyvsp[-3].strValue));
                                                      yyval.nPtr->opr.next = new_set(new_var(yyvsp[-3].strValue), yyvsp[-1].nPtr); }
    break;

  case 6:
#line 291 "goom_script_scanner.y"
    { yyval.nPtr = new_float_decl(new_var(yyvsp[-1].strValue)); }
    break;

  case 7:
#line 292 "goom_script_scanner.y"
    { yyval.nPtr = new_int_decl(new_var(yyvsp[-1].strValue)); }
    break;

  case 8:
#line 293 "goom_script_scanner.y"
    { yyval.nPtr = new_if(yyvsp[-3].nPtr,yyvsp[0].nPtr); }
    break;

  case 9:
#line 294 "goom_script_scanner.y"
    { lastNode = yyvsp[-2].nPtr->opr.op[1]; yyval.nPtr=yyvsp[-2].nPtr; }
    break;

  case 10:
#line 295 "goom_script_scanner.y"
    { yyval.nPtr = new_nop("nop"); }
    break;

  case 11:
#line 298 "goom_script_scanner.y"
    { yyval.nPtr = new_block(lastNode); lastNode = yyval.nPtr->opr.op[0]; }
    break;

  case 12:
#line 301 "goom_script_scanner.y"
    { yyval.nPtr = yyvsp[0].nPtr; }
    break;

  case 13:
#line 302 "goom_script_scanner.y"
    { yyval.nPtr = yyvsp[0].nPtr; }
    break;

  case 14:
#line 303 "goom_script_scanner.y"
    { yyval.nPtr = new_mul(yyvsp[-2].nPtr,yyvsp[0].nPtr); }
    break;

  case 15:
#line 304 "goom_script_scanner.y"
    { yyval.nPtr = new_add(yyvsp[-2].nPtr,yyvsp[0].nPtr); }
    break;

  case 16:
#line 305 "goom_script_scanner.y"
    { yyval.nPtr = new_equ(yyvsp[-2].nPtr,yyvsp[0].nPtr); }
    break;

  case 17:
#line 306 "goom_script_scanner.y"
    { yyval.nPtr = new_low(yyvsp[-2].nPtr,yyvsp[0].nPtr); }
    break;

  case 18:
#line 307 "goom_script_scanner.y"
    { yyval.nPtr = yyvsp[-1].nPtr; }
    break;

  case 19:
#line 310 "goom_script_scanner.y"
    { yyval.nPtr = new_var(yyvsp[0].strValue);   }
    break;

  case 20:
#line 311 "goom_script_scanner.y"
    { yyval.nPtr = new_param(yyvsp[0].strValue); }
    break;

  case 21:
#line 314 "goom_script_scanner.y"
    { yyval.nPtr = new_constFloat(yyvsp[0].strValue); }
    break;

  case 22:
#line 315 "goom_script_scanner.y"
    { yyval.nPtr = new_constInt(yyvsp[0].strValue); }
    break;


    }

/* Line 999 of yacc.c.  */
#line 1376 "goom_script_scanner.tab.c"

  yyvsp -= yylen;
  yyssp -= yylen;


  YY_STACK_PRINT (yyss, yyssp);

  *++yyvsp = yyval;


  /* Now `shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */

  yyn = yyr1[yyn];

  yystate = yypgoto[yyn - YYNTOKENS] + *yyssp;
  if (0 <= yystate && yystate <= YYLAST && yycheck[yystate] == *yyssp)
    yystate = yytable[yystate];
  else
    yystate = yydefgoto[yyn - YYNTOKENS];

  goto yynewstate;


/*------------------------------------.
| yyerrlab -- here on detecting error |
`------------------------------------*/
yyerrlab:
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
#if YYERROR_VERBOSE
      yyn = yypact[yystate];

      if (YYPACT_NINF < yyn && yyn < YYLAST)
	{
	  YYSIZE_T yysize = 0;
	  int yytype = YYTRANSLATE (yychar);
	  char *yymsg;
	  int yyx, yycount;

	  yycount = 0;
	  /* Start YYX at -YYN if negative to avoid negative indexes in
	     YYCHECK.  */
	  for (yyx = yyn < 0 ? -yyn : 0;
	       yyx < (int) (sizeof (yytname) / sizeof (char *)); yyx++)
	    if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR)
	      yysize += yystrlen (yytname[yyx]) + 15, yycount++;
	  yysize += yystrlen ("syntax error, unexpected ") + 1;
	  yysize += yystrlen (yytname[yytype]);
	  yymsg = (char *) YYSTACK_ALLOC (yysize);
	  if (yymsg != 0)
	    {
	      char *yyp = yystpcpy (yymsg, "syntax error, unexpected ");
	      yyp = yystpcpy (yyp, yytname[yytype]);

	      if (yycount < 5)
		{
		  yycount = 0;
		  for (yyx = yyn < 0 ? -yyn : 0;
		       yyx < (int) (sizeof (yytname) / sizeof (char *));
		       yyx++)
		    if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR)
		      {
			const char *yyq = ! yycount ? ", expecting " : " or ";
			yyp = yystpcpy (yyp, yyq);
			yyp = yystpcpy (yyp, yytname[yyx]);
			yycount++;
		      }
		}
	      yyerror (yymsg);
	      YYSTACK_FREE (yymsg);
	    }
	  else
	    yyerror ("syntax error; also virtual memory exhausted");
	}
      else
#endif /* YYERROR_VERBOSE */
	yyerror ("syntax error");
    }



  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
	 error, discard it.  */

      /* Return failure if at end of input.  */
      if (yychar == YYEOF)
        {
	  /* Pop the error token.  */
          YYPOPSTACK;
	  /* Pop the rest of the stack.  */
	  while (yyss < yyssp)
	    {
	      YYDSYMPRINTF ("Error: popping", yystos[*yyssp], yyvsp, yylsp);
	      yydestruct (yystos[*yyssp], yyvsp);
	      YYPOPSTACK;
	    }
	  YYABORT;
        }

      YYDSYMPRINTF ("Error: discarding", yytoken, &yylval, &yylloc);
      yydestruct (yytoken, &yylval);
      yychar = YYEMPTY;

    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*----------------------------------------------------.
| yyerrlab1 -- error raised explicitly by an action.  |
`----------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;	/* Each real token shifted decrements this.  */

  for (;;)
    {
      yyn = yypact[yystate];
      if (yyn != YYPACT_NINF)
	{
	  yyn += YYTERROR;
	  if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYTERROR)
	    {
	      yyn = yytable[yyn];
	      if (0 < yyn)
		break;
	    }
	}

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
	YYABORT;

      YYDSYMPRINTF ("Error: popping", yystos[*yyssp], yyvsp, yylsp);
      yydestruct (yystos[yystate], yyvsp);
      yyvsp--;
      yystate = *--yyssp;

      YY_STACK_PRINT (yyss, yyssp);
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  YYDPRINTF ((stderr, "Shifting error token, "));

  *++yyvsp = yylval;


  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturn;

/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturn;

#ifndef yyoverflow
/*----------------------------------------------.
| yyoverflowlab -- parser overflow comes here.  |
`----------------------------------------------*/
yyoverflowlab:
  yyerror ("parser stack overflow");
  yyresult = 2;
  /* Fall through.  */
#endif

yyreturn:
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
  return yyresult;
}


#line 318 "goom_script_scanner.y"


    NodeType *nodeNew(const char *str, int type) {
        NodeType *node = (NodeType*)malloc(sizeof(NodeType));
        node->type = type;
        node->str  = (char*)malloc(strlen(str)+1);
        strcpy(node->str, str);
        return node;
    }

    void nodeFreeInternals(NodeType *node) {
        free(node->str);
    }
    
    void nodeFree(NodeType *node) {
        nodeFreeInternals(node);
        free(node);
    }

    NodeType *new_constInt(const char *str) {
        NodeType *node = nodeNew(str, CONST_INT_NODE);
        node->constInt.val = atoi(str);
        return node;
    }

    NodeType *new_constFloat(const char *str) {
        NodeType *node = nodeNew(str, CONST_FLOAT_NODE);
        node->constFloat.val = atof(str);
        return node;
    }

    NodeType *new_var(const char *str) {
        NodeType *node = nodeNew(str, VAR_NODE);
        return node;
    }
    
    NodeType *new_param(const char *str) {
        NodeType *node = nodeNew(str, PARAM_NODE);
        return node;
    }
    
    NodeType *new_nop(const char *str) {
        NodeType *node = new_op(str, EMPTY_NODE, 0);
        return node;
    }
    
    NodeType *new_op(const char *str, int type, int nbOp) {
        int i;
        NodeType *node = nodeNew(str, OPR_NODE);
        node->opr.next = 0;
        node->opr.type = type;
        node->opr.nbOp = nbOp;
        for (i=0;i<nbOp;++i) node->opr.op[i] = 0;
        return node;
    }

void yyerror(char *str) {
    fprintf(stderr, "GSL ERROR: %s\n", str);
}


