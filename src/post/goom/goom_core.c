#include <stdlib.h>
#include <string.h>
#include "goom_core.h"
#include "goom_tools.h"
#include "filters.h"
#include "lines.h"
#include "ifs.h"

//#define VERBOSE

#ifdef VERBOSE
#include <stdio.h>
#endif

#define STOP_SPEED 128

#define TIME_BTW_CHG 300

/**-----------------------------------------------------**
 **  SHARED DATA                                        **
 **-----------------------------------------------------**/
static guint32 *pixel ;
static guint32 *back ;
static guint32 *p1,*p2,*tmp;
static guint32 cycle;

guint32 resolx, resoly, buffsize,
  c_black_height=0, /* hauteur des bande noires en bas et en haut */
  c_offset=0, c_resoly=0; /* avec prise en compte de ca */

// effet de ligne..
static GMLine *gmline1 = NULL;
static GMLine *gmline2 = NULL;

void choose_a_goom_line (float *param1, float *param2, int *couleur,
						 int *mode);

void goom_init (guint32 resx, guint32 resy, int cinemascope)
{
#ifdef VERBOSE
  printf ("GOOM: init (%d, %d);\n", resx,resy);
#endif
	if (cinemascope)
	  c_black_height = resy/5;
	else
	  c_black_height = 0;

    resolx = resx;
    resoly = resy;
    buffsize = resx * resy;

	c_offset = c_black_height * resx;
	c_resoly = resy - c_black_height * 2;

    pixel = (guint32 *) malloc (buffsize * sizeof(guint32) + 128);
    back = (guint32 *) malloc (buffsize * sizeof(guint32) + 128);
    RAND_INIT ((guint32)pixel) ;
    cycle = 0 ;

    p1 = (guint32 *)((1+((unsigned int)(pixel))/128)*128);
    p2 = (guint32 *)((1+((unsigned int)(back))/128)*128);

    init_ifs (resx,c_resoly);
	gmline1 = goom_lines_init (resx,c_resoly,
							  GML_HLINE, c_resoly, GML_BLACK,
							  GML_CIRCLE, 0.4f*(float)c_resoly, GML_VERT);
	gmline2 = goom_lines_init (resx,c_resoly,
							  GML_HLINE, 0, GML_BLACK,
							  GML_CIRCLE, 0.2f*(float)c_resoly, GML_RED);
}


void goom_set_resolution (guint32 resx, guint32 resy, int cinemascope)
{
    free (pixel);
    free (back);

	if (cinemascope)
	  c_black_height = resy/8;
	else
	  c_black_height = 0;

	c_offset = c_black_height * resx;
	c_resoly = resy - c_black_height * 2;

    resolx = resx;
    resoly = resy;
    buffsize = resx * resy;

    pixel = (guint32 *) malloc (buffsize * sizeof(guint32) + 128);
    bzero(pixel,buffsize * sizeof(guint32) + 128);
    back = (guint32 *) malloc (buffsize * sizeof(guint32) + 128);
    bzero(back,buffsize * sizeof(guint32) + 128);
    p1 = (guint32 *)((1+((unsigned int)(pixel))/128)*128);
    p2 = (guint32 *)((1+((unsigned int)(back))/128)*128);

    init_ifs (resx,c_resoly);
	goom_lines_set_res (gmline1,resx,c_resoly);
	goom_lines_set_res (gmline2,resx,c_resoly);
}


guint32 * goom_update (gint16 data [2][512],int forceMode)
{
    static int	lockvar = 0 ; // pour empecher de nouveaux changements
    static int 	goomvar = 0 ; // boucle des gooms
    static int 	totalgoom = 0 ; // nombre de gooms par seconds
    static int 	agoom = 0 ; // un goom a eu lieu..	
    static int 	loopvar = 0 ; // mouvement des points
    static int 	speedvar = 0 ; // vitesse des particules

	// duree de la transition entre afficher les lignes ou pas
#define DRAWLINES 70
    static int	lineMode = DRAWLINES ; // l'effet lineaire a dessiner
	static int  nombreCDDC = 0; // nombre de Cycle Depuis Dernier Changement
    guint32 * return_val;
    guint32 pointWidth;
    guint32 pointHeight;
    int 	incvar ; // volume du son
    int 	accelvar ; // acceleration des particules
    int 	i ;
    float 	largfactor ; // elargissement de l'intervalle d'évolution des points

	static int ifs_incr = 1; // dessiner l'ifs (0 = non: > = increment)
	static int decay_ifs = 0; // disparition de l'ifs
	static int recay_ifs = 0; // dédisparition de l'ifs

#define SWITCHMULT (19.0f/20.0f)
#define SWITCHINCR 0xff
	static float switchMult = 1.0f;
	static int switchIncr = SWITCHINCR;

	/*static int lastgoom = 0;*/

    static char	goomlimit = 2 ; // sensibilité du goom
    static 	ZoomFilterData zfd =
    {
	127,	8,	16,
	1,	1,	0,	NORMAL_MODE,
	0,	0,	0, 0, 0};

    ZoomFilterData *pzfd;

    /* test if the config has changed, update it if so */
    pointWidth = (resolx * 2) / 5;
    pointHeight = ((c_resoly) * 2) / 5;

    /* ! etude du signal ... */
    incvar = 0 ;
    for (i=0;i<512;i++)
    {
        if (incvar < data[0][i]) incvar = data[0][i] ;
    }

    accelvar = incvar / 5000 ;
    if (speedvar>5)
    {
        accelvar-- ;
        if (speedvar>20) accelvar -- ;
        if (speedvar>40) speedvar = 40 ;
    }
    accelvar -- ;
    speedvar += accelvar ;
	
    if (speedvar<0) speedvar=0;
    if (speedvar>40) speedvar = 40 ;

	
	/* ! calcul du deplacement des petits points ... */
	
	largfactor = ((float)speedvar / 40.0f + (float)incvar / 50000.0f) / 1.5f ;
	if (largfactor>1.5f) largfactor = 1.5f ;

	if ((ifs_incr==1) && (iRAND(200)==0) && (decay_ifs<-300) && (agoom)) {
	  decay_ifs = 200;
	}

	decay_ifs --;
	if (decay_ifs > 0)
	  ifs_incr += 2;
	if (decay_ifs == 0)
	  ifs_incr = 0;

	if ((ifs_incr==0) && (iRAND(200)==0) && (agoom) && (decay_ifs < -100)) {
	  recay_ifs = 5;
	  ifs_incr = 11;
	}

	if (recay_ifs) {
	  ifs_incr -= 2;
	  recay_ifs --;
	  if (recay_ifs == 0)
		ifs_incr = 1;
	}

	if (ifs_incr > 0)
	  ifs_update (p1+c_offset, p2+c_offset, resolx, c_resoly, ifs_incr);

//	(p1+c_offset)[resolx/2 + c_resoly/2 * resolx] = 0;

	if (ifs_incr != 1) {
	  for (i = 1 ; i*15 <= speedvar + 15; i ++) {
		loopvar += speedvar + 1 ;
		
		pointFilter(p1+c_offset,
					YELLOW,
					((pointWidth - 6.0f) * largfactor + 5.0f),
					((pointHeight - 6.0f) * largfactor + 5.0f),
					i * 152.0f, 128.0f,
					loopvar + i*2032);
		pointFilter(p1+c_offset, ORANGE,
					((pointWidth / 2) * largfactor) / i + 10.0f * i,
					((pointHeight / 2) * largfactor) / i + 10.0f * i,
					96.0f, i * 80.0f, loopvar / i);
		pointFilter(p1+c_offset, VIOLET,
					((pointHeight / 3 + 5.0f) * largfactor) / i + 10.0f * i,
					((pointHeight / 3 + 5.0f) * largfactor) / i + 10.0f * i,
					i + 122.0f, 134.0f, loopvar / i);
		pointFilter(p1+c_offset, BLACK,
					((pointHeight / 3) * largfactor + 20.0f),
					((pointHeight / 3) * largfactor + 20.0f),
					58.0f, i * 66.0f, loopvar / i);
		pointFilter(p1+c_offset, WHITE,
					(pointHeight * largfactor + 10.0f * i) / i,
					(pointHeight * largfactor + 10.0f * i) / i,
					66.0f, 74.0f, loopvar + i * 500);
	  }
	}
    // par défaut pas de changement de zoom
	pzfd = NULL ;

	/*
	 * Test forceMode
	 */
#ifdef VERBOSE
	if (forceMode != 0) {
		printf ("forcemode = %d\n",forceMode);
	}
#endif
	
	
    // diminuer de 1 le temps de lockage
    // note pour ceux qui n'ont pas suivis : le lockvar permet d'empecher un
    // changement d'etat du plugins juste apres un autre changement d'etat. oki ?
    if (--lockvar < 0) lockvar = 0 ;

    // temps du goom
    if (--agoom < 0) agoom = 0 ;

    // on verifie qu'il ne se pas un truc interressant avec le son.
    if ((accelvar>goomlimit) || (accelvar<-goomlimit) || (forceMode>0)
		|| (nombreCDDC > TIME_BTW_CHG)) {

//	  if (nombreCDDC > 300) {
//	  }

	  // UN GOOM !!! YAHOO !
	  totalgoom ++ ;
	  agoom = 20 ; // mais pdt 20 cycles, il n'y en aura plus.
	  //	  lineMode = (lineMode + 1)%40; // Tous les 10 gooms on change de mode lineaire
	  
	  //		if (iRAND(12) == 0)
	  //		  zfd.vitesse=STOP_SPEED-1;
	  //		if (iRAND(13) == 0)
	  //		  zfd.vitesse=STOP_SPEED+1;
	  
	  // changement eventuel de mode
	  switch (iRAND(28))
		{
		case 0:
		case 10:
		  zfd.hypercosEffect=iRAND(2);
		case 13:
		case 20:
		case 21:
		  zfd.mode=WAVE_MODE;
		  zfd.reverse=0;
		  zfd.waveEffect= (iRAND(3)==0);
		  if (iRAND(2))
			  zfd.vitesse = (zfd.vitesse + 127) >> 1;
		  break;
        case 1:
		case 11:
		  zfd.mode=CRYSTAL_BALL_MODE;
		  zfd.waveEffect=0;
		  zfd.hypercosEffect=0;
		  break;
        case 2:
		case 12:
		  zfd.mode=AMULETTE_MODE;
		  zfd.waveEffect=(iRAND(3)==0);
		  zfd.hypercosEffect=(iRAND(3)==0);
		  break;
        case 3:
		  zfd.mode = WATER_MODE ;
		  zfd.waveEffect=0;
		  zfd.hypercosEffect=0;
		  break;
        case 4:
		case 14:
		  zfd.mode=SCRUNCH_MODE;
		  zfd.waveEffect=0;
		  zfd.hypercosEffect=0;
		  break;
		case 5:
		case 15:
		  zfd.mode=HYPERCOS1_MODE;
		  zfd.waveEffect=0;
		  zfd.hypercosEffect=(iRAND(3)==0);
		  break;
		case 6:
		case 16:
		  zfd.mode=HYPERCOS2_MODE;
		  zfd.waveEffect=0;
		  zfd.hypercosEffect=0;
		  break;
        case 7:
		case 17:
		  zfd.mode=CRYSTAL_BALL_MODE;
		  zfd.waveEffect=(iRAND(4)==0);
		  zfd.hypercosEffect=iRAND(2);
		  break;
		case 8:
		case 18:
		case 19:
		  zfd.mode=SCRUNCH_MODE;
		  zfd.waveEffect=1;
		  zfd.hypercosEffect=1;
		  break;
        default:
		  zfd.mode=NORMAL_MODE;
		  zfd.waveEffect=0;
		  zfd.hypercosEffect=0;
        }
    }

    // tout ceci ne sera fait qu'en cas de non-blocage
    if (lockvar == 0)
    {
        // reperage de goom (acceleration forte de l'acceleration du volume)
        // -> coup de boost de la vitesse si besoin..
        if ( (accelvar>goomlimit) || (accelvar<-goomlimit) )
        {
            goomvar ++ ;
            //if (goomvar % 1 == 0)
            {
			  guint32 vtmp ;
                guint32 newvit ;
			  lockvar = 50;
                newvit = STOP_SPEED - speedvar / 2 ;
                // retablir le zoom avant..
                if ((zfd.reverse) &&
                        (!(cycle%13)) &&
                        (rand ()%5==0))
                {
                    zfd.reverse = 0 ;
                    zfd.vitesse = STOP_SPEED - 2 ;
                    lockvar = 75 ;
                }
                if (iRAND (10) == 0)
                {
                    zfd.reverse = 1;
                    lockvar = 100;
                }

        if (iRAND(10) == 0)
          zfd.vitesse=STOP_SPEED-1;
        if (iRAND(12) == 0)
		          zfd.vitesse=STOP_SPEED+1;

		// changement de milieu..
                switch (iRAND(25))
                {
				case 0:
				case 3:
				case 6:
				  zfd.middleY = c_resoly - 1 ;
				  zfd.middleX = resolx / 2 ;
				  break ;
				case 1:
				case 4:
				  zfd.middleX = resolx - 1 ;
				  break ;
				case 2:
				case 5:
				  zfd.middleX = 1 ;
				  break ;
				default:
				  zfd.middleY = c_resoly / 2 ;
				  zfd.middleX = resolx / 2 ;
                }
				
                if (zfd.mode == WATER_MODE)
                {
                    zfd.middleX = resolx / 2;
                    zfd.middleY = c_resoly / 2;
                }

                switch (vtmp = (iRAND (15)))
                {
                    case 0:
                        zfd.vPlaneEffect = iRAND(3) - iRAND(3);
                        zfd.hPlaneEffect = iRAND(3) - iRAND(3);
                        break;
                    case 3:
                        zfd.vPlaneEffect = 0 ;
                        zfd.hPlaneEffect = iRAND(8) - iRAND(8);
                        break;
                    case 4:
                    case 5:
                    case 6:
                    case 7:
                        zfd.vPlaneEffect = iRAND(5) - iRAND (5);
                        zfd.hPlaneEffect = - zfd.vPlaneEffect;
                        break;
                    case 8:
                        zfd.hPlaneEffect = 5 + iRAND (8);
                        zfd.vPlaneEffect = - zfd.hPlaneEffect ;
                        break;
                    case 9:
                        zfd.vPlaneEffect = 5 + iRAND (8);
                        zfd.hPlaneEffect = - zfd.hPlaneEffect ;
                        break;
                    case 13:
                        zfd.hPlaneEffect = 0;
                        zfd.vPlaneEffect = iRAND(10) - iRAND(10);
                        break;
                    case 14:
                        zfd.hPlaneEffect = iRAND(10) - iRAND(10);
                        zfd.vPlaneEffect = iRAND(10) - iRAND(10);
                        break;
                    default:
                        if (vtmp < 10)
                        {
                            zfd.vPlaneEffect = 0;
                            zfd.hPlaneEffect = 0;
                        }
                    }

                if (iRAND (5) != 0) zfd.noisify = 0 ;
                else
                {
                    zfd.noisify = iRAND (3) + 2 ;
                    lockvar *= 2;
                }
			
                if (zfd.mode == AMULETTE_MODE)
                {
                    zfd.vPlaneEffect = 0;
                    zfd.hPlaneEffect = 0;
                    zfd.noisify = 0;
                }

                if ((zfd.middleX == 1) || (zfd.middleX == resolx - 1))
                {
                    zfd.vPlaneEffect = 0 ;
                    zfd.hPlaneEffect = iRAND (2) ? 0 : zfd.hPlaneEffect;
                }

                if (newvit < zfd.vitesse) // on accelere
                {
                    pzfd = &zfd;
                    if ( ( (newvit < STOP_SPEED - 7) &&
                                (zfd.vitesse < STOP_SPEED - 6) &&
                                (cycle % 3 == 0)) ||
                                (iRAND (40) == 0))
                    {
                        zfd.vitesse = STOP_SPEED - iRAND(2) + iRAND(2) ;
                        zfd.reverse = ! zfd.reverse ;
                    }
                    else
                    {
                        zfd.vitesse = (newvit + zfd.vitesse * 4) / 5 ;
                    }
                    lockvar += 50 ;
                }
            }

			if (lockvar > 150) {
			  switchIncr = SWITCHINCR;
			  switchMult = 1.0f;
			}
        }
	  // mode mega-lent
	  if (iRAND(700) == 0)
		{
		  /* 
			 printf ("coup du sort...\n") ;
		  */
		  pzfd = &zfd ;
		  zfd.vitesse = STOP_SPEED - 1 ;
		  zfd.pertedec = 8 ;
		  zfd.sqrtperte = 16 ;
		  goomvar = 1 ;
		  lockvar += 50 ;
		  switchIncr = SWITCHINCR;
		  switchMult = 1.0f;
		}
	}
	
  // gros frein si la musique est calme
  if ((speedvar < 1) && (zfd.vitesse < STOP_SPEED - 4) && (cycle % 16 == 0))
	{
	  /*
		printf ("++slow part... %i\n", zfd.vitesse) ;
	  */
	  pzfd = &zfd ;
	  zfd.vitesse += 3 ;
	  zfd.pertedec = 8 ;
	  zfd.sqrtperte = 16 ;
	  goomvar = 0 ;
	  /*
		printf ("--slow part... %i\n", zfd.vitesse) ;
	  */
	}
	
  // baisser regulierement la vitesse...
  if ( (cycle % 73 == 0) && (zfd.vitesse < STOP_SPEED - 5))
	{
	  /*
		printf ("slow down...\n") ;
	  */
	  pzfd = &zfd ;
	  zfd.vitesse ++ ;
	}
	
  // arreter de decrémenter au bout d'un certain temps
  if ((cycle % 101 == 0) && (zfd.pertedec == 7))
	{
	  pzfd = &zfd ;
	  zfd.pertedec=8 ;
	  zfd.sqrtperte=16 ;
	}
	
	if ((forceMode > 0) && (forceMode <= NB_FX)) {
		pzfd=&zfd;
		pzfd->mode = forceMode - 1;
	}

	if (forceMode == -1) {
		pzfd = NULL;
	}

	if (pzfd != NULL) {
	  static int exvit = 128;
	  int dif;
	  nombreCDDC = 0;

	  switchIncr = SWITCHINCR;

	  dif = zfd.vitesse - exvit;
	  if (dif < 0) dif=-dif;

	  if (dif > 2) {
		switchIncr *= (dif + 2) / 2;
	  }
	  exvit = zfd.vitesse;
	  switchMult = 1.0f;
	  
	  if (((accelvar > goomlimit) && (totalgoom < 2))||(forceMode>0)) {
		switchIncr = 0;
		switchMult = SWITCHMULT;
	  }
	}
  else {
	if (nombreCDDC > TIME_BTW_CHG) {
	  pzfd = &zfd;
	  nombreCDDC = 0;
	}
	else
	  nombreCDDC ++;
  }

#ifdef VERBOSE
  if (pzfd)
	{
	  printf ("GOOM: pzfd->mode = %d\n", pzfd->mode);
	}
#endif



    // si on est dans un goom : afficher les lignes...
	
	if (lineMode != DRAWLINES) {
	  lineMode --;
	  if (lineMode == -1)
		lineMode = 0;
	}

	if ((agoom > 0) && (totalgoom>2) && (cycle % 120 == 0) && (iRAND(3)==0)) {
	  if (lineMode == 0)
		lineMode = DRAWLINES;
	  else if (lineMode == DRAWLINES) {
		float param1,param2;
		int couleur;
		int mode;
		
		lineMode --;
		choose_a_goom_line (&param1, &param2, &couleur,
							&mode);
		
		goom_lines_switch_to (gmline1, mode, param1, GML_BLACK);
		goom_lines_switch_to (gmline2, mode, param2, GML_BLACK);
	  }
	}

	if ((lineMode != 0) || (agoom > 15)) {
	  gmline2->power = gmline1->power;

	  goom_lines_draw (gmline1,data[0],p1+c_offset);
	  goom_lines_draw (gmline2,data[1],p1+c_offset);
	  
	  if (((cycle % 101)==9) && (iRAND (3)==1)
		  && ((lineMode == 0) || (lineMode == DRAWLINES))) {
		float param1,param2;
		int couleur;
		int mode;

		choose_a_goom_line (&param1, &param2, &couleur,
							&mode);

		goom_lines_switch_to (gmline1, mode, param1, couleur);
		goom_lines_switch_to (gmline2, mode, param2, 5-couleur);
	  }
	}
	/*    if (agoom > 15) goom_lines_draw
		  (gmline,data,
		  ( (zfd.middleX == resolx/2)
		  && (zfd.middleY == c_resoly/2)
		  && (zfd.mode != WATER_MODE) )
		  ? (lineMode/10) : 0,
		  p2+c_offset,agoom-15);
	*/
    // Zoom here !
    zoomFilterFastRGB (p1+c_offset, p2+c_offset, pzfd, resolx, c_resoly,
		       switchIncr, switchMult) ;


    return_val = p2 ;
    tmp=p1;
    p1=p2;
    p2=tmp;

    // affichage et swappage des buffers..
    cycle++;
	
    // tous les 100 cycles : vérifier si le taux de goom est correct
    // et le modifier sinon..
    if (!(cycle%100))
    {
        if (totalgoom>15)
        {
            //	printf ("less gooms\n") ;
            goomlimit ++ ;
        }
        else
        {
            if ((totalgoom==0) && (goomlimit>1))
                goomlimit -- ;
        }        
        totalgoom = 0 ;
    }
    return return_val;
}

void goom_close ()
{
  if (pixel!=NULL) free (pixel) ;
  if (back!=NULL) free (back) ;
  	pixel = back = NULL;
  RAND_CLOSE();
  release_ifs ();
  goom_lines_free (&gmline1);
  goom_lines_free (&gmline2);
}


void choose_a_goom_line (float *param1, float *param2, int *couleur,
						 int *mode)
{
  *mode = iRAND (3);
  switch (*mode) {
  case GML_CIRCLE:
	if (iRAND (3)==0) {
	  *param1 = *param2 = 0;
	}
	else if (iRAND(2)) {
	  *param1 = 0.40f * c_resoly;
	  *param2 = 0.20f * c_resoly;
	}
	else {
	  *param1 = *param2 = c_resoly * 0.25;
	}
	break;
  case GML_HLINE:
	if (iRAND (4)) {
	  *param1 = c_resoly / 7;
	  *param2 = 6.0f * c_resoly / 7.0f;
	}
	else {
	  *param1 = *param2 = c_resoly / 2.0f;
	}
	break;
  case GML_VLINE:
	if (iRAND (3)) {
	  *param1 = resolx / 7.0f;
	  *param2 = 6.0f * resolx / 7.0f;
	}
	else {
	  *param1 = *param2 = resolx / 2.0f;
	}
	break;
  }
  
  *couleur = iRAND (6);
}
