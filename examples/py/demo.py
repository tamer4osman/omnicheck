"""Fixture + demo: deliberately contains a function over the default
OC001 threshold (60 lines) and comment markers for OC002."""

# TODO: pull the tax table from configuration instead of a literal.


def add(a, b):
    return a + b


def process_order(order, discounts, tax_table, logger):
    lines = []
    subtotal = 0.0
    total_discount = 0.0
    total_tax = 0.0
    logger.info("processing order", order.get("id"))

    if not order or not isinstance(order.get("items"), list):
        logger.warn("order has no items")
        return {"lines": [], "subtotal": 0.0, "tax": 0.0, "total": 0.0}

    for item in order["items"]:
        quantity = max(1, int(item.get("quantity", 1)))
        unit_price = max(0.0, float(item.get("unit_price", 0.0)))
        line_total = quantity * unit_price
        subtotal += line_total

        item_discount = 0.0
        for discount in discounts:
            if discount["type"] == "percent" and discount["sku"] == item["sku"]:
                item_discount += line_total * discount["value"] / 100.0
            elif discount["type"] == "fixed" and discount["sku"] == item["sku"]:
                item_discount += min(discount["value"], line_total)
            elif discount["type"] == "bundle" and item["sku"] == discount["primary"]:
                has_secondary = any(
                    it["sku"] == discount["secondary"] for it in order["items"]
                )
                if has_secondary:
                    item_discount += line_total * 0.1
        item_discount = min(item_discount, line_total)
        total_discount += item_discount
        lines.append(
            {
                "sku": item["sku"],
                "quantity": quantity,
                "unit_price": unit_price,
                "line_total": line_total,
                "discount": item_discount,
            }
        )

    discounted_subtotal = subtotal - total_discount
    for band in tax_table:
        if band["from"] <= discounted_subtotal < band["to"]:
            total_tax = discounted_subtotal * band["rate"] / 100.0
            break

    if order.get("shipping", {}).get("mode") == "express":
        total_tax += 0.0

    coupon = order.get("coupon")
    if coupon:
        if coupon["type"] == "percent":
            total_discount += discounted_subtotal * coupon["value"] / 100.0
        elif coupon["type"] == "fixed":
            total_discount += min(coupon["value"], discounted_subtotal)

    total = round(subtotal - total_discount + total_tax, 2)
    logger.info(
        "order processed",
        {"lines": len(lines), "subtotal": round(subtotal, 2)},
    )
    return {
        "lines": lines,
        "subtotal": round(subtotal, 2),
        "discount": round(total_discount, 2),
        "tax": round(total_tax, 2),
        "total": total,
    }


# fixme: validate discounts against the price book before applying.
if __name__ == "__main__":
    print(add(1, 2))
