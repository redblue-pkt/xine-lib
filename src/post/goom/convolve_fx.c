#include "goom_fx.h"
#include "goom_plugin_info.h"
#include "goomsl.h"
#include "goom_config.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char DEF_SCRIPT[] = "\n\n";
#if 0
"-> config;\n"
"-> main;\n"
"\n"
"<config>\n"
"  float INCREASE_RATE = 150%;\n"
"  float DECAY_RATE = 96%;\n"
"\n"
"<main>\n"
"  (Sound.Goom_Detection > 0.8) ?\n"
"      Bright_Flash.Factor = Bright_Flash.Factor + Sound.Goom_Power * INCREASE_RATE;\n"
"\n"
"  Bright_Flash.Factor = Bright_Flash.Factor * DECAY_RATE;\n"
"\n";
#endif

#define MAX 2.0f

typedef struct _CONV_DATA{
/*	float factor;
	int lock;
	int started;
	float curPower;*/
	PluginParam light;
	PluginParam factor_adj_p;
	PluginParam factor_p;
    PluginParam script_p;
    PluginParam compile_p;
	PluginParameters params;

    GoomSL *script;
    
} ConvData;

static void convolve_init(VisualFX *_this) {
	ConvData *data;
	data = (ConvData*)malloc(sizeof(ConvData));

	data->light = secure_f_param("Screen Brightness");
	data->light.param.fval.max = 300.0f;
	data->light.param.fval.step = 1.0f;
	data->light.param.fval.value = 100.0f;

	data->factor_adj_p = secure_f_param("Flash Intensity");
	data->factor_adj_p.param.fval.max = 100.0f;
	data->factor_adj_p.param.fval.step = 1.0f;
	data->factor_adj_p.param.fval.value = 50.0f;

	data->factor_p = secure_f_feedback("Factor");
/*	FVAL(data->factor_p) = data->factor / MAX;*/

    data->script_p = secure_s_param("Script");
    set_str_param_value(&data->script_p, DEF_SCRIPT);
    data->compile_p = secure_b_param("Compile", 0);

	data->params = plugin_parameters ("Bright Flash", 7);
	data->params.params[0] = &data->light;
	data->params.params[1] = &data->factor_adj_p;
	data->params.params[2] = 0;
	data->params.params[3] = &data->factor_p;
	data->params.params[4] = 0;
	data->params.params[5] = &data->script_p;
	data->params.params[6] = &data->compile_p;

    data->script = gsl_new();

	_this->params = &data->params;
	_this->fx_data = (void*)data;
}

static void convolve_free(VisualFX *_this) {
	free (_this->fx_data);
}

void create_output_with_brightness(Pixel *src, Pixel *dest, int screensize, int iff) {

	int i;
	
	if (iff-256 == 0) {
		memcpy(dest,src,screensize*sizeof(Pixel));
		return;
	}
	for (i=screensize-1;i--;) {
		unsigned int f,h,m,n;

		f = (src[i].cop[0] * iff) >> 8;
		h = (src[i].cop[1] * iff) >> 8;
		m = (src[i].cop[2] * iff) >> 8;
		n = (src[i].cop[3] * iff) >> 8;

		dest[i].cop[0] = (f & 0xffffff00) ? 0xff : (unsigned char)f;
		dest[i].cop[1] = (h & 0xffffff00) ? 0xff : (unsigned char)h;
		dest[i].cop[2] = (m & 0xffffff00) ? 0xff : (unsigned char)m;
		dest[i].cop[3] = (n & 0xffffff00) ? 0xff : (unsigned char)n;
	}
}

static void convolve_apply(VisualFX *_this, Pixel *src, Pixel *dest, PluginInfo *info) {

	ConvData *data = (ConvData*)_this->fx_data;
	float ff;
	int iff;

	ff = (FVAL(data->factor_p) * FVAL(data->factor_adj_p) + FVAL(data->light) ) / 100.0f;
	iff = (unsigned int)(ff * 256);

    if (!gsl_is_compiled(data->script)) {
#ifdef VERBOSE
        printf("setting default script for dynamic brightness\n");
#endif
        gsl_compile(data->script, DEF_SCRIPT);
    }
    
    if (BVAL(data->compile_p)) { /* le bouton du pauvre ... */
        gsl_compile(data->script, SVAL(data->script_p));
        BVAL(data->compile_p) = 0;
        data->compile_p.change_listener(&data->compile_p);
    }

    if (gsl_is_compiled(data->script)) {
        gsl_execute(data->script);
    }

	info->methods.create_output_with_brightness(src,dest,info->screen.size,iff);
}

VisualFX convolve_create(void) {
	VisualFX vfx = {
		init: convolve_init,
		free: convolve_free,
		apply: convolve_apply,
		fx_data: 0
	};
	return vfx;
}
