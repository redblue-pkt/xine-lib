#ifndef _GOOM_SCRIPT_SCANNER_H
#define _GOOM_SCRIPT_SCANNER_H

#include "goom_plugin_info.h"

void goom_script_scanner_compile(GoomScriptScanner *scanner, PluginInfo *pluginInfo, const char *script);
void goom_script_scanner_execute(GoomScriptScanner *scanner);
int goom_script_scanner_is_compiled(GoomScriptScanner *gss);

GoomScriptScanner *goom_script_scanner_new(void);
void goom_script_scanner_free(GoomScriptScanner *gss);


/* -- internal use -- */

#include "goom_hash.h"

#define EMPTY_NODE 0
#define CONST_INT_NODE 1
#define CONST_FLOAT_NODE 2
#define VAR_NODE 3
#define PARAM_NODE 4
#define READ_PARAM_NODE 5
#define OPR_NODE 6

#define OPR_SET 1
#define OPR_DECLARE_INT 2
#define OPR_DECLARE_FLOAT 3
#define OPR_IF 4
#define OPR_BLOCK 5
#define OPR_ADD 6
#define OPR_MUL 7
#define OPR_EQU 8
#define OPR_LOW 9
#define OPR_DIV 10
#define OPR_SUB 11
#define OPR_FUNC_INTRO 12
#define OPR_FUNC_OUTRO 13
#define OPR_CALL 14

typedef struct {
/*    char *name;*/
} ParamNodeType;

typedef struct {
/*    char *name;*/
} VarNodeType;

typedef struct {
    int val;
} ConstIntNodeType;

typedef struct {
    float val;
} ConstFloatNodeType;

typedef struct {
    int type;
    int nbOp;
    struct _NODE_TYPE *op[3]; /* maximal number of operand needed */
    struct _NODE_TYPE *next;
} OprNodeType;

typedef struct _NODE_TYPE{
    int type;
    char *str;
    union {
        ParamNodeType param;
        VarNodeType var;
        ConstIntNodeType constInt;
        ConstFloatNodeType constFloat;
        OprNodeType opr;
    } unode;
} NodeType;

void gsl_commit_compilation(void);

/* ------------- SCRIPT_EXEC_ENV ------------ */

typedef struct _SCRIPT_EXEC_ENV {
    int ip;
    GoomHash *vars;
} ScriptExecEnv;

/* ------------- INSTRUCTIONS -------------- */

typedef struct _INSTRUCTION {

    int id;
    GoomScriptScanner *parent;
    const char *name; /* name of the instruction */

    char **params; /* parametres de l'instruction */
    int *types;    /* type des parametres de l'instruction */
    int cur_param;
    int nb_param;

    int address;
    char *jump_label;

    union {

        /* PARAM - INTEGER */
        struct {
            PluginParam *param;
            int value;
        } p_i;

        /* PARAM - FLOAT */
        struct {
            PluginParam *param;
            float value;
        } p_f;

        /* VAR - PARAM */
        struct {
            PluginParam *param;
            HashValue *var;
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

    } data;

} Instruction;

Instruction *instr_init(GoomScriptScanner *parent, const char *name, int id, int nb_param);
void instr_add_param(Instruction *_this, char *param, int type);
void instr_display(Instruction *_this);

/* ----------- INSTRUCTION_FLOW ------------- */

typedef struct _INSTRUCTION_FLOW {

    Instruction **instr;
    int number;
    int tabsize;
    GoomHash *labels;

} InstructionFlow;

/* ----------- GOOM SCRIPT SCANNER ------------- */

struct _GoomScriptScanner {
    int num_lines;
    Instruction *instr;     /* instruction en cours de construction */

    InstructionFlow *iflow; /* flow d'instruction racine */
    GoomHash *vars;         /* table de variables */
    InstructionFlow *current_flow;

    PluginInfo *pluginInfo;
    int compilationOK;
};

/* #define TYPE_PARAM    1
   #define TYPE_INTEGER  2
   #define TYPE_FLOAT    3
   #define TYPE_VAR      4 */
#define TYPE_LABEL    5
#define TYPE_OP_EQUAL 6

#define INSTR_JUMP    6

#define INSTR_SETI    10001
#define INSTR_SETF    10002
#define INSTR_INT     10003
#define INSTR_LABEL   10004
#define INSTR_ISLOWERI  10005
#define INSTR_ISLOWERF  10006
#define INSTR_ADDI    10007
#define INSTR_ADDF    10008
#define INSTR_MULI    10009
#define INSTR_MULF    10010
#define INSTR_DIVF    10011
#define INSTR_SUBF    10012
#define INSTR_ISEQUALI  10013
#define INSTR_ISEQUALF  10014
#define INSTR_JZERO   29
#define INSTR_CALL    34 
#define INSTR_RET     35

#endif
