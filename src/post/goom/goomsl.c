#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "goomsl.h"
#include "goomsl_private.h"
#include "goomsl_yacc.h"

/* #define TRACE_SCRIPT */

 /* {{{ definition of the instructions number */
#define INSTR_SETI_VAR_INTEGER     1
#define INSTR_SETI_VAR_VAR         2
#define INSTR_SETF_VAR_FLOAT       3
#define INSTR_SETF_VAR_VAR         4
#define INSTR_NOP                  5
/* #define INSTR_JUMP              6 */
#define INSTR_SETP_VAR_PTR         7
#define INSTR_SETP_VAR_VAR         8
#define INSTR_SUBI_VAR_INTEGER     9
#define INSTR_SUBI_VAR_VAR         10
#define INSTR_SUBF_VAR_FLOAT       11
#define INSTR_SUBF_VAR_VAR         12
#define INSTR_ISLOWERF_VAR_VAR     13
#define INSTR_ISLOWERF_VAR_FLOAT   14
#define INSTR_ISLOWERI_VAR_VAR     15
#define INSTR_ISLOWERI_VAR_INTEGER 16
#define INSTR_ADDI_VAR_INTEGER     17
#define INSTR_ADDI_VAR_VAR         18
#define INSTR_ADDF_VAR_FLOAT       19
#define INSTR_ADDF_VAR_VAR         20
#define INSTR_MULI_VAR_INTEGER     21
#define INSTR_MULI_VAR_VAR         22
#define INSTR_MULF_VAR_FLOAT       23
#define INSTR_MULF_VAR_VAR         24
#define INSTR_DIVI_VAR_INTEGER     25
#define INSTR_DIVI_VAR_VAR         26
#define INSTR_DIVF_VAR_FLOAT       27
#define INSTR_DIVF_VAR_VAR         28
/* #define INSTR_JZERO             29 */
#define INSTR_ISEQUALP_VAR_VAR     30
#define INSTR_ISEQUALP_VAR_PTR     31
#define INSTR_ISEQUALI_VAR_VAR     32
#define INSTR_ISEQUALI_VAR_INTEGER 33
#define INSTR_ISEQUALF_VAR_VAR     34
#define INSTR_ISEQUALF_VAR_FLOAT   35
/* #define INSTR_CALL              36 */
/* #define INSTR_RET               37 */
/* #define INSTR_EXT_CALL          38 */
#define INSTR_NOT_VAR              39
/* #define INSTR_JNZERO            40 */
 /* }}} */
/* {{{ definition of the validation error types */
static const char *VALIDATE_OK = "ok"; 
#define VALIDATE_ERROR "error while validating "
#define VALIDATE_TODO "todo"
#define VALIDATE_SYNTHAX_ERROR "synthax error"
#define VALIDATE_NO_SUCH_INT "no such integer variable"
#define VALIDATE_NO_SUCH_VAR "no such variable"
#define VALIDATE_NO_SUCH_DEST_VAR "no such destination variable"
#define VALIDATE_NO_SUCH_SRC_VAR "no such src variable"
/* }}} */

  /***********************************/
 /* PROTOTYPE OF INTERNAL FUNCTIONS */
/***********************************/

/* {{{ */
static void gsl_instr_free(Instruction *_this);
static const char *gsl_instr_validate(Instruction *_this);
static void gsl_instr_display(Instruction *_this);

static InstructionFlow *iflow_new(void);
static void iflow_add_instr(InstructionFlow *_this, Instruction *instr);
static void iflow_clean(InstructionFlow *_this);
static void iflow_free(InstructionFlow *_this);
static void iflow_execute(FastInstructionFlow *_this, GoomSL *gsl);
/* }}} */

  /************************************/
 /* DEFINITION OF INTERNAL FUNCTIONS */
/************************************/

void iflow_free(InstructionFlow *_this)
{ /* {{{ */
  goom_hash_free(_this->labels);
  free(_this); /*TODO: finir cette fonction */
} /* }}} */

void iflow_clean(InstructionFlow *_this)
{ /* {{{ */
  /* TODO: clean chaque instruction du flot */
  _this->number = 0;
  goom_hash_free(_this->labels);
  _this->labels = goom_hash_new();
} /* }}} */

InstructionFlow *iflow_new(void)
{ /* {{{ */
  InstructionFlow *_this = (InstructionFlow*)malloc(sizeof(InstructionFlow));
  _this->number = 0;
  _this->tabsize = 6;
  _this->instr = (Instruction**)malloc(_this->tabsize * sizeof(Instruction*));
  _this->labels = goom_hash_new();

  return _this;
} /* }}} */

void iflow_add_instr(InstructionFlow *_this, Instruction *instr)
{ /* {{{ */
  if (_this->number == _this->tabsize) {
    _this->tabsize *= 2;
    _this->instr = (Instruction**)realloc(_this->instr, _this->tabsize * sizeof(Instruction*));
  }
  _this->instr[_this->number] = instr;
  instr->address = _this->number;
  _this->number++;
} /* }}} */

void gsl_instr_set_namespace(Instruction *_this, GoomHash *ns)
{ /* {{{ */
  if (_this->cur_param <= 0) {
    fprintf(stderr, "ERROR: Line %d, No more params to instructions\n", _this->line_number);
    exit(1);
  }
  _this->vnamespace[_this->cur_param-1] = ns;
} /* }}} */

void gsl_instr_add_param(Instruction *instr, char *param, int type)
{ /* {{{ */
  int len;
  if (instr==NULL)
    return;
  if (instr->cur_param==0)
    return;
  --instr->cur_param;
  len = strlen(param);
  instr->params[instr->cur_param] = (char*)malloc(len+1);
  strcpy(instr->params[instr->cur_param], param);
  instr->types[instr->cur_param] = type;
  if (instr->cur_param == 0) {

    const char *result = gsl_instr_validate(instr);
    if (result != VALIDATE_OK) {
      printf("ERROR: Line %d: ", instr->parent->num_lines + 1);
      gsl_instr_display(instr);
      printf("... %s\n", result);
      instr->parent->compilationOK = 0;
      exit(1);
    }

#if USE_JITC_X86
    iflow_add_instr(instr->parent->iflow, instr);
#else
    if (instr->id != INSTR_NOP)
      iflow_add_instr(instr->parent->iflow, instr);
    else
      gsl_instr_free(instr);
#endif
  }
} /* }}} */

Instruction *gsl_instr_init(GoomSL *parent, const char *name, int id, int nb_param, int line_number)
{ /* {{{ */
  Instruction *instr = (Instruction*)malloc(sizeof(Instruction));
  instr->params     = (char**)malloc(nb_param*sizeof(char*));
  instr->vnamespace = (GoomHash**)malloc(nb_param*sizeof(GoomHash*));
  instr->types = (int*)malloc(nb_param*sizeof(int));
  instr->cur_param = instr->nb_param = nb_param;
  instr->parent = parent;
  instr->id = id;
  instr->name = name;
  instr->jump_label = NULL;
  instr->line_number = line_number;
  return instr;
} /* }}} */

void gsl_instr_free(Instruction *_this)
{ /* {{{ */
  int i;
  free(_this->types);
  for (i=_this->cur_param; i<_this->nb_param; ++i)
    free(_this->params[i]);
  free(_this->params);
  free(_this);
} /* }}} */

void gsl_instr_display(Instruction *_this)
{ /* {{{ */
  int i=_this->nb_param-1;
  printf("%s", _this->name);
  while(i>=_this->cur_param) {
    printf(" %s", _this->params[i]);
    --i;
  }
} /* }}} */

  /****************************************/
 /* VALIDATION OF INSTRUCTION PARAMETERS */
/****************************************/

static const char *validate_v_v(Instruction *_this)
{ /* {{{ */
  _this->data.v_v.var_dest = goom_hash_get(_this->vnamespace[1], _this->params[1]);
  _this->data.v_v.var_src =  goom_hash_get(_this->vnamespace[0], _this->params[0]);

  if (_this->data.v_v.var_dest == NULL) {
    return VALIDATE_NO_SUCH_DEST_VAR;
  }
  if (_this->data.v_v.var_src == NULL) {
    return VALIDATE_NO_SUCH_SRC_VAR;
  }
  return VALIDATE_OK;
} /* }}} */

static const char *validate_v_i(Instruction *_this)
{ /* {{{ */
  _this->data.v_i.var = goom_hash_get(_this->vnamespace[1], _this->params[1]);
  _this->data.v_i.value = strtol(_this->params[0],NULL,0);

  if (_this->data.v_i.var == NULL) {
    return VALIDATE_NO_SUCH_INT;
  }
  return VALIDATE_OK;
} /* }}} */

static const char *validate_v_p(Instruction *_this)
{ /* {{{ */
  _this->data.v_p.var = goom_hash_get(_this->vnamespace[1], _this->params[1]);
  _this->data.v_p.value = strtol(_this->params[0],NULL,0);

  if (_this->data.v_p.var == NULL) {
    return VALIDATE_NO_SUCH_INT;
  }
  return VALIDATE_OK;
} /* }}} */

static const char *validate_v_f(Instruction *_this)
{ /* {{{ */
  _this->data.v_f.var = goom_hash_get(_this->vnamespace[1], _this->params[1]);
  _this->data.v_f.value = atof(_this->params[0]);

  if (_this->data.v_f.var == NULL) {
    return VALIDATE_NO_SUCH_VAR;
  }
  return VALIDATE_OK;
} /* }}} */

static const char *validate(Instruction *_this, int vf_f_id, int vf_v_id, int vi_i_id, int vi_v_id, int vp_p_id, int vp_v_id)
{ /* {{{ */
  if ((_this->types[1] == TYPE_FVAR) && (_this->types[0] == TYPE_FLOAT)) {
    _this->id = vf_f_id;
    return validate_v_f(_this);
  }
  else if ((_this->types[1] == TYPE_FVAR) && (_this->types[0] == TYPE_VAR)) {
    _this->id = vf_v_id;
    return validate_v_v(_this);
  }
  else if ((_this->types[1] == TYPE_IVAR) && (_this->types[0] == TYPE_INTEGER)) {
    _this->id = vi_i_id;
    return validate_v_i(_this);
  }
  else if ((_this->types[1] == TYPE_IVAR) && (_this->types[0] == TYPE_VAR)) {
    _this->id = vi_v_id;
    return validate_v_v(_this);
  }
  else if ((_this->types[1] == TYPE_PVAR) && (_this->types[0] == TYPE_PTR)) {
    if (vp_p_id == INSTR_NOP) return VALIDATE_ERROR;
    _this->id = vp_p_id;
    return validate_v_p(_this);
  }
  else if ((_this->types[1] == TYPE_PVAR) && (_this->types[0] == TYPE_VAR)) {
    _this->id = vp_v_id;
    if (vp_v_id == INSTR_NOP) return VALIDATE_ERROR;
    return validate_v_v(_this);
  }
  return VALIDATE_ERROR;
} /* }}} */

const char *gsl_instr_validate(Instruction *_this)
{ /* {{{ */
  if (_this->id != INSTR_EXT_CALL) {
    int i;
    for (i=_this->nb_param-1;i>=0;--i)
      if (_this->types[i] == TYPE_VAR) {
        HashValue *val = goom_hash_get(_this->vnamespace[i], _this->params[i]);
        if (val && val->i == INSTR_INT)
          _this->types[i] = TYPE_IVAR;
        else if (val && val->i == INSTR_FLOAT)
          _this->types[i] = TYPE_FVAR;
        else if (val && val->i == INSTR_PTR)
          _this->types[i] = TYPE_PVAR;
        else fprintf(stderr,"WARNING: Line %d, %s has no namespace\n", _this->line_number, _this->params[i]);
        break;
      }
  }

  switch (_this->id) {

    /* set */ 
    case INSTR_SET:
      return validate(_this,
          INSTR_SETF_VAR_FLOAT, INSTR_SETF_VAR_VAR,
          INSTR_SETI_VAR_INTEGER, INSTR_SETI_VAR_VAR,
          INSTR_SETP_VAR_PTR, INSTR_SETP_VAR_VAR);

      /* extcall */
    case INSTR_EXT_CALL:
      if (_this->types[0] == TYPE_VAR) {
        HashValue *fval = goom_hash_get(_this->parent->functions, _this->params[0]);
        if (fval) {
          _this->data.external_function = (struct _ExternalFunctionStruct*)fval->ptr;
          return VALIDATE_OK;
        }
      }
      return VALIDATE_ERROR;

      /* call */
    case INSTR_CALL:
      if (_this->types[0] == TYPE_LABEL) {
        _this->jump_label = _this->params[0];
        return VALIDATE_OK;
      }
      return VALIDATE_ERROR;

      /* ret */
    case INSTR_RET:
      return VALIDATE_OK;

      /* jump */
    case INSTR_JUMP:

      if (_this->types[0] == TYPE_LABEL) {
        _this->jump_label = _this->params[0];
        return VALIDATE_OK;
      }
      return VALIDATE_ERROR;

      /* jzero / jnzero */
    case INSTR_JZERO:
    case INSTR_JNZERO:

      if (_this->types[0] == TYPE_LABEL) {
        _this->jump_label = _this->params[0];
        return VALIDATE_OK;
      }
      return VALIDATE_ERROR;

      /* label */
    case INSTR_LABEL:

      if (_this->types[0] == TYPE_LABEL) {
        _this->id = INSTR_NOP;
        _this->nop_label = _this->params[0];
        goom_hash_put_int(_this->parent->iflow->labels, _this->params[0], _this->parent->iflow->number);
        return VALIDATE_OK;
      }
      return VALIDATE_ERROR;

      /* isequal */
    case INSTR_ISEQUAL:
      return validate(_this,
          INSTR_ISEQUALF_VAR_FLOAT, INSTR_ISEQUALF_VAR_VAR,
          INSTR_ISEQUALI_VAR_INTEGER, INSTR_ISEQUALI_VAR_VAR,
          INSTR_ISEQUALP_VAR_PTR, INSTR_ISEQUALP_VAR_VAR);

      /* not */
    case INSTR_NOT:
      _this->id = INSTR_NOT_VAR;
      return VALIDATE_OK;

      /* islower */
    case INSTR_ISLOWER:
      return validate(_this,
          INSTR_ISLOWERF_VAR_FLOAT, INSTR_ISLOWERF_VAR_VAR,
          INSTR_ISLOWERI_VAR_INTEGER, INSTR_ISLOWERI_VAR_VAR,
          INSTR_NOP, INSTR_NOP);

      /* add */
    case INSTR_ADD:
      return validate(_this,
          INSTR_ADDF_VAR_FLOAT, INSTR_ADDF_VAR_VAR,
          INSTR_ADDI_VAR_INTEGER, INSTR_ADDI_VAR_VAR,
          INSTR_NOP, INSTR_NOP);

      /* mul */
    case INSTR_MUL:
      return validate(_this,
          INSTR_MULF_VAR_FLOAT, INSTR_MULF_VAR_VAR,
          INSTR_MULI_VAR_INTEGER, INSTR_MULI_VAR_VAR,
          INSTR_NOP, INSTR_NOP);

      /* sub */
    case INSTR_SUB:
      return validate(_this,
          INSTR_SUBF_VAR_FLOAT, INSTR_SUBF_VAR_VAR,
          INSTR_SUBI_VAR_INTEGER, INSTR_SUBI_VAR_VAR,
          INSTR_NOP, INSTR_NOP);

      /* div */
    case INSTR_DIV:
      return validate(_this,
          INSTR_DIVF_VAR_FLOAT, INSTR_DIVF_VAR_VAR,
          INSTR_DIVI_VAR_INTEGER, INSTR_DIVI_VAR_VAR,
          INSTR_NOP,INSTR_NOP);

    default:
      return VALIDATE_TODO;
  }
  return VALIDATE_ERROR;
} /* }}} */

  /*************/
 /* EXECUTION */
/*************/
void iflow_execute(FastInstructionFlow *_this, GoomSL *gsl)
{ /* {{{ */
  int flag = 0;
  int ip = 0;
  FastInstruction *instr = _this->instr;
  int stack[0x10000];
  int stack_pointer = 0;

  stack[stack_pointer++] = -1;

  while (1) {
#ifdef TRACE_SCRIPT 
    printf("execute "); gsl_instr_display(instr[ip].proto); printf("\n");
#endif
    switch (instr[ip].id) {

      /* SET.I */
      case INSTR_SETI_VAR_INTEGER:
        instr[ip].data.v_i.var->i = instr[ip].data.v_i.value;
        ++ip; break;

      case INSTR_SETI_VAR_VAR:
        instr[ip].data.v_v.var_dest->i = instr[ip].data.v_v.var_src->i;
        ++ip; break;

        /* SET.F */
      case INSTR_SETF_VAR_FLOAT:
        instr[ip].data.v_f.var->f = instr[ip].data.v_f.value;
        ++ip; break;

      case INSTR_SETF_VAR_VAR:
        instr[ip].data.v_v.var_dest->f = instr[ip].data.v_v.var_src->f;
        ++ip; break;

        /* SET.P */
      case INSTR_SETP_VAR_VAR:
        instr[ip].data.v_v.var_dest->ptr = instr[ip].data.v_v.var_src->ptr;
        ++ip; break;

      case INSTR_SETP_VAR_PTR:
        instr[ip].data.v_p.var->i = instr[ip].data.v_p.value;
        ++ip; break;

        /* JUMP */
      case INSTR_JUMP:
        ip += instr[ip].data.jump_offset; break;

        /* JZERO */
      case INSTR_JZERO:
        ip += (flag ? 1 : instr[ip].data.jump_offset); break;

      case INSTR_NOP:
        ++ip; break;

        /* ISEQUAL.P */
      case INSTR_ISEQUALP_VAR_VAR:
        flag = (instr[ip].data.v_v.var_dest->i == instr[ip].data.v_v.var_src->i);
        ++ip; break;

      case INSTR_ISEQUALP_VAR_PTR:
        flag = (instr[ip].data.v_p.var->i == instr[ip].data.v_p.value);
        ++ip; break;

        /* ISEQUAL.I */
      case INSTR_ISEQUALI_VAR_VAR:
        flag = (instr[ip].data.v_v.var_dest->i == instr[ip].data.v_v.var_src->i);
        ++ip; break;

      case INSTR_ISEQUALI_VAR_INTEGER:
        flag = (instr[ip].data.v_i.var->i == instr[ip].data.v_i.value);
        ++ip; break;

        /* ISEQUAL.F */
      case INSTR_ISEQUALF_VAR_VAR:
        flag = (instr[ip].data.v_v.var_dest->f == instr[ip].data.v_v.var_src->f);
        ++ip; break;

      case INSTR_ISEQUALF_VAR_FLOAT:
        flag = (instr[ip].data.v_f.var->f == instr[ip].data.v_f.value);
        ++ip; break;

        /* ISLOWER.I */
      case INSTR_ISLOWERI_VAR_VAR:
        flag = (instr[ip].data.v_v.var_dest->i < instr[ip].data.v_v.var_src->i);
        ++ip; break;

      case INSTR_ISLOWERI_VAR_INTEGER:
        flag = (instr[ip].data.v_i.var->i < instr[ip].data.v_i.value);
        ++ip; break;

        /* ISLOWER.F */
      case INSTR_ISLOWERF_VAR_VAR:
        flag = (instr[ip].data.v_v.var_dest->f < instr[ip].data.v_v.var_src->f);
        ++ip; break;

      case INSTR_ISLOWERF_VAR_FLOAT:
        flag = (instr[ip].data.v_f.var->f < instr[ip].data.v_f.value);
        ++ip; break;

        /* ADD.I */
      case INSTR_ADDI_VAR_VAR:
        instr[ip].data.v_v.var_dest->i += instr[ip].data.v_v.var_src->i;
        ++ip; break;

      case INSTR_ADDI_VAR_INTEGER:
        instr[ip].data.v_i.var->i += instr[ip].data.v_i.value;
        ++ip; break;

        /* ADD.F */
      case INSTR_ADDF_VAR_VAR:
        instr[ip].data.v_v.var_dest->f += instr[ip].data.v_v.var_src->f;
        ++ip; break;

      case INSTR_ADDF_VAR_FLOAT:
        instr[ip].data.v_f.var->f += instr[ip].data.v_f.value;
        ++ip; break;

        /* MUL.I */
      case INSTR_MULI_VAR_VAR:
        instr[ip].data.v_v.var_dest->i *= instr[ip].data.v_v.var_src->i;
        ++ip; break;

      case INSTR_MULI_VAR_INTEGER:
        instr[ip].data.v_i.var->i *= instr[ip].data.v_i.value;
        ++ip; break;

        /* MUL.F */
      case INSTR_MULF_VAR_FLOAT:
        instr[ip].data.v_f.var->f *= instr[ip].data.v_f.value;
        ++ip; break;

      case INSTR_MULF_VAR_VAR:
        instr[ip].data.v_v.var_dest->f *= instr[ip].data.v_v.var_src->f;
        ++ip; break;

        /* DIV.I */
      case INSTR_DIVI_VAR_VAR:
        instr[ip].data.v_v.var_dest->i /= instr[ip].data.v_v.var_src->i;
        ++ip; break;

      case INSTR_DIVI_VAR_INTEGER:
        instr[ip].data.v_i.var->i /= instr[ip].data.v_i.value;
        ++ip; break;

        /* DIV.F */
      case INSTR_DIVF_VAR_FLOAT:
        instr[ip].data.v_f.var->f /= instr[ip].data.v_f.value;
        ++ip; break;

      case INSTR_DIVF_VAR_VAR:
        instr[ip].data.v_v.var_dest->f /= instr[ip].data.v_v.var_src->f;
        ++ip; break;

        /* SUB.I */
      case INSTR_SUBI_VAR_VAR:
        instr[ip].data.v_v.var_dest->i -= instr[ip].data.v_v.var_src->i;
        ++ip; break;

      case INSTR_SUBI_VAR_INTEGER:
        instr[ip].data.v_i.var->i -= instr[ip].data.v_i.value;
        ++ip; break;

        /* SUB.F */
      case INSTR_SUBF_VAR_FLOAT:
        instr[ip].data.v_f.var->f -= instr[ip].data.v_f.value;
        ++ip; break;

      case INSTR_SUBF_VAR_VAR:
        instr[ip].data.v_v.var_dest->f -= instr[ip].data.v_v.var_src->f;
        ++ip; break;

        /* CALL */
      case INSTR_CALL:
        stack[stack_pointer++] = ip + 1;
        ip += instr[ip].data.jump_offset; break;

        /* RET */
      case INSTR_RET:
        ip = stack[--stack_pointer];
        if (ip<0) return;
        break;

        /* EXT_CALL */
      case INSTR_EXT_CALL:
        instr[ip].data.external_function->function(gsl, gsl->vars, instr[ip].data.external_function->vars);
        ++ip; break;

        /* NOT */
      case INSTR_NOT_VAR:
        flag = !flag;
        ++ip; break;

        /* JNZERO */
      case INSTR_JNZERO:
        ip += (flag ? instr[ip].data.jump_offset : 1); break;

      default:
        printf("NOT IMPLEMENTED : %d\n", instr[ip].id);
        ++ip;
        exit(1);
    }
  }
} /* }}} */

int gsl_malloc(GoomSL *_this, int size)
{ /* {{{ */
  if (_this->nbPtr >= _this->ptrArraySize) {
    _this->ptrArraySize *= 2;
    _this->ptrArray = (void**)realloc(_this->ptrArray, sizeof(void*) * _this->ptrArraySize);
  }
  _this->ptrArray[_this->nbPtr] = malloc(size);
  return _this->nbPtr++;
} /* }}} */

void *gsl_get_ptr(GoomSL *_this, int id)
{ /* {{{ */
  if ((id>=0)&&(id<_this->nbPtr))
    return _this->ptrArray[id];
  fprintf(stderr,"INVALID GET PTR %d\n", id);
  return NULL;
} /* }}} */

void gsl_free_ptr(GoomSL *_this, int id)
{ /* {{{ */
  if ((id>=0)&&(id<_this->nbPtr)) {
    free(_this->ptrArray[id]);
    _this->ptrArray[id] = 0;
  }
} /* }}} */

void gsl_enternamespace(const char *name)
{ /* {{{ */
  HashValue *val = goom_hash_get(currentGoomSL->functions, name);
  if (val) {
    ExternalFunctionStruct *function = (ExternalFunctionStruct*)val->ptr;
    currentGoomSL->currentNS++;
    currentGoomSL->namespaces[currentGoomSL->currentNS] = function->vars;
  }
  else {
    fprintf(stderr, "ERROR: Line %d, Could not find namespace: %s\n", currentGoomSL->num_lines, name);
    exit(1);
  }
} /* }}} */

void gsl_leavenamespace(void)
{ /* {{{ */
  currentGoomSL->currentNS--;
} /* }}} */

GoomHash *gsl_find_namespace(const char *name)
{ /* {{{ */
  int i;
  for (i=currentGoomSL->currentNS;i>=0;--i) {
    if (goom_hash_get(currentGoomSL->namespaces[i], name))
      return currentGoomSL->namespaces[i];
  }
  return NULL;
} /* }}} */

void gsl_declare_task(const char *name)
{ /* {{{ */
  if (goom_hash_get(currentGoomSL->functions, name)) {
    return;
  }
  else {
    ExternalFunctionStruct *gef = (ExternalFunctionStruct*)malloc(sizeof(ExternalFunctionStruct));
    gef->function = 0;
    gef->vars = goom_hash_new();
    gef->is_extern = 0;
    goom_hash_put_ptr(currentGoomSL->functions, name, (void*)gef);
  }
} /* }}} */

void gsl_declare_external_task(const char *name)
{ /* {{{ */
  if (goom_hash_get(currentGoomSL->functions, name)) {
    fprintf(stderr, "ERROR: Line %d, Duplicate declaration of %s\n", currentGoomSL->num_lines, name);
    return;
  }
  else {
    ExternalFunctionStruct *gef = (ExternalFunctionStruct*)malloc(sizeof(ExternalFunctionStruct));
    gef->function = 0;
    gef->vars = goom_hash_new();
    gef->is_extern = 1;
    goom_hash_put_ptr(currentGoomSL->functions, name, (void*)gef);
  }
} /* }}} */

static void reset_scanner(GoomSL *gss)
{ /* {{{ */
  gss->num_lines = 0;
  gss->instr = NULL;
  iflow_clean(gss->iflow);

  /* reset variables */
  goom_hash_free(gss->vars);
  gss->vars = goom_hash_new();
  gss->currentNS = 0;
  gss->namespaces[0] = gss->vars;

  gss->compilationOK = 1;
} /* }}} */

static void calculate_labels(InstructionFlow *iflow)
{ /* {{{ */
  int i = 0;
  while (i < iflow->number) {
    Instruction *instr = iflow->instr[i];
    if (instr->jump_label) {
      HashValue *label = goom_hash_get(iflow->labels,instr->jump_label);
      if (label) {
        instr->data.jump_offset = -instr->address + label->i;
      }
      else {
        fprintf(stderr, "ERROR: Line %d, Could not find label %s\n", instr->line_number, instr->jump_label);
        instr->id = INSTR_NOP;
        instr->nop_label = 0;
        exit(1);
      }
    }
    ++i;
  }
} /* }}} */

/* Cree un flow d'instruction optimise */
static void gsl_create_fast_iflow(void)
{ /* {{{ */
  int number = currentGoomSL->iflow->number;
  int i;
#ifdef USE_JITC_X86
  JitcX86Env *jitc;
  if (currentGoomSL->jitc != NULL)
    jitc_x86_delete(currentGoomSL->jitc);
  jitc = currentGoomSL->jitc = jitc_x86_env_new(0xffff);
  currentGoomSL->jitc_func = jitc_prepare_func(jitc);

  JITC_JUMP_LABEL(jitc, "__very_end__");
  JITC_ADD_LABEL (jitc, "__very_start__");
  
  for (i=0;i<number;++i) {
    Instruction *instr = currentGoomSL->iflow->instr[i];
    switch (instr->id) {
      case INSTR_SETI_VAR_INTEGER     :
        JITC_LOAD_REG_IMM32(jitc, EAX, instr->data.v_i.value);          /* eax  = value */
        JITC_LOAD_REG_IMM32(jitc, EBX, &instr->data.v_i.var->i);        /* ebx  = &dest */
        JITC_LOAD_pREG_REG (jitc, EBX, EAX);                            /* *ebx = eax   */
        break;
      case INSTR_SETI_VAR_VAR         :
        JITC_LOAD_REG_IMM32(jitc, EAX, &(instr->data.v_v.var_src->i));  /* eax  = &src  */
        JITC_LOAD_REG_IMM32(jitc, EBX, &(instr->data.v_v.var_dest->i)); /* ebx  = &dest */
        JITC_LOAD_REG_pREG (jitc, EAX, EAX);                            /* eax  = *eax  */
        JITC_LOAD_pREG_REG (jitc, EBX, EAX);                            /* *ebx = eax   */
        break;
        /* SET.F */
      case INSTR_SETF_VAR_FLOAT       :
        JITC_LOAD_REG_IMM32(jitc, EAX, *(int*)&(instr->data.v_f.value)); /* eax  = value */
        JITC_LOAD_REG_IMM32(jitc, EBX, &(instr->data.v_f.var->f));       /* ebx  = &dest */
        JITC_LOAD_pREG_REG (jitc, EBX, EAX);                             /* *ebx = eax   */
        break;
      case INSTR_SETF_VAR_VAR         :
        JITC_LOAD_REG_IMM32(jitc, EAX, &(instr->data.v_v.var_src->f));  /* eax  = &src  */
        JITC_LOAD_REG_IMM32(jitc, EBX, &(instr->data.v_v.var_dest->f)); /* ebx  = &dest */
        JITC_LOAD_REG_pREG (jitc, EAX, EAX);                            /* eax  = *eax  */
        JITC_LOAD_pREG_REG (jitc, EBX, EAX);                            /* *ebx = eax   */
        break;
      case INSTR_NOP                  :
        if (instr->nop_label != 0)
          JITC_ADD_LABEL(jitc, instr->nop_label);
        break;
      case INSTR_JUMP                 :
        JITC_JUMP_LABEL(jitc,instr->jump_label);
        break;
      case INSTR_SETP_VAR_PTR         :
        JITC_LOAD_REG_IMM32(jitc, EAX, instr->data.v_p.value);           /* eax  = value */
        JITC_LOAD_REG_IMM32(jitc, EBX, &(instr->data.v_p.var->ptr));     /* ebx  = &dest */
        JITC_LOAD_pREG_REG (jitc, EBX, EAX);                             /* *ebx = eax   */
        break;
      case INSTR_SETP_VAR_VAR         :
        JITC_LOAD_REG_IMM32(jitc, EAX, &(instr->data.v_v.var_src->ptr));  /* eax  = &src  */
        JITC_LOAD_REG_IMM32(jitc, EBX, &(instr->data.v_v.var_dest->ptr)); /* ebx  = &dest */
        JITC_LOAD_REG_pREG (jitc, EAX, EAX);                              /* eax  = *eax  */
        JITC_LOAD_pREG_REG (jitc, EBX, EAX);                              /* *ebx = eax   */
        break;
      case INSTR_SUBI_VAR_INTEGER     :
        JITC_LOAD_REG_IMM32(jitc, EBX, &(instr->data.v_i.var->i));      /* ebx = &var    */
        JITC_LOAD_REG_pREG (jitc, EAX, EBX);                            /* eax = *ebx    */
        JITC_SUB_REG_IMM32(jitc,  EAX, instr->data.v_i.value);          /* eax -= value  */
        JITC_LOAD_pREG_REG (jitc, EBX, EAX);                            /* *ebx = eax    */
        break;
      case INSTR_SUBI_VAR_VAR         :
        JITC_LOAD_REG_IMM32(jitc, ECX, &(instr->data.v_v.var_dest->i)); /* ecx  = &dest */
        JITC_LOAD_REG_IMM32(jitc, EBX, &(instr->data.v_v.var_src->i));  /* ebx  = &src  */
        JITC_LOAD_REG_pREG (jitc, EAX, ECX);                            /* eax  = *ecx  */
        JITC_LOAD_REG_pREG (jitc, EBX, EBX);                            /* ebx  = *ebx  */
        JITC_SUB_REG_REG   (jitc, EAX, EBX);                            /* eax -=  ebx  */
        JITC_LOAD_pREG_REG (jitc, ECX, EAX);                            /* *ecx =  eax  */
        break;
      case INSTR_SUBF_VAR_FLOAT       :
        printf("NOT IMPLEMENTED : %d\n", instr->id);
        break;
      case INSTR_SUBF_VAR_VAR         :
        printf("NOT IMPLEMENTED : %d\n", instr->id);
        break;
      case INSTR_ISLOWERF_VAR_VAR     :
        printf("NOT IMPLEMENTED : %d\n", instr->id);
        break;
      case INSTR_ISLOWERF_VAR_FLOAT   :
        printf("NOT IMPLEMENTED : %d\n", instr->id);
        break;
      case INSTR_ISLOWERI_VAR_VAR     :
        JITC_LOAD_REG_IMM32(jitc, EAX, &(instr->data.v_v.var_dest->i));
        JITC_LOAD_REG_IMM32(jitc, EBX, &(instr->data.v_v.var_src->i));
        JITC_LOAD_REG_pREG (jitc, EAX, EAX);
        JITC_LOAD_REG_pREG (jitc, EBX, EBX);
        JITC_LOAD_REG_IMM32(jitc, EDX, 0);
        JITC_CMP_REG_REG   (jitc, EAX, EBX);
        JITC_JUMP_COND     (jitc, COND_NOT_BELOW, 1);
        JITC_INC_REG       (jitc, EDX);
        break;
      case INSTR_ISLOWERI_VAR_INTEGER :
        JITC_LOAD_REG_IMM32(jitc, EAX, &(instr->data.v_i.var->i));
        JITC_LOAD_REG_IMM32(jitc, EBX, instr->data.v_i.value);
        JITC_LOAD_REG_pREG (jitc, EAX, EAX);
        JITC_LOAD_REG_IMM32(jitc, EDX, 0);
        JITC_CMP_REG_REG   (jitc, EAX, EBX);
        JITC_JUMP_COND     (jitc, COND_NOT_BELOW, 1);
        JITC_INC_REG       (jitc, EDX);
        break;
      case INSTR_ADDI_VAR_INTEGER     :
        JITC_LOAD_REG_IMM32(jitc, EBX, &(instr->data.v_i.var->i));      /* ebx = &var    */
        JITC_LOAD_REG_pREG (jitc, EAX, EBX);                            /* eax = *ebx    */
        JITC_ADD_REG_IMM32(jitc,  EAX, instr->data.v_i.value);          /* eax += value  */
        JITC_LOAD_pREG_REG (jitc, EBX, EAX);                            /* *ebx = eax    */
        break;
      case INSTR_ADDI_VAR_VAR         :
        JITC_LOAD_REG_IMM32(jitc, ECX, &(instr->data.v_v.var_dest->i)); /* ecx  = &dest */
        JITC_LOAD_REG_IMM32(jitc, EBX, &(instr->data.v_v.var_src->i));  /* ebx  = &src  */
        JITC_LOAD_REG_pREG (jitc, EAX, ECX);                            /* eax  = *ecx  */
        JITC_LOAD_REG_pREG (jitc, EBX, EBX);                            /* ebx  = *ebx  */
        JITC_ADD_REG_REG   (jitc, EAX, EBX);                            /* eax = eax + ebx */
        JITC_LOAD_pREG_REG (jitc, ECX, EAX);                            /* *ecx = eax   */
        break;
      case INSTR_ADDF_VAR_FLOAT       :
        printf("NOT IMPLEMENTED : %d\n", instr->id);
        break;
      case INSTR_ADDF_VAR_VAR         :
        printf("NOT IMPLEMENTED : %d\n", instr->id);
        break;
      case INSTR_MULI_VAR_INTEGER     :
        JITC_LOAD_REG_IMM32(jitc, EBX, &(instr->data.v_i.var->i));
        JITC_LOAD_REG_IMM32(jitc, ECX, instr->data.v_i.value);
        JITC_LOAD_REG_pREG (jitc, EAX, EBX);                      
        JITC_IMUL_EAX_REG  (jitc, ECX);                      
        JITC_LOAD_pREG_REG (jitc, EBX, EAX);                      
        break;
      case INSTR_MULI_VAR_VAR         :
        JITC_LOAD_REG_IMM32(jitc, ECX, &(instr->data.v_v.var_dest->i)); /* ecx  = &dest */
        JITC_LOAD_REG_IMM32(jitc, EBX, &(instr->data.v_v.var_src->i));  /* ebx  = &src  */
        JITC_LOAD_REG_pREG (jitc, EAX, ECX);                            /* eax  = *ecx  */
        JITC_LOAD_REG_pREG (jitc, EBX, EBX);                            /* ebx  = *ebx  */
        JITC_IMUL_EAX_REG  (jitc, EBX);                                 /* eax = eax * ebx */
        JITC_LOAD_pREG_REG (jitc, ECX, EAX);                            /* *ecx = eax   */
        break;
      case INSTR_MULF_VAR_FLOAT       :
        printf("NOT IMPLEMENTED : %d\n", instr->id);
        break;
      case INSTR_MULF_VAR_VAR         :
        printf("NOT IMPLEMENTED : %d\n", instr->id);
        break;
      case INSTR_DIVI_VAR_INTEGER     :
        JITC_LOAD_REG_IMM32(jitc, EBX, &(instr->data.v_i.var->i));
        JITC_LOAD_REG_IMM32(jitc, ECX, instr->data.v_i.value);
        JITC_LOAD_REG_pREG (jitc, EAX, EBX);                      
        JITC_IDIV_EAX_REG  (jitc, ECX);                      
        JITC_LOAD_pREG_REG (jitc, EBX, EAX);                      
        break;
      case INSTR_DIVI_VAR_VAR         :
        JITC_LOAD_REG_IMM32(jitc, ECX, &(instr->data.v_v.var_dest->i)); /* ecx  = &dest */
        JITC_LOAD_REG_IMM32(jitc, EBX, &(instr->data.v_v.var_src->i));  /* ebx  = &src  */
        JITC_LOAD_REG_pREG (jitc, EAX, ECX);                            /* eax  = *ecx  */
        JITC_LOAD_REG_pREG (jitc, EBX, EBX);                            /* ebx  = *ebx  */
        JITC_IDIV_EAX_REG  (jitc, EBX);                                 /* eax = eax * ebx */
        JITC_LOAD_pREG_REG (jitc, ECX, EAX);                            /* *ecx = eax   */
        break;
      case INSTR_DIVF_VAR_FLOAT       :
        printf("NOT IMPLEMENTED : %d\n", instr->id);
        break;
      case INSTR_DIVF_VAR_VAR         :
        printf("NOT IMPLEMENTED : %d\n", instr->id);
        break;
      case INSTR_JZERO                :
        JITC_CMP_REG_IMM32(jitc,EDX,1);
        JITC_JUMP_COND_LABEL(jitc,COND_NOT_EQUAL,instr->jump_label);
        break;
      case INSTR_ISEQUALP_VAR_VAR     :
        JITC_LOAD_REG_IMM32(jitc, EAX, &(instr->data.v_v.var_dest->ptr)); /* eax  = &dest */
        JITC_LOAD_REG_IMM32(jitc, EBX, &(instr->data.v_v.var_src->ptr));  /* ebx  = &src  */
        JITC_LOAD_REG_pREG (jitc, EAX, EAX);
        JITC_LOAD_REG_pREG (jitc, EBX, EBX);
        JITC_LOAD_REG_IMM32(jitc, EDX, 0);
        JITC_CMP_REG_REG   (jitc, EAX, EBX);
        JITC_JUMP_COND     (jitc, COND_NOT_EQUAL, 1);
        JITC_INC_REG       (jitc, EDX);
        break;
      case INSTR_ISEQUALP_VAR_PTR     :
        JITC_LOAD_REG_IMM32(jitc, EAX, &(instr->data.v_p.var->ptr)); /* eax  = &dest */
        JITC_LOAD_REG_IMM32(jitc, EDX, 0);
        JITC_LOAD_REG_pREG (jitc, EAX, EAX);
        JITC_CMP_REG_IMM32 (jitc, EAX, instr->data.v_p.value);
        JITC_JUMP_COND     (jitc, COND_NOT_EQUAL, 1);
        JITC_INC_REG       (jitc, EDX);
        break;
      case INSTR_ISEQUALI_VAR_VAR     :
        JITC_LOAD_REG_IMM32(jitc, EAX, &(instr->data.v_v.var_dest->i)); /* eax  = &dest */
        JITC_LOAD_REG_IMM32(jitc, EBX, &(instr->data.v_v.var_src->i));  /* ebx  = &src  */
        JITC_LOAD_REG_pREG (jitc, EAX, EAX);
        JITC_LOAD_REG_pREG (jitc, EBX, EBX);
        JITC_LOAD_REG_IMM32(jitc, EDX, 0);
        JITC_CMP_REG_REG   (jitc, EAX, EBX);
        JITC_JUMP_COND     (jitc, COND_NOT_EQUAL, 1);
        JITC_INC_REG       (jitc, EDX);
        break;
      case INSTR_ISEQUALI_VAR_INTEGER :
        JITC_LOAD_REG_IMM32(jitc, EAX, &(instr->data.v_i.var->i)); /* eax  = &dest */
        JITC_LOAD_REG_IMM32(jitc, EDX, 0);
        JITC_LOAD_REG_pREG (jitc, EAX, EAX);
        JITC_CMP_REG_IMM32 (jitc, EAX, instr->data.v_i.value);
        JITC_JUMP_COND     (jitc, COND_NOT_EQUAL, 1);
        JITC_INC_REG       (jitc, EDX);
        break;
      case INSTR_ISEQUALF_VAR_VAR     :
        JITC_LOAD_REG_IMM32(jitc, EAX, &(instr->data.v_v.var_dest->f)); /* eax  = &dest */
        JITC_LOAD_REG_IMM32(jitc, EBX, &(instr->data.v_v.var_src->f));  /* ebx  = &src  */
        JITC_LOAD_REG_pREG (jitc, EAX, EAX);
        JITC_LOAD_REG_pREG (jitc, EBX, EBX);
        JITC_LOAD_REG_IMM32(jitc, EDX, 0);
        JITC_CMP_REG_REG   (jitc, EAX, EBX);
        JITC_JUMP_COND     (jitc, COND_NOT_EQUAL, 1);
        JITC_INC_REG       (jitc, EDX);
        break;
      case INSTR_ISEQUALF_VAR_FLOAT   :
        JITC_LOAD_REG_IMM32(jitc, EAX, &(instr->data.v_f.var->f)); /* eax  = &dest */
        JITC_LOAD_REG_IMM32(jitc, EDX, 0);
        JITC_LOAD_REG_pREG (jitc, EAX, EAX);
        JITC_CMP_REG_IMM32 (jitc, EAX, *(int*)(&instr->data.v_f.value));
        JITC_JUMP_COND     (jitc, COND_NOT_EQUAL, 1);
        JITC_INC_REG       (jitc, EDX);
        break;
      case INSTR_CALL                 :
        JITC_CALL_LABEL(jitc, instr->jump_label);
        break;
      case INSTR_RET                  :
        JITC_RETURN_FUNCTION(jitc);
        break;
      case INSTR_EXT_CALL             :
        JITC_LOAD_REG_IMM32(jitc, EAX, &(instr->data.external_function->vars));
        JITC_LOAD_REG_pREG(jitc,EAX,EAX);
        JITC_PUSH_REG(jitc,EAX);
        
        JITC_LOAD_REG_IMM32(jitc, EAX, &(currentGoomSL->vars));
        JITC_LOAD_REG_pREG(jitc,EAX,EAX);
        JITC_PUSH_REG(jitc,EAX);

        JITC_LOAD_REG_IMM32(jitc, EAX, &(currentGoomSL));
        JITC_LOAD_REG_pREG(jitc,EAX,EAX);
        JITC_PUSH_REG(jitc,EAX);

        JITC_LOAD_REG_IMM32(jitc,EAX,&(instr->data.external_function));
        JITC_LOAD_REG_pREG(jitc,EAX,EAX);
        JITC_LOAD_REG_pREG(jitc,EAX,EAX);

        JITC_CALL_pREG(jitc,EAX);

        JITC_POP_REG(jitc,EAX);
        JITC_POP_REG(jitc,EAX);
        JITC_POP_REG(jitc,EAX);
        break;
      case INSTR_NOT_VAR              :
        JITC_LOAD_REG_REG(jitc,EAX,EDX);
        JITC_LOAD_REG_IMM32(jitc,EDX,1);
        JITC_SUB_REG_REG(jitc,EDX,EAX);
        break;
      case INSTR_JNZERO               :
        JITC_CMP_REG_IMM32(jitc,EDX,1);
        JITC_JUMP_COND_LABEL(jitc,COND_EQUAL,instr->jump_label);
        break;
    }
  }

  JITC_ADD_LABEL (jitc, "__very_end__");
  JITC_CALL_LABEL(jitc, "__very_start__");
  JITC_LOAD_REG_IMM32(jitc, EAX, 0);
  jitc_validate_func(jitc);
#else
  InstructionFlow     *iflow     = currentGoomSL->iflow;
  FastInstructionFlow *fastiflow = (FastInstructionFlow*)malloc(sizeof(FastInstructionFlow));
  fastiflow->mallocedInstr = calloc(number*16, sizeof(FastInstruction));
  /* fastiflow->instr = (FastInstruction*)(((int)fastiflow->mallocedInstr) + 16 - (((int)fastiflow->mallocedInstr)%16)); */
  fastiflow->instr = (FastInstruction*)fastiflow->mallocedInstr;
  fastiflow->number = number;
  for(i=0;i<number;++i) {
    fastiflow->instr[i].id    = iflow->instr[i]->id;
    fastiflow->instr[i].data  = iflow->instr[i]->data;
    fastiflow->instr[i].proto = iflow->instr[i];
  }
  currentGoomSL->fastiflow = fastiflow;
#endif
} /* }}} */

void yy_scan_string(const char *str);
void yyparse(void);

void gsl_compile(GoomSL *_currentGoomSL, const char *script)
{ /* {{{ */
#ifdef VERBOSE
  printf("\n=== Starting Compilation ===\n");
#endif

  /* 0- reset */
  currentGoomSL = _currentGoomSL;
  reset_scanner(currentGoomSL);

  /* 1- create the syntaxic tree */
  yy_scan_string(script);
  yyparse();

  /* 2- generate code */
  gsl_commit_compilation();

  /* 3- resolve symbols */
  calculate_labels(currentGoomSL->iflow);

  /* 4- optimize code */
  gsl_create_fast_iflow();

#ifdef VERBOSE
  printf("=== Compilation done. # of lines: %d. # of instr: %d ===\n", currentGoomSL->num_lines, currentGoomSL->iflow->number);
#endif
} /* }}} */

void gsl_execute(GoomSL *scanner)
{ /* {{{ */
  if (scanner->compilationOK) {
#if USE_JITC_X86
    scanner->jitc_func();
#else
    iflow_execute(scanner->fastiflow, scanner);
#endif
  }
} /* }}} */

GoomSL *gsl_new(void)
{ /* {{{ */
  GoomSL *gss = (GoomSL*)malloc(sizeof(GoomSL));

  gss->iflow = iflow_new();
  gss->vars  = goom_hash_new();
  gss->functions = goom_hash_new();
  gss->currentNS = 0;
  gss->namespaces[0] = gss->vars;
  reset_scanner(gss);
  gss->compilationOK = 0;
  gss->nbPtr=0;
  gss->ptrArraySize=256;
  gss->ptrArray = (void**)malloc(gss->ptrArraySize * sizeof(void*));
#ifdef USE_JITC_X86
  gss->jitc = NULL;
#endif
  return gss;
} /* }}} */

void gsl_bind_function(GoomSL *gss, const char *fname, GoomSL_ExternalFunction func)
{ /* {{{ */
  HashValue *val = goom_hash_get(gss->functions, fname);
  if (val) {
    ExternalFunctionStruct *gef = (ExternalFunctionStruct*)val->ptr;
    gef->function = func;
  }
  else fprintf(stderr, "Unable to bind function %s\n", fname);
} /* }}} */

int gsl_is_compiled(GoomSL *gss)
{ /* {{{ */
  return gss->compilationOK;
} /* }}} */

void gsl_free(GoomSL *gss)
{ /* {{{ */
  iflow_free(gss->iflow);
  free(gss->vars);
  free(gss->functions);
  free(gss);
} /* }}} */


static int gsl_nb_import;
static char gsl_already_imported[256][256];

char *gsl_init_buffer(const char *fname)
{
    char *fbuffer;
    fbuffer = (char*)malloc(512);
    fbuffer[0]=0;
    gsl_nb_import = 0;
    if (fname)
      gsl_append_file_to_buffer(fname,&fbuffer);
    return fbuffer;
}

static char *gsl_read_file(const char *fname)
{
  FILE *f;
  char *buffer;
  int fsize;
  f = fopen(fname,"rt");
  if (!f) {
    fprintf(stderr, "ERROR: Could not load file %s\n", fname);
    exit(1);
  }
  fseek(f,0,SEEK_END);
  fsize = ftell(f);
  rewind(f);
  buffer = (char*)malloc(fsize+512);
  fread(buffer,1,fsize,f);
  fclose(f);
  buffer[fsize]=0;
  return buffer;
}

void gsl_append_file_to_buffer(const char *fname, char **buffer)
{
    char *fbuffer;
    int size,fsize,i=0;
    char reset_msg[256];
    
    /* look if the file have not been already imported */
    for (i=0;i<gsl_nb_import;++i) {
      if (strcmp(gsl_already_imported[i], fname) == 0)
        return;
    }
    
    /* add fname to the already imported files. */
    strcpy(gsl_already_imported[gsl_nb_import++], fname);

    /* load the file */
    fbuffer = gsl_read_file(fname);
    fsize = strlen(fbuffer);
        
    /* look for #import */
    while (fbuffer[i]) {
      if ((fbuffer[i]=='#') && (fbuffer[i+1]=='i')) {
        char impName[256];
        int j;
        while (fbuffer[i] && (fbuffer[i]!=' '))
          i++;
        i++;
        j=0;
        while (fbuffer[i] && (fbuffer[i]!='\n'))
          impName[j++] = fbuffer[i++];
        impName[j++] = 0;
        gsl_append_file_to_buffer(impName, buffer);
      }
      i++;
    }
    
    sprintf(reset_msg, "\n#FILE %s#\n#RST_LINE#\n", fname);
    strcat(*buffer, reset_msg);
    size=strlen(*buffer);
    *buffer = (char*)realloc(*buffer, size+fsize+256);
    strcat((*buffer)+size, fbuffer);
    free(fbuffer);
}


