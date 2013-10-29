/*
 * Copyright (C)2002 Anders Johansson ajh@atri.curtin.edu.au
 *
 * This file is part of MPlayer.
 *
 * MPlayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with MPlayer; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/* This audio filter changes the volume of the sound, and can be used
   when the mixer doesn't support the PCM channel. It can handle
   between 1 and AF_NCH channels. The volume can be adjusted between -60dB
   to +20dB and is set on a per channels basis. The is accessed through
   AF_CONTROL_VOLUME_LEVEL.

   The filter has support for soft-clipping, it is enabled by
   AF_CONTROL_VOLUME_SOFTCLIPP.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <inttypes.h>
#include <math.h>
#include <limits.h>

#include "libavutil/common.h"
#include "mp_msg.h"
#include "af.h"

// Data for specific instances of this filter
typedef struct af_volume_s
{
  float	max;			// Max Power level [dB]
  float level[AF_NCH];		// Gain level for each channel
  int soft;			// Enable/disable soft clipping
  int fast;			// Use fix-point volume control
}af_volume_t;

// Initialization and runtime control
static int control(struct af_instance_s* af, int cmd, void* arg)
{
  af_volume_t* s   = (af_volume_t*)af->setup;

  switch(cmd){
  case AF_CONTROL_REINIT:
    // Sanity check
    if(!arg) return AF_ERROR;

    af->data->rate   = ((af_data_t*)arg)->rate;
    af->data->nch    = ((af_data_t*)arg)->nch;

    if(s->fast && (((af_data_t*)arg)->format != (AF_FORMAT_FLOAT_NE))){
      af->data->format = AF_FORMAT_S16_NE;
      af->data->bps    = 2;
    }
    else{
      af->data->format = AF_FORMAT_FLOAT_NE;
      af->data->bps    = 4;
    }
    return af_test_output(af,(af_data_t*)arg);
  case AF_CONTROL_COMMAND_LINE:{
    float v=0.0;
    float vol[AF_NCH];
    int   i;
    sscanf((char*)arg,"%f:%i", &v, &s->soft);
    for(i=0;i<AF_NCH;i++) vol[i]=v;
    return control(af,AF_CONTROL_VOLUME_LEVEL | AF_CONTROL_SET, vol);
  }
  case AF_CONTROL_POST_CREATE:
    s->fast = ((((af_cfg_t*)arg)->force & AF_INIT_FORMAT_MASK) ==
      AF_INIT_FLOAT) ? 0 : 1;
    return AF_OK;
  case AF_CONTROL_VOLUME_SOFTCLIP | AF_CONTROL_SET:
    s->soft = *(int*)arg;
    return AF_OK;
  case AF_CONTROL_VOLUME_SOFTCLIP | AF_CONTROL_GET:
    *(int*)arg = s->soft;
    return AF_OK;
  case AF_CONTROL_VOLUME_LEVEL | AF_CONTROL_SET:
    return af_from_dB(AF_NCH,(float*)arg,s->level,20.0,-200.0,60.0);
  case AF_CONTROL_VOLUME_LEVEL | AF_CONTROL_GET:
    return af_to_dB(AF_NCH,s->level,(float*)arg,20.0);
  case AF_CONTROL_PRE_DESTROY:
    if(!s->fast){
	float m = s->max;
	af_to_dB(1, &m, &m, 10.0);
	mp_msg(MSGT_AFILTER, MSGL_INFO, "[volume] The maximum volume was %0.2fdB \n", m);
    }
    return AF_OK;
  }
  return AF_UNKNOWN;
}

// Deallocate memory
static void uninit(struct af_instance_s* af)
{
    free(af->data);
    free(af->setup);
}

// Filter data through filter
static af_data_t* play(struct af_instance_s* af, af_data_t* data)
{
  af_data_t*    c   = data;			// Current working data
  af_volume_t*  s   = af->setup;		// Setup for this instance
  int           ch  = 0;			// Channel counter
  register int	nch = c->nch;			// Number of channels
  register int  i   = 0;

  // Basic operation volume control only (used on slow machines)
  if(af->data->format == (AF_FORMAT_S16_NE)){
    int16_t*    a   = (int16_t*)c->audio;	// Audio data
    int         len = c->len/2;			// Number of samples
    for(ch = 0; ch < nch ; ch++){
	register int vol = (int)(255.0 * s->level[ch]);
	for(i=ch;i<len;i+=nch){
	  register int x = (a[i] * vol) >> 8;
	  a[i]=av_clip_int16(x);
	}
    }
  }
  // Machine is fast and data is floating point
  else if(af->data->format == (AF_FORMAT_FLOAT_NE)){
    float*   	a   	= (float*)c->audio;	// Audio data
    int       	len 	= c->len/4;		// Number of samples
    for (i = 0; !s->fast && i < len; i++)
      // Check maximum power value
      s->max = FFMAX(s->max, a[i] * a[i]);
    for(ch = 0; ch < nch ; ch++){
      // Volume control (fader)
	for(i=ch;i<len;i+=nch){
	  register float x 	= a[i];
	  // Set volume
	  x *= s->level[ch];
	  /* Soft clipping, the sound of a dream, thanks to Jon Wattes
	     post to Musicdsp.org */
	  if(s->soft)
	    x=af_softclip(x);
	  // Hard clipping
	  else
	    x=av_clipf(x,-1.0,1.0);
	  a[i] = x;
	}
    }
  }
  return c;
}

// Allocate memory and set function pointers
static int af_open(af_instance_t* af){
  int i = 0;
  af->control=control;
  af->uninit=uninit;
  af->play=play;
  af->mul=1;
  af->data=calloc(1,sizeof(af_data_t));
  af->setup=calloc(1,sizeof(af_volume_t));
  if(af->data == NULL || af->setup == NULL)
    return AF_ERROR;
  // Enable volume control and set initial volume to 0dB.
  for(i=0;i<AF_NCH;i++){
    ((af_volume_t*)af->setup)->level[i]  = 1.0;
  }
  return AF_OK;
}

// Description of this filter
af_info_t af_info_volume = {
    "Volume control audio filter",
    "volume",
    "Anders",
    "",
    AF_FLAGS_NOT_REENTRANT,
    af_open
};
