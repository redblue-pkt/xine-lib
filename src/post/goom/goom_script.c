#include "goom_script.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static PluginParameters *getBaseForVariable (PluginInfo *pluginInfo, const char *name) {

	int i;
	if (name == NULL) {
		printf("ERROR: No variable container name specified\n");
		return NULL;
	}

	/* TODO: using an hashmap */
	for (i=0;i<pluginInfo->nbParams;i++) {
		if (!strcmp(name, pluginInfo->params[i].name)) {
			return &(pluginInfo->params[i]);
		}
	}

	printf ("ERROR: No such variable container: %s\n", name);
	return NULL;
}

static PluginParam *getParamForVariable(PluginParameters *params, const char *name) {

	int i;
	if (name == NULL)
		return NULL;

	for (i=0;i<params->nbParams;i++) {
		if (params->params[i] && (!strcmp(name, params->params[i]->name))) {
			return params->params[i];
		}
	}
	printf ("ERROR: No such variable into %s: %s\n", params->name, name);
	return NULL;
}
					    
PluginParam *goom_script_get_param(PluginInfo *pluginInfo, const char *name) {

	int i;
	char *base;
	char *var;
	int len = strlen(name);
	int hasDot = 0;
	PluginParameters *pparams;

	if (name == NULL)
		return NULL;
	if (pluginInfo == NULL) {
		printf("ERROR: programming %s on line %d\n", __FILE__, __LINE__);
		return NULL;
	}
	
	base = (char*)calloc(len+1,1);
	var = (char*)calloc(len+1,1);

	for (i=0;i<len;i++) {
		
		char c = name[i];
		if (c == '_') {
			c=' ';
		}
		if (c == '.') {
			if (i==0)
				return NULL;
			hasDot = i;
		}
		else if (hasDot)
			var[i-hasDot-1] = c;
		else
			base[i] = c;
	}
	if ((hasDot==0)||(var[0]==0)||(base[0]==0))
		return NULL;

	pparams = getBaseForVariable(pluginInfo,base);
	if (pparams==NULL)
		return NULL;
	return getParamForVariable(pparams, var);
}

void goom_execute_script(PluginInfo *pluginInfo, const char *cmds) {

    goom_script_scanner_compile(pluginInfo->scanner, pluginInfo, cmds);
    goom_script_scanner_execute(pluginInfo->scanner);
}

/* set a script that will be executed every loop */
void goom_set_main_script(PluginInfo *pluginInfo, const char *script) {

    pluginInfo->main_script_str = script;
    goom_script_scanner_compile(pluginInfo->main_scanner, pluginInfo, script);
}

void goom_execute_main_script(PluginInfo *pluginInfo) {
    
    goom_script_scanner_execute(pluginInfo->main_scanner);
}

/* return a script containing the current goom state */
char *goom_create_state_script(PluginInfo *pluginInfo) {
	printf("not implemented\n");
	return NULL;
}

