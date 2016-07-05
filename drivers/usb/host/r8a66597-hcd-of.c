#include <linux/module.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/usb/r8a66597.h>

static const struct r8a66597_platdata r8a66597_pdata = {
	.endian = 0,
	.on_chip = 1,
	.xtal = R8A66597_PLATDATA_XTAL_48MHZ,
};

static int r8a66597_of_probe(struct platform_device *pdev) {
	struct platform_device_info* platinfo;
	static struct resource* resources;
	static struct resource *base, *irq;
	int ret;

	platinfo = devm_kzalloc(&pdev->dev, sizeof(*platinfo), GFP_KERNEL);
	if (!platinfo) {
		ret = -ENOMEM;
		goto cleanup;
	}

	resources = devm_kzalloc(&pdev->dev, sizeof(struct resource) * 2,
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

	irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (IS_ERR(irq)) {
		ret = -ENODEV;
		dev_err(&pdev->dev, "couldn't get irq\n");
		goto cleanup;
	}

	memcpy(resources, base, sizeof(struct resource));
	memcpy(resources + 1, irq, sizeof(struct resource));

	platinfo->name = "r8a66597_hcd";
	platinfo->id = 0;
	platinfo->data = &r8a66597_pdata;
	platinfo->size_data = sizeof(r8a66597_pdata);
	platinfo->res = resources;
	platinfo->num_res = 2;

	platform_device_register_full(platinfo);

	return 0;
	cleanup: //
	if (platinfo)
		devm_kfree(&pdev->dev, platinfo);
	if (resources)
		devm_kfree(&pdev->dev, resources);
	return ret;
}

static int r8a66597_of_remove(struct platform_device *pdev) {
	return 0;
}

static const struct of_device_id of_r8a66597_match[] = {
	{
		.compatible = "renesas,r8a66597-hcd",
	}
	,
	{},
};

MODULE_DEVICE_TABLE(of, of_r8a66597_match);

static struct platform_driver r8a66597_of_driver = {
	.probe = r8a66597_of_probe,
	.remove = r8a66597_of_remove,
	.driver = {
		.name = "r8a66597-hcd-of",
		.owner = THIS_MODULE,
		.of_match_table = of_r8a66597_match,
	},
};

module_platform_driver (r8a66597_of_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Daniel Palmer <daniel@0x0f.com>");
MODULE_DESCRIPTION("Device tree wrapper for r8a66597 HCD");
