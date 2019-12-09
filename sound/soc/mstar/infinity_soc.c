/*------------------------------------------------------------------------------
 *   Copyright (c) 2008 MStar Semiconductor, Inc.  All rights reserved.
 *------------------------------------------------------------------------------*/
//------------------------------------------------------------------------------
//  Include files
//------------------------------------------------------------------------------
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/jack.h>

#include "infinity_pcm.h"
#include "infinity_codec.h"
#include "infinity_dai.h"
#include "infinity.h"

static int infinity_soc_dai_link_init(struct snd_soc_pcm_runtime *rtd) {
	return 0;
}

static int infinity_soc_dai_link_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params) {
	return 0;
}

static int infinity_soc_card_probe(struct snd_soc_card *card) {
	return 0;
}

static int infinity_soc_card_suspend_pre(struct snd_soc_card *card) {
	int i;

	struct snd_soc_dapm_widget *w;

#if 0
	for (i = 0; i < card->num_rtd; i++) {
		struct snd_soc_dai *codec_dai = card->rtd[i].codec_dai;

		if (card->rtd[i].dai_link->ignore_suspend)
			continue;

		w = codec_dai->playback_widget;
		if (w->active) {
			AUD_PRINTF(TRACE_LEVEL,
					"snd_soc_dapm_stream_event(): stop PLAYBACK %d\n",
					i);
			snd_soc_dapm_stream_event(&card->rtd[i],
					SNDRV_PCM_STREAM_PLAYBACK,
					SND_SOC_DAPM_STREAM_STOP);

			stream_playback_active |= 1 << i;

		}
		w = codec_dai->capture_widget;
		if (w->active) {
			AUD_PRINTF(TRACE_LEVEL,
					"snd_soc_dapm_stream_event(): stop CAPTURE %d\n",
					i);
			snd_soc_dapm_stream_event(&card->rtd[i],
					SNDRV_PCM_STREAM_CAPTURE,
					SND_SOC_DAPM_STREAM_STOP);

			stream_capture_active |= 1 << i;

		}
	}

#endif

#if 1
	snd_soc_dapm_disable_pin(&card->dapm, "DMARD");
	snd_soc_dapm_disable_pin(&card->dapm, "LINEIN");
	snd_soc_dapm_sync(&card->dapm);
#endif

	return 0;
}

static int infinity_soc_card_suspend_post(struct snd_soc_card *card) {
	return 0;

}

static int infinity_soc_card_resume_pre(struct snd_soc_card *card) {
	return 0;

}

static int infinity_soc_card_resume_post(struct snd_soc_card *card) {
	int i;

#if 1
	snd_soc_dapm_enable_pin(&card->dapm, "DMARD");
	snd_soc_dapm_enable_pin(&card->dapm, "LINEIN");
	snd_soc_dapm_sync(&card->dapm);
#endif

#if 0
	for (i = 0; i < card->num_rtd; i++) {
		//struct snd_soc_dai *codec_dai = card->rtd[i].codec_dai;

		if (stream_playback_active & (1 << i)) {
			snd_soc_dapm_stream_event(&card->rtd[i],
					SNDRV_PCM_STREAM_PLAYBACK,
					SND_SOC_DAPM_STREAM_START);
			stream_playback_active &= ~(1 << i);

		}

		if (stream_capture_active & (1 << i)) {
			snd_soc_dapm_stream_event(&card->rtd[i],
					SNDRV_PCM_STREAM_CAPTURE,
					SND_SOC_DAPM_STREAM_START);
			stream_capture_active &= ~(1 << i);
		}
	}
#endif

	return 0;

}

static struct snd_soc_ops infinity_soc_ops = {
	.hw_params = infinity_soc_dai_link_hw_params,
};

static struct snd_soc_dai_link_component infinity_soc_cpus[] = {
	{
		.name = "infinity-cpu-dai",
	}
};

static struct snd_soc_dai_link_component infinity_soc_codecs[] = {
	{
		.name = "infinity-codec",
		.dai_name = "infinity-codec-dai-main",
	}
};

static struct snd_soc_dai_link_component infinity_soc_platforms[] = {
	{
		.name = "infinity-platform",
	}
};

static struct snd_soc_dai_link infinity_soc_dais[] = {
	{
		.name = "Infinity Soc Dai Link",
		.stream_name = "msb2501_dai_stream",
		.cpus = infinity_soc_cpus,
		.num_cpus = ARRAY_SIZE(infinity_soc_cpus),
		.codecs = infinity_soc_codecs,
		.num_codecs = ARRAY_SIZE(infinity_soc_codecs),
		.platforms = infinity_soc_platforms,
		.num_platforms = ARRAY_SIZE(infinity_soc_platforms),
		.init = infinity_soc_dai_link_init,
		.ops = &infinity_soc_ops,
	},
};

static struct snd_soc_card infinity_soc_card = {
		.name = "infinity_snd_machine",
		.owner = THIS_MODULE,
		.dai_link = infinity_soc_dais,
		.num_links = ARRAY_SIZE(infinity_soc_dais),
		.probe = infinity_soc_card_probe,
		.suspend_pre = infinity_soc_card_suspend_pre,
		.suspend_post = infinity_soc_card_suspend_post,
		.resume_pre = infinity_soc_card_resume_pre,
		.resume_post = infinity_soc_card_resume_post,
};

static int infinity_audio_probe(struct platform_device *pdev) {
	//int ret = 0;

	//struct resource *res_mem;
	//struct resource *res_irq;
	//struct device_node *np = pdev->dev.of_node;



	/*
	 ret = of_property_read_u32(np, "debug", &val);
	 if (ret == 0)
	 priv->mclk_fs = val;

	 if (of_get_property(np, "fiq-merged", NULL) != NULL)
	 {
	 pr_info(" ms_irq_of_init->fiq-merged !\n");
	 mic->fiq_merged = TRUE;

	 } else
	 mic->fiq_merged = FALSE;


	 //res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	 BACH_BASE_ADDR = (unsigned int)of_iomap(np, 0);
	 if (BACH_BASE_ADDR)
	 {
	 //BACH_BASE_ADDR = (unsigned int)res_mem->start;
	 AUD_PRINTF(TRACE_LEVEL, "Get resource IORESOURCE_MEM and IO mapping = 0x%x\n", (unsigned int)BACH_BASE_ADDR);
	 }
	 else
	 return -EINVAL;
	 */

	infinity_soc_card.dev = &pdev->dev;

	//return devm_snd_soc_register_card(&pdev->dev, &infinity_soc_card);

	return snd_soc_register_card(&infinity_soc_card);
}

int infinity_audio_remove(struct platform_device *pdev) {
	return snd_soc_unregister_card(&infinity_soc_card);
}

static const struct of_device_id infinity_audio_of_match[] = {
		{ .compatible = "mstar,snd-infinity", },
		{ },
};
MODULE_DEVICE_TABLE(of, infinity_audio_of_match);

static struct platform_driver infinity_audio = {
	.driver = {
		.name = "infinity-audio",
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = infinity_audio_of_match,
	},
	.probe = infinity_audio_probe,
	.remove = infinity_audio_remove,
};

module_platform_driver(infinity_audio);

MODULE_AUTHOR("Roger Lai, roger.lai@mstarsemi.com");
MODULE_DESCRIPTION("iNfinity Bach Audio ASLA SoC Machine");
