#include <linux/module.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_platform.h>

static int sh_rtc_of_probe(struct platform_device *pdev) {
	struct platform_device_info* platinfo;
	struct platform_device* platdevice;
	static struct resource* resources;
	static struct resource *base, *irqperiod, *irqcarry, *irqalarm;
	int ret;

	dev_info(&pdev->dev, "creating sh-rtc from device tree\n");

	platinfo = devm_kzalloc(&pdev->dev, sizeof(*platinfo), GFP_KERNEL);
	if (!platinfo) {
		ret = -ENOMEM;
		goto cleanup;
	}

	resources = devm_kzalloc(&pdev->dev, sizeof(struct resource) * 4,
			GFP_KERNEL);
	if (!resources) {
		ret = -ENOMEM;
		goto cleanup;
	}

	base = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (IS_ERR(base)) {
		ret = -ENODEV;
		dev_err(&pdev->dev, "couldn't get reg\n");
		goto cleanup;
	}

	irqperiod = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (IS_ERR(irqperiod)) {
		ret = -ENODEV;
		dev_err(&pdev->dev, "couldn't get period irq\n");
		goto cleanup;
	}

	irqcarry = platform_get_resource(pdev, IORESOURCE_IRQ, 1);
	if (IS_ERR(irqcarry)) {
		ret = -ENODEV;
		dev_err(&pdev->dev, "couldn't get carry irq\n");
		goto cleanup;
	}

	irqalarm = platform_get_resource(pdev, IORESOURCE_IRQ, 2);
	if (IS_ERR(irqalarm)) {
		ret = -ENODEV;
		dev_err(&pdev->dev, "couldn't get alarm irq\n");
		goto cleanup;
	}

	memcpy(resources, base, sizeof(struct resource));
	memcpy(resources + 1, irqperiod, sizeof(struct resource));
	memcpy(resources + 2, irqcarry, sizeof(struct resource));
	memcpy(resources + 3, irqalarm, sizeof(struct resource));

	platinfo->name = "sh-rtc";
	platinfo->id = -1;
	platinfo->res = resources;
	platinfo->num_res = 4;

	platdevice = platform_device_register_full(platinfo);
	if (IS_ERR(platdevice)) {
		dev_err(&pdev->dev, "failed to register platform device\n");
		goto cleanup;
	}
	else
		dev_info(&pdev->dev, "registered\n");

	return 0;
	cleanup: //
	if (platinfo)
		devm_kfree(&pdev->dev, platinfo);
	if (resources)
		devm_kfree(&pdev->dev, resources);
	return ret;
}

static int sh_rtc_of_remove(struct platform_device *pdev) {
	return 0;
}

static const struct of_device_id of_sh_rtc_match[] =
{
		{ .compatible = "renesas,sh-rtc", },
		{ },
};

MODULE_DEVICE_TABLE( of, of_sh_rtc_match);

static struct platform_driver sh_rtc_of_driver =
{
		.probe = sh_rtc_of_probe,
		.remove = sh_rtc_of_remove,
		.driver =
		{
				.name = "sh-rtc-of",
				.owner = THIS_MODULE,
				.of_match_table = of_sh_rtc_match,
		},
};

module_platform_driver( sh_rtc_of_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Daniel Palmer <daniel@0x0f.com>");
MODULE_DESCRIPTION("Device tree wrapper for sh-rtc");
