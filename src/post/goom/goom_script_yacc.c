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
     FLOAT_TK = 263,
     ARROW_TK = 264
   };
#endif
#define TYPE_INTEGER 258
#define TYPE_FLOAT 259
#define TYPE_VAR 260
#define TYPE_PARAM 261
#define INT_TK 262
#define FLOAT_TK 263
#define ARROW_TK 264




/* Copy the first part of user declarations.  */
#line 6 "goom_script_yacc.y"

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
    static NodeType *new_read_param(const char *str);
    static NodeType *new_nop(const char *str);
    static NodeType *new_op(const char *str, int type, int nbOp);

    static int allocateLabel(void);
    #define allocateTemp allocateLabel

    /* SETTER */
    static NodeType *new_set(NodeType *lvalue, NodeType *expression) {
        NodeType *set = new_op("set", OPR_SET, 2);
        set->unode.opr.op[0] = lvalue;
        set->unode.opr.op[1] = expression;
        return set;
    }
    static void commit_set(NodeType *set) {
        precommit_node(set->unode.opr.op[1]);
#ifdef VERBOSE
        printf("set.f %s %s\n", set->unode.opr.op[0]->str, set->unode.opr.op[1]->str);
#endif
        currentScanner->instr = instr_init(currentScanner, "set.f", INSTR_SETF, 2);
        commit_node(set->unode.opr.op[0]);
        commit_node(set->unode.opr.op[1]);
    }

    /* FLOAT */
    static NodeType *new_float_decl(NodeType *name) {
        NodeType *fld = new_op("float", OPR_DECLARE_FLOAT, 1);
        fld->unode.opr.op[0] = name;
        return fld;
    }
    static void commit_float(NodeType *var) {
#ifdef VERBOSE
        printf("float %s\n", var->unode.opr.op[0]->str);
#endif
        currentScanner->instr = instr_init(currentScanner, "float", INSTR_INT, 1);
        commit_node(var->unode.opr.op[0]);
    }
    
    /* INT */
    static NodeType *new_int_decl(NodeType *name) {
        NodeType *intd = new_op("int", OPR_DECLARE_INT, 1);
        intd->unode.opr.op[0] = name;
        return intd;
    }
    static void commit_int(NodeType *var) {
#ifdef VERBOSE
        printf("int %s\n", var->unode.opr.op[0]->str);
#endif
        currentScanner->instr = instr_init(currentScanner, "int", INSTR_INT, 1);
        commit_node(var->unode.opr.op[0]);
    }

    /* precommit read_param: a read param is a param for reading.
     *  precommit copy it to a temporary variable.
     */
    static void precommit_read_param(NodeType *rparam) {
        char stmp[256];
        NodeType *tmp;
        
        /* declare a float to store the result */
        sprintf(stmp,"__tmp%i",allocateTemp());
        commit_node(new_float_decl(new_var(stmp)));
        /* set the float to the value of "op1" */
        commit_node(new_set(new_var(stmp),new_param(rparam->str)));

        /* redefine the ADD node now as the computed variable */
        nodeFreeInternals(rparam);
        tmp = new_var(stmp);
        *rparam = *tmp;
        free(tmp);
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
        precommit_node(expr->unode.opr.op[0]);
        precommit_node(expr->unode.opr.op[1]);

        if (is_tmp_expr(expr->unode.opr.op[0])) {
            strcpy(stmp, expr->unode.opr.op[0]->str);
            toAdd = 1;
        }
        else if (is_tmp_expr(expr->unode.opr.op[1])) {
            strcpy(stmp,expr->unode.opr.op[1]->str);
            toAdd = 0;
        }
        else {
            /* declare a float to store the result */
            sprintf(stmp,"__tmp%i",allocateTemp());
            commit_node(new_float_decl(new_var(stmp)));
            /* set the float to the value of "op1" */
            commit_node(new_set(new_var(stmp),expr->unode.opr.op[0]));
            toAdd = 1;
        }

        /* add op2 to tmp */
#ifdef VERBOSE
        printf("%s %s %s\n", type, stmp, expr->unode.opr.op[toAdd]->str);
#endif
        currentScanner->instr = instr_init(currentScanner, type, instr_id, 2);
        commit_node(new_var(stmp));
        commit_node(expr->unode.opr.op[toAdd]);
    
        /* redefine the ADD node now as the computed variable */
        nodeFreeInternals(expr);
        tmp = new_var(stmp);
        *expr = *tmp;
        free(tmp);
    }

    static NodeType *new_expr2(const char *name, int id, NodeType *expr1, NodeType *expr2) {
        NodeType *add = new_op(name, id, 2);
        add->unode.opr.op[0] = expr1;
        add->unode.opr.op[1] = expr2;
        return add;
    }

    /* ADD */
    static NodeType *new_add(NodeType *expr1, NodeType *expr2) {
        return new_expr2("add.f", OPR_ADD, expr1, expr2);
    }
    static void precommit_add(NodeType *add) {
        precommit_expr(add,"add.f",INSTR_ADDF);
    }

    /* SUB */
    static NodeType *new_sub(NodeType *expr1, NodeType *expr2) {
        return new_expr2("sub.f", OPR_SUB, expr1, expr2);
    }
    static void precommit_sub(NodeType *sub) {
        precommit_expr(sub,"sub.f",INSTR_SUBF);
    }

    /* MUL */
    static NodeType *new_mul(NodeType *expr1, NodeType *expr2) {
        return new_expr2("mul.f", OPR_MUL, expr1, expr2);
    }
    static void precommit_mul(NodeType *mul) {
        precommit_expr(mul,"mul.f",INSTR_MULF);
    }
    
    /* DIV */
    static NodeType *new_div(NodeType *expr1, NodeType *expr2) {
        return new_expr2("div.f", OPR_DIV, expr1, expr2);
    }
    static void precommit_div(NodeType *mul) {
        precommit_expr(mul,"div.f",INSTR_DIVF);
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
        node->unode.opr.op[0] = expression;
        node->unode.opr.op[1] = instr;
        return node;
    }
    static void commit_if(NodeType *node) {

        char slab[1024];
        precommit_node(node->unode.opr.op[0]);

        /* jzero.i <expression> <endif> */
        sprintf(slab, "|eif%d|", allocateLabel());
#ifdef VERBOSE
        printf("jzero.i %s %s\n", node->unode.opr.op[0]->str, slab);
#endif
        currentScanner->instr = instr_init(currentScanner, "jzero.i", INSTR_JZERO, 2);
        commit_node(node->unode.opr.op[0]);
        instr_add_param(currentScanner->instr, slab, TYPE_LABEL);

        /* [... instrs of the if ...] */
        commit_node(node->unode.opr.op[1]);
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
        blk->unode.opr.op[0] = new_nop("start_of_block");
        blk->unode.opr.op[1] = lastNode;        
        return blk;
    }
    static void commit_block(NodeType *node) {
        commit_node(node->unode.opr.op[0]->unode.opr.next);
    }

    /* FUNCTION INTRO */
    static NodeType *new_function_intro(const char *name) {
        char stmp[256];
        if (strlen(name) < 200) {
           sprintf(stmp, "|__func_%s|", name);
        }
        return new_op(stmp, OPR_FUNC_INTRO, 0);
    }
    static void commit_function_intro(NodeType *node) {
        currentScanner->instr = instr_init(currentScanner, "label", INSTR_LABEL, 1);
        instr_add_param(currentScanner->instr, node->str, TYPE_LABEL);
#ifdef VERBOSE
        printf("label %s\n", node->str);
#endif
    }

    /* FUNCTION OUTRO */
    static NodeType *new_function_outro(void) {
        return new_op("ret", OPR_FUNC_OUTRO, 0);
    }
    static void commit_function_outro(NodeType *node) {
        currentScanner->instr = instr_init(currentScanner, "ret", INSTR_RET, 1);
        instr_add_param(currentScanner->instr, "|dummy|", TYPE_LABEL);
#ifdef VERBOSE
        printf("ret\n");
#endif
    }
    
    /* FUNCTION CALL */
    static NodeType *new_call(const char *name) {
        char stmp[256];
        if (strlen(name) < 200) {
           sprintf(stmp, "|__func_%s|", name);
        }
        return new_op(stmp, OPR_CALL, 0);
    }
    static void commit_call(NodeType *node) {
        currentScanner->instr = instr_init(currentScanner, "call", INSTR_CALL, 1);
        instr_add_param(currentScanner->instr, node->str, TYPE_LABEL);
#ifdef VERBOSE
        printf("call %s\n", node->str);
#endif
    }
    

    /** **/

    static NodeType *rootNode = 0; // TODO: reinitialiser a chaque compilation.
    static NodeType *lastNode = 0;
    static NodeType *gsl_append(NodeType *curNode) {
        if (lastNode)
            lastNode->unode.opr.next = curNode;
        lastNode = curNode;
        while(lastNode->unode.opr.next) lastNode = lastNode->unode.opr.next;
        if (rootNode == 0)
            rootNode = curNode;
        return curNode;
    }

    static int lastLabel = 0;
    int allocateLabel(void) {
        return ++lastLabel;
    }

    void gsl_commit_compilation(void) {
        commit_node(rootNode);
        rootNode = 0;
        lastNode = 0;
    }
    
    void precommit_node(NodeType *node) {
        /* do here stuff for expression.. for exemple */
        switch(node->type) {
            case OPR_NODE:
                switch(node->unode.opr.type) {
                    case OPR_ADD: precommit_add(node); break;
                    case OPR_SUB: precommit_sub(node); break;
                    case OPR_MUL: precommit_mul(node); break;
                    case OPR_DIV: precommit_div(node); break;
                    case OPR_EQU: precommit_equ(node); break;
                    case OPR_LOW: precommit_low(node); break;
                }
                break;
            case READ_PARAM_NODE:
                precommit_read_param(node); break;
        }
    }
    
    void commit_node(NodeType *node) {

        if (node == 0) return;
        
        switch(node->type) {
            case OPR_NODE:
                switch(node->unode.opr.type) {
                    case OPR_DECLARE_FLOAT: commit_float(node); break;
                    case OPR_DECLARE_INT:   commit_int(node);   break;
                    case OPR_SET:           commit_set(node); break;
                    case OPR_IF:            commit_if(node); break;
                    case OPR_BLOCK:         commit_block(node); break;
                    case OPR_FUNC_INTRO:    commit_function_intro(node); break;
                    case OPR_FUNC_OUTRO:    commit_function_outro(node); break;
                    case OPR_CALL:          commit_call(node); break;
#ifdef VERBOSE
                    case EMPTY_NODE:        printf("NOP\n"); break;
#endif
                }

                commit_node(node->unode.opr.next); /* recursive for the moment, maybe better to do something iterative? */
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
#line 361 "goom_script_yacc.y"
typedef union YYSTYPE {
    int intValue;
    float floatValue;
    char charValue;
    char strValue[2048];
    NodeType *nPtr;
  } YYSTYPE;
/* Line 191 of yacc.c.  */
#line 457 "y.tab.c"
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif



/* Copy the second part of user declarations.  */


/* Line 214 of yacc.c.  */
#line 469 "y.tab.c"

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
#define YYFINAL  3
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   80

/* YYNTOKENS -- Number of terminals. */
#define YYNTOKENS  23
/* YYNNTS -- Number of nonterminals. */
#define YYNNTS  13
/* YYNRULES -- Number of rules. */
#define YYNRULES  34
/* YYNRULES -- Number of states. */
#define YYNSTATES  65

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   264

#define YYTRANSLATE(YYX) 						\
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[YYLEX] -- Bison symbol number corresponding to YYLEX.  */
static const unsigned char yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
      18,    19,    15,    13,     2,    14,     2,    16,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,    17,
      11,    10,    12,    20,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    21,     2,    22,     2,     2,     2,     2,
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
       5,     6,     7,     8,     9
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const unsigned char yyprhs[] =
{
       0,     0,     3,     7,    10,    11,    14,    15,    19,    23,
      24,    29,    35,    39,    43,    49,    54,    58,    60,    61,
      63,    65,    69,    73,    77,    81,    85,    89,    93,    97,
      99,   101,   103,   105,   107
};

/* YYRHS -- A `-1'-separated list of the rules' RHS. */
static const yysigned_char yyrhs[] =
{
      24,     0,    -1,    25,    29,    26,    -1,    25,    30,    -1,
      -1,    26,    27,    -1,    -1,    28,    25,    29,    -1,    11,
       5,    12,    -1,    -1,    34,    10,    32,    17,    -1,     8,
       5,    10,    32,    17,    -1,     8,     5,    17,    -1,     7,
       5,    17,    -1,    18,    32,    19,    20,    30,    -1,    21,
      31,    24,    22,    -1,     9,     5,    17,    -1,    17,    -1,
      -1,    33,    -1,    35,    -1,    32,    15,    32,    -1,    32,
      16,    32,    -1,    32,    13,    32,    -1,    32,    14,    32,
      -1,    32,    10,    32,    -1,    32,    11,    32,    -1,    32,
      12,    32,    -1,    18,    32,    19,    -1,     5,    -1,     6,
      -1,     5,    -1,     6,    -1,     4,    -1,     3,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const unsigned short yyrline[] =
{
       0,   384,   384,   386,   387,   390,   391,   394,   396,   397,
     399,   400,   402,   403,   404,   405,   406,   407,   410,   413,
     414,   415,   416,   417,   418,   419,   420,   421,   422,   425,
     426,   429,   430,   433,   434
};
#endif

#if YYDEBUG || YYERROR_VERBOSE
/* YYTNME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals. */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "TYPE_INTEGER", "TYPE_FLOAT", "TYPE_VAR", 
  "TYPE_PARAM", "INT_TK", "FLOAT_TK", "ARROW_TK", "'='", "'<'", "'>'", 
  "'+'", "'-'", "'*'", "'/'", "';'", "'('", "')'", "'?'", "'{'", "'}'", 
  "$accept", "gsl", "gsl_code", "gsl_def_functions", "function", 
  "function_intro", "function_outro", "instruction", "start_block", 
  "expression", "read_variable", "write_variable", "constValue", 0
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[YYLEX-NUM] -- Internal token number corresponding to
   token YYLEX-NUM.  */
static const unsigned short yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
      61,    60,    62,    43,    45,    42,    47,    59,    40,    41,
      63,   123,   125
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const unsigned char yyr1[] =
{
       0,    23,    24,    25,    25,    26,    26,    27,    28,    29,
      30,    30,    30,    30,    30,    30,    30,    30,    31,    32,
      32,    32,    32,    32,    32,    32,    32,    32,    32,    33,
      33,    34,    34,    35,    35
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const unsigned char yyr2[] =
{
       0,     2,     3,     2,     0,     2,     0,     3,     3,     0,
       4,     5,     3,     3,     5,     4,     3,     1,     0,     1,
       1,     3,     3,     3,     3,     3,     3,     3,     3,     1,
       1,     1,     1,     1,     1
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const unsigned char yydefact[] =
{
       4,     0,     9,     1,    31,    32,     0,     0,     0,    17,
       0,    18,     6,     3,     0,     0,     0,     0,    34,    33,
      29,    30,     0,     0,    19,    20,     4,     2,     0,    13,
       0,    12,    16,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     5,     4,     0,     0,    28,    25,
      26,    27,    23,    24,    21,    22,     0,    15,     0,     9,
      10,    11,    14,     8,     7
};

/* YYDEFGOTO[NTERM-NUM]. */
static const yysigned_char yydefgoto[] =
{
      -1,     1,     2,    27,    44,    45,    12,    13,    26,    23,
      24,    14,    25
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -23
static const yysigned_char yypact[] =
{
     -23,     1,    14,   -23,   -23,   -23,     0,     2,     5,   -23,
      21,   -23,   -23,   -23,    18,    -6,    -8,    12,   -23,   -23,
     -23,   -23,    21,    30,   -23,   -23,   -23,    19,    21,   -23,
      21,   -23,   -23,    40,    21,    21,    21,    21,    21,    21,
      21,    13,    15,    29,   -23,   -23,    50,    58,   -23,    63,
      63,    63,   -12,   -12,    20,   -23,    14,   -23,    26,    14,
     -23,   -23,   -23,   -23,   -23
};

/* YYPGOTO[NTERM-NUM].  */
static const yysigned_char yypgoto[] =
{
     -23,    22,    35,   -23,   -23,   -23,    -2,    -9,   -23,   -22,
     -23,   -23,   -23
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -1
static const unsigned char yytable[] =
{
      33,     3,    30,    39,    40,    15,    46,    16,    47,    31,
      17,    29,    49,    50,    51,    52,    53,    54,    55,     4,
       5,     6,     7,     8,    18,    19,    20,    21,    28,    32,
      43,     9,    10,    56,    58,    11,    40,    57,    63,    22,
      34,    35,    36,    37,    38,    39,    40,    62,    42,    41,
      34,    35,    36,    37,    38,    39,    40,    64,     0,    48,
      34,    35,    36,    37,    38,    39,    40,    60,    34,    35,
      36,    37,    38,    39,    40,    61,    37,    38,    39,    40,
      59
};

static const yysigned_char yycheck[] =
{
      22,     0,    10,    15,    16,     5,    28,     5,    30,    17,
       5,    17,    34,    35,    36,    37,    38,    39,    40,     5,
       6,     7,     8,     9,     3,     4,     5,     6,    10,    17,
      11,    17,    18,    20,     5,    21,    16,    22,    12,    18,
      10,    11,    12,    13,    14,    15,    16,    56,    26,    19,
      10,    11,    12,    13,    14,    15,    16,    59,    -1,    19,
      10,    11,    12,    13,    14,    15,    16,    17,    10,    11,
      12,    13,    14,    15,    16,    17,    13,    14,    15,    16,
      45
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const unsigned char yystos[] =
{
       0,    24,    25,     0,     5,     6,     7,     8,     9,    17,
      18,    21,    29,    30,    34,     5,     5,     5,     3,     4,
       5,     6,    18,    32,    33,    35,    31,    26,    10,    17,
      10,    17,    17,    32,    10,    11,    12,    13,    14,    15,
      16,    19,    24,    11,    27,    28,    32,    32,    19,    32,
      32,    32,    32,    32,    32,    32,    20,    22,     5,    25,
      17,    17,    30,    12,    29
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
        case 3:
#line 386 "goom_script_yacc.y"
    { gsl_append(yyvsp[0].nPtr); }
    break;

  case 8:
#line 396 "goom_script_yacc.y"
    { gsl_append(new_function_intro(yyvsp[-1].strValue)); }
    break;

  case 9:
#line 397 "goom_script_yacc.y"
    { gsl_append(new_function_outro()); }
    break;

  case 10:
#line 399 "goom_script_yacc.y"
    { yyval.nPtr = new_set(yyvsp[-3].nPtr,yyvsp[-1].nPtr); }
    break;

  case 11:
#line 400 "goom_script_yacc.y"
    { yyval.nPtr = new_float_decl(new_var(yyvsp[-3].strValue));
                                                      yyval.nPtr->unode.opr.next = new_set(new_var(yyvsp[-3].strValue), yyvsp[-1].nPtr); }
    break;

  case 12:
#line 402 "goom_script_yacc.y"
    { yyval.nPtr = new_float_decl(new_var(yyvsp[-1].strValue)); }
    break;

  case 13:
#line 403 "goom_script_yacc.y"
    { yyval.nPtr = new_int_decl(new_var(yyvsp[-1].strValue)); }
    break;

  case 14:
#line 404 "goom_script_yacc.y"
    { yyval.nPtr = new_if(yyvsp[-3].nPtr,yyvsp[0].nPtr); }
    break;

  case 15:
#line 405 "goom_script_yacc.y"
    { lastNode = yyvsp[-2].nPtr->unode.opr.op[1]; yyval.nPtr=yyvsp[-2].nPtr; }
    break;

  case 16:
#line 406 "goom_script_yacc.y"
    { yyval.nPtr = new_call(yyvsp[-1].strValue); }
    break;

  case 17:
#line 407 "goom_script_yacc.y"
    { yyval.nPtr = new_nop("nop"); }
    break;

  case 18:
#line 410 "goom_script_yacc.y"
    { yyval.nPtr = new_block(lastNode); lastNode = yyval.nPtr->unode.opr.op[0]; }
    break;

  case 19:
#line 413 "goom_script_yacc.y"
    { yyval.nPtr = yyvsp[0].nPtr; }
    break;

  case 20:
#line 414 "goom_script_yacc.y"
    { yyval.nPtr = yyvsp[0].nPtr; }
    break;

  case 21:
#line 415 "goom_script_yacc.y"
    { yyval.nPtr = new_mul(yyvsp[-2].nPtr,yyvsp[0].nPtr); }
    break;

  case 22:
#line 416 "goom_script_yacc.y"
    { yyval.nPtr = new_div(yyvsp[-2].nPtr,yyvsp[0].nPtr); }
    break;

  case 23:
#line 417 "goom_script_yacc.y"
    { yyval.nPtr = new_add(yyvsp[-2].nPtr,yyvsp[0].nPtr); }
    break;

  case 24:
#line 418 "goom_script_yacc.y"
    { yyval.nPtr = new_sub(yyvsp[-2].nPtr,yyvsp[0].nPtr); }
    break;

  case 25:
#line 419 "goom_script_yacc.y"
    { yyval.nPtr = new_equ(yyvsp[-2].nPtr,yyvsp[0].nPtr); }
    break;

  case 26:
#line 420 "goom_script_yacc.y"
    { yyval.nPtr = new_low(yyvsp[-2].nPtr,yyvsp[0].nPtr); }
    break;

  case 27:
#line 421 "goom_script_yacc.y"
    { yyval.nPtr = new_low(yyvsp[0].nPtr,yyvsp[-2].nPtr); }
    break;

  case 28:
#line 422 "goom_script_yacc.y"
    { yyval.nPtr = yyvsp[-1].nPtr; }
    break;

  case 29:
#line 425 "goom_script_yacc.y"
    { yyval.nPtr = new_var(yyvsp[0].strValue);   }
    break;

  case 30:
#line 426 "goom_script_yacc.y"
    { yyval.nPtr = new_read_param(yyvsp[0].strValue); }
    break;

  case 31:
#line 429 "goom_script_yacc.y"
    { yyval.nPtr = new_var(yyvsp[0].strValue);   }
    break;

  case 32:
#line 430 "goom_script_yacc.y"
    { yyval.nPtr = new_param(yyvsp[0].strValue); }
    break;

  case 33:
#line 433 "goom_script_yacc.y"
    { yyval.nPtr = new_constFloat(yyvsp[0].strValue); }
    break;

  case 34:
#line 434 "goom_script_yacc.y"
    { yyval.nPtr = new_constInt(yyvsp[0].strValue); }
    break;


    }

/* Line 999 of yacc.c.  */
#line 1536 "y.tab.c"

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


#line 437 "goom_script_yacc.y"


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
        node->unode.constInt.val = atoi(str);
        return node;
    }

    NodeType *new_constFloat(const char *str) {
        NodeType *node = nodeNew(str, CONST_FLOAT_NODE);
        node->unode.constFloat.val = atof(str);
        return node;
    }

    NodeType *new_var(const char *str) {
        NodeType *node = nodeNew(str, VAR_NODE);
        return node;
    }
    
    NodeType *new_read_param(const char *str) {
        NodeType *node = nodeNew(str, READ_PARAM_NODE);
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
        node->unode.opr.next = 0;
        node->unode.opr.type = type;
        node->unode.opr.nbOp = nbOp;
        for (i=0;i<nbOp;++i) node->unode.opr.op[i] = 0;
        return node;
    }

void yyerror(char *str) {
    fprintf(stderr, "GSL ERROR: %s\n", str);
}


