typedef union {
    int intValue;
    float floatValue;
    char charValue;
    char strValue[2048];
    NodeType *nPtr;
  } YYSTYPE;
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


extern YYSTYPE yylval;
