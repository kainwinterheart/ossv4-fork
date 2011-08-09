/*
 * Purpose: Virtual mixing audio driver output mixing routines
 *
 * This file contains the actual mixing and resampling engine for output.
 * The actual algorithms are implemented in outexport.inc and playmix.inc.
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2006. All rights reserved.

#define SWAP_SUPPORT
#include <oss_config.h>
#include "vmix.h"

#if 0
/* Debugging macros*/
extern unsigned char tmp_status;
# define UP_STATUS(v) OUTB(NULL, (tmp_status=tmp_status|(v)), 0x378)
# define DOWN_STATUS(v) OUTB(NULL, (tmp_status=tmp_status&~(v)), 0x378)
#else
# define UP_STATUS(v)
# define DOWN_STATUS(v)
#endif

#undef  SINE_DEBUG

#ifndef CONFIG_OSS_VMIX_FLOAT
#undef SINE_DEBUG
#endif

#ifdef SINE_DEBUG
#define SINE_SIZE	48
static const float sine_table[SINE_SIZE] = {
  0.000000, 0.130526, 0.258819, 0.382683,
  0.500000, 0.608761, 0.707107, 0.793353,
  0.866025, 0.923880, 0.965926, 0.991445,
  1.000000, 0.991445, 0.965926, 0.923880,
  0.866025, 0.793353, 0.707107, 0.608761,
  0.500000, 0.382683, 0.258819, 0.130526,
  0.000000, -0.130526, -0.258819, -0.382683,
  -0.500000, -0.608761, -0.707107, -0.793353,
  -0.866025, -0.923880, -0.965926, -0.991445,
  -1.000000, -0.991445, -0.965926, -0.923880,
  -0.866025, -0.793353, -0.707107, -0.608761,
  -0.500000, -0.382683, -0.258819, -0.130526
};
static int sine_phase[MAX_PLAY_CHANNELS] = { 0 };
#endif

#ifndef CONFIG_OSS_VMIX_FLOAT
/*
 * Simple limiter to prevent overflows when using fixed point computations
 */
void
process_limiter (unsigned int *statevar, int *chbufs[], int nchannels,
		 int nsamples)
{
#define Abs(x) ((x) < 0 ? -(x) : (x))

  int k, t;
  unsigned int q, amp, amp2;

  for (t = 0; t < nsamples; t++)
    {
      amp = (unsigned) Abs (chbufs[0][t]);

      for (k = 1; k < nchannels; k++)
	{
	  amp2 = (unsigned) Abs (chbufs[k][t]);
	  if (amp2 > amp)
	    amp = amp2;
	}

      amp >>= 8;
      q = 0x10000;

      if (amp > 0x7FFF)
	q = 0x7FFF0000 / amp;

      if (*statevar > q)
	*statevar = q;
      else
	{
	  q = *statevar;

	  /*
	   * Simplier (linear) tracking algo 
	   * (gives less distortion, but more pumping)
	   */
	  *statevar += 2;
	  if (*statevar > 0x10000)
	    *statevar = 0x10000;

	  /*
	   * Classic tracking algo
	   * gives more distortion with no-lookahead
	   * *statevar=0x10000-((0x10000-*statevar)*0xFFF4>>16);
	   */
	}

      for (k = 0; k < nchannels; k++)
	{
	  int in = chbufs[k][t];
	  int out = 0;
	  unsigned int p;

	  if (in >= 0)
	    {
	      p = in;
	      p = ((p & 0xFFFF) * (q >> 4) >> 12) + (p >> 16) * q;
	      out = p;
	    }
	  else
	    {
	      p = -in;
	      p = ((p & 0xFFFF) * (q >> 4) >> 12) + (p >> 16) * q;
	      out = -p;
	    }
	  /* safety code */
	  /* if output after limiter is clamped, then it can be dropped */
	  if (out > 0x7FFFFF)
	    out = 0x7FFFFF;
	  else if (out < -0x7FFFFF)
	    out = -0x7FFFFF;

	  chbufs[k][t] = out;
	}
    }
}
#endif

/*
 * Output export functions
 */

#undef  INT_EXPORT
#define INT_EXPORT(x) (x / 256)

static void
export16ne (vmix_engine_t * eng, void *outbuf, vmix_sample_t * chbufs[],
	    int channels, int samples)
{
  short *op;
#define SAMPLE_TYPE	short
#define SAMPLE_RANGE	32768.0
#undef VMIX_BYTESWAP
#define VMIX_BYTESWAP(x) x

#include "outexport.inc"
}

static void
export16oe (vmix_engine_t * eng, void *outbuf, vmix_sample_t * chbufs[],
	    int channels, int samples)
{
  short *op;
#undef  SAMPLE_TYPE
#undef  SAMPLE_RANGE
#define SAMPLE_TYPE	short
#define SAMPLE_RANGE	32768.0
#undef VMIX_BYTESWAP
#define VMIX_BYTESWAP(x) bswap16(x)

#include "outexport.inc"
}

#undef  INT_EXPORT
#define INT_EXPORT(x) (x * 256)

static void
export32ne (vmix_engine_t * eng, void *outbuf, vmix_sample_t * chbufs[],
	    int channels, int samples)
{
  int *op;
#undef  SAMPLE_TYPE
#undef  SAMPLE_RANGE
#define SAMPLE_TYPE	int
#define SAMPLE_RANGE	2147483648.0
#undef VMIX_BYTESWAP
#define VMIX_BYTESWAP(x) x

#include "outexport.inc"
}

static void
export32oe (vmix_engine_t * eng, void *outbuf, vmix_sample_t * chbufs[],
	    int channels, int samples)
{
  int *op;
#define SAMPLE_TYPE	int
#define SAMPLE_RANGE	2147483648.0
#undef VMIX_BYTESWAP
#define VMIX_BYTESWAP(x) bswap32(x)

#include "outexport.inc"
}

/*
 * Mixing functions
 */
#undef  BUFFER_TYPE
#define BUFFER_TYPE short *

#undef  INT_OUTMIX
#define INT_OUTMIX(x) (x * 256)

void
vmix_outmix_16ne (vmix_portc_t * portc, int nsamples)
{
  short *inp;
#ifdef CONFIG_OSS_VMIX_FLOAT
  double range = 3.0517578125e-5;	/* 1.0 / 32768.0 */
#endif
#undef VMIX_BYTESWAP
#define VMIX_BYTESWAP(x) x
#include "playmix.inc"
}

void
vmix_outmix_16oe (vmix_portc_t * portc, int nsamples)
{
  short *inp;
#ifdef CONFIG_OSS_VMIX_FLOAT
  double range = 3.0517578125e-5;	/* 1.0 / 32768.0 */
#endif
#undef VMIX_BYTESWAP
#define VMIX_BYTESWAP(x) bswap16(x)
#include "playmix.inc"
}

#undef BUFFER_TYPE
#define BUFFER_TYPE int *
#undef  INT_OUTMIX
#define INT_OUTMIX(x) (x / 256)

void
vmix_outmix_32ne (vmix_portc_t * portc, int nsamples)
{
  int *inp;
#undef VMIX_BYTESWAP
#define VMIX_BYTESWAP(x) x
#ifdef CONFIG_OSS_VMIX_FLOAT
  double range = 4.65661287308e-10;	/* 1.0 / 2147483648.0 */
#endif
#include "playmix.inc"
}

void
vmix_outmix_32oe (vmix_portc_t * portc, int nsamples)
{
  int *inp;
#undef VMIX_BYTESWAP
#define VMIX_BYTESWAP(x) bswap32(x)
#ifdef CONFIG_OSS_VMIX_FLOAT
  double range = 4.65661287308e-10;	/* 1.0 / 2147483648.0 */
#endif
#include "playmix.inc"
}

#ifdef CONFIG_OSS_VMIX_FLOAT
void
vmix_outmix_float (vmix_portc_t * portc, int nsamples)
{
  float *inp;
  double range = 1.0;
#undef BUFFER_TYPE
#define BUFFER_TYPE float *
#undef VMIX_BYTESWAP
#define VMIX_BYTESWAP(x) x
#include "playmix.inc"
}
#endif

#ifdef CONFIG_OSS_VMIX_FLOAT
/*
 * Mixing functions (with sample rate conversions)
 */
#undef  BUFFER_TYPE
#define BUFFER_TYPE short *

#undef  INT_OUTMIX
#define INT_OUTMIX(x) (x * 256)

void
vmix_outmix_16ne_src (vmix_portc_t * portc, int nsamples)
{
  short *inp;
  double range = 3.0517578125e-5;	/* 1.0 / 32768.0 */
#undef VMIX_BYTESWAP
#define VMIX_BYTESWAP(x) x
#include "playmix_src.inc"
}

void
vmix_outmix_16oe_src (vmix_portc_t * portc, int nsamples)
{
  short *inp;
  double range = 3.0517578125e-5;	/* 1.0 / 32768.0 */
#undef VMIX_BYTESWAP
#define VMIX_BYTESWAP(x) bswap16(x)
#include "playmix_src.inc"
}

#undef BUFFER_TYPE
#define BUFFER_TYPE int *

#undef  INT_OUTMIX
#define INT_OUTMIX(x) (x / 256)

void
vmix_outmix_32ne_src (vmix_portc_t * portc, int nsamples)
{
  int *inp;
  double range = 4.65661287308e-10;	/* 1.0 / 2147483648.0 */
#undef VMIX_BYTESWAP
#define VMIX_BYTESWAP(x) x
#include "playmix_src.inc"
}

void
vmix_outmix_32oe_src (vmix_portc_t * portc, int nsamples)
{
  int *inp;
  double range = 4.65661287308e-10;	/* 1.0 / 2147483648.0 */
#undef VMIX_BYTESWAP
#define VMIX_BYTESWAP(x) bswap32(x)
#include "playmix_src.inc"
}

void
vmix_outmix_float_src (vmix_portc_t * portc, int nsamples)
{
  float *inp;
  double range = 1.0;
#undef BUFFER_TYPE
#define BUFFER_TYPE float *
#undef VMIX_BYTESWAP
#define VMIX_BYTESWAP(x) x
#include "playmix_src.inc"
}
#endif

static void
vmix_play_callback (int dev, int parm)
{
  int n;

  adev_t *adev = audio_engines[dev];
  dmap_t *dmap = adev->dmap_out;
  oss_native_word flags;
  int i;

  vmix_mixer_t *mixer = adev->vmix_mixer;
  vmix_engine_t *eng = &mixer->play_engine;

#ifdef CONFIG_OSS_VMIX_FLOAT
  fp_env_t fp_buf;
  short *fp_env = fp_buf;
  fp_flags_t fp_flags;
#endif

  UP_STATUS (0x04);
  if (dmap == NULL || dmap->dmabuf == NULL)
    return;

  if (dmap->bytes_in_use == 0)
    {
      cmn_err (CE_WARN, "Bytes in use=0, eng=%d\n", adev->engine_num);
      return;
    }

  MUTEX_ENTER_IRQDISABLE (mixer->mutex, flags);

  if (dmap->dmabuf == NULL)
    {
      MUTEX_EXIT_IRQRESTORE (mixer->mutex, flags);
      return;
    }

#ifdef CONFIG_OSS_VMIX_FLOAT

  {
    /*
     * Align the FP save buffer to 16 byte boundary
     */
    oss_native_word p;
    p = (oss_native_word) fp_buf;

    p = ((p + 15ULL) / 16) * 16;
    fp_env = (short *) p;
  }
  FP_SAVE (fp_env, fp_flags);
#endif

  n = 0;
  while (n++ < eng->max_playahead
	 && dmap_get_qlen (dmap) < eng->max_playahead)
    {
      int p;
      unsigned char *outbuf;
      int nstreams = 0;

      for (i = 0; i < eng->channels; i++)
	memset (eng->chbufs[i], 0, CHBUF_SAMPLES * sizeof (vmix_sample_t));
      for (i = 0; i < mixer->num_clientdevs; i++)
	if (mixer->client_portc[i]->trigger_bits & PCM_ENABLE_OUTPUT)
	  {
	    vmix_portc_t *portc = mixer->client_portc[i];

	    if (portc->play_mixing_func == NULL)
	      continue;
	    if (portc->play_choffs + portc->channels >
		mixer->play_engine.channels)
	      portc->play_choffs = 0;
	    portc->play_mixing_func (portc,
				     mixer->play_engine.samples_per_frag);
	    nstreams++;		/* Count the number of active output streams */
	  }

      eng->num_active_outputs = (nstreams > 0) ? nstreams : 1;

      /* Export the output mix to the device */
      p = (int) (dmap->user_counter % dmap->bytes_in_use);
      outbuf = dmap->dmabuf + p;
      if (dmap->dmabuf != NULL)
	{
#ifndef CONFIG_OSS_VMIX_FLOAT
	  process_limiter (&eng->limiter_statevar, eng->chbufs, eng->channels,
			   eng->samples_per_frag);
#endif
	  eng->converter (eng, outbuf, eng->chbufs, eng->channels,
			  eng->samples_per_frag);
	}

      dmap->user_counter += dmap->fragment_size;

    }

#ifdef CONFIG_OSS_VMIX_FLOAT
  FP_RESTORE (fp_env, fp_flags);
#endif
  MUTEX_EXIT_IRQRESTORE (mixer->mutex, flags);
/*
 * Call oss_audio_outputintr outside FP mode because it may 
 * cause a task switch (under Solaris). Task switch may turn on CR0.TS under
 * x86 which in turn will cause #nm exception.
 */
  for (i = 0; i < mixer->num_clientdevs; i++)
    if (mixer->client_portc[i]->trigger_bits & PCM_ENABLE_OUTPUT)
      {
	vmix_portc_t *portc = mixer->client_portc[i];
	oss_audio_outputintr (portc->audio_dev, 1);
      }

  for (i = 0; i < mixer->num_loopdevs; i++)
    {
      if (mixer->loop_portc[i]->trigger_bits & PCM_ENABLE_INPUT)
	{
	  oss_audio_inputintr (mixer->loop_portc[i]->audio_dev, 0);
	}
    }
  DOWN_STATUS (0x04);
}

void
vmix_setup_play_engine (vmix_mixer_t * mixer, adev_t * adev, dmap_t * dmap)
{
  int fmt;
  int frags = 0x7fff0007;	/* fragment size of 128 bytes */
  int i;
  int old_min;

/*
 * Sample format (and endianess) setup 
 *
 */

  /* First make sure a sane format is selected before starting to probe */
  fmt = adev->d->adrv_set_format (mixer->masterdev, AFMT_S16_LE);
  fmt = adev->d->adrv_set_format (mixer->masterdev, AFMT_S16_NE);

  /* Find out the "best" sample format supported by the device */

  if (adev->oformat_mask & AFMT_S16_OE)
    fmt = AFMT_S16_OE;
  if (adev->oformat_mask & AFMT_S16_NE)
    fmt = AFMT_S16_NE;

  if (mixer->multich_enable)	/* Better quality enabled */
    {
      if (adev->oformat_mask & AFMT_S32_OE)
	fmt = AFMT_S32_OE;
      if (adev->oformat_mask & AFMT_S32_NE)
	fmt = AFMT_S32_NE;
    }

  fmt = adev->d->adrv_set_format (mixer->masterdev, fmt);
  mixer->play_engine.fmt = fmt;

  switch (fmt)
    {
    case AFMT_S16_NE:
      mixer->play_engine.bits = 16;
      mixer->play_engine.converter = export16ne;
      break;

    case AFMT_S16_OE:
      mixer->play_engine.bits = 16;
      mixer->play_engine.converter = export16oe;
      break;

    case AFMT_S32_NE:
      mixer->play_engine.bits = 32;
      mixer->play_engine.converter = export32ne;
      break;

    case AFMT_S32_OE:
      mixer->play_engine.bits = 32;
      mixer->play_engine.converter = export32oe;
      break;

    default:
      cmn_err (CE_CONT, "Unrecognized sample format %x\n", fmt);
      return;
    }
/*
 * Number of channels
 */
  mixer->play_engine.channels = mixer->max_channels;

  if (mixer->play_engine.channels > MAX_PLAY_CHANNELS)
     mixer->play_engine.channels = MAX_PLAY_CHANNELS;

  if (!mixer->multich_enable)
    mixer->play_engine.channels = 2;

  /* Force the device to stereo before trying with (possibly) multiple channels */
  adev->d->adrv_set_channels (mixer->masterdev, 2);

  mixer->play_engine.channels =
    adev->d->adrv_set_channels (mixer->masterdev,
				mixer->play_engine.channels);

  if (mixer->play_engine.channels > MAX_PLAY_CHANNELS)
    {
      cmn_err (CE_WARN,
	       "Number of channels (%d) is larger than maximum (%d)\n",
	       mixer->play_engine.channels, MAX_PLAY_CHANNELS);
      return;
    }

  if (mixer->play_engine.channels > 2)
    {
      DDB (cmn_err
	   (CE_CONT, "Enabling multi channel play mode, %d hw channels\n",
	    mixer->play_engine.channels));
    }
  else if (mixer->play_engine.channels != 2)
    {
      cmn_err (CE_WARN,
	       "Master device doesn't support suitable channel configuration\n");

      return;
    }

  mixer->play_engine.rate =
    oss_audio_set_rate (mixer->masterdev, mixer->rate);
  mixer->rate = mixer->play_engine.rate;

  if (mixer->play_engine.rate <= 22050)
    frags = 0x7fff0004;		/* Use smaller fragments */

  audio_engines[mixer->masterdev]->hw_parms.channels =
    mixer->play_engine.channels;
  audio_engines[mixer->masterdev]->hw_parms.rate = mixer->play_engine.rate;
  audio_engines[mixer->masterdev]->dmap_out->data_rate =
    mixer->play_engine.rate * mixer->play_engine.channels *
    mixer->play_engine.bits / 8;
  audio_engines[mixer->masterdev]->dmap_out->frame_size =
    mixer->play_engine.channels * mixer->play_engine.bits / 8;

  old_min = adev->min_fragments;
#if 0
  if ((adev->max_fragments == 0 || adev->max_fragments >= 4)
      && adev->min_block == 0)
    adev->min_fragments = 4;
#endif

  oss_audio_ioctl (mixer->masterdev, NULL, SNDCTL_DSP_SETFRAGMENT,
		   (ioctl_arg) & frags);
  oss_audio_ioctl (mixer->masterdev, NULL, SNDCTL_DSP_GETBLKSIZE,
		   (ioctl_arg) & mixer->play_engine.fragsize);

  dmap->bytes_in_use = dmap->fragment_size * dmap->nfrags;

  oss_audio_ioctl (mixer->masterdev, NULL, SNDCTL_DSP_GETBLKSIZE,
		   (ioctl_arg) & mixer->play_engine.fragsize);
  adev->min_fragments = old_min;

  mixer->play_engine.fragsize = dmap->fragment_size;

/* 
 * Determine how many fragments we need to keep filled. 
 */
  if (adev->vmix_flags & VMIX_MULTIFRAG)
    mixer->play_engine.max_playahead = 32;
  else
    mixer->play_engine.max_playahead = 4;

  if (mixer->play_engine.max_playahead >
      audio_engines[mixer->masterdev]->dmap_out->nfrags)
    mixer->play_engine.max_playahead =
      audio_engines[mixer->masterdev]->dmap_out->nfrags;

/*
 * Try to keep one empty fragment after the one currently being played
 * by the device. Writing too close to the playback point may cause
 * massive clicking with some devices.
 */
  if (dmap->nfrags > 2 && mixer->play_engine.max_playahead == dmap->nfrags)
    mixer->play_engine.max_playahead--;

  mixer->play_engine.samples_per_frag =
    mixer->play_engine.fragsize / mixer->play_engine.channels /
    (mixer->play_engine.bits / 8);

  if (mixer->play_engine.samples_per_frag > CHBUF_SAMPLES)
    {
      cmn_err (CE_WARN, "Too many samples per fragment (%d,%d)\n",
	       mixer->play_engine.samples_per_frag, CHBUF_SAMPLES);
      return;
    }

  for (i = 0; i < mixer->play_engine.channels; i++)
    {
      if (mixer->play_engine.chbufs[i] == NULL)	/* Not allocated yet */
	{
	  mixer->play_engine.chbufs[i] =
	    PMALLOC (mixer->master_portc,
		     CHBUF_SAMPLES * sizeof (vmix_sample_t));
	  if (mixer->play_engine.chbufs[i] == NULL)
	    {
	      cmn_err (CE_WARN, "Out of memory\n");
	      return;
	    }
	}

      if (mixer->play_engine.channel_order[i] >= mixer->play_engine.channels)
	mixer->play_engine.channel_order[i] = i;
    }

  dmap->audio_callback = vmix_play_callback;	/* Enable conversions */
  dmap->callback_parm = mixer->instance_num;
  dmap->dma_mode = PCM_ENABLE_OUTPUT;

  if (mixer->inputdev == mixer->masterdev)
    {
      mixer->record_engine.rate = mixer->play_engine.rate;
      mixer->record_engine.bits = mixer->play_engine.bits;
      mixer->record_engine.fragsize = mixer->play_engine.fragsize;
      mixer->record_engine.channels = mixer->play_engine.channels;
      mixer->record_engine.samples_per_frag =
	mixer->play_engine.samples_per_frag;
    }

  if (mixer->num_clientdevs > 1)
    {
      adev->redirect_out = mixer->client_portc[0]->audio_dev;
    }

  /*
   * Fill in the initial playback data (silence) to avoid underruns
   */
  vmix_play_callback (mixer->masterdev, mixer->instance_num);

  if (mixer->masterdev == mixer->inputdev)
    finalize_record_engine (mixer, fmt, audio_engines[mixer->inputdev],
			    audio_engines[mixer->inputdev]->dmap_in);
}
