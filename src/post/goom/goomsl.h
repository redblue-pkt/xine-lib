#ifndef _GOOMSL_H
#define _GOOMSL_H

#include "goomsl_hash.h"

typedef struct _GoomSL GoomSL;
typedef void (*GoomSL_ExternalFunction)(GoomSL *gsl, GoomHash *global_vars, GoomHash *local_vars);

GoomSL*gsl_new(void);
void   gsl_free(GoomSL *gss);

char *gsl_init_buffer(const char *file_name);
void  gsl_append_file_to_buffer(const char *file_name, char **buffer);

void   gsl_compile (GoomSL *scanner, const char *script);
void   gsl_execute (GoomSL *scanner);
int    gsl_is_compiled  (GoomSL *gss);
void   gsl_bind_function(GoomSL *gss, const char *fname, GoomSL_ExternalFunction func);

int    gsl_malloc  (GoomSL *_this, int size);
void  *gsl_get_ptr (GoomSL *_this, int id);
void   gsl_free_ptr(GoomSL *_this, int id);

#define gsl_local_ptr(gsl,local,name)   gsl_get_ptr(gsl, goom_hash_get(local,name)->i)
#define gsl_local_int(gsl,local,name)   goom_hash_get(local,name)->i
#define gsl_local_float(gsl,local,name) goom_hash_get(local,name)->i

#define gsl_global_ptr(gsl,global,name)   gsl_get_ptr(gsl, goom_hash_get(global,name)->i)
#define gsl_global_int(gsl,global,name)   goom_hash_get(global,name)->i
#define gsl_global_float(gsl,global,name) goom_hash_get(global,name)->i

#endif
