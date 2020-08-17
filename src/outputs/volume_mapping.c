// from alsa-utils/alsamixer/volume_mapping.c

/*
 * Copyright (c) 2010 Clemens Ladisch <clemens@ladisch.de>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * The functions in this file map the value ranges of ALSA mixer controls onto
 * the interval 0..1.
 *
 * The mapping is designed so that the position in the interval is proportional
 * to the volume as a human ear would perceive it (i.e., the position is the
 * cubic root of the linear sample multiplication factor).  For controls with
 * a small range (24 dB or less), the mapping is linear in the dB values so
 * that each step has the same size visually.  Only for controls without dB
 * information, a linear mapping of the hardware volume register values is used
 * (this is the same algorithm as used in the old alsamixer).
 *
 * When setting the volume, 'dir' is the rounding direction:
 * -1/0/1 = down/nearest/up.
 */

#define _ISOC99_SOURCE /* lrint() */
#include <math.h>
#include <stdbool.h>
#include <alsa/asoundlib.h>

#define MAX_LINEAR_DB_SCALE	24

static inline bool use_linear_dB_scale(long dBmin, long dBmax)
{
	return dBmax - dBmin <= MAX_LINEAR_DB_SCALE * 100;
}

static long lrint_dir(double x, int dir)
{
	if (dir > 0)
		return lrint(ceil(x));
	else if (dir < 0)
		return lrint(floor(x));
	else
		return lrint(x);
}

enum ctl_dir { PLAYBACK, CAPTURE };

static int (* const get_dB_range[2])(snd_mixer_elem_t *, long *, long *) = {
	snd_mixer_selem_get_playback_dB_range,
	snd_mixer_selem_get_capture_dB_range,
};
static int (* const get_raw_range[2])(snd_mixer_elem_t *, long *, long *) = {
	snd_mixer_selem_get_playback_volume_range,
	snd_mixer_selem_get_capture_volume_range,
};
static int (* const set_dB[2])(snd_mixer_elem_t *, long, int) = {
	snd_mixer_selem_set_playback_dB_all,
	snd_mixer_selem_set_capture_dB_all,
};
static int (* const set_raw[2])(snd_mixer_elem_t *, long) = {
	snd_mixer_selem_set_playback_volume_all,
	snd_mixer_selem_set_capture_volume_all,
};


static int set_normalized_volume(snd_mixer_elem_t *elem,
				 double volume,
				 int dir,
				 enum ctl_dir ctl_dir)
{
	long min, max, value;
	double min_norm;
	int err;

	err = get_dB_range[ctl_dir](elem, &min, &max);
	if (err < 0 || min >= max) {
		err = get_raw_range[ctl_dir](elem, &min, &max);
		if (err < 0)
			return err;

		value = lrint_dir(volume * (max - min), dir) + min;
		return set_raw[ctl_dir](elem, value);
	}

	// Corner case from mpd - log10() expects non-zero
	if (volume <= 0)
		return set_dB[ctl_dir](elem, min, dir);
	else if (volume >= 1)
		return set_dB[ctl_dir](elem, max, dir);

	if (use_linear_dB_scale(min, max)) {
		value = lrint_dir(volume * (max - min), dir) + min;
		return set_dB[ctl_dir](elem, value, dir);
	}

	if (min != SND_CTL_TLV_DB_GAIN_MUTE) {
		min_norm = pow(10, (min - max) / 6000.0);
		volume = volume * (1 - min_norm) + min_norm;
	}
	value = lrint_dir(6000.0 * log10(volume), dir) + max;
	return set_dB[ctl_dir](elem, value, dir);
}


// public i/f for forked-daapd
int
alsa_cubic_set_volume(snd_mixer_elem_t *elem, int volume)
{
  return set_normalized_volume(elem, volume >= 0 && volume <= 100 ? volume/100.0 : 75.0, 0, PLAYBACK);
}
