/*
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>

#define DRIVER_NAME "msc313-pwm"

#define CHANSZ		0xc
#define REG_DIV		0x0
#define REG_DUTY	0x4
#define REG_PERIOD	0x8

struct msc313e_pwm {
	__iomem void *base;
};

static const struct of_device_id msc313e_pwm_dt_ids[] = {
	{ .compatible = "mstar,msc313e-pwm" },
	{},
};

MODULE_DEVICE_TABLE(of, msc313e_pwm_dt_ids);

static int msc313e_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
		      int duty_ns, int period_ns)
{
	return 0;
};

static int msc313e_pwm_set_polarity(struct pwm_chip *chip, struct pwm_device *pwm,
			    enum pwm_polarity polarity)
{
	return 0;
}

static int msc313e_pwm_capture(struct pwm_chip *chip, struct pwm_device *pwm,
	       struct pwm_capture *result, unsigned long timeout)
		   {
	return 0;
}

static int msc313e_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	return 0;
}

static void msc313e_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm){
}

static int msc313e_apply(struct pwm_chip *chip, struct pwm_device *pwm,
		     struct pwm_state *state)
{
	return 0;
}

static void msc313e_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
			  struct pwm_state *state){
}

static const struct pwm_ops msc313e_pwm_ops = {
	.config = msc313e_pwm_config,
	.set_polarity = msc313e_pwm_set_polarity,
	.capture = msc313e_pwm_capture,
	.enable = msc313e_pwm_enable,
	.disable = msc313e_pwm_disable,
	.apply = msc313e_apply,
	.get_state = msc313e_get_state,
	.owner = THIS_MODULE
};

static int msc313e_pwm_probe(struct platform_device *pdev)
{
	int ret;
	struct msc313e_pwm *pwm;
	struct resource *res;
	struct pwm_chip *pwmchip;

	dev_info(&pdev->dev, "probe");

	pwm = devm_kzalloc(&pdev->dev, sizeof(*pwm), GFP_KERNEL);
	if (!pwm)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pwm->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pwm->base))
		return PTR_ERR(pwm->base);

	pwmchip = devm_kzalloc(&pdev->dev, sizeof(*pwmchip), GFP_KERNEL);
	if (!pwmchip)
		return -ENOMEM;

	pwmchip->dev = &pdev->dev;
	pwmchip->ops = &msc313e_pwm_ops;
	pwmchip->base = -1;
	pwmchip->npwm = 4;
	pwmchip->of_xlate = of_pwm_xlate_with_flags;
	pwmchip->of_pwm_n_cells = 3;

	ret = pwmchip_add(pwmchip);
	if(ret)
		dev_err(&pdev->dev, "failed to register pwm chip");

	return ret;
}

static int msc313e_pwm_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver msc313e_pwm_driver = {
	.probe = msc313e_pwm_probe,
	.remove = msc313e_pwm_remove,
	.driver = {
		   .name = DRIVER_NAME,
		   .of_match_table = msc313e_pwm_dt_ids,
	},
};

module_platform_driver(msc313e_pwm_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Mstar MSC313e PWM driver");
MODULE_AUTHOR("Daniel Palmer <daniel@0x0f.com>");
