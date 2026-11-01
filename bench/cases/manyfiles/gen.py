#!/usr/bin/env python3
"""Generate the "manyfiles" benchmark family: per-file overhead workloads.

Usage: python3 gen.py --out <data_root> --split dev|holdout

Writes into <data_root>/manyfiles/<split>/. Output is byte-identical across
runs because every generator uses a fixed seed and never reads the clock. The split directory is removed and rebuilt on
every run, so reruns never leave stale files behind.

dev layout:
  orders/order-NNNNN.xml      10000 files, 1-20 KB, <orders><order>... records
  telemetry/report-NNNNN.xml   4000 files, 1-15 KB, schema drifts v1..v4
  catalog/catalog-NNN.xml       600 files, ~200 KB, <catalog><product>...
  pages/page-NNNNN.html        8000 files, 2-10 KB, small article pages

holdout layout (different seed and shape):
  shipments/ship-NNNNN.xml     7000 files, 1-8 KB, attribute-heavy, deeper nesting
  ledger/ledger-NNNNN.xml      4000 files, schema drifts across 5 revisions
"""

import argparse
import os
import random
import shutil
from xml.sax.saxutils import escape

FAMILY = "manyfiles"

FIRST = ["Ana", "Björn", "Chloé", "Dmitri", "Eitan", "Fatima", "Günther", "Hana", "Ines", "José",
         "Kenji", "Lena", "Malik", "Noor", "Olga", "Priya", "Quentin", "Rosa", "Søren", "Tomás",
         "Uma", "Viktor", "Wen", "Ximena", "Yusuf", "Zoë", "明", "Αλέξης", "Иван", "Łukasz"]
LAST = ["Andersen", "Becker", "Costa", "Dubois", "Eriksson", "Fischer", "García", "Horvat",
        "Ivanova", "Jansen", "Kowalski", "López", "Müller", "Nakamura", "O'Brien", "Petrov",
        "Quispe", "Rossi", "Schmidt", "Tanaka", "Ünal", "Varga", "Wójcik", "Xu", "Yılmaz", "Zhang"]
WORDS = ("the of and a to in is for on with as by at from that this be are was it or an not "
         "shipment delivery package fragile customer request warehouse order invoice priority "
         "standard returned damaged replacement backorder discount loyalty gift wrap note "
         "café naïve résumé façade jalapeño smörgåsbord crème brûlée über Größe straße 東京 "
         "北京 서울 Москва Αθήνα fast slow early late quickly carefully please thanks").split()
PRODUCTS = ["Widget", "Gadget", "Sprocket", "Bracket", "Flange", "Gasket", "Valve", "Bearing",
            "Coupler", "Hinge", "Bolt", "Washer", "Spindle", "Pulley", "Lever", "Grommet"]
ADJ = ["Steel", "Brass", "Nylon", "Titanium", "Ceramic", "Carbon", "Copper", "Rubber", "Oak",
       "Aluminium", "Glass", "Bamboo"]
CITIES = [("Berlin", "DE"), ("Paris", "FR"), ("São Paulo", "BR"), ("Tōkyō", "JP"), ("Zürich", "CH"),
          ("Kraków", "PL"), ("Montréal", "CA"), ("Reykjavík", "IS"), ("Austin", "US"),
          ("Seattle", "US"), ("Lagos", "NG"), ("Mumbai", "IN"), ("Sydney", "AU"), ("Oslo", "NO")]
STATUSES = ["pending", "paid", "shipped", "delivered", "cancelled", "refunded"]
CURRENCIES = ["EUR", "USD", "GBP", "JPY", "CHF", "SEK"]


def sentence(rng, lo, hi):
    n = rng.randint(lo, hi)
    s = " ".join(rng.choice(WORDS) for _ in range(n))
    # sprinkle characters that must be escaped as entities
    if rng.random() < 0.3:
        s += rng.choice([" & co", " <urgent>", " \"quoted\"", " 5 > 3", " R&D"])
    return s[0].upper() + s[1:] + "."


def person(rng):
    return f"{rng.choice(FIRST)} {rng.choice(LAST)}"


def date(rng, y0, y1):
    return f"{rng.randint(y0, y1):04d}-{rng.randint(1, 12):02d}-{rng.randint(1, 28):02d}"


def timestamp(rng, y0, y1):
    return f"{date(rng, y0, y1)}T{rng.randint(0, 23):02d}:{rng.randint(0, 59):02d}:{rng.randint(0, 59):02d}"


def e(s):
    return escape(s)


def ea(s):
    return escape(s, {'"': "&quot;"})


def write(path, text):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)



def gen_orders(root, rng, nfiles):
    d = os.path.join(root, "orders")
    os.makedirs(d)
    oid = 100000
    for i in range(nfiles):
        n_orders = rng.choice([1, 2, 3, 4, 6, 8, 12, 18, 24])
        out = ['<?xml version="1.0" encoding="UTF-8"?>',
               f'<orders batch="B{i:05d}" region="{rng.choice(["emea", "amer", "apac"])}">']
        for k in range(n_orders):
            oid += rng.randint(1, 7)
            full = k == 0  # first record carries every optional field so inference is stable
            cur = rng.choice(CURRENCIES)
            items = []
            total = 0
            for _ in range(rng.randint(1, 6)):
                qty = rng.randint(1, 9)
                cents = rng.randint(99, 49999)
                total += qty * cents
                items.append(
                    f'      <item sku="SKU-{rng.randint(1000, 99999)}" qty="{qty}">'
                    f'<name>{e(rng.choice(ADJ) + " " + rng.choice(PRODUCTS))}</name>'
                    f'<price>{cents // 100}.{cents % 100:02d}</price></item>')
            city, cc = rng.choice(CITIES)
            out.append(f'  <order id="{oid}" status="{rng.choice(STATUSES)}">')
            out.append(f'    <customer>{e(person(rng))}</customer>')
            out.append(f'    <email>user{rng.randint(1, 999999)}@example.com</email>')
            out.append(f'    <placed>{date(rng, 2019, 2025)}</placed>')
            out.append(f'    <total>{total // 100}.{total % 100:02d}</total>')
            out.append(f'    <currency>{cur}</currency>')
            out.append(f'    <express>{"true" if rng.random() < 0.3 else "false"}</express>')
            out.append(f'    <ship_to><city>{e(city)}</city><country>{cc}</country></ship_to>')
            out.append('    <items>')
            out.extend(items)
            out.append('    </items>')
            if full or rng.random() < 0.4:
                out.append(f'    <note>{e(sentence(rng, 4, 30))}</note>')
            if full or rng.random() < 0.15:
                out.append(f'    <coupon>SAVE{rng.choice([5, 10, 15, 20])}-{rng.choice("ABCDEFGH")}</coupon>')
            out.append('  </order>')
        out.append('</orders>')
        write(os.path.join(d, f"order-{i:05d}.xml"), "\n".join(out) + "\n")



def gen_telemetry(root, rng, nfiles):
    """Each report file is one device; the reading schema drifts by file index."""
    d = os.path.join(root, "telemetry")
    os.makedirs(d)
    for i in range(nfiles):
        ver = 1 + (i * 4) // nfiles  # v1 .. v4 in contiguous runs
        dev = f"dev-{rng.randint(1, 800):04d}"
        out = ['<?xml version="1.0" encoding="UTF-8"?>',
               f'<report device="{dev}" schema="v{ver}">']
        for _ in range(rng.randint(4, 45)):
            out.append('  <reading>')
            out.append(f'    <ts>{timestamp(rng, 2023, 2025)}</ts>')
            out.append(f'    <sensor>{rng.choice(["temp", "humidity", "pressure", "co2", "lux"])}</sensor>')
            if ver == 1:
                out.append(f'    <value>{rng.randint(0, 5000)}</value>')
                out.append(f'    <uptime>{rng.randint(0, 900000)}</uptime>')
            else:
                out.append(f'    <value>{rng.uniform(-40, 5000):.3f}</value>')
            if ver >= 2:
                out.append(f'    <uptime_s>{rng.randint(0, 2**40)}</uptime_s>')
                out.append(f'    <battery>{rng.randint(0, 100)}</battery>')
            if ver >= 3:
                out.append(f'    <ok>{"true" if rng.random() < 0.9 else "false"}</ok>')
                out.append(f'    <firmware>{rng.randint(1, 4)}.{rng.randint(0, 20)}.{rng.randint(0, 9)}</firmware>')
            if ver == 4:
                city, cc = rng.choice(CITIES)
                out.append(f'    <site><name>{e(city)}</name><country>{cc}</country>'
                           f'<lat>{rng.uniform(-60, 70):.5f}</lat><lon>{rng.uniform(-170, 170):.5f}</lon></site>')
                out.append(f'    <comment>{e(sentence(rng, 2, 12))}</comment>')
            out.append('  </reading>')
        out.append('</report>')
        write(os.path.join(d, f"report-{i:05d}.xml"), "\n".join(out) + "\n")



def gen_catalog(root, rng, nfiles):
    d = os.path.join(root, "catalog")
    os.makedirs(d)
    pid = 1
    for i in range(nfiles):
        out = ['<?xml version="1.0" encoding="UTF-8"?>',
               f'<catalog vendor="V{i:03d}" season="{rng.choice(["spring", "summer", "autumn", "winter"])}">']
        size = 0
        k = 0
        while size < 200_000:
            pid += 1
            full = k == 0
            lines = [f'  <product pid="{pid}" active="{"true" if rng.random() < 0.85 else "false"}">',
                     f'    <title>{e(rng.choice(ADJ) + " " + rng.choice(PRODUCTS) + " " + str(rng.randint(1, 999)))}</title>',
                     f'    <price>{rng.randint(1, 9999)}.{rng.randint(0, 99):02d}</price>',
                     f'    <stock>{rng.randint(0, 5000)}</stock>',
                     f'    <released>{date(rng, 2010, 2025)}</released>',
                     f'    <description>{e(sentence(rng, 10, 60))}</description>']
            if full or rng.random() < 0.5:
                lines.append(f'    <weight_kg>{rng.uniform(0.01, 80):.2f}</weight_kg>')
            if full or rng.random() < 0.3:
                lines.append(f'    <supplier>{e(person(rng))}</supplier>')
            lines.append('  </product>')
            chunk = "\n".join(lines)
            size += len(chunk.encode("utf-8")) + 1
            out.append(chunk)
            k += 1
        out.append('</catalog>')
        write(os.path.join(d, f"catalog-{i:03d}.xml"), "\n".join(out) + "\n")



def gen_pages(root, rng, nfiles):
    d = os.path.join(root, "pages")
    os.makedirs(d)
    for i in range(nfiles):
        title = e(sentence(rng, 3, 9).rstrip("."))
        body = []
        for _ in range(rng.randint(2, 12)):
            para = e(sentence(rng, 15, 60))
            if rng.random() < 0.6:
                j = rng.randint(0, nfiles - 1)
                para += f' See <a href="/articles/page-{j:05d}.html">related story</a>.'
            body.append(f"      <p>{para}</p>")
        nav = "".join(f'<li><a href="/section/{s}">{s.title()}</a></li>'
                      for s in rng.sample(["news", "sport", "tech", "culture", "travel", "food"], 4))
        img = (f'      <img src="/img/{i:05d}.jpg" alt="{ea(sentence(rng, 2, 6))}" width="640" height="360">\n'
               if rng.random() < 0.5 else "")
        html = (
            "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n  <meta charset=\"utf-8\">\n"
            f"  <title>{title}</title>\n"
            f"  <meta name=\"author\" content=\"{ea(person(rng))}\">\n</head>\n<body>\n"
            f"  <nav><ul>{nav}</ul></nav>\n  <article id=\"a{i}\">\n    <h1>{title}</h1>\n"
            f"    <time datetime=\"{date(rng, 2015, 2025)}\">published</time>\n"
            f"{img}" + "\n".join(body) + "\n  </article>\n"
            "  <footer><p>&copy; Example News &mdash; all rights reserved</p></footer>\n</body>\n</html>\n")
        write(os.path.join(d, f"page-{i:05d}.html"), html)



def gen_shipments(root, rng, nfiles):
    """Attribute-heavy, deeper nesting, fewer text children than dev orders."""
    d = os.path.join(root, "shipments")
    os.makedirs(d)
    sid = 7000000
    for i in range(nfiles):
        out = ['<?xml version="1.0" encoding="UTF-8"?>',
               f'<manifest carrier="{rng.choice(["DHL", "UPS", "FedEx", "PostNL", "Yamato"])}" '
               f'day="{date(rng, 2022, 2025)}">']
        for k in range(rng.choice([1, 1, 2, 3, 5, 8, 10])):
            sid += rng.randint(1, 3)
            full = k == 0
            city, cc = rng.choice(CITIES)
            ocity, occ = rng.choice(CITIES)
            out.append(f'  <shipment id="{sid}" weight="{rng.uniform(0.1, 300):.2f}" '
                       f'insured="{"true" if rng.random() < 0.2 else "false"}" pieces="{rng.randint(1, 40)}">')
            out.append(f'    <origin code="{occ}"><address><line>{rng.randint(1, 400)} {e(rng.choice(LAST))}straße</line>'
                       f'<city>{e(ocity)}</city><geo lat="{rng.uniform(-60, 70):.4f}" lon="{rng.uniform(-170, 170):.4f}"/>'
                       f'</address></origin>')
            out.append(f'    <destination code="{cc}"><address><line>{rng.randint(1, 400)} {e(rng.choice(LAST))} Ave</line>'
                       f'<city>{e(city)}</city><geo lat="{rng.uniform(-60, 70):.4f}" lon="{rng.uniform(-170, 170):.4f}"/>'
                       f'</address><recipient>{e(person(rng))}</recipient></destination>')
            out.append('    <events>')
            for _ in range(rng.randint(1, 5)):
                out.append(f'      <event at="{timestamp(rng, 2022, 2025)}" code="{rng.choice(["PU", "IT", "AR", "OD", "DL", "EX"])}">'
                           f'{e(rng.choice(["Picked up", "In transit", "Arrived at hub", "Out for delivery", "Delivered", "Exception: address unknown"]))}'
                           '</event>')
            out.append('    </events>')
            if full or rng.random() < 0.25:
                out.append(f'    <instructions>{e(sentence(rng, 3, 20))}</instructions>')
            if full or rng.random() < 0.1:
                out.append(f'    <customs value="{rng.randint(1, 20000)}.{rng.randint(0, 99):02d}" currency="{rng.choice(CURRENCIES)}"/>')
            out.append('  </shipment>')
        out.append('</manifest>')
        write(os.path.join(d, f"ship-{i:05d}.xml"), "\n".join(out) + "\n")



def gen_ledger(root, rng, nfiles):
    """Accounting entries whose schema changes over 5 revisions, revisions interleaved in runs."""
    d = os.path.join(root, "ledger")
    os.makedirs(d)
    for i in range(nfiles):
        rev = (i // 97) % 5  # short interleaved runs instead of contiguous halves
        out = ['<?xml version="1.0" encoding="UTF-8"?>',
               f'<ledger book="GL-{rng.randint(1, 40):02d}" rev="{rev}">']
        for _ in range(rng.randint(2, 30)):
            out.append(f'  <entry no="{rng.randint(1, 10**7)}">')
            out.append(f'    <posted>{date(rng, 2018, 2025)}</posted>')
            out.append(f'    <account>{rng.randint(1000, 8999)}</account>')
            if rev in (0, 1):
                out.append(f'    <amount>{rng.randint(-50000, 50000)}</amount>')
            else:
                out.append(f'    <amount>{rng.randint(-5000000, 5000000) / 100:.2f}</amount>')
            if rev >= 1:
                out.append(f'    <memo>{e(sentence(rng, 1, 15))}</memo>')
            if rev in (2, 4):
                out.append(f'    <reconciled>{"true" if rng.random() < 0.5 else "false"}</reconciled>')
            if rev == 3:
                out.append(f'    <approver>{e(person(rng))}</approver>')
                out.append(f'    <approved_at>{timestamp(rng, 2018, 2025)}</approved_at>')
            if rev == 4:
                out.append(f'    <dims><cost_center>CC{rng.randint(10, 99)}</cost_center>'
                           f'<project>P-{rng.randint(100, 999)}</project></dims>')
            out.append('  </entry>')
        out.append('</ledger>')
        write(os.path.join(d, f"ledger-{i:05d}.xml"), "\n".join(out) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--split", required=True, choices=["dev", "holdout"])
    args = ap.parse_args()

    root = os.path.join(args.out, FAMILY, args.split)
    if os.path.isdir(root):
        shutil.rmtree(root)
    os.makedirs(root)

    if args.split == "dev":
        gen_orders(root, random.Random(1101), 10000)
        gen_telemetry(root, random.Random(1102), 4000)
        gen_catalog(root, random.Random(1103), 600)
        gen_pages(root, random.Random(1104), 8000)
    else:
        gen_shipments(root, random.Random(2201), 7000)
        gen_ledger(root, random.Random(2202), 4000)


if __name__ == "__main__":
    main()
