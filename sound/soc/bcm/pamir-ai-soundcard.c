/*
 * Driver for Pamir AI soundcard for Raspberry Pi.
 *
 * Author: UtsavBalar <utsavbalar1231@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <sound/soc-dapm.h>

#define PAMIR_RATE_MIN_HZ 32000
#define PAMIR_RATE_MAX_HZ 96000
#define DRV_NAME "pamir-ai-soundcard"

struct pamir_ai_priv {
	struct platform_device *pdev;
};

static int pamir_ai_component_probe(struct snd_soc_component *component)
{
	dev_info(component->dev, "Pamir AI component probe\n");
	return 0;
}

static void pamir_ai_component_remove(struct snd_soc_component *component)
{
	dev_info(component->dev, "Pamir AI component remove\n");
}

static const struct snd_soc_dapm_widget pamir_ai_dapm_widgets[] = {
	SND_SOC_DAPM_OUTPUT("Speaker"),
	SND_SOC_DAPM_INPUT("Mic"),
};

static const struct snd_soc_dapm_route pamir_ai_dapm_routes[] = {
	{ "Speaker", NULL, "HiFi Playback" },
	{ "HiFi Capture", NULL, "Mic" },
};

static const struct snd_soc_component_driver pamir_ai_component_driver = {
	.probe = pamir_ai_component_probe,
	.remove = pamir_ai_component_remove,
	.dapm_widgets = pamir_ai_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(pamir_ai_dapm_widgets),
	.dapm_routes = pamir_ai_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(pamir_ai_dapm_routes),
};

static int pamir_ai_daiops_trigger(struct snd_pcm_substream *substream, int cmd,
				   struct snd_soc_dai *dai)
{
	dev_info(dai->dev, "Trigger - CMD %d, Stream: %s\n", cmd,
		 substream->stream == SNDRV_PCM_STREAM_PLAYBACK ? "Playback" :
								  "Capture");
	dev_info(dai->dev, "Playback Active: %d, Capture Active: %d\n",
		 dai->stream[SNDRV_PCM_STREAM_PLAYBACK].active,
		 dai->stream[SNDRV_PCM_STREAM_CAPTURE].active);
	return 0;
}

static int pamir_ai_daiops_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params,
				     struct snd_soc_dai *dai)
{
	return 0;
}

static const struct snd_soc_dai_ops pamir_ai_dai_ops = {
	.trigger = pamir_ai_daiops_trigger,
	.hw_params = pamir_ai_daiops_hw_params,
};

static struct snd_soc_dai_driver pamir_ai_dai = {
	.name = "pamir-ai-hifi",
	.capture = { .stream_name = "HiFi Capture",
		     .channels_min = 2,
		     .channels_max = 2,
		     .rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000,
		     .formats = SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S24_LE |
				SNDRV_PCM_FMTBIT_S32_LE },
	.playback = { .stream_name = "HiFi Playback",
		      .channels_min = 2,
		      .channels_max = 2,
		      .rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000,
		      .formats = SNDRV_PCM_FMTBIT_S16_LE |
				 SNDRV_PCM_FMTBIT_S24_LE |
				 SNDRV_PCM_FMTBIT_S32_LE },
	.ops = &pamir_ai_dai_ops,
	.symmetric_rate = 1,
};

#ifdef CONFIG_OF
static const struct of_device_id pamir_ai_ids[] = {
	{
		.compatible = "pamir-ai,soundcard",
	},
	{},
};
MODULE_DEVICE_TABLE(of, pamir_ai_ids);
#endif

static int pamir_ai_platform_probe(struct platform_device *pdev)
{
	struct pamir_ai_priv *pamir;
	int ret;

	dev_info(&pdev->dev, "Probing Pamir AI Soundcard driver\n");

	pamir = devm_kzalloc(&pdev->dev, sizeof(*pamir), GFP_KERNEL);
	if (!pamir) {
		dev_err(&pdev->dev,
			"Failed to allocate memory for driver data\n");
		return -ENOMEM;
	}

	pamir->pdev = pdev;
	dev_set_drvdata(&pdev->dev, pamir);

	ret = snd_soc_register_component(&pdev->dev, &pamir_ai_component_driver,
					 &pamir_ai_dai, 1);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register component: %d\n", ret);
		return ret;
	}

	dev_info(&pdev->dev, "Pamir AI Soundcard driver initialized\n");
	return 0;
}

static void pamir_ai_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_component(&pdev->dev);
}

static struct platform_driver pamir_ai_driver = {
    .driver = {
        .name = DRV_NAME,
        .of_match_table = of_match_ptr(pamir_ai_ids),
    },
    .probe = pamir_ai_platform_probe,
    .remove = pamir_ai_platform_remove,
};

module_platform_driver(pamir_ai_driver);

MODULE_DESCRIPTION("Pamir AI SoundCard driver");
MODULE_AUTHOR("UtsavBalar <utsavbalar1231@gmail.com>");
MODULE_LICENSE("GPL v2");
