#ifndef _GOOM_SCRIPT_H
#define _GOOM_SCRIPT_H

#include "goom_plugin_info.h"

void goom_execute_script(PluginInfo *pluginInfo, const char *cmds);

/* set a script that will be executed every loop */
void goom_set_main_script(PluginInfo *pluginInfo, const char *script);
void goom_execute_main_script(PluginInfo *pluginInfo);

    /* return a script containing the current goom state */
char *goom_create_state_script(PluginInfo *pluginInfo);

PluginParam *goom_script_get_param(PluginInfo *pluginInfo, const char *name);

#endif /* _GOOM_SCRIPT_H */
