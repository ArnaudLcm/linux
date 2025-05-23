// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * spitz.c  --  SoC audio for Sharp SL-Cxx00 models Spitz, Borzoi and Akita
 *
 * Copyright 2005 Wolfson Microelectronics PLC.
 * Copyright 2005 Openedhand Ltd.
 *
 * Authors: Liam Girdwood <lrg@slimlogic.co.uk>
 *          Richard Purdie <richard@openedhand.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#include <asm/mach-types.h>
#include "../codecs/wm8750.h"
#include "pxa2xx-i2s.h"

#define SPITZ_HP        0
#define SPITZ_MIC       1
#define SPITZ_LINE      2
#define SPITZ_HEADSET   3
#define SPITZ_HP_OFF    4
#define SPITZ_SPK_ON    0
#define SPITZ_SPK_OFF   1

 /* audio clock in Hz - rounded from 12.235MHz */
#define SPITZ_AUDIO_CLOCK 12288000

static int spitz_jack_func;
static int spitz_spk_func;
static struct gpio_desc *gpiod_mic, *gpiod_mute_l, *gpiod_mute_r;

static void spitz_ext_control(struct snd_soc_dapm_context *dapm)
{
	snd_soc_dapm_mutex_lock(dapm);

	if (spitz_spk_func == SPITZ_SPK_ON)
		snd_soc_dapm_enable_pin_unlocked(dapm, "Ext Spk");
	else
		snd_soc_dapm_disable_pin_unlocked(dapm, "Ext Spk");

	/* set up jack connection */
	switch (spitz_jack_func) {
	case SPITZ_HP:
		/* enable and unmute hp jack, disable mic bias */
		snd_soc_dapm_disable_pin_unlocked(dapm, "Headset Jack");
		snd_soc_dapm_disable_pin_unlocked(dapm, "Mic Jack");
		snd_soc_dapm_disable_pin_unlocked(dapm, "Line Jack");
		snd_soc_dapm_enable_pin_unlocked(dapm, "Headphone Jack");
		gpiod_set_value(gpiod_mute_l, 1);
		gpiod_set_value(gpiod_mute_r, 1);
		break;
	case SPITZ_MIC:
		/* enable mic jack and bias, mute hp */
		snd_soc_dapm_disable_pin_unlocked(dapm, "Headphone Jack");
		snd_soc_dapm_disable_pin_unlocked(dapm, "Headset Jack");
		snd_soc_dapm_disable_pin_unlocked(dapm, "Line Jack");
		snd_soc_dapm_enable_pin_unlocked(dapm, "Mic Jack");
		gpiod_set_value(gpiod_mute_l, 0);
		gpiod_set_value(gpiod_mute_r, 0);
		break;
	case SPITZ_LINE:
		/* enable line jack, disable mic bias and mute hp */
		snd_soc_dapm_disable_pin_unlocked(dapm, "Headphone Jack");
		snd_soc_dapm_disable_pin_unlocked(dapm, "Headset Jack");
		snd_soc_dapm_disable_pin_unlocked(dapm, "Mic Jack");
		snd_soc_dapm_enable_pin_unlocked(dapm, "Line Jack");
		gpiod_set_value(gpiod_mute_l, 0);
		gpiod_set_value(gpiod_mute_r, 0);
		break;
	case SPITZ_HEADSET:
		/* enable and unmute headset jack enable mic bias, mute L hp */
		snd_soc_dapm_disable_pin_unlocked(dapm, "Headphone Jack");
		snd_soc_dapm_enable_pin_unlocked(dapm, "Mic Jack");
		snd_soc_dapm_disable_pin_unlocked(dapm, "Line Jack");
		snd_soc_dapm_enable_pin_unlocked(dapm, "Headset Jack");
		gpiod_set_value(gpiod_mute_l, 0);
		gpiod_set_value(gpiod_mute_r, 1);
		break;
	case SPITZ_HP_OFF:

		/* jack removed, everything off */
		snd_soc_dapm_disable_pin_unlocked(dapm, "Headphone Jack");
		snd_soc_dapm_disable_pin_unlocked(dapm, "Headset Jack");
		snd_soc_dapm_disable_pin_unlocked(dapm, "Mic Jack");
		snd_soc_dapm_disable_pin_unlocked(dapm, "Line Jack");
		gpiod_set_value(gpiod_mute_l, 0);
		gpiod_set_value(gpiod_mute_r, 0);
		break;
	}

	snd_soc_dapm_sync_unlocked(dapm);

	snd_soc_dapm_mutex_unlock(dapm);
}

static int spitz_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);

	/* check the jack status at stream startup */
	spitz_ext_control(&rtd->card->dapm);

	return 0;
}

static int spitz_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	unsigned int clk = 0;
	int ret = 0;

	switch (params_rate(params)) {
	case 8000:
	case 16000:
	case 48000:
	case 96000:
		clk = 12288000;
		break;
	case 11025:
	case 22050:
	case 44100:
		clk = 11289600;
		break;
	}

	/* set the codec system clock for DAC and ADC */
	ret = snd_soc_dai_set_sysclk(codec_dai, WM8750_SYSCLK, clk,
		SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	/* set the I2S system clock as input (unused) */
	ret = snd_soc_dai_set_sysclk(cpu_dai, PXA2XX_I2S_SYSCLK, 0,
		SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct snd_soc_ops spitz_ops = {
	.startup = spitz_startup,
	.hw_params = spitz_hw_params,
};

static int spitz_get_jack(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = spitz_jack_func;
	return 0;
}

static int spitz_set_jack(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);

	if (spitz_jack_func == ucontrol->value.enumerated.item[0])
		return 0;

	spitz_jack_func = ucontrol->value.enumerated.item[0];
	spitz_ext_control(&card->dapm);
	return 1;
}

static int spitz_get_spk(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = spitz_spk_func;
	return 0;
}

static int spitz_set_spk(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);

	if (spitz_spk_func == ucontrol->value.enumerated.item[0])
		return 0;

	spitz_spk_func = ucontrol->value.enumerated.item[0];
	spitz_ext_control(&card->dapm);
	return 1;
}

static int spitz_mic_bias(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *k, int event)
{
	gpiod_set_value_cansleep(gpiod_mic, SND_SOC_DAPM_EVENT_ON(event));
	return 0;
}

/* spitz machine dapm widgets */
static const struct snd_soc_dapm_widget wm8750_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", spitz_mic_bias),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
	SND_SOC_DAPM_LINE("Line Jack", NULL),

	/* headset is a mic and mono headphone */
	SND_SOC_DAPM_HP("Headset Jack", NULL),
};

/* Spitz machine audio_map */
static const struct snd_soc_dapm_route spitz_audio_map[] = {

	/* headphone connected to LOUT1, ROUT1 */
	{"Headphone Jack", NULL, "LOUT1"},
	{"Headphone Jack", NULL, "ROUT1"},

	/* headset connected to ROUT1 and LINPUT1 with bias (def below) */
	{"Headset Jack", NULL, "ROUT1"},

	/* ext speaker connected to LOUT2, ROUT2  */
	{"Ext Spk", NULL, "ROUT2"},
	{"Ext Spk", NULL, "LOUT2"},

	/* mic is connected to input 1 - with bias */
	{"LINPUT1", NULL, "Mic Bias"},
	{"Mic Bias", NULL, "Mic Jack"},

	/* line is connected to input 1 - no bias */
	{"LINPUT1", NULL, "Line Jack"},
};

static const char * const jack_function[] = {"Headphone", "Mic", "Line",
	"Headset", "Off"};
static const char * const spk_function[] = {"On", "Off"};
static const struct soc_enum spitz_enum[] = {
	SOC_ENUM_SINGLE_EXT(5, jack_function),
	SOC_ENUM_SINGLE_EXT(2, spk_function),
};

static const struct snd_kcontrol_new wm8750_spitz_controls[] = {
	SOC_ENUM_EXT("Jack Function", spitz_enum[0], spitz_get_jack,
		spitz_set_jack),
	SOC_ENUM_EXT("Speaker Function", spitz_enum[1], spitz_get_spk,
		spitz_set_spk),
};

/* spitz digital audio interface glue - connects codec <--> CPU */
SND_SOC_DAILINK_DEFS(wm8750,
	DAILINK_COMP_ARRAY(COMP_CPU("pxa2xx-i2s")),
	DAILINK_COMP_ARRAY(COMP_CODEC("wm8750.0-001b", "wm8750-hifi")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("pxa-pcm-audio")));

static struct snd_soc_dai_link spitz_dai = {
	.name = "wm8750",
	.stream_name = "WM8750",
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		   SND_SOC_DAIFMT_CBC_CFC,
	.ops = &spitz_ops,
	SND_SOC_DAILINK_REG(wm8750),
};

/* spitz audio machine driver */
static struct snd_soc_card snd_soc_spitz = {
	.name = "Spitz",
	.owner = THIS_MODULE,
	.dai_link = &spitz_dai,
	.num_links = 1,

	.controls = wm8750_spitz_controls,
	.num_controls = ARRAY_SIZE(wm8750_spitz_controls),
	.dapm_widgets = wm8750_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(wm8750_dapm_widgets),
	.dapm_routes = spitz_audio_map,
	.num_dapm_routes = ARRAY_SIZE(spitz_audio_map),
	.fully_routed = true,
};

static int spitz_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &snd_soc_spitz;
	int ret;

	gpiod_mic = devm_gpiod_get(&pdev->dev, "mic", GPIOD_OUT_LOW);
	if (IS_ERR(gpiod_mic))
		return PTR_ERR(gpiod_mic);
	gpiod_mute_l = devm_gpiod_get(&pdev->dev, "mute-l", GPIOD_OUT_LOW);
	if (IS_ERR(gpiod_mute_l))
		return PTR_ERR(gpiod_mute_l);
	gpiod_mute_r = devm_gpiod_get(&pdev->dev, "mute-r", GPIOD_OUT_LOW);
	if (IS_ERR(gpiod_mute_r))
		return PTR_ERR(gpiod_mute_r);

	card->dev = &pdev->dev;

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret)
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n",
			ret);

	return ret;
}

static struct platform_driver spitz_driver = {
	.driver		= {
		.name	= "spitz-audio",
		.pm     = &snd_soc_pm_ops,
	},
	.probe		= spitz_probe,
};

module_platform_driver(spitz_driver);

MODULE_AUTHOR("Richard Purdie");
MODULE_DESCRIPTION("ALSA SoC Spitz");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:spitz-audio");
