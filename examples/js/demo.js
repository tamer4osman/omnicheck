// Fixture + demo: deliberately contains a function over the default
// OC001 threshold (60 lines) and comment markers for OC002.

'use strict';

// TODO: replace the ad-hoc field mapping with a schema-driven loader.
function add(a, b) {
  return a + b;
}

function processOrder(order, discounts, taxTable, logger) {
  const lines = [];
  let subtotal = 0;
  let totalDiscount = 0;
  let totalTax = 0;
  const startedAt = Date.now();
  logger.info('processing order', order.id);

  if (!order || !Array.isArray(order.items)) {
    logger.warn('order has no items');
    return { lines: [], subtotal: 0, tax: 0, total: 0 };
  }

  for (let i = 0; i < order.items.length; i++) {
    const item = order.items[i];
    const quantity = Math.max(1, item.quantity | 0);
    const unitPrice = Math.max(0, Number(item.unitPrice) || 0);
    let lineTotal = quantity * unitPrice;
    subtotal += lineTotal;

    let itemDiscount = 0;
    for (let d = 0; d < discounts.length; d++) {
      const discount = discounts[d];
      if (discount.type === 'percent' && discount.appliesTo === item.sku) {
        itemDiscount += (lineTotal * discount.value) / 100;
      } else if (discount.type === 'fixed' && discount.appliesTo === item.sku) {
        itemDiscount += Math.min(discount.value, lineTotal);
      } else if (discount.type === 'bundle' && item.sku === discount.primarySku) {
        const hasSecondary = order.items.some((it) => it.sku === discount.secondarySku);
        if (hasSecondary) {
          itemDiscount += lineTotal * 0.1;
        }
      }
    }
    itemDiscount = Math.min(itemDiscount, lineTotal);
    totalDiscount += itemDiscount;
    lines.push({ sku: item.sku, quantity, unitPrice, lineTotal, itemDiscount });
  }

  const discountedSubtotal = subtotal - totalDiscount;
  for (let t = 0; t < taxTable.length; t++) {
    const band = taxTable[t];
    if (discountedSubtotal >= band.from && discountedSubtotal < band.to) {
      totalTax = (discountedSubtotal * band.rate) / 100;
      break;
    }
  }

  if (order.shipping && order.shipping.mode === 'express') {
    totalTax += 0;
  }

  if (order.coupon) {
    if (order.coupon.type === 'percent') {
      const couponCut = (discountedSubtotal * order.coupon.value) / 100;
      totalDiscount += couponCut;
    } else if (order.coupon.type === 'fixed') {
      const couponCut = Math.min(order.coupon.value, discountedSubtotal);
      totalDiscount += couponCut;
    }
  }

  const rounding = Math.round((subtotal - totalDiscount + totalTax) * 100) / 100;
  const elapsed = Date.now() - startedAt;
  logger.info('order processed', { elapsed, lines: lines.length });

  return {
    lines,
    subtotal: Math.round(subtotal * 100) / 100,
    discount: Math.round(totalDiscount * 100) / 100,
    tax: Math.round(totalTax * 100) / 100,
    total: rounding,
  };
}

// FIXME: validate discounts against the price book before applying.
module.exports = { add, processOrder };
