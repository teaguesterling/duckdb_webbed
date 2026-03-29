#!/usr/bin/env python3
"""Generate a large XML file for SAX streaming stress testing.

Usage: python3 generate_stress_test.py [num_records] [output_path]

Default: 1,000,000 records (~382MB) written to test/xml/sax_stress_test.xml

The generated file contains varied record content designed to exercise:
- UTF-8 characters (Cyrillic, Japanese, Turkish, French)
- XML special characters (&, <, >, ")
- Multiple data types (INTEGER, DOUBLE, BOOLEAN, DATE, VARCHAR)
- Attributes on record elements (id, sku)
- 10 rotating categories including one with & (food & beverage)
"""

import os
import sys


def generate(num_records=1000000, output_path=None):
    if output_path is None:
        output_path = os.path.join(os.path.dirname(__file__), "sax_stress_test.xml")

    categories = [
        "electronics",
        "clothing",
        "food & beverage",
        "toys",
        "books & media",
        "health",
        "automotive",
        "sports",
        "home & garden",
        "office",
    ]

    special_names = [
        'Standard Item',
        'Item with "quotes"',
        "Item with <brackets>",
        'Item with & ampersand',
        'Таймаут продукт',
        '日本語の製品',
        'Ürün açıklaması',
        'Produit français',
    ]

    with open(output_path, "w", encoding="utf-8") as f:
        f.write('<?xml version="1.0" encoding="UTF-8"?>\n')
        f.write("<catalog>\n")

        for i in range(1, num_records + 1):
            cat = categories[i % len(categories)]
            name_base = special_names[i % len(special_names)]
            price = round(0.01 + (i % 99999) * 0.01, 2)
            quantity = i % 10000
            month = (i % 12) + 1
            day = (i % 28) + 1
            active = "true" if i % 3 != 0 else "false"
            rating = round(1.0 + (i % 50) * 0.1, 1)

            name = f"{name_base} #{i}"
            name = name.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")
            cat_escaped = cat.replace("&", "&amp;")
            desc = f"This is the description for product {i}. It contains enough text to make each record larger."

            f.write(f'  <product id="{i}" sku="SKU-{i:07d}">\n')
            f.write(f"    <name>{name}</name>\n")
            f.write(f"    <category>{cat_escaped}</category>\n")
            f.write(f"    <price>{price}</price>\n")
            f.write(f"    <quantity>{quantity}</quantity>\n")
            f.write(f"    <date>2024-{month:02d}-{day:02d}</date>\n")
            f.write(f"    <active>{active}</active>\n")
            f.write(f"    <rating>{rating}</rating>\n")
            f.write(f"    <description>{desc}</description>\n")
            f.write("  </product>\n")

            if i % 200000 == 0:
                print(f"  Generated {i}/{num_records} records...", file=sys.stderr)

        f.write("</catalog>\n")

    size_mb = os.path.getsize(output_path) / (1024 * 1024)
    print(f"Generated {output_path}: {size_mb:.1f} MB, {num_records} records")


if __name__ == "__main__":
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 1000000
    p = sys.argv[2] if len(sys.argv) > 2 else None
    generate(n, p)
