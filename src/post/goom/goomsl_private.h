#ifndef _GSL_PRIVATE_H
#define _GSL_PRIVATE_H

/* -- internal use -- */

#include "goomsl.h"

#ifdef USE_JITC_X86
#include "jitc_x86.h"
#endif

/* {{{ type of nodes */
#define EMPTY_NODE 0
#define CONST_INT_NODE 1
#define CONST_FLOAT_NODE 2
#define CONST_PTR_NODE 3
#define VAR_NODE 4
#define PARAM_NODE 5
#define READ_PARAM_NODE 6
#define OPR_NODE 7
/* }}} */
/* {{{ type of operations */
#define OPR_SET 1
#define OPR_IF 2
#define OPR_WHILE 3
#define OPR_BLOCK 4
#define OPR_ADD 5
#define OPR_MUL 6
#define OPR_EQU 7
#define OPR_NOT 8
#define OPR_LOW 9
#define OPR_DIV 10
#define OPR_SUB 11
#define OPR_FUNC_INTRO 12
#define OPR_FUNC_OUTRO 13
#define OPR_CALL 14
#define OPR_EXT_CALL 15
#define OPR_PLUS_EQ 16
#define OPR_SUB_EQ 17
#define OPR_MUL_EQ 18
#define OPR_DIV_EQ 19
#define OPR_CALL_EXPR 20
#define OPR_AFFECT_LIST 21

/* }}} */

typedef struct _ConstIntNodeType { /* {{{ */
    int val;
} ConstIntNodeType; /* }}} */
typedef struct _ConstFloatNodeType { /* {{{ */
    float val;
} ConstFloatNodeType; /* }}} */
typedef struct _ConstPtrNodeType { /* {{{ */
    int id;
} ConstPtrNodeType; /* }}} */
typedef struct _OprNodeType { /* {{{ */
    int type;
    int nbOp;
    struct _NODE_TYPE *op[3]; /* maximal number of operand needed */
    struct _NODE_TYPE *next;
} OprNodeType; /* }}} */
typedef struct _NODE_TYPE { /* {{{ */
    int type;
    char *str;
    GoomHash *vnamespace;
    int line_number;
    union {
        ConstIntNodeType constInt;
        ConstFloatNodeType constFloat;
        ConstPtrNodeType constPtr;
        OprNodeType opr;
    } unode;
} NodeType; /* }}} */
typedef union _INSTRUCTION_DATA { /* {{{ */
  
  /* VAR - PTR */
  struct {
    HashValue *var;
    int value;
  } v_p;
  /* VAR - INTEGER */
  struct {
    HashValue *var;
    int value;
  } v_i;
  /* VAR - FLOAT */
  struct {
    HashValue *var;
    float value;
  } v_f;
  /* VAR - VAR */
  struct {
    HashValue *var_src;
    HashValue *var_dest;
  } v_v;
  /* VAR */
  struct {
    int jump_offset;
    HashValue *var;
  } v;
  int jump_offset;
  struct _ExternalFunctionStruct *external_function;
} InstructionData;
/* }}} */
typedef struct _INSTRUCTION { /* {{{ */

    int id;
    InstructionData data;
    GoomSL *parent;
    const char *name; /* name of the instruction */

    char     **params; /* parametres de l'instruction */
    GoomHash **vnamespace;
    int       *types;  /* type des parametres de l'instruction */
    int cur_param;
    int nb_param;

    int address;
    char *jump_label;
    char *nop_label;

    int line_number;

} Instruction; /* }}} */
typedef struct _INSTRUCTION_FLOW { /* {{{ */

    Instruction **instr;
    int number;
    int tabsize;
    GoomHash *labels;
} InstructionFlow; /* }}} */
typedef struct _FAST_INSTRUCTION { /* {{{ */
  int id;
  InstructionData data;
  Instruction *proto;
} FastInstruction; /* }}} */
typedef struct _FastInstructionFlow { /* {{{ */
  int number;
  FastInstruction *instr;
  void *mallocedInstr;
} FastInstructionFlow; /* }}} */
typedef struct _ExternalFunctionStruct { /* {{{ */
  GoomSL_ExternalFunction function;
  GoomHash *vars;
  int is_extern;
} ExternalFunctionStruct; /* }}} */
struct _GoomSL { /* {{{ */
    int num_lines;
    Instruction *instr;     /* instruction en cours de construction */

    InstructionFlow     *iflow;  /* flow d'instruction 'normal' */
    FastInstructionFlow *fastiflow; /* flow d'instruction optimise */
    
    GoomHash *vars;         /* table de variables */
    int currentNS;
    GoomHash *namespaces[16];
    
    GoomHash *functions;    /* table des fonctions externes */

    int    nbPtr;
    int    ptrArraySize;
    void **ptrArray;
    
    int compilationOK;
#ifdef USE_JITC_X86
    JitcX86Env *jitc;
    JitcFunc    jitc_func;
#endif
}; /* }}} */

extern GoomSL *currentGoomSL;

Instruction *gsl_instr_init(GoomSL *parent, const char *name, int id, int nb_param, int line_number);
void gsl_instr_add_param(Instruction *_this, char *param, int type);
void gsl_instr_set_namespace(Instruction *_this, GoomHash *ns);

void gsl_declare_task(const char *name);
void gsl_declare_external_task(const char *name);

void gsl_enternamespace(const char *name);
void gsl_leavenamespace(void);
GoomHash *gsl_find_namespace(const char *name);

void gsl_commit_compilation(void);

/* #define TYPE_PARAM    1
   #define TYPE_INTEGER  2
   #define TYPE_FLOAT    3
   #define TYPE_VAR      4 */
#define TYPE_LABEL    5
#define TYPE_OP_EQUAL 6
#define TYPE_IVAR     7
#define TYPE_FVAR     8 
#define TYPE_PVAR     9

#define INSTR_JUMP     6
#define INSTR_JZERO    29
#define INSTR_CALL     36 
#define INSTR_RET      37
#define INSTR_EXT_CALL 38
#define INSTR_JNZERO   40

#define INSTR_SET     10001
#define INSTR_INT     10002
#define INSTR_FLOAT   10003
#define INSTR_PTR     10004
#define INSTR_LABEL   10005
#define INSTR_ISLOWER 10006
#define INSTR_ADD     10007
#define INSTR_MUL     10008
#define INSTR_DIV     10009
#define INSTR_SUB     10010
#define INSTR_ISEQUAL 10011
#define INSTR_NOT     10012


#endif
