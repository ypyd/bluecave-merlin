/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  Copyright (C) 2012 John Crispin <blogic@openwrt.org>
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/of_platform.h>
#include <linux/reset.h>

#include <lantiq_soc.h>

#define XRX200_GPHY_FW_ALIGN	(16 * 1024)
/* GPHY releated */
extern void gsw_reg_w32(uint32_t val, uint32_t reg_off);
struct reset_control *phy_rst;

#define RCU_RD_GPHY2_XRX500 BIT(29)
#define RCU_RD_GPHY3_XRX500 BIT(28)
#define RCU_RD_GPHY4_XRX500 BIT(26)
#define RCU_RD_GPHY5_XRX500 BIT(25)
#define RCU_RD_GPHYF_XRX500 BIT(31)

/* xRX500 gphy (GSW-L) registers */
#define GPHY2_LBADR_XRX500     0x0228
#define GPHY2_MBADR_XRX500     0x022C
#define GPHY3_LBADR_XRX500     0x0238
#define GPHY3_MBADR_XRX500     0x023c
#define GPHY4_LBADR_XRX500     0x0248
#define GPHY4_MBADR_XRX500     0x024C
#define GPHY5_LBADR_XRX500     0x0258
#define GPHY5_MBADR_XRX500     0x025C
#define GPHYF_LBADR_XRX500     0x0268
#define GPHYF_MBADR_XRX500     0x026C


/* reset / boot a gphy */
static struct ltq_xrx500_gphy_reset {
	       u32 rd;
	       u32 addr;
	} xrx500_gphy[] = {
		       {RCU_RD_GPHY2_XRX500, GPHY2_LBADR_XRX500},
		       {RCU_RD_GPHY3_XRX500, GPHY3_LBADR_XRX500},
		       {RCU_RD_GPHY4_XRX500, GPHY4_LBADR_XRX500},
		       {RCU_RD_GPHY5_XRX500, GPHY5_LBADR_XRX500},
		       {RCU_RD_GPHYF_XRX500, GPHYF_LBADR_XRX500},
		};
/* reset and boot a gphy. these phys only exist on xrx200 SoC */
int xrx500_gphy_boot(struct device *dev, unsigned int id, dma_addr_t dev_addr)
{
	       if (of_machine_is_compatible("lantiq,grx500")) {
		               if (id > 4) {
			                       dev_info(dev, "%u is an invalid gphy id\n", id);
			                       return -EINVAL;
			               }
						   reset_control_assert(phy_rst);
			               gsw_reg_w32((dev_addr & 0xFFFF), xrx500_gphy[id].addr);
			               gsw_reg_w32(((dev_addr >> 16) & 0xFFFF), (xrx500_gphy[id].addr + 4));
						   reset_control_deassert(phy_rst);
			               dev_info(dev, "booting GPHY%u firmware at %X for GRX500\n", id, dev_addr);
			       }
			       return 0;
}
		
static dma_addr_t xway_gphy_load(struct platform_device *pdev)
{
	const struct firmware *fw;
	dma_addr_t dev_addr = 0;
	const char *fw_name;
	void *fw_addr;
	size_t size;

	if (of_property_read_string(pdev->dev.of_node, "firmware", &fw_name)) {
		dev_err(&pdev->dev, "failed to load firmware filename\n");
		return 0;
	}

	dev_info(&pdev->dev, "requesting %s\n", fw_name);
	if (request_firmware(&fw, fw_name, &pdev->dev)) {
		dev_err(&pdev->dev, "failed to load firmware: %s\n", fw_name);
		return 0;
	}

	/*
	 * GPHY cores need the firmware code in a persistent and contiguous
	 * memory area with a 16 kB boundary aligned start address
	 */
	size = fw->size + XRX200_GPHY_FW_ALIGN;

	fw_addr = dma_alloc_coherent(&pdev->dev, size, &dev_addr, GFP_KERNEL);
	if (fw_addr) {
		fw_addr = PTR_ALIGN(fw_addr, XRX200_GPHY_FW_ALIGN);
		dev_addr = ALIGN(dev_addr, XRX200_GPHY_FW_ALIGN);
		memcpy(fw_addr, fw->data, fw->size);
	} else {
		dev_err(&pdev->dev, "failed to alloc firmware memory\n");
	}

	release_firmware(fw);
	return dev_addr;
}

static int xway_phy_fw_probe(struct platform_device *pdev)
{
	dma_addr_t fw_addr;
	struct property *pp;
	unsigned char *phyids;
	int i, ret = 0;
	char phy_str[7];

	fw_addr = xway_gphy_load(pdev);
	if (!fw_addr)
		return -EINVAL;
	
	pp = of_find_property(pdev->dev.of_node, "phy_id", NULL);
	if (!pp)
		return -ENOENT;

	phyids = pp->value;
	for (i = 0; i < pp->length && !ret; i++) {
		sprintf(phy_str, "phy%d", phyids[i]);
		phy_rst = devm_reset_control_get(&pdev->dev, phy_str);
		if (IS_ERR(phy_rst))
			return PTR_ERR(phy_rst);
		ret = xrx500_gphy_boot(&pdev->dev, phyids[i], fw_addr);
	if (!ret)
		mdelay(100);

	}
	return ret;
}

static const struct of_device_id xway_phy_match[] = {
	{ .compatible = "lantiq,phy-xrx500" },
	{},
};
MODULE_DEVICE_TABLE(of, xway_phy_match);

static struct platform_driver xway_phy_driver = {
	.probe = xway_phy_fw_probe,
	.driver = {
		.name = "phy-xrx500",
		.owner = THIS_MODULE,
		.of_match_table = xway_phy_match,
	},
};

module_platform_driver(xway_phy_driver);

MODULE_AUTHOR("John Crispin <blogic@openwrt.org>");
MODULE_DESCRIPTION("Lantiq XRX200 PHY Firmware Loader");
MODULE_LICENSE("GPL");
