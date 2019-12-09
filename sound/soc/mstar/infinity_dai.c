/*------------------------------------------------------------------------------
 *   Copyright (c) 2008 MStar Semiconductor, Inc.  All rights reserved.
 *------------------------------------------------------------------------------*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/clk.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>

//#include <mach/hardware.h>
#include <asm/io.h>
#include <asm/dma.h>

#include "infinity_pcm.h"
#include "infinity_dai.h"
#include "infinity_codec.h"

static int infinity_soc_dai_ops_startup(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
  return 0;
}

static void infinity_soc_dai_ops_shutdown(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
}

static int infinity_soc_dai_ops_trigger(struct snd_pcm_substream *substream, int cmd, struct snd_soc_dai *dai)
{
  return 0;
}

static int infinity_soc_dai_ops_prepare(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
  return 0;
}

static int infinity_soc_dai_ops_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
  return 0;
}

static int infinity_soc_dai_ops_hw_free(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
  return 0;
}

static int infinity_soc_dai_ops_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
  return 0;
}

static int infinity_soc_dai_ops_set_clkdiv(struct snd_soc_dai *dai, int div_id, int div)
{
  return 0;
}

static int infinity_soc_dai_ops_set_sysclk(struct snd_soc_dai *dai, int clk_id, unsigned int freq, int dir)
{
  return 0;
}

static struct snd_soc_dai_ops infinity_soc_cpu_dai_ops =
{
  .set_sysclk = infinity_soc_dai_ops_set_sysclk,
  .set_pll    = NULL,
  .set_clkdiv = infinity_soc_dai_ops_set_clkdiv,
  .set_fmt    = infinity_soc_dai_ops_set_fmt,
  .startup		= infinity_soc_dai_ops_startup,
  .shutdown		= infinity_soc_dai_ops_shutdown,
  .trigger		= infinity_soc_dai_ops_trigger,
  .prepare      = infinity_soc_dai_ops_prepare,
  .hw_params	= infinity_soc_dai_ops_hw_params,
  .hw_free      = infinity_soc_dai_ops_hw_free,
};


static int infinity_soc_dai_probe(struct snd_soc_dai *dai)
{
  return 0;
}

static int infinity_soc_dai_remove(struct snd_soc_dai *dai)
{
  return 0;
}

static int infinity_soc_dai_suspend(struct snd_soc_dai *dai)
{
  return 0;
}

static int infinity_soc_dai_resume(struct snd_soc_dai *dai)
{
  return 0;
}

struct snd_soc_dai_driver infinity_soc_cpu_dai_drv =
{
	.probe	  = infinity_soc_dai_probe,
	.remove	  = infinity_soc_dai_remove,
	.suspend  = infinity_soc_dai_suspend,
	.resume	  = infinity_soc_dai_resume,
	.playback =
	{
		.channels_min	= 1,
		.channels_max	= 2,
		.rates		= SNDRV_PCM_RATE_8000_48000,
		.formats	= SNDRV_PCM_FMTBIT_S16_LE
	},
	.capture =
	{
		.channels_min	= 1,
		.channels_max	= 2,
		.rates		= SNDRV_PCM_RATE_8000_48000,
		.formats	= SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &infinity_soc_cpu_dai_ops,
};

static const struct snd_soc_component_driver infinity_soc_component = {
	.name		= "mstar-bach",
};

static int infinity_cpu_dai_probe(struct platform_device *pdev)
{
  int ret;

  ret = snd_soc_register_component(&pdev->dev, &infinity_soc_component, &infinity_soc_cpu_dai_drv, 1);
  if (ret)
  {
    return ret;
  }

  return 0;
}

static int infinity_cpu_dai_remove(struct platform_device *pdev)
{
  snd_soc_unregister_component(&pdev->dev);
  return 0;
}


static struct platform_driver infinity_cpu_dai_driver =
{
  .probe = infinity_cpu_dai_probe,
  .remove = (infinity_cpu_dai_remove),
  .driver = {
    .name = "infinity-cpu-dai",
    .owner = THIS_MODULE,
  },
};

static struct platform_device *infinity_cpu_dai_device = NULL;
static int __init infinity_cpu_dai_init(void)
{

  int ret = 0;

  struct device_node *np;

  infinity_cpu_dai_device = platform_device_alloc("infinity-cpu-dai", -1);
  if (!infinity_cpu_dai_device)
  {

    return -ENOMEM;
  }

  np = of_find_compatible_node(NULL, NULL, "mstar,infinity-audio");
  if (np)
  {
	  infinity_cpu_dai_device->dev.of_node = of_node_get(np);
    of_node_put(np);
  }

  ret = platform_device_add(infinity_cpu_dai_device);
  if (ret)
  {
    platform_device_put(infinity_cpu_dai_device);
  }

  ret = platform_driver_register(&infinity_cpu_dai_driver);
  if (ret)
  {
    platform_device_unregister(infinity_cpu_dai_device);
  }

  return ret;
}

static void __exit infinity_cpu_dai_exit(void)
{
  platform_device_unregister(infinity_cpu_dai_device);
  platform_driver_unregister(&infinity_cpu_dai_driver);
}

module_init(infinity_cpu_dai_init);
module_exit(infinity_cpu_dai_exit);


/* Module information */
MODULE_AUTHOR("Trevor Wu, trevor.wu@mstarsemi.com");
MODULE_DESCRIPTION("Infinity Bach Audio ALSA SoC Dai");
