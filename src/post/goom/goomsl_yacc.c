
/*  A Bison parser, made from goomsl_yacc.y
    by GNU Bison version 1.28  */

#define YYBISON 1  /* Identify Bison output.  */

#define	TYPE_INTEGER	257
#define	TYPE_FLOAT	258
#define	TYPE_VAR	259
#define	TYPE_PTR	260
#define	PTR_TK	261
#define	INT_TK	262
#define	FLOAT_TK	263
#define	DECLARE	264
#define	EXTERNAL	265
#define	WHILE	266
#define	DO	267
#define	NOT	268
#define	PLUS_EQ	269
#define	SUB_EQ	270
#define	DIV_EQ	271
#define	MUL_EQ	272
#define	SUP_EQ	273
#define	LOW_EQ	274

#line 6 "goomsl_yacc.y"

    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include "goomsl.h"
    #include "goomsl_private.h"
    
    int yylex(void);
    void yyerror(char *);
    extern GoomSL *currentGoomSL;

    static NodeType *nodeNew(const char *str, int type, int line_number);
    static NodeType *nodeClone(NodeType *node);
    static void nodeFreeInternals(NodeType *node);
    static void nodeFree(NodeType *node);

    static void commit_node(NodeType *node, int releaseIfTemp);
    static void precommit_node(NodeType *node);

    static NodeType *new_constInt(const char *str, int line_number);
    static NodeType *new_constFloat(const char *str, int line_number);
    static NodeType *new_constPtr(const char *str, int line_number);
    static NodeType *new_var(const char *str, int line_number);
    static NodeType *new_nop(const char *str);
    static NodeType *new_op(const char *str, int type, int nbOp);

    static int  allocateLabel(void);
    static int  allocateTemp(void);
    static void releaseTemp(int n);
    static void releaseAllTemps(void);

    static int is_tmp_expr(NodeType *node) {
        if (node->str) {
            return (!strncmp(node->str,"_i_tmp_",7))
              || (!strncmp(node->str,"_f_tmp_",7))
              || (!strncmp(node->str,"_p_tmp",7));
        }
        return 0;
    }
    /* pre: is_tmp_expr(node); */
    static int get_tmp_id(NodeType *node)  { return atoi((node->str)+5); }

    static int is_commutative_expr(int itype)
    { /* {{{ */
        return (itype == INSTR_ADD)
            || (itype == INSTR_MUL)
            || (itype == INSTR_ISEQUAL);
    } /* }}} */

    static void GSL_PUT_LABEL(char *name, int line_number)
    { /* {{{ */
#ifdef VERBOSE
      printf("label %s\n", name);
#endif
      currentGoomSL->instr = gsl_instr_init(currentGoomSL, "label", INSTR_LABEL, 1, line_number);
      gsl_instr_add_param(currentGoomSL->instr, name, TYPE_LABEL);
    } /* }}} */
    static void GSL_PUT_JUMP(char *name, int line_number)
    { /* {{{ */
#ifdef VERBOSE
      printf("jump %s\n", name);
#endif
      currentGoomSL->instr = gsl_instr_init(currentGoomSL, "jump", INSTR_JUMP, 1, line_number);
      gsl_instr_add_param(currentGoomSL->instr, name, TYPE_LABEL);
    } /* }}} */

    static void GSL_PUT_JXXX(char *name, char *iname, int instr_id, int line_number)
    { /* {{{ */
#ifdef VERBOSE
      printf("%s %s\n", iname, name);
#endif
      currentGoomSL->instr = gsl_instr_init(currentGoomSL, iname, instr_id, 1, line_number);
      gsl_instr_add_param(currentGoomSL->instr, name, TYPE_LABEL);
    } /* }}} */
    static void GSL_PUT_JZERO(char *name,int line_number)
    { /* {{{ */
      GSL_PUT_JXXX(name,"jzero.i",INSTR_JZERO,line_number);
    } /* }}} */
    static void GSL_PUT_JNZERO(char *name, int line_number)
    { /* {{{ */
      GSL_PUT_JXXX(name,"jnzero.i",INSTR_JNZERO,line_number);
    } /* }}} */

    /* FLOAT */
    #define gsl_float_decl_global(name) goom_hash_put_int(currentGoomSL->vars,name,INSTR_FLOAT)
    #define gsl_int_decl_global(name)   goom_hash_put_int(currentGoomSL->vars,name,INSTR_INT)
    #define gsl_ptr_decl_global(name)   goom_hash_put_int(currentGoomSL->vars,name,INSTR_PTR)
    static void gsl_float_decl(const char *name)
    {
        goom_hash_put_int(currentGoomSL->namespaces[currentGoomSL->currentNS],name,INSTR_FLOAT);
    }
    /* INT */
    static void gsl_int_decl(const char *name)
    {
        goom_hash_put_int(currentGoomSL->namespaces[currentGoomSL->currentNS],name,INSTR_INT);
    }
    /* PTR */
    static void gsl_ptr_decl(const char *name)
    {
        goom_hash_put_int(currentGoomSL->namespaces[currentGoomSL->currentNS],name,INSTR_PTR);
    }

    static void commit_test2(NodeType *set,const char *type, int instr);
    static NodeType *new_call(const char *name, NodeType *affect_list);

    /* SETTER */
    static NodeType *new_set(NodeType *lvalue, NodeType *expression)
    { /* {{{ */
        NodeType *set = new_op("set", OPR_SET, 2);
        set->unode.opr.op[0] = lvalue;
        set->unode.opr.op[1] = expression;
        return set;
    } /* }}} */
    static void commit_set(NodeType *set)
    { /* {{{ */
      commit_test2(set,"set",INSTR_SET);
    } /* }}} */

    /* PLUS_EQ */
    static NodeType *new_plus_eq(NodeType *lvalue, NodeType *expression) /* {{{ */
    {
        NodeType *set = new_op("plus_eq", OPR_PLUS_EQ, 2);
        set->unode.opr.op[0] = lvalue;
        set->unode.opr.op[1] = expression;
        return set;
    }
    static void commit_plus_eq(NodeType *set)
    {
        precommit_node(set->unode.opr.op[1]);
#ifdef VERBOSE
        printf("add %s %s\n", set->unode.opr.op[0]->str, set->unode.opr.op[1]->str);
#endif
        currentGoomSL->instr = gsl_instr_init(currentGoomSL, "add", INSTR_ADD, 2, set->line_number);
        commit_node(set->unode.opr.op[0],0);
        commit_node(set->unode.opr.op[1],1);
    } /* }}} */

    /* SUB_EQ */
    static NodeType *new_sub_eq(NodeType *lvalue, NodeType *expression) /* {{{ */
    {
        NodeType *set = new_op("sub_eq", OPR_SUB_EQ, 2);
        set->unode.opr.op[0] = lvalue;
        set->unode.opr.op[1] = expression;
        return set;
    }
    static void commit_sub_eq(NodeType *set)
    {
        precommit_node(set->unode.opr.op[1]);
#ifdef VERBOSE
        printf("sub %s %s\n", set->unode.opr.op[0]->str, set->unode.opr.op[1]->str);
#endif
        currentGoomSL->instr = gsl_instr_init(currentGoomSL, "sub", INSTR_SUB, 2, set->line_number);
        commit_node(set->unode.opr.op[0],0);
        commit_node(set->unode.opr.op[1],1);
    } /* }}} */

    /* MUL_EQ */
    static NodeType *new_mul_eq(NodeType *lvalue, NodeType *expression) /* {{{ */
    {
        NodeType *set = new_op("mul_eq", OPR_MUL_EQ, 2);
        set->unode.opr.op[0] = lvalue;
        set->unode.opr.op[1] = expression;
        return set;
    }
    static void commit_mul_eq(NodeType *set)
    {
        precommit_node(set->unode.opr.op[1]);
#ifdef VERBOSE
        printf("mul %s %s\n", set->unode.opr.op[0]->str, set->unode.opr.op[1]->str);
#endif
        currentGoomSL->instr = gsl_instr_init(currentGoomSL, "mul", INSTR_MUL, 2, set->line_number);
        commit_node(set->unode.opr.op[0],0);
        commit_node(set->unode.opr.op[1],1);
    } /* }}} */

    /* DIV_EQ */
    static NodeType *new_div_eq(NodeType *lvalue, NodeType *expression) /* {{{ */
    {
        NodeType *set = new_op("div_eq", OPR_PLUS_EQ, 2);
        set->unode.opr.op[0] = lvalue;
        set->unode.opr.op[1] = expression;
        return set;
    }
    static void commit_div_eq(NodeType *set)
    {
        precommit_node(set->unode.opr.op[1]);
#ifdef VERBOSE
        printf("div %s %s\n", set->unode.opr.op[0]->str, set->unode.opr.op[1]->str);
#endif
        currentGoomSL->instr = gsl_instr_init(currentGoomSL, "div", INSTR_DIV, 2, set->line_number);
        commit_node(set->unode.opr.op[0],0);
        commit_node(set->unode.opr.op[1],1);
    } /* }}} */

    /* commodity method for add, mult, ... */

    static void precommit_expr(NodeType *expr, const char *type, int instr_id)
    { /* {{{ */
        NodeType *tmp, *tmpcpy;
        int toAdd;

        /* compute "left" and "right" */
        switch (expr->unode.opr.nbOp) {
        case 2:
        precommit_node(expr->unode.opr.op[1]);
        case 1:
        precommit_node(expr->unode.opr.op[0]);
        }

        if (is_tmp_expr(expr->unode.opr.op[0])) {
            tmp = expr->unode.opr.op[0];
            toAdd = 1;
        }
        else if (is_commutative_expr(instr_id) && (expr->unode.opr.nbOp==2) && is_tmp_expr(expr->unode.opr.op[1])) {
            tmp = expr->unode.opr.op[1];
            toAdd = 0;
        }
        else {
            char stmp[256];
            /* declare a float to store the result */
            if (expr->unode.opr.op[0]->type == CONST_INT_NODE) {
                sprintf(stmp,"_i_tmp_%i",allocateTemp());
                gsl_int_decl_global(stmp);
            }
            else if (expr->unode.opr.op[0]->type == CONST_FLOAT_NODE) {
                sprintf(stmp,"_f_tmp%i",allocateTemp());
                gsl_float_decl_global(stmp);
            }
            else if (expr->unode.opr.op[0]->type == CONST_PTR_NODE) {
                sprintf(stmp,"_p_tmp%i",allocateTemp());
                gsl_ptr_decl_global(stmp);
            }
            else {
                HashValue *val = goom_hash_get(expr->unode.opr.op[0]->vnamespace, expr->unode.opr.op[0]->str);
                if (val && (val->i == INSTR_FLOAT)) {
                    sprintf(stmp,"_f_tmp_%i",allocateTemp());
                    gsl_float_decl_global(stmp);
                }
                else if (val && (val->i == INSTR_PTR)) {
                    sprintf(stmp,"_p_tmp_%i",allocateTemp());
                    gsl_ptr_decl_global(stmp);
                }
                else if (val && (val->i == INSTR_INT)) {
                    sprintf(stmp,"_i_tmp_%i",allocateTemp());
                    gsl_int_decl_global(stmp);
                }
                else if (val == 0) {
                    fprintf(stderr, "ERROR: Line %d, Could not find variable '%s'\n", expr->line_number, expr->unode.opr.op[0]->str);
                    exit(1);
                }
            }
            tmp = new_var(stmp,expr->line_number);

            /* set the tmp to the value of "op1" */
            tmpcpy = nodeClone(tmp);
            commit_node(new_set(tmp,expr->unode.opr.op[0]),0);
            toAdd = 1;

            tmp = tmpcpy;
        }

        /* add op2 to tmp */
#ifdef VERBOSE
        if (expr->unode.opr.nbOp == 2)
          printf("%s %s %s\n", type, tmp->str, expr->unode.opr.op[toAdd]->str);
        else
          printf("%s %s\n", type, tmp->str);
#endif
        currentGoomSL->instr = gsl_instr_init(currentGoomSL, type, instr_id, expr->unode.opr.nbOp, expr->line_number);
        tmpcpy = nodeClone(tmp);
        commit_node(tmp,0);
        if (expr->unode.opr.nbOp == 2) {
          commit_node(expr->unode.opr.op[toAdd],1);
        }
    
        /* redefine the ADD node now as the computed variable */
        nodeFreeInternals(expr);
        *expr = *tmpcpy;
        free(tmpcpy);
    } /* }}} */

    static NodeType *new_expr1(const char *name, int id, NodeType *expr1)
    { /* {{{ */
        NodeType *add = new_op(name, id, 1);
        add->unode.opr.op[0] = expr1;
        return add;
    } /* }}} */

    static NodeType *new_expr2(const char *name, int id, NodeType *expr1, NodeType *expr2)
    { /* {{{ */
        NodeType *add = new_op(name, id, 2);
        add->unode.opr.op[0] = expr1;
        add->unode.opr.op[1] = expr2;
        return add;
    } /* }}} */

    /* ADD */
    static NodeType *new_add(NodeType *expr1, NodeType *expr2) { /* {{{ */
        return new_expr2("add", OPR_ADD, expr1, expr2);
    }
    static void precommit_add(NodeType *add) {
        precommit_expr(add,"add",INSTR_ADD);
    } /* }}} */

    /* SUB */
    static NodeType *new_sub(NodeType *expr1, NodeType *expr2) { /* {{{ */
        return new_expr2("sub", OPR_SUB, expr1, expr2);
    }
    static void precommit_sub(NodeType *sub) {
        precommit_expr(sub,"sub",INSTR_SUB);
    } /* }}} */

    /* MUL */
    static NodeType *new_mul(NodeType *expr1, NodeType *expr2) { /* {{{ */
        return new_expr2("mul", OPR_MUL, expr1, expr2);
    }
    static void precommit_mul(NodeType *mul) {
        precommit_expr(mul,"mul",INSTR_MUL);
    } /* }}} */
    
    /* DIV */
    static NodeType *new_div(NodeType *expr1, NodeType *expr2) { /* {{{ */
        return new_expr2("div", OPR_DIV, expr1, expr2);
    }
    static void precommit_div(NodeType *mul) {
        precommit_expr(mul,"div",INSTR_DIV);
    } /* }}} */

    /* CALL EXPRESSION */
    static NodeType *new_call_expr(const char *name, NodeType *affect_list) { /* {{{ */
        NodeType *call = new_call(name,affect_list);
        NodeType *node = new_expr1(name, OPR_CALL_EXPR, call);
        node->vnamespace = gsl_find_namespace(name);
        if (node->vnamespace == NULL)
          fprintf(stderr, "ERROR: Line %d, No return type for: '%s'\n", currentGoomSL->num_lines, name);
        return node;
    }
    static void precommit_call_expr(NodeType *call) {
        char stmp[256];
        NodeType *tmp,*tmpcpy;
        HashValue *val = goom_hash_get(call->vnamespace, call->str);
        if (val && (val->i == INSTR_FLOAT)) {
          sprintf(stmp,"_f_tmp_%i",allocateTemp());
          gsl_float_decl_global(stmp);
        }
        else if (val && (val->i == INSTR_PTR)) {
          sprintf(stmp,"_p_tmp_%i",allocateTemp());
          gsl_ptr_decl_global(stmp);
        }
        else if (val && (val->i == INSTR_INT)) {
          sprintf(stmp,"_i_tmp_%i",allocateTemp());
          gsl_int_decl_global(stmp);
        }
        else if (val == 0) {
          fprintf(stderr, "ERROR: Line %d, Could not find variable '%s'\n", call->line_number, call->str);
          exit(1);
        }
        tmp = new_var(stmp,call->line_number);
        commit_node(call->unode.opr.op[0],0);
        tmpcpy = nodeClone(tmp);
        commit_node(new_set(tmp,new_var(call->str,call->line_number)),0);
        
        nodeFreeInternals(call);
        *call = *tmpcpy;
        free(tmpcpy);
    } /* }}} */

    static void commit_test2(NodeType *set,const char *type, int instr)
    { /* {{{ */
        NodeType *tmp;
        char stmp[256];
        precommit_node(set->unode.opr.op[0]);
        precommit_node(set->unode.opr.op[1]);
        tmp = set->unode.opr.op[0];
        
        stmp[0] = 0;
        if (set->unode.opr.op[0]->type == CONST_INT_NODE) {
            sprintf(stmp,"_i_tmp_%i",allocateTemp());
            gsl_int_decl_global(stmp);
        }
        else if (set->unode.opr.op[0]->type == CONST_FLOAT_NODE) {
            sprintf(stmp,"_f_tmp%i",allocateTemp());
            gsl_float_decl_global(stmp);
        }
        else if (set->unode.opr.op[0]->type == CONST_PTR_NODE) {
            sprintf(stmp,"_p_tmp%i",allocateTemp());
            gsl_ptr_decl_global(stmp);
        }
        if (stmp[0]) {
            NodeType *tmpcpy;
            tmp = new_var(stmp, set->line_number);
            tmpcpy = nodeClone(tmp);
            commit_node(new_set(tmp,set->unode.opr.op[0]),0);
            tmp = tmpcpy;
        }

#ifdef VERBOSE
        printf("%s %s %s\n", type, tmp->str, set->unode.opr.op[1]->str);
#endif
        currentGoomSL->instr = gsl_instr_init(currentGoomSL, type, instr, 2, set->line_number);
        commit_node(tmp,instr!=INSTR_SET);
        commit_node(set->unode.opr.op[1],1);
    } /* }}} */
    
    /* NOT */
    static NodeType *new_not(NodeType *expr1) { /* {{{ */
        return new_expr1("not", OPR_NOT, expr1);
    }
    static void commit_not(NodeType *set)
    {
        commit_node(set->unode.opr.op[0],0);
#ifdef VERBOSE
        printf("not\n");
#endif
        currentGoomSL->instr = gsl_instr_init(currentGoomSL, "not", INSTR_NOT, 1, set->line_number);
        gsl_instr_add_param(currentGoomSL->instr, "|dummy|", TYPE_LABEL);
    } /* }}} */
    
    /* EQU */
    static NodeType *new_equ(NodeType *expr1, NodeType *expr2) { /* {{{ */
        return new_expr2("isequal", OPR_EQU, expr1, expr2);
    }
    static void commit_equ(NodeType *mul) {
        commit_test2(mul,"isequal",INSTR_ISEQUAL);
    } /* }}} */
    
    /* INF */
    static NodeType *new_low(NodeType *expr1, NodeType *expr2) { /* {{{ */
        return new_expr2("islower", OPR_LOW, expr1, expr2);
    }
    static void commit_low(NodeType *mul) {
        commit_test2(mul,"islower",INSTR_ISLOWER);
    } /* }}} */

    /* WHILE */
    static NodeType *new_while(NodeType *expression, NodeType *instr) { /* {{{ */
        NodeType *node = new_op("while", OPR_WHILE, 2);
        node->unode.opr.op[0] = expression;
        node->unode.opr.op[1] = instr;
        return node;
    }

    static void commit_while(NodeType *node)
    {
        int lbl = allocateLabel();
        char start_while[1024], test_while[1024];
        sprintf(start_while, "|start_while_%d|", lbl);
        sprintf(test_while, "|test_while_%d|", lbl);
       
        GSL_PUT_JUMP(test_while,node->line_number);
        GSL_PUT_LABEL(start_while,node->line_number);

        /* code */
        commit_node(node->unode.opr.op[1],0);

        GSL_PUT_LABEL(test_while,node->line_number);
        commit_node(node->unode.opr.op[0],0);
        GSL_PUT_JNZERO(start_while,node->line_number);
    } /* }}} */

    /* IF */
    static NodeType *new_if(NodeType *expression, NodeType *instr) { /* {{{ */
        NodeType *node = new_op("if", OPR_IF, 2);
        node->unode.opr.op[0] = expression;
        node->unode.opr.op[1] = instr;
        return node;
    }
    static void commit_if(NodeType *node) {

        char slab[1024];
        sprintf(slab, "|eif%d|", allocateLabel());
        commit_node(node->unode.opr.op[0],0);
        GSL_PUT_JZERO(slab,node->line_number);
        /* code */
        commit_node(node->unode.opr.op[1],0);
        GSL_PUT_LABEL(slab,node->line_number);
    } /* }}} */

    /* BLOCK */
    static NodeType *new_block(NodeType *lastNode) { /* {{{ */
        NodeType *blk = new_op("block", OPR_BLOCK, 2);
        blk->unode.opr.op[0] = new_nop("start_of_block");
        blk->unode.opr.op[1] = lastNode;        
        return blk;
    }
    static void commit_block(NodeType *node) {
        commit_node(node->unode.opr.op[0]->unode.opr.next,0);
    } /* }}} */

    /* FUNCTION INTRO */
    static NodeType *new_function_intro(const char *name) { /* {{{ */
        char stmp[256];
        if (strlen(name) < 200) {
           sprintf(stmp, "|__func_%s|", name);
        }
        return new_op(stmp, OPR_FUNC_INTRO, 0);
    }
    static void commit_function_intro(NodeType *node) {
        currentGoomSL->instr = gsl_instr_init(currentGoomSL, "label", INSTR_LABEL, 1, node->line_number);
        gsl_instr_add_param(currentGoomSL->instr, node->str, TYPE_LABEL);
#ifdef VERBOSE
        printf("label %s\n", node->str);
#endif
    } /* }}} */

    /* FUNCTION OUTRO */
    static NodeType *new_function_outro(void) { /* {{{ */
        return new_op("ret", OPR_FUNC_OUTRO, 0);
    }
    static void commit_function_outro(NodeType *node) {
        currentGoomSL->instr = gsl_instr_init(currentGoomSL, "ret", INSTR_RET, 1, node->line_number);
        gsl_instr_add_param(currentGoomSL->instr, "|dummy|", TYPE_LABEL);
        releaseAllTemps();
#ifdef VERBOSE
        printf("ret\n");
#endif
    } /* }}} */
    
    /* FUNCTION CALL */
    static NodeType *new_call(const char *name, NodeType *affect_list) { /* {{{ */
        HashValue *fval;
        fval = goom_hash_get(currentGoomSL->functions, name);
        if (!fval) {
            gsl_declare_task(name);
            fval = goom_hash_get(currentGoomSL->functions, name);
        }
        if (!fval) {
            fprintf(stderr, "ERROR: Line %d, Could not find function %s\n", currentGoomSL->num_lines, name);
            exit(1);
            return NULL;
        }
        else {
            ExternalFunctionStruct *gef = (ExternalFunctionStruct*)fval->ptr;
            if (gef->is_extern) {
                NodeType *node =  new_op(name, OPR_EXT_CALL, 1);
                node->unode.opr.op[0] = affect_list;
                return node;
            }
            else {
                NodeType *node;
                char stmp[256];
                if (strlen(name) < 200) {
                    sprintf(stmp, "|__func_%s|", name);
                }
                node = new_op(stmp, OPR_CALL, 1);
                node->unode.opr.op[0] = affect_list;
                return node;
            }
        }
    }
    static void commit_ext_call(NodeType *node) {
        commit_node(node->unode.opr.op[0],0);
        currentGoomSL->instr = gsl_instr_init(currentGoomSL, "extcall", INSTR_EXT_CALL, 1, node->line_number);
        gsl_instr_add_param(currentGoomSL->instr, node->str, TYPE_VAR);
#ifdef VERBOSE
        printf("extcall %s\n", node->str);
#endif
    }
    static void commit_call(NodeType *node) {
        commit_node(node->unode.opr.op[0],0);
        currentGoomSL->instr = gsl_instr_init(currentGoomSL, "call", INSTR_CALL, 1, node->line_number);
        gsl_instr_add_param(currentGoomSL->instr, node->str, TYPE_LABEL);
#ifdef VERBOSE
        printf("call %s\n", node->str);
#endif
    } /* }}} */

    /* AFFECTATION LIST */
    static NodeType *new_affec_list(NodeType *set, NodeType *next)
    {
      NodeType *node = new_op("affect_list", OPR_AFFECT_LIST, 2);
      node->unode.opr.op[0] = set;
      node->unode.opr.op[1] = next;
      return node;
    }
    static void commit_affect_list(NodeType *node)
    {
      NodeType *cur = node;
      while(cur != NULL) {
        NodeType *set = cur->unode.opr.op[0];
        precommit_node(set->unode.opr.op[0]);
        precommit_node(set->unode.opr.op[1]);
        cur = cur->unode.opr.op[1];
      }
      cur = node;
      while(cur != NULL) {
        NodeType *set = cur->unode.opr.op[0];
        commit_node(set,0);
        cur = cur->unode.opr.op[1];
      }
    }

    /** **/

    static NodeType *rootNode = 0; /* TODO: reinitialiser a chaque compilation. */
    static NodeType *lastNode = 0;
    static NodeType *gsl_append(NodeType *curNode) {
        if (curNode == 0) return 0; /* {{{ */
        if (lastNode)
            lastNode->unode.opr.next = curNode;
        lastNode = curNode;
        while(lastNode->unode.opr.next) lastNode = lastNode->unode.opr.next;
        if (rootNode == 0)
            rootNode = curNode;
        return curNode;
    } /* }}} */

#if 1
    static int allocateTemp(void) {
      return allocateLabel();
    }
    static void releaseAllTemps(void) {}
    static void releaseTemp(int n) {}
#else
    static int nbTemp = 0;
    static int *tempArray = 0;
    static int tempArraySize = 0;
    static int allocateTemp(void) { /* TODO: allocateITemp, allocateFTemp */
        int i = 0; /* {{{ */
        if (tempArray == 0) {
          tempArraySize = 256;
          tempArray = (int*)malloc(tempArraySize * sizeof(int));
        }
        while (1) {
          int j;
          for (j=0;j<nbTemp;++j) {
            if (tempArray[j] == i) break;
          }
          if (j == nbTemp) {
            if (nbTemp == tempArraySize) {
              tempArraySize *= 2;
              tempArray = (int*)realloc(tempArray,tempArraySize * sizeof(int));
            }
            tempArray[nbTemp++] = i;
            return i;
          }
          i++;
        }
    } /* }}} */
    static void releaseAllTemps(void) {
      nbTemp = 0; /* {{{ */
    } /* }}} */
    static void releaseTemp(int n) {
      int j; /* {{{ */
      for (j=0;j<nbTemp;++j) {
        if (tempArray[j] == n) {
          tempArray[j] = tempArray[--nbTemp];
          break;
        }
      }
    } /* }}} */
#endif

    static int lastLabel = 0;
    static int allocateLabel(void) {
        return ++lastLabel; /* {{{ */
    } /* }}} */

    void gsl_commit_compilation(void)
    { /* {{{ */
        commit_node(rootNode,0);
        rootNode = 0;
        lastNode = 0;
    } /* }}} */
    
    void precommit_node(NodeType *node)
    { /* {{{ */
        /* do here stuff for expression.. for exemple */
        if (node->type == OPR_NODE)
            switch(node->unode.opr.type) {
                case OPR_ADD: precommit_add(node); break;
                case OPR_SUB: precommit_sub(node); break;
                case OPR_MUL: precommit_mul(node); break;
                case OPR_DIV: precommit_div(node); break;
                case OPR_CALL_EXPR: precommit_call_expr(node); break;
            }
    } /* }}} */
    
    void commit_node(NodeType *node, int releaseIfTmp)
    { /* {{{ */
        if (node == 0) return;
        
        switch(node->type) {
            case OPR_NODE:
                switch(node->unode.opr.type) {
                    case OPR_SET:           commit_set(node); break;
                    case OPR_PLUS_EQ:       commit_plus_eq(node); break;
                    case OPR_SUB_EQ:        commit_sub_eq(node); break;
                    case OPR_MUL_EQ:        commit_mul_eq(node); break;
                    case OPR_DIV_EQ:        commit_div_eq(node); break;
                    case OPR_IF:            commit_if(node); break;
                    case OPR_WHILE:         commit_while(node); break;
                    case OPR_BLOCK:         commit_block(node); break;
                    case OPR_FUNC_INTRO:    commit_function_intro(node); break;
                    case OPR_FUNC_OUTRO:    commit_function_outro(node); break;
                    case OPR_CALL:          commit_call(node); break;
                    case OPR_EXT_CALL:      commit_ext_call(node); break;
                    case OPR_EQU:           commit_equ(node); break;
                    case OPR_LOW:           commit_low(node); break;
                    case OPR_NOT:           commit_not(node); break;
                    case OPR_AFFECT_LIST:   commit_affect_list(node); break;
#ifdef VERBOSE
                    case EMPTY_NODE:        printf("NOP\n"); break;
#endif
                }

                commit_node(node->unode.opr.next,0); /* recursive for the moment, maybe better to do something iterative? */
                break;

            case VAR_NODE:         gsl_instr_set_namespace(currentGoomSL->instr, node->vnamespace);
                                   gsl_instr_add_param(currentGoomSL->instr, node->str, TYPE_VAR); break;
            case CONST_INT_NODE:   gsl_instr_add_param(currentGoomSL->instr, node->str, TYPE_INTEGER); break;
            case CONST_FLOAT_NODE: gsl_instr_add_param(currentGoomSL->instr, node->str, TYPE_FLOAT); break;
            case CONST_PTR_NODE:   gsl_instr_add_param(currentGoomSL->instr, node->str, TYPE_PTR); break;
        }
        if (releaseIfTmp && is_tmp_expr(node))
          releaseTemp(get_tmp_id(node));
        
        nodeFree(node);
    } /* }}} */

    NodeType *nodeNew(const char *str, int type, int line_number) {
        NodeType *node = (NodeType*)malloc(sizeof(NodeType)); /* {{{ */
        node->type = type;
        node->str  = (char*)malloc(strlen(str)+1);
        node->vnamespace = NULL;
        node->line_number = line_number;
        strcpy(node->str, str);
        return node;
    } /* }}} */
    static NodeType *nodeClone(NodeType *node) {
        NodeType *ret = nodeNew(node->str, node->type, node->line_number); /* {{{ */
        ret->vnamespace = node->vnamespace;
        ret->unode = node->unode;
        return ret;
    } /* }}} */

    void nodeFreeInternals(NodeType *node) {
        free(node->str); /* {{{ */
    } /* }}} */
    
    void nodeFree(NodeType *node) {
        nodeFreeInternals(node); /* {{{ */
        free(node);
    } /* }}} */

    NodeType *new_constInt(const char *str, int line_number) {
        NodeType *node = nodeNew(str, CONST_INT_NODE, line_number); /* {{{ */
        node->unode.constInt.val = atoi(str);
        return node;
    } /* }}} */

    NodeType *new_constPtr(const char *str, int line_number) {
        NodeType *node = nodeNew(str, CONST_PTR_NODE, line_number); /* {{{ */
        node->unode.constPtr.id = strtol(str,NULL,0);
        return node;
    } /* }}} */

    NodeType *new_constFloat(const char *str, int line_number) {
        NodeType *node = nodeNew(str, CONST_FLOAT_NODE, line_number); /* {{{ */
        node->unode.constFloat.val = atof(str);
        return node;
    } /* }}} */

    NodeType *new_var(const char *str, int line_number) {
        NodeType *node = nodeNew(str, VAR_NODE, line_number); /* {{{ */
        node->vnamespace = gsl_find_namespace(str);
        if (node->vnamespace == 0) {
            fprintf(stderr, "ERROR: Line %d, Variable not found: '%s'\n", line_number, str);
            exit(1);
        }
        return node;
    } /* }}} */
    
    NodeType *new_nop(const char *str) {
        NodeType *node = new_op(str, EMPTY_NODE, 0); /* {{{ */
        return node;
    } /* }}} */
    
    NodeType *new_op(const char *str, int type, int nbOp) {
        int i; /* {{{ */
        NodeType *node = nodeNew(str, OPR_NODE, currentGoomSL->num_lines);
        node->unode.opr.next = 0;
        node->unode.opr.type = type;
        node->unode.opr.nbOp = nbOp;
        for (i=0;i<nbOp;++i) node->unode.opr.op[i] = 0;
        return node;
    } /* }}} */


    static void DECL_VAR(int type, char *name) {
      switch(type){
        case FLOAT_TK:gsl_float_decl_global(name);break;
        case INT_TK:  gsl_int_decl_global(name);break;
        case PTR_TK:  gsl_ptr_decl_global(name);break;
      }
    }


#line 806 "goomsl_yacc.y"
typedef union {
    int intValue;
    float floatValue;
    char charValue;
    char strValue[2048];
    NodeType *nPtr;
  } YYSTYPE;
#include <stdio.h>

#ifndef __cplusplus
#ifndef __STDC__
#define const
#endif
#endif



#define	YYFINAL		149
#define	YYFLAG		-32768
#define	YYNTBASE	38

#define YYTRANSLATE(x) ((unsigned)(x) <= 274 ? yytranslate[x] : 59)

static const char yytranslate[] = {     0,
     2,     2,     2,     2,     2,     2,     2,     2,     2,    28,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,    31,
    32,    26,    24,    30,    25,     2,    27,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,    29,     2,    22,
    21,    23,    33,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
    36,     2,    37,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,    34,     2,    35,     2,     2,     2,     2,     2,
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
     2,     2,     2,     2,     2,     1,     3,     4,     5,     6,
     7,     8,     9,    10,    11,    12,    13,    14,    15,    16,
    17,    18,    19,    20
};

#if YYDEBUG != 0
static const short yyprhs[] = {     0,
     0,     4,     7,    16,    27,    36,    47,    50,    51,    52,
    55,    58,    61,    63,    66,    67,    71,    76,    83,    84,
    86,    87,    89,    93,    98,   103,   108,   111,   114,   117,
   120,   123,   130,   137,   144,   146,   150,   154,   158,   162,
   164,   165,   169,   175,   178,   180,   184,   185,   187,   189,
   193,   197,   201,   205,   209,   214,   221,   223,   227,   231,
   235,   239,   243,   246,   248,   250
};

static const short yyrhs[] = {    39,
    45,    42,     0,    39,    50,     0,    39,    11,    22,    41,
    23,    40,    28,    47,     0,    39,    11,    22,    41,    29,
    48,    23,    40,    28,    47,     0,    39,    10,    22,    46,
    23,    40,    28,    47,     0,    39,    10,    22,    46,    29,
    48,    23,    40,    28,    47,     0,    39,    28,     0,     0,
     0,    29,     8,     0,    29,     9,     0,    29,     7,     0,
     5,     0,    42,    43,     0,     0,    44,    39,    45,     0,
    22,    46,    23,    28,     0,    22,    46,    29,    48,    23,
    28,     0,     0,     5,     0,     0,    49,     0,    49,    30,
    48,     0,     9,     5,    21,    56,     0,     8,     5,    21,
    56,     0,     7,     5,    21,    56,     0,     9,     5,     0,
     8,     5,     0,     7,     5,     0,    54,    28,     0,    49,
    28,     0,    31,    57,    32,    33,    51,    50,     0,    12,
    57,    51,    13,    51,    50,     0,    34,    28,    55,    39,
    35,    28,     0,    52,     0,     5,    15,    56,     0,     5,
    16,    56,     0,     5,    18,    56,     0,     5,    17,    56,
     0,    28,     0,     0,    46,    28,    47,     0,    46,    29,
    53,    28,    47,     0,    54,    53,     0,    54,     0,     5,
    21,    56,     0,     0,     5,     0,    58,     0,    56,    26,
    56,     0,    56,    27,    56,     0,    56,    24,    56,     0,
    56,    25,    56,     0,    31,    56,    32,     0,    36,    46,
    47,    37,     0,    36,    46,    29,    53,    37,    47,     0,
    56,     0,    56,    21,    56,     0,    56,    22,    56,     0,
    56,    23,    56,     0,    56,    19,    56,     0,    56,    20,
    56,     0,    14,    57,     0,     4,     0,     3,     0,     6,
     0
};

#endif

#if YYDEBUG != 0
static const short yyrline[] = { 0,
   835,   837,   838,   839,   840,   841,   842,   843,   846,   847,
   848,   849,   852,   854,   855,   858,   860,   861,   862,   864,
   865,   867,   868,   871,   872,   873,   874,   875,   876,   879,
   880,   881,   882,   883,   884,   885,   886,   887,   888,   891,
   891,   893,   894,   897,   898,   900,   903,   906,   907,   908,
   909,   910,   911,   912,   913,   914,   917,   918,   919,   920,
   921,   922,   923,   926,   927,   928
};
#endif


#if YYDEBUG != 0 || defined (YYERROR_VERBOSE)

static const char * const yytname[] = {   "$","error","$undefined.","TYPE_INTEGER",
"TYPE_FLOAT","TYPE_VAR","TYPE_PTR","PTR_TK","INT_TK","FLOAT_TK","DECLARE","EXTERNAL",
"WHILE","DO","NOT","PLUS_EQ","SUB_EQ","DIV_EQ","MUL_EQ","SUP_EQ","LOW_EQ","'='",
"'<'","'>'","'+'","'-'","'*'","'/'","'\\n'","':'","','","'('","')'","'?'","'{'",
"'}'","'['","']'","gsl","gsl_code","return_type","ext_task_name","gsl_def_functions",
"function","function_intro","function_outro","task_name","leave_namespace","arglist",
"declaration","instruction","opt_nl","func_call","affectations","affectation",
"start_block","expression","test","constValue", NULL
};
#endif

static const short yyr1[] = {     0,
    38,    39,    39,    39,    39,    39,    39,    39,    40,    40,
    40,    40,    41,    42,    42,    43,    44,    44,    45,    46,
    47,    48,    48,    49,    49,    49,    49,    49,    49,    50,
    50,    50,    50,    50,    50,    50,    50,    50,    50,    51,
    51,    52,    52,    53,    53,    54,    55,    56,    56,    56,
    56,    56,    56,    56,    56,    56,    57,    57,    57,    57,
    57,    57,    57,    58,    58,    58
};

static const short yyr2[] = {     0,
     3,     2,     8,    10,     8,    10,     2,     0,     0,     2,
     2,     2,     1,     2,     0,     3,     4,     6,     0,     1,
     0,     1,     3,     4,     4,     4,     2,     2,     2,     2,
     2,     6,     6,     6,     1,     3,     3,     3,     3,     1,
     0,     3,     5,     2,     1,     3,     0,     1,     1,     3,
     3,     3,     3,     3,     4,     6,     1,     3,     3,     3,
     3,     3,     2,     1,     1,     1
};

static const short yydefact[] = {     8,
    19,    20,     0,     0,     0,     0,     0,     0,     7,     0,
     0,    15,     0,     0,     2,    35,     0,     0,     0,     0,
     0,     0,    29,    28,    27,     0,     0,    65,    64,    48,
    66,     0,     0,     0,    57,    41,    49,     0,    47,     1,
    21,     0,    31,    30,    36,    37,    39,    38,    46,     0,
     0,     0,    20,     0,    13,     0,    63,     0,    21,     0,
     0,     0,     0,     0,     0,     0,     0,     0,    40,     0,
     0,     8,     0,    14,     8,    42,     0,     0,    45,    26,
    25,    24,     9,     0,     9,     0,    54,     0,     0,    61,
    62,    58,    59,    60,    52,    53,    50,    51,    41,    41,
     0,     0,    19,    21,    44,     0,     0,     0,    22,     0,
     0,     0,    55,     0,     0,     0,     0,     0,    16,    43,
    12,    10,    11,    21,     9,     0,    21,     9,    21,    33,
    32,    34,    17,     0,     5,     0,    23,     3,     0,    56,
     0,    21,    21,    18,     6,     4,     0,     0,     0
};

static const short yydefgoto[] = {   147,
     1,   107,    56,    40,    74,    75,    12,    13,    76,   108,
    14,    15,    70,    16,    78,    17,    72,    35,    36,    37
};

static const short yypact[] = {-32768,
   111,   131,    -3,     8,    10,     3,     5,    26,-32768,    26,
    -5,-32768,    47,     6,-32768,-32768,    35,    68,    68,    68,
    68,    68,    46,    48,    56,    77,    92,-32768,-32768,-32768,
-32768,    26,    68,    77,   109,    75,-32768,    85,-32768,   103,
-32768,   121,-32768,-32768,    69,    69,    69,    69,    69,    68,
    68,    68,-32768,   -13,-32768,    -9,-32768,    54,   108,    68,
    68,    68,    68,    68,    68,    68,    68,    68,-32768,   125,
   107,-32768,    77,-32768,-32768,-32768,   120,   115,   121,    69,
    69,    69,   122,    52,   122,    52,-32768,   121,   113,    69,
    69,    69,    69,    69,    83,    83,   117,-32768,    75,    75,
    80,    -1,   111,-32768,-32768,   146,   128,   134,   129,   130,
   137,   124,-32768,    93,    93,   135,   136,    52,-32768,-32768,
-32768,-32768,-32768,-32768,   122,    52,-32768,   122,-32768,-32768,
-32768,-32768,-32768,   139,-32768,   138,-32768,-32768,   140,-32768,
   141,-32768,-32768,-32768,-32768,-32768,   165,   167,-32768
};

static const short yypgoto[] = {-32768,
   -33,   -84,-32768,-32768,-32768,-32768,    67,   -17,   -59,   -83,
   -60,    -8,    13,-32768,   -67,   -24,-32768,   -14,     1,-32768
};


#define	YYLAST		170


static const short yytable[] = {    89,
   110,    23,   111,    45,    46,    47,    48,    49,    54,    83,
    38,   105,    24,    85,    25,    84,    59,    79,    58,    86,
   112,   117,    39,   109,    26,   109,    27,   118,    28,    29,
    30,    31,    57,    43,   134,    80,    81,    82,   101,    32,
   136,   103,   137,   139,   120,    90,    91,    92,    93,    94,
    95,    96,    97,    98,    79,   102,    33,   109,     3,     4,
     5,    34,    44,    79,   135,   109,    50,   138,    51,   140,
    28,    29,    30,    31,    41,    42,    52,    65,    66,    67,
    68,    53,   145,   146,     2,    87,     3,     4,     5,     6,
     7,     8,    65,    66,    67,    68,    55,     2,    33,     3,
     4,     5,    69,    34,     8,   130,   131,     9,    67,    68,
    10,   114,   115,    11,   116,     2,    71,     3,     4,     5,
     6,     7,     8,    10,    73,    77,    11,    60,    61,    62,
    63,    64,    65,    66,    67,    68,    88,    99,     9,   100,
    22,    10,   104,    68,    11,    18,    19,    20,    21,   113,
   106,    22,   121,   122,   123,   124,   125,   127,   126,   128,
   129,   141,   132,   133,   148,   142,   149,   143,   144,   119
};

static const short yycheck[] = {    59,
    85,     5,    86,    18,    19,    20,    21,    22,    26,    23,
    10,    79,     5,    23,     5,    29,    34,    42,    33,    29,
    88,    23,    28,    84,    22,    86,    22,    29,     3,     4,
     5,     6,    32,    28,   118,    50,    51,    52,    72,    14,
   125,    75,   126,   128,   104,    60,    61,    62,    63,    64,
    65,    66,    67,    68,    79,    73,    31,   118,     7,     8,
     9,    36,    28,    88,   124,   126,    21,   127,    21,   129,
     3,     4,     5,     6,    28,    29,    21,    24,    25,    26,
    27,     5,   142,   143,     5,    32,     7,     8,     9,    10,
    11,    12,    24,    25,    26,    27,     5,     5,    31,     7,
     8,     9,    28,    36,    12,   114,   115,    28,    26,    27,
    31,    99,   100,    34,    35,     5,    32,     7,     8,     9,
    10,    11,    12,    31,    22,     5,    34,    19,    20,    21,
    22,    23,    24,    25,    26,    27,    29,    13,    28,    33,
    21,    31,    28,    27,    34,    15,    16,    17,    18,    37,
    29,    21,     7,     8,     9,    28,    23,    28,    30,    23,
    37,    23,    28,    28,     0,    28,     0,    28,    28,   103
};
/* -*-C-*-  Note some compilers choke on comments on `#line' lines.  */
#line 3 "/usr/share/bison.simple"
/* This file comes from bison-1.28.  */

/* Skeleton output parser for bison,
   Copyright (C) 1984, 1989, 1990 Free Software Foundation, Inc.

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

/* This is the parser code that is written into each bison parser
  when the %semantic_parser declaration is not specified in the grammar.
  It was written by Richard Stallman by simplifying the hairy parser
  used when %semantic_parser is specified.  */

#ifndef YYSTACK_USE_ALLOCA
#ifdef alloca
#define YYSTACK_USE_ALLOCA
#else /* alloca not defined */
#ifdef __GNUC__
#define YYSTACK_USE_ALLOCA
#define alloca __builtin_alloca
#else /* not GNU C.  */
#if (!defined (__STDC__) && defined (sparc)) || defined (__sparc__) || defined (__sparc) || defined (__sgi) || (defined (__sun) && defined (__i386))
#define YYSTACK_USE_ALLOCA
#include <alloca.h>
#else /* not sparc */
/* We think this test detects Watcom and Microsoft C.  */
/* This used to test MSDOS, but that is a bad idea
   since that symbol is in the user namespace.  */
#if (defined (_MSDOS) || defined (_MSDOS_)) && !defined (__TURBOC__)
#if 0 /* No need for malloc.h, which pollutes the namespace;
	 instead, just don't use alloca.  */
#include <malloc.h>
#endif
#else /* not MSDOS, or __TURBOC__ */
#if defined(_AIX)
/* I don't know what this was needed for, but it pollutes the namespace.
   So I turned it off.   rms, 2 May 1997.  */
/* #include <malloc.h>  */
 #pragma alloca
#define YYSTACK_USE_ALLOCA
#else /* not MSDOS, or __TURBOC__, or _AIX */
#if 0
#ifdef __hpux /* haible@ilog.fr says this works for HPUX 9.05 and up,
		 and on HPUX 10.  Eventually we can turn this on.  */
#define YYSTACK_USE_ALLOCA
#define alloca __builtin_alloca
#endif /* __hpux */
#endif
#endif /* not _AIX */
#endif /* not MSDOS, or __TURBOC__ */
#endif /* not sparc */
#endif /* not GNU C */
#endif /* alloca not defined */
#endif /* YYSTACK_USE_ALLOCA not defined */

#ifdef YYSTACK_USE_ALLOCA
#define YYSTACK_ALLOC alloca
#else
#define YYSTACK_ALLOC malloc
#endif

/* Note: there must be only one dollar sign in this file.
   It is replaced by the list of actions, each action
   as one case of the switch.  */

#define yyerrok		(yyerrstatus = 0)
#define yyclearin	(yychar = YYEMPTY)
#define YYEMPTY		-2
#define YYEOF		0
#define YYACCEPT	goto yyacceptlab
#define YYABORT 	goto yyabortlab
#define YYERROR		goto yyerrlab1
/* Like YYERROR except do call yyerror.
   This remains here temporarily to ease the
   transition to the new meaning of YYERROR, for GCC.
   Once GCC version 2 has supplanted version 1, this can go.  */
#define YYFAIL		goto yyerrlab
#define YYRECOVERING()  (!!yyerrstatus)
#define YYBACKUP(token, value) \
do								\
  if (yychar == YYEMPTY && yylen == 1)				\
    { yychar = (token), yylval = (value);			\
      yychar1 = YYTRANSLATE (yychar);				\
      YYPOPSTACK;						\
      goto yybackup;						\
    }								\
  else								\
    { yyerror ("syntax error: cannot back up"); YYERROR; }	\
while (0)

#define YYTERROR	1
#define YYERRCODE	256

#ifndef YYPURE
#define YYLEX		yylex()
#endif

#ifdef YYPURE
#ifdef YYLSP_NEEDED
#ifdef YYLEX_PARAM
#define YYLEX		yylex(&yylval, &yylloc, YYLEX_PARAM)
#else
#define YYLEX		yylex(&yylval, &yylloc)
#endif
#else /* not YYLSP_NEEDED */
#ifdef YYLEX_PARAM
#define YYLEX		yylex(&yylval, YYLEX_PARAM)
#else
#define YYLEX		yylex(&yylval)
#endif
#endif /* not YYLSP_NEEDED */
#endif

/* If nonreentrant, generate the variables here */

#ifndef YYPURE

int	yychar;			/*  the lookahead symbol		*/
YYSTYPE	yylval;			/*  the semantic value of the		*/
				/*  lookahead symbol			*/

#ifdef YYLSP_NEEDED
YYLTYPE yylloc;			/*  location data for the lookahead	*/
				/*  symbol				*/
#endif

int yynerrs;			/*  number of parse errors so far       */
#endif  /* not YYPURE */

#if YYDEBUG != 0
int yydebug;			/*  nonzero means print parse trace	*/
/* Since this is uninitialized, it does not stop multiple parsers
   from coexisting.  */
#endif

/*  YYINITDEPTH indicates the initial size of the parser's stacks	*/

#ifndef	YYINITDEPTH
#define YYINITDEPTH 200
#endif

/*  YYMAXDEPTH is the maximum size the stacks can grow to
    (effective only if the built-in stack extension method is used).  */

#if YYMAXDEPTH == 0
#undef YYMAXDEPTH
#endif

#ifndef YYMAXDEPTH
#define YYMAXDEPTH 10000
#endif

/* Define __yy_memcpy.  Note that the size argument
   should be passed with type unsigned int, because that is what the non-GCC
   definitions require.  With GCC, __builtin_memcpy takes an arg
   of type size_t, but it can handle unsigned int.  */

#if __GNUC__ > 1		/* GNU C and GNU C++ define this.  */
#define __yy_memcpy(TO,FROM,COUNT)	__builtin_memcpy(TO,FROM,COUNT)
#else				/* not GNU C or C++ */
#ifndef __cplusplus

/* This is the most reliable way to avoid incompatibilities
   in available built-in functions on various systems.  */
static void
__yy_memcpy (to, from, count)
     char *to;
     char *from;
     unsigned int count;
{
  register char *f = from;
  register char *t = to;
  register int i = count;

  while (i-- > 0)
    *t++ = *f++;
}

#else /* __cplusplus */

/* This is the most reliable way to avoid incompatibilities
   in available built-in functions on various systems.  */
static void
__yy_memcpy (char *to, char *from, unsigned int count)
{
  register char *t = to;
  register char *f = from;
  register int i = count;

  while (i-- > 0)
    *t++ = *f++;
}

#endif
#endif

#line 217 "/usr/share/bison.simple"

/* The user can define YYPARSE_PARAM as the name of an argument to be passed
   into yyparse.  The argument should have type void *.
   It should actually point to an object.
   Grammar actions can access the variable by casting it
   to the proper pointer type.  */

#ifdef YYPARSE_PARAM
#ifdef __cplusplus
#define YYPARSE_PARAM_ARG void *YYPARSE_PARAM
#define YYPARSE_PARAM_DECL
#else /* not __cplusplus */
#define YYPARSE_PARAM_ARG YYPARSE_PARAM
#define YYPARSE_PARAM_DECL void *YYPARSE_PARAM;
#endif /* not __cplusplus */
#else /* not YYPARSE_PARAM */
#define YYPARSE_PARAM_ARG
#define YYPARSE_PARAM_DECL
#endif /* not YYPARSE_PARAM */

/* Prevent warning if -Wstrict-prototypes.  */
#ifdef __GNUC__
#ifdef YYPARSE_PARAM
int yyparse (void *);
#else
int yyparse (void);
#endif
#endif

int
yyparse(YYPARSE_PARAM_ARG)
     YYPARSE_PARAM_DECL
{
  register int yystate;
  register int yyn;
  register short *yyssp;
  register YYSTYPE *yyvsp;
  int yyerrstatus;	/*  number of tokens to shift before error messages enabled */
  int yychar1 = 0;		/*  lookahead token as an internal (translated) token number */

  short	yyssa[YYINITDEPTH];	/*  the state stack			*/
  YYSTYPE yyvsa[YYINITDEPTH];	/*  the semantic value stack		*/

  short *yyss = yyssa;		/*  refer to the stacks thru separate pointers */
  YYSTYPE *yyvs = yyvsa;	/*  to allow yyoverflow to reallocate them elsewhere */

#ifdef YYLSP_NEEDED
  YYLTYPE yylsa[YYINITDEPTH];	/*  the location stack			*/
  YYLTYPE *yyls = yylsa;
  YYLTYPE *yylsp;

#define YYPOPSTACK   (yyvsp--, yyssp--, yylsp--)
#else
#define YYPOPSTACK   (yyvsp--, yyssp--)
#endif

  int yystacksize = YYINITDEPTH;
  int yyfree_stacks = 0;

#ifdef YYPURE
  int yychar;
  YYSTYPE yylval;
  int yynerrs;
#ifdef YYLSP_NEEDED
  YYLTYPE yylloc;
#endif
#endif

  YYSTYPE yyval;		/*  the variable used to return		*/
				/*  semantic values from the action	*/
				/*  routines				*/

  int yylen;

#if YYDEBUG != 0
  if (yydebug)
    fprintf(stderr, "Starting parse\n");
#endif

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY;		/* Cause a token to be read.  */

  /* Initialize stack pointers.
     Waste one element of value and location stack
     so that they stay on the same level as the state stack.
     The wasted elements are never initialized.  */

  yyssp = yyss - 1;
  yyvsp = yyvs;
#ifdef YYLSP_NEEDED
  yylsp = yyls;
#endif

/* Push a new state, which is found in  yystate  .  */
/* In all cases, when you get here, the value and location stacks
   have just been pushed. so pushing a state here evens the stacks.  */
yynewstate:

  *++yyssp = yystate;

  if (yyssp >= yyss + yystacksize - 1)
    {
      /* Give user a chance to reallocate the stack */
      /* Use copies of these so that the &'s don't force the real ones into memory. */
      YYSTYPE *yyvs1 = yyvs;
      short *yyss1 = yyss;
#ifdef YYLSP_NEEDED
      YYLTYPE *yyls1 = yyls;
#endif

      /* Get the current used size of the three stacks, in elements.  */
      int size = yyssp - yyss + 1;

#ifdef yyoverflow
      /* Each stack pointer address is followed by the size of
	 the data in use in that stack, in bytes.  */
#ifdef YYLSP_NEEDED
      /* This used to be a conditional around just the two extra args,
	 but that might be undefined if yyoverflow is a macro.  */
      yyoverflow("parser stack overflow",
		 &yyss1, size * sizeof (*yyssp),
		 &yyvs1, size * sizeof (*yyvsp),
		 &yyls1, size * sizeof (*yylsp),
		 &yystacksize);
#else
      yyoverflow("parser stack overflow",
		 &yyss1, size * sizeof (*yyssp),
		 &yyvs1, size * sizeof (*yyvsp),
		 &yystacksize);
#endif

      yyss = yyss1; yyvs = yyvs1;
#ifdef YYLSP_NEEDED
      yyls = yyls1;
#endif
#else /* no yyoverflow */
      /* Extend the stack our own way.  */
      if (yystacksize >= YYMAXDEPTH)
	{
	  yyerror("parser stack overflow");
	  if (yyfree_stacks)
	    {
	      free (yyss);
	      free (yyvs);
#ifdef YYLSP_NEEDED
	      free (yyls);
#endif
	    }
	  return 2;
	}
      yystacksize *= 2;
      if (yystacksize > YYMAXDEPTH)
	yystacksize = YYMAXDEPTH;
#ifndef YYSTACK_USE_ALLOCA
      yyfree_stacks = 1;
#endif
      yyss = (short *) YYSTACK_ALLOC (yystacksize * sizeof (*yyssp));
      __yy_memcpy ((char *)yyss, (char *)yyss1,
		   size * (unsigned int) sizeof (*yyssp));
      yyvs = (YYSTYPE *) YYSTACK_ALLOC (yystacksize * sizeof (*yyvsp));
      __yy_memcpy ((char *)yyvs, (char *)yyvs1,
		   size * (unsigned int) sizeof (*yyvsp));
#ifdef YYLSP_NEEDED
      yyls = (YYLTYPE *) YYSTACK_ALLOC (yystacksize * sizeof (*yylsp));
      __yy_memcpy ((char *)yyls, (char *)yyls1,
		   size * (unsigned int) sizeof (*yylsp));
#endif
#endif /* no yyoverflow */

      yyssp = yyss + size - 1;
      yyvsp = yyvs + size - 1;
#ifdef YYLSP_NEEDED
      yylsp = yyls + size - 1;
#endif

#if YYDEBUG != 0
      if (yydebug)
	fprintf(stderr, "Stack size increased to %d\n", yystacksize);
#endif

      if (yyssp >= yyss + yystacksize - 1)
	YYABORT;
    }

#if YYDEBUG != 0
  if (yydebug)
    fprintf(stderr, "Entering state %d\n", yystate);
#endif

  goto yybackup;
 yybackup:

/* Do appropriate processing given the current state.  */
/* Read a lookahead token if we need one and don't already have one.  */
/* yyresume: */

  /* First try to decide what to do without reference to lookahead token.  */

  yyn = yypact[yystate];
  if (yyn == YYFLAG)
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* yychar is either YYEMPTY or YYEOF
     or a valid token in external form.  */

  if (yychar == YYEMPTY)
    {
#if YYDEBUG != 0
      if (yydebug)
	fprintf(stderr, "Reading a token: ");
#endif
      yychar = YYLEX;
    }

  /* Convert token to internal form (in yychar1) for indexing tables with */

  if (yychar <= 0)		/* This means end of input. */
    {
      yychar1 = 0;
      yychar = YYEOF;		/* Don't call YYLEX any more */

#if YYDEBUG != 0
      if (yydebug)
	fprintf(stderr, "Now at end of input.\n");
#endif
    }
  else
    {
      yychar1 = YYTRANSLATE(yychar);

#if YYDEBUG != 0
      if (yydebug)
	{
	  fprintf (stderr, "Next token is %d (%s", yychar, yytname[yychar1]);
	  /* Give the individual parser a way to print the precise meaning
	     of a token, for further debugging info.  */
#ifdef YYPRINT
	  YYPRINT (stderr, yychar, yylval);
#endif
	  fprintf (stderr, ")\n");
	}
#endif
    }

  yyn += yychar1;
  if (yyn < 0 || yyn > YYLAST || yycheck[yyn] != yychar1)
    goto yydefault;

  yyn = yytable[yyn];

  /* yyn is what to do for this token type in this state.
     Negative => reduce, -yyn is rule number.
     Positive => shift, yyn is new state.
       New state is final state => don't bother to shift,
       just return success.
     0, or most negative number => error.  */

  if (yyn < 0)
    {
      if (yyn == YYFLAG)
	goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }
  else if (yyn == 0)
    goto yyerrlab;

  if (yyn == YYFINAL)
    YYACCEPT;

  /* Shift the lookahead token.  */

#if YYDEBUG != 0
  if (yydebug)
    fprintf(stderr, "Shifting token %d (%s), ", yychar, yytname[yychar1]);
#endif

  /* Discard the token being shifted unless it is eof.  */
  if (yychar != YYEOF)
    yychar = YYEMPTY;

  *++yyvsp = yylval;
#ifdef YYLSP_NEEDED
  *++yylsp = yylloc;
#endif

  /* count tokens shifted since error; after three, turn off error status.  */
  if (yyerrstatus) yyerrstatus--;

  yystate = yyn;
  goto yynewstate;

/* Do the default action for the current state.  */
yydefault:

  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;

/* Do a reduction.  yyn is the number of a rule to reduce with.  */
yyreduce:
  yylen = yyr2[yyn];
  if (yylen > 0)
    yyval = yyvsp[1-yylen]; /* implement default value of the action */

#if YYDEBUG != 0
  if (yydebug)
    {
      int i;

      fprintf (stderr, "Reducing via rule %d (line %d), ",
	       yyn, yyrline[yyn]);

      /* Print the symbols being reduced, and their result.  */
      for (i = yyprhs[yyn]; yyrhs[i] > 0; i++)
	fprintf (stderr, "%s ", yytname[yyrhs[i]]);
      fprintf (stderr, " -> %s\n", yytname[yyr1[yyn]]);
    }
#endif


  switch (yyn) {

case 2:
#line 837 "goomsl_yacc.y"
{ gsl_append(yyvsp[0].nPtr); ;
    break;}
case 3:
#line 838 "goomsl_yacc.y"
{ DECL_VAR(yyvsp[-2].intValue,yyvsp[-4].strValue); ;
    break;}
case 4:
#line 839 "goomsl_yacc.y"
{ DECL_VAR(yyvsp[-2].intValue,yyvsp[-6].strValue); ;
    break;}
case 5:
#line 840 "goomsl_yacc.y"
{ DECL_VAR(yyvsp[-2].intValue,yyvsp[-4].strValue); ;
    break;}
case 6:
#line 841 "goomsl_yacc.y"
{ DECL_VAR(yyvsp[-2].intValue,yyvsp[-6].strValue); ;
    break;}
case 9:
#line 846 "goomsl_yacc.y"
{ yyval.intValue=-1; ;
    break;}
case 10:
#line 847 "goomsl_yacc.y"
{ yyval.intValue=INT_TK; ;
    break;}
case 11:
#line 848 "goomsl_yacc.y"
{ yyval.intValue=FLOAT_TK; ;
    break;}
case 12:
#line 849 "goomsl_yacc.y"
{ yyval.intValue=PTR_TK; ;
    break;}
case 13:
#line 852 "goomsl_yacc.y"
{ gsl_declare_external_task(yyvsp[0].strValue); gsl_enternamespace(yyvsp[0].strValue); strcpy(yyval.strValue,yyvsp[0].strValue); ;
    break;}
case 16:
#line 858 "goomsl_yacc.y"
{ gsl_leavenamespace(); ;
    break;}
case 17:
#line 860 "goomsl_yacc.y"
{ gsl_append(new_function_intro(yyvsp[-2].strValue)); ;
    break;}
case 18:
#line 861 "goomsl_yacc.y"
{ gsl_append(new_function_intro(yyvsp[-4].strValue)); ;
    break;}
case 19:
#line 862 "goomsl_yacc.y"
{ gsl_append(new_function_outro()); ;
    break;}
case 20:
#line 864 "goomsl_yacc.y"
{ gsl_declare_task(yyvsp[0].strValue); gsl_enternamespace(yyvsp[0].strValue); strcpy(yyval.strValue,yyvsp[0].strValue); strcpy(yyval.strValue,yyvsp[0].strValue); ;
    break;}
case 21:
#line 865 "goomsl_yacc.y"
{ gsl_leavenamespace();   ;
    break;}
case 22:
#line 867 "goomsl_yacc.y"
{ gsl_append(yyvsp[0].nPtr); ;
    break;}
case 23:
#line 868 "goomsl_yacc.y"
{ gsl_append(yyvsp[-2].nPtr); ;
    break;}
case 24:
#line 871 "goomsl_yacc.y"
{ gsl_float_decl(yyvsp[-2].strValue); yyval.nPtr = new_set(new_var(yyvsp[-2].strValue,currentGoomSL->num_lines), yyvsp[0].nPtr); ;
    break;}
case 25:
#line 872 "goomsl_yacc.y"
{ gsl_int_decl(yyvsp[-2].strValue);   yyval.nPtr = new_set(new_var(yyvsp[-2].strValue,currentGoomSL->num_lines), yyvsp[0].nPtr); ;
    break;}
case 26:
#line 873 "goomsl_yacc.y"
{ gsl_ptr_decl(yyvsp[-2].strValue);   yyval.nPtr = new_set(new_var(yyvsp[-2].strValue,currentGoomSL->num_lines), yyvsp[0].nPtr); ;
    break;}
case 27:
#line 874 "goomsl_yacc.y"
{ yyval.nPtr = 0; gsl_float_decl(yyvsp[0].strValue); ;
    break;}
case 28:
#line 875 "goomsl_yacc.y"
{ yyval.nPtr = 0; gsl_int_decl(yyvsp[0].strValue);   ;
    break;}
case 29:
#line 876 "goomsl_yacc.y"
{ yyval.nPtr = 0; gsl_ptr_decl(yyvsp[0].strValue);   ;
    break;}
case 30:
#line 879 "goomsl_yacc.y"
{ yyval.nPtr = yyvsp[-1].nPtr; ;
    break;}
case 31:
#line 880 "goomsl_yacc.y"
{ yyval.nPtr = yyvsp[-1].nPtr; ;
    break;}
case 32:
#line 881 "goomsl_yacc.y"
{ yyval.nPtr = new_if(yyvsp[-4].nPtr,yyvsp[0].nPtr); ;
    break;}
case 33:
#line 882 "goomsl_yacc.y"
{ yyval.nPtr = new_while(yyvsp[-4].nPtr,yyvsp[0].nPtr); ;
    break;}
case 34:
#line 883 "goomsl_yacc.y"
{ lastNode = yyvsp[-3].nPtr->unode.opr.op[1]; yyval.nPtr=yyvsp[-3].nPtr; ;
    break;}
case 35:
#line 884 "goomsl_yacc.y"
{ yyval.nPtr = yyvsp[0].nPtr; ;
    break;}
case 36:
#line 885 "goomsl_yacc.y"
{ yyval.nPtr = new_plus_eq(new_var(yyvsp[-2].strValue,currentGoomSL->num_lines),yyvsp[0].nPtr); ;
    break;}
case 37:
#line 886 "goomsl_yacc.y"
{ yyval.nPtr = new_sub_eq(new_var(yyvsp[-2].strValue,currentGoomSL->num_lines),yyvsp[0].nPtr); ;
    break;}
case 38:
#line 887 "goomsl_yacc.y"
{ yyval.nPtr = new_mul_eq(new_var(yyvsp[-2].strValue,currentGoomSL->num_lines),yyvsp[0].nPtr); ;
    break;}
case 39:
#line 888 "goomsl_yacc.y"
{ yyval.nPtr = new_div_eq(new_var(yyvsp[-2].strValue,currentGoomSL->num_lines),yyvsp[0].nPtr); ;
    break;}
case 42:
#line 893 "goomsl_yacc.y"
{ yyval.nPtr = new_call(yyvsp[-2].strValue,NULL); ;
    break;}
case 43:
#line 894 "goomsl_yacc.y"
{ yyval.nPtr = new_call(yyvsp[-4].strValue,yyvsp[-2].nPtr); ;
    break;}
case 44:
#line 897 "goomsl_yacc.y"
{ yyval.nPtr = new_affec_list(yyvsp[-1].nPtr,yyvsp[0].nPtr);   ;
    break;}
case 45:
#line 898 "goomsl_yacc.y"
{ yyval.nPtr = new_affec_list(yyvsp[0].nPtr,NULL); ;
    break;}
case 46:
#line 900 "goomsl_yacc.y"
{ yyval.nPtr = new_set(new_var(yyvsp[-2].strValue,currentGoomSL->num_lines),yyvsp[0].nPtr); ;
    break;}
case 47:
#line 903 "goomsl_yacc.y"
{ yyval.nPtr = new_block(lastNode); lastNode = yyval.nPtr->unode.opr.op[0]; ;
    break;}
case 48:
#line 906 "goomsl_yacc.y"
{ yyval.nPtr = new_var(yyvsp[0].strValue,currentGoomSL->num_lines); ;
    break;}
case 49:
#line 907 "goomsl_yacc.y"
{ yyval.nPtr = yyvsp[0].nPtr; ;
    break;}
case 50:
#line 908 "goomsl_yacc.y"
{ yyval.nPtr = new_mul(yyvsp[-2].nPtr,yyvsp[0].nPtr); ;
    break;}
case 51:
#line 909 "goomsl_yacc.y"
{ yyval.nPtr = new_div(yyvsp[-2].nPtr,yyvsp[0].nPtr); ;
    break;}
case 52:
#line 910 "goomsl_yacc.y"
{ yyval.nPtr = new_add(yyvsp[-2].nPtr,yyvsp[0].nPtr); ;
    break;}
case 53:
#line 911 "goomsl_yacc.y"
{ yyval.nPtr = new_sub(yyvsp[-2].nPtr,yyvsp[0].nPtr); ;
    break;}
case 54:
#line 912 "goomsl_yacc.y"
{ yyval.nPtr = yyvsp[-1].nPtr; ;
    break;}
case 55:
#line 913 "goomsl_yacc.y"
{ yyval.nPtr = new_call_expr(yyvsp[-2].strValue,NULL); ;
    break;}
case 56:
#line 914 "goomsl_yacc.y"
{ yyval.nPtr = new_call_expr(yyvsp[-4].strValue,yyvsp[-2].nPtr); ;
    break;}
case 57:
#line 917 "goomsl_yacc.y"
{ yyval.nPtr=yyvsp[0].nPtr; ;
    break;}
case 58:
#line 918 "goomsl_yacc.y"
{ yyval.nPtr = new_equ(yyvsp[-2].nPtr,yyvsp[0].nPtr); ;
    break;}
case 59:
#line 919 "goomsl_yacc.y"
{ yyval.nPtr = new_low(yyvsp[-2].nPtr,yyvsp[0].nPtr); ;
    break;}
case 60:
#line 920 "goomsl_yacc.y"
{ yyval.nPtr = new_low(yyvsp[0].nPtr,yyvsp[-2].nPtr); ;
    break;}
case 61:
#line 921 "goomsl_yacc.y"
{ yyval.nPtr = new_not(new_low(yyvsp[-2].nPtr,yyvsp[0].nPtr)); ;
    break;}
case 62:
#line 922 "goomsl_yacc.y"
{ yyval.nPtr = new_not(new_low(yyvsp[0].nPtr,yyvsp[-2].nPtr)); ;
    break;}
case 63:
#line 923 "goomsl_yacc.y"
{ yyval.nPtr = new_not(yyvsp[0].nPtr);    ;
    break;}
case 64:
#line 926 "goomsl_yacc.y"
{ yyval.nPtr = new_constFloat(yyvsp[0].strValue,currentGoomSL->num_lines); ;
    break;}
case 65:
#line 927 "goomsl_yacc.y"
{ yyval.nPtr = new_constInt(yyvsp[0].strValue,currentGoomSL->num_lines); ;
    break;}
case 66:
#line 928 "goomsl_yacc.y"
{ yyval.nPtr = new_constPtr(yyvsp[0].strValue,currentGoomSL->num_lines); ;
    break;}
}
   /* the action file gets copied in in place of this dollarsign */
#line 543 "/usr/share/bison.simple"

  yyvsp -= yylen;
  yyssp -= yylen;
#ifdef YYLSP_NEEDED
  yylsp -= yylen;
#endif

#if YYDEBUG != 0
  if (yydebug)
    {
      short *ssp1 = yyss - 1;
      fprintf (stderr, "state stack now");
      while (ssp1 != yyssp)
	fprintf (stderr, " %d", *++ssp1);
      fprintf (stderr, "\n");
    }
#endif

  *++yyvsp = yyval;

#ifdef YYLSP_NEEDED
  yylsp++;
  if (yylen == 0)
    {
      yylsp->first_line = yylloc.first_line;
      yylsp->first_column = yylloc.first_column;
      yylsp->last_line = (yylsp-1)->last_line;
      yylsp->last_column = (yylsp-1)->last_column;
      yylsp->text = 0;
    }
  else
    {
      yylsp->last_line = (yylsp+yylen-1)->last_line;
      yylsp->last_column = (yylsp+yylen-1)->last_column;
    }
#endif

  /* Now "shift" the result of the reduction.
     Determine what state that goes to,
     based on the state we popped back to
     and the rule number reduced by.  */

  yyn = yyr1[yyn];

  yystate = yypgoto[yyn - YYNTBASE] + *yyssp;
  if (yystate >= 0 && yystate <= YYLAST && yycheck[yystate] == *yyssp)
    yystate = yytable[yystate];
  else
    yystate = yydefgoto[yyn - YYNTBASE];

  goto yynewstate;

yyerrlab:   /* here on detecting error */

  if (! yyerrstatus)
    /* If not already recovering from an error, report this error.  */
    {
      ++yynerrs;

#ifdef YYERROR_VERBOSE
      yyn = yypact[yystate];

      if (yyn > YYFLAG && yyn < YYLAST)
	{
	  int size = 0;
	  char *msg;
	  int x, count;

	  count = 0;
	  /* Start X at -yyn if nec to avoid negative indexes in yycheck.  */
	  for (x = (yyn < 0 ? -yyn : 0);
	       x < (sizeof(yytname) / sizeof(char *)); x++)
	    if (yycheck[x + yyn] == x)
	      size += strlen(yytname[x]) + 15, count++;
	  msg = (char *) malloc(size + 15);
	  if (msg != 0)
	    {
	      strcpy(msg, "parse error");

	      if (count < 5)
		{
		  count = 0;
		  for (x = (yyn < 0 ? -yyn : 0);
		       x < (sizeof(yytname) / sizeof(char *)); x++)
		    if (yycheck[x + yyn] == x)
		      {
			strcat(msg, count == 0 ? ", expecting `" : " or `");
			strcat(msg, yytname[x]);
			strcat(msg, "'");
			count++;
		      }
		}
	      yyerror(msg);
	      free(msg);
	    }
	  else
	    yyerror ("parse error; also virtual memory exceeded");
	}
      else
#endif /* YYERROR_VERBOSE */
	yyerror("parse error");
    }

  goto yyerrlab1;
yyerrlab1:   /* here on error raised explicitly by an action */

  if (yyerrstatus == 3)
    {
      /* if just tried and failed to reuse lookahead token after an error, discard it.  */

      /* return failure if at end of input */
      if (yychar == YYEOF)
	YYABORT;

#if YYDEBUG != 0
      if (yydebug)
	fprintf(stderr, "Discarding token %d (%s).\n", yychar, yytname[yychar1]);
#endif

      yychar = YYEMPTY;
    }

  /* Else will try to reuse lookahead token
     after shifting the error token.  */

  yyerrstatus = 3;		/* Each real token shifted decrements this */

  goto yyerrhandle;

yyerrdefault:  /* current state does not do anything special for the error token. */

#if 0
  /* This is wrong; only states that explicitly want error tokens
     should shift them.  */
  yyn = yydefact[yystate];  /* If its default is to accept any token, ok.  Otherwise pop it.*/
  if (yyn) goto yydefault;
#endif

yyerrpop:   /* pop the current state because it cannot handle the error token */

  if (yyssp == yyss) YYABORT;
  yyvsp--;
  yystate = *--yyssp;
#ifdef YYLSP_NEEDED
  yylsp--;
#endif

#if YYDEBUG != 0
  if (yydebug)
    {
      short *ssp1 = yyss - 1;
      fprintf (stderr, "Error: state stack now");
      while (ssp1 != yyssp)
	fprintf (stderr, " %d", *++ssp1);
      fprintf (stderr, "\n");
    }
#endif

yyerrhandle:

  yyn = yypact[yystate];
  if (yyn == YYFLAG)
    goto yyerrdefault;

  yyn += YYTERROR;
  if (yyn < 0 || yyn > YYLAST || yycheck[yyn] != YYTERROR)
    goto yyerrdefault;

  yyn = yytable[yyn];
  if (yyn < 0)
    {
      if (yyn == YYFLAG)
	goto yyerrpop;
      yyn = -yyn;
      goto yyreduce;
    }
  else if (yyn == 0)
    goto yyerrpop;

  if (yyn == YYFINAL)
    YYACCEPT;

#if YYDEBUG != 0
  if (yydebug)
    fprintf(stderr, "Shifting error token, ");
#endif

  *++yyvsp = yylval;
#ifdef YYLSP_NEEDED
  *++yylsp = yylloc;
#endif

  yystate = yyn;
  goto yynewstate;

 yyacceptlab:
  /* YYACCEPT comes here.  */
  if (yyfree_stacks)
    {
      free (yyss);
      free (yyvs);
#ifdef YYLSP_NEEDED
      free (yyls);
#endif
    }
  return 0;

 yyabortlab:
  /* YYABORT comes here.  */
  if (yyfree_stacks)
    {
      free (yyss);
      free (yyvs);
#ifdef YYLSP_NEEDED
      free (yyls);
#endif
    }
  return 1;
}
#line 931 "goomsl_yacc.y"



void yyerror(char *str)
{ /* {{{ */
    fprintf(stderr, "ERROR: Line %d, %s\n", currentGoomSL->num_lines, str);
    currentGoomSL->compilationOK = 0;
    exit(1);
} /* }}} */

