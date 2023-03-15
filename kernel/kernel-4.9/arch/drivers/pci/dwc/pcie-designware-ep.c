/**
 * Synopsys DesignWare PCIe Endpoint controller driver
 *
 * Copyright (C) 2017 Texas Instruments
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/of.h>

#include "pcie-designware.h"
#include <linux/pci-epc.h>
#include <linux/pci-epf.h>

void dw_pcie_ep_linkup(struct dw_pcie_ep *ep)
{
	struct pci_epc *epc = ep->epc;

	pci_epc_linkup(epc);
}

static void dw_pcie_ep_reset_bar(struct dw_pcie *pci, enum pci_barno bar)
{
	u32 reg;

	reg = PCI_BASE_ADDRESS_0 + (4 * bar);
	dw_pcie_writel_dbi2(pci, reg, 0x0);
	dw_pcie_writel_dbi(pci, reg, 0x0);
}

static void dw_pcie_ep_write_header_regs(struct dw_pcie_ep *ep)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct pci_epf_header *hdr = &(ep->cached_hdr);

	dw_pcie_writew_dbi(pci, PCI_VENDOR_ID, hdr->vendorid);
	dw_pcie_writew_dbi(pci, PCI_DEVICE_ID, hdr->deviceid);
	dw_pcie_writeb_dbi(pci, PCI_REVISION_ID, hdr->revid);
	dw_pcie_writeb_dbi(pci, PCI_CLASS_PROG, hdr->progif_code);
	dw_pcie_writew_dbi(pci, PCI_CLASS_DEVICE,
			   hdr->subclass_code | hdr->baseclass_code << 8);
	dw_pcie_writeb_dbi(pci, PCI_CACHE_LINE_SIZE,
			   hdr->cache_line_size);
	dw_pcie_writew_dbi(pci, PCI_SUBSYSTEM_VENDOR_ID,
			   hdr->subsys_vendor_id);
	dw_pcie_writew_dbi(pci, PCI_SUBSYSTEM_ID, hdr->subsys_id);
	dw_pcie_writeb_dbi(pci, PCI_INTERRUPT_PIN,
			   hdr->interrupt_pin);

	return;
}

static int dw_pcie_ep_write_header(struct pci_epc *epc,
				   struct pci_epf_header *hdr)
{
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);

	ep->cached_hdr = *hdr;

	if (ep->hw_regs_not_available)
		return 0;

	dw_pcie_ep_write_header_regs(ep);

	return 0;
}

static int dw_pcie_ep_inbound_atu(struct dw_pcie_ep *ep, enum pci_barno bar,
				  dma_addr_t cpu_addr,
				  enum dw_pcie_as_type as_type)
{
	int ret;
	u32 free_win;
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	free_win = find_first_zero_bit(&ep->ib_window_map,
				       sizeof(ep->ib_window_map));
	if (free_win >= ep->num_ib_windows) {
		dev_err(pci->dev, "no free inbound window\n");
		return -EINVAL;
	}

	ep->cached_inbound_atus[free_win].bar = bar;
	ep->cached_inbound_atus[free_win].cpu_addr = cpu_addr;
	ep->cached_inbound_atus[free_win].as_type = as_type;
	ep->cached_bars[bar].atu_index = free_win;
	set_bit(free_win, &ep->ib_window_map);

	if (ep->hw_regs_not_available)
		return 0;

	ret = dw_pcie_prog_inbound_atu(pci, free_win, bar, cpu_addr,
				       as_type);
	if (ret < 0) {
		dev_err(pci->dev, "Failed to program IB window\n");
		return ret;
	}

	return 0;
}

static int dw_pcie_ep_outbound_atu(struct dw_pcie_ep *ep, phys_addr_t phys_addr,
				   u64 pci_addr, size_t size)
{
	u32 free_win;
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	free_win = find_first_zero_bit(&ep->ob_window_map,
				       sizeof(ep->ob_window_map));
	if (free_win >= ep->num_ob_windows) {
		dev_err(pci->dev, "no free outbound window\n");
		return -EINVAL;
	}

	ep->cached_outbound_atus[free_win].addr = phys_addr;
	ep->cached_outbound_atus[free_win].pci_addr = pci_addr;
	ep->cached_outbound_atus[free_win].size = size;
	set_bit(free_win, &ep->ob_window_map);

	if (ep->hw_regs_not_available)
		return 0;

	dw_pcie_prog_outbound_atu(pci, free_win, PCIE_ATU_TYPE_MEM,
				  phys_addr, pci_addr, size);

	return 0;
}

static void dw_pcie_ep_clear_bar_regs(struct dw_pcie_ep *ep,
				      enum pci_barno bar)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	u32 atu_index = ep->cached_bars[bar].atu_index;

	dw_pcie_ep_reset_bar(pci, bar);
	dw_pcie_disable_atu(pci, atu_index, DW_PCIE_REGION_INBOUND);
}

static void dw_pcie_ep_clear_bar(struct pci_epc *epc, enum pci_barno bar)
{
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	u32 atu_index = ep->cached_bars[bar].atu_index;

	clear_bit(atu_index, &ep->ib_window_map);

	if (ep->hw_regs_not_available)
		return;

	dw_pcie_ep_clear_bar_regs(ep, bar);
}

static void dw_pcie_ep_set_bar_regs(struct dw_pcie_ep *ep, enum pci_barno bar)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	size_t size = ep->cached_bars[bar].size;
	int flags = ep->cached_bars[bar].flags;
	u32 reg = PCI_BASE_ADDRESS_0 + (4 * bar);

	dw_pcie_writel_dbi2(pci, reg, lower_32_bits(size - 1));
	dw_pcie_writel_dbi(pci, reg, flags);

	if (flags & PCI_BASE_ADDRESS_MEM_TYPE_64) {
		dw_pcie_writel_dbi2(pci, reg + 4, upper_32_bits(size - 1));
		dw_pcie_writel_dbi(pci, reg + 4, 0);
	}
}

static int dw_pcie_ep_set_bar(struct pci_epc *epc, enum pci_barno bar,
			      dma_addr_t bar_phys, size_t size, int flags)
{
	int ret;
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	enum dw_pcie_as_type as_type;

	if (!(flags & PCI_BASE_ADDRESS_SPACE))
		as_type = DW_PCIE_AS_MEM;
	else
		as_type = DW_PCIE_AS_IO;

	ret = dw_pcie_ep_inbound_atu(ep, bar, bar_phys, as_type);
	if (ret)
		return ret;

	ep->cached_bars[bar].size = size;
	ep->cached_bars[bar].flags = flags;

	if (ep->hw_regs_not_available)
		return 0;

	dw_pcie_ep_set_bar_regs(ep, bar);

	return 0;
}

static int dw_pcie_find_index(struct dw_pcie_ep *ep, phys_addr_t addr,
			      u32 *atu_index)
{
	u32 index;

	for (index = 0; index < ep->num_ob_windows; index++) {
		if (ep->cached_outbound_atus[index].addr != addr)
			continue;
		*atu_index = index;
		return 0;
	}

	return -EINVAL;
}

static void dw_pcie_ep_unmap_addr(struct pci_epc *epc, phys_addr_t addr)
{
	int ret;
	u32 atu_index;
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	ret = dw_pcie_find_index(ep, addr, &atu_index);
	if (ret < 0)
		return;

	clear_bit(atu_index, &ep->ob_window_map);

	if (ep->hw_regs_not_available)
		return;

	dw_pcie_disable_atu(pci, atu_index, DW_PCIE_REGION_OUTBOUND);
}

static int dw_pcie_ep_map_addr(struct pci_epc *epc, phys_addr_t addr,
			       u64 pci_addr, size_t size)
{
	int ret;
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	ret = dw_pcie_ep_outbound_atu(ep, addr, pci_addr, size);
	if (ret) {
		dev_err(pci->dev, "failed to enable address\n");
		return ret;
	}

	return 0;
}

static int dw_pcie_ep_get_msi(struct pci_epc *epc)
{
	int val;
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	if (ep->hw_regs_not_available)
		val = ep->cached_msi_ctrl;
	else
		val = dw_pcie_readw_dbi(pci, MSI_MESSAGE_CONTROL);

	if (!(val & MSI_CAP_MSI_EN_MASK))
		return -EINVAL;

	val = (val & MSI_CAP_MME_MASK) >> MSI_CAP_MME_SHIFT;
	return val;
}

static int dw_pcie_ep_set_msi(struct pci_epc *epc, u8 encode_int)
{
	int val;
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	val = (encode_int << MSI_CAP_MMC_SHIFT);
	ep->cached_msi_ctrl = val;

	if (ep->hw_regs_not_available)
		return 0;

	dw_pcie_writew_dbi(pci, MSI_MESSAGE_CONTROL, val);

	return 0;
}

void dw_pcie_set_regs_available(struct dw_pcie *pci)
{
	struct dw_pcie_ep *ep = &(pci->ep);
	int i;

	ep->hw_regs_not_available = false;

	dw_pcie_ep_write_header_regs(ep);
	for_each_set_bit(i, &ep->ib_window_map, ep->num_ib_windows) {
		dw_pcie_prog_inbound_atu(pci, i,
			ep->cached_inbound_atus[i].bar,
			ep->cached_inbound_atus[i].cpu_addr,
			ep->cached_inbound_atus[i].as_type);
		dw_pcie_ep_set_bar_regs(ep, ep->cached_inbound_atus[i].bar);
	}
	for_each_set_bit(i, &ep->ob_window_map, ep->num_ob_windows)
		dw_pcie_prog_outbound_atu(pci, i, PCIE_ATU_TYPE_MEM,
			ep->cached_outbound_atus[i].addr,
			ep->cached_outbound_atus[i].pci_addr,
			ep->cached_outbound_atus[i].size);
	dw_pcie_dbi_ro_wr_en(pci);
	dw_pcie_writew_dbi(pci, MSI_MESSAGE_CONTROL, ep->cached_msi_ctrl);
	dw_pcie_dbi_ro_wr_dis(pci);
}

static int dw_pcie_ep_raise_irq(struct pci_epc *epc,
				enum pci_epc_irq_type type, u8 interrupt_num)
{
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);

	if (!ep->ops->raise_irq)
		return -EINVAL;

	if (ep->hw_regs_not_available)
		return -EAGAIN;

	return ep->ops->raise_irq(ep, type, interrupt_num);
}

static void dw_pcie_ep_stop(struct pci_epc *epc)
{
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	if (!pci->ops->stop_link)
		return;

	pci->ops->stop_link(pci);
}

static int dw_pcie_ep_start(struct pci_epc *epc)
{
	struct dw_pcie_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	if (!pci->ops->start_link)
		return -EINVAL;

	return pci->ops->start_link(pci);
}

static const struct pci_epc_ops epc_ops = {
	.write_header		= dw_pcie_ep_write_header,
	.set_bar		= dw_pcie_ep_set_bar,
	.clear_bar		= dw_pcie_ep_clear_bar,
	.map_addr		= dw_pcie_ep_map_addr,
	.unmap_addr		= dw_pcie_ep_unmap_addr,
	.set_msi		= dw_pcie_ep_set_msi,
	.get_msi		= dw_pcie_ep_get_msi,
	.raise_irq		= dw_pcie_ep_raise_irq,
	.start			= dw_pcie_ep_start,
	.stop			= dw_pcie_ep_stop,
};

void dw_pcie_ep_exit(struct dw_pcie_ep *ep)
{
	struct pci_epc *epc = ep->epc;

	pci_epc_mem_exit(epc);
}
EXPORT_SYMBOL(dw_pcie_ep_exit);

int dw_pcie_ep_init(struct dw_pcie_ep *ep)
{
	int ret;
	struct pci_epc *epc;
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct device *dev = pci->dev;
	struct device_node *np = dev->of_node;

	if (!pci->dbi_base || !pci->dbi_base2) {
		dev_err(dev, "dbi_base/deb_base2 is not populated\n");
		return -EINVAL;
	}
	if (pci->iatu_unroll_enabled && !pci->atu_base) {
		dev_err(dev, "atu_base is not populated\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(np, "num-ib-windows", &ep->num_ib_windows);
	if (ret < 0) {
		dev_err(dev, "unable to read *num-ib-windows* property\n");
		return ret;
	}
	ep->cached_inbound_atus = devm_kzalloc(dev,
		sizeof(ep->cached_inbound_atus[0]) * ep->num_ib_windows,
		GFP_KERNEL);
	if (!ep->cached_inbound_atus)
		return -ENOMEM;

	ret = of_property_read_u32(np, "num-ob-windows", &ep->num_ob_windows);
	if (ret < 0) {
		dev_err(dev, "unable to read *num-ob-windows* property\n");
		return ret;
	}
	ep->cached_outbound_atus = devm_kzalloc(dev,
		sizeof(ep->cached_outbound_atus[0]) * ep->num_ob_windows,
		GFP_KERNEL);
	if (!ep->cached_outbound_atus)
		return -ENOMEM;

	if (ep->ops->ep_init)
		ep->ops->ep_init(ep);

	epc = devm_pci_epc_create(dev, &epc_ops);
	if (IS_ERR(epc)) {
		dev_err(dev, "failed to create epc device\n");
		return PTR_ERR(epc);
	}

	ret = of_property_read_u8(np, "max-functions", &epc->max_functions);
	if (ret < 0)
		epc->max_functions = 1;

	ret = __pci_epc_mem_init(epc, ep->phys_base, ep->addr_size,
				 ep->page_size);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize address space\n");
		return ret;
	}

	ep->msi_mem = pci_epc_mem_alloc_addr(epc, &ep->msi_mem_phys,
					     epc->mem->page_size);
	if (!ep->msi_mem) {
		dev_err(dev, "Failed to reserve memory for MSI/MSI-X\n");
		return -ENOMEM;
	}

	ep->epc = epc;
	epc_set_drvdata(epc, ep);
	if (ep->ops->ep_setup)
		ep->ops->ep_setup(ep);
	else
		dw_pcie_setup(pci);

	return 0;
}
EXPORT_SYMBOL(dw_pcie_ep_init);
