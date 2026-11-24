def tangled(items, flag):
    total = 0
    if flag and len(items) > 1:
        total += 1
    if flag and len(items) > 2:
        total += 2
    if flag and len(items) > 3:
        total += 3
    if flag and len(items) > 4:
        total += 4
    if flag and len(items) > 5:
        total += 5
    if flag and len(items) > 6:
        total += 6
    if flag and len(items) > 7:
        total += 7
    if flag and len(items) > 8:
        total += 8
    for item1 in items:
        total += item1 % 1 or 0
    for item2 in items:
        total += item2 % 2 or 0
    for item3 in items:
        total += item3 % 3 or 0
    for item4 in items:
        total += item4 % 4 or 0
    try:
        total += int(flag)
    except (TypeError, ValueError):
        total -= 1
    except Exception:
        total -= 2
    result = total if total > 0 else -total
    other = 1 if flag else 0
    return total + result + other
