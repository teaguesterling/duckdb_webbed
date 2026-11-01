#!/usr/bin/env python3
"""Generate deeply nested XML for the `nested` schema-inference benchmark family.

Usage: python3 gen.py --out <data_root> --split dev|holdout

Writes <data_root>/nested/<split>/{catalog,org,claims}.xml. Output is a pure
function of the split, so reruns are byte-identical. The holdout split uses a
different seed and a different shape (extra nesting level, other optional-field
mix, longer text, attributes moved onto different elements).
"""

import argparse
import os
import random
from xml.sax.saxutils import escape

PROFILES = {
    "dev": {
        "seed": 20261014,
        "catalog_products": 6500,
        "org_regions": 16,
        "claims": 25000,
        "attachments": (1, 1),
        "extra_level": False,
        "p_warranty": 0.35,
        "p_recall": 0.01,
        "p_bundle": 0.15,
        "reviews": (0, 6),
        "variants": (1, 5),
        "text_words": (6, 30),
        "dims_as_attrs": False,
    },
    "holdout": {
        "seed": 7741,
        "catalog_products": 5000,
        "org_regions": 13,
        "claims": 27000,
        "attachments": (1, 3),
        "extra_level": True,
        "p_warranty": 0.7,
        "p_recall": 0.05,
        "p_bundle": 0.02,
        "reviews": (0, 3),
        "variants": (1, 9),
        "text_words": (15, 60),
        "dims_as_attrs": True,
    },
}

WORDS = (
    "alpha robust compact durable sleek ergonomic wireless modular premium "
    "lightweight waterproof certified recycled organic vintage handmade "
    "industrial portable smart quiet efficient adjustable reinforced "
    "café naïve façade jalapeño über résumé smörgåsbord crème brûlée "
    "Zürich São Paulo Kraków Ærø 東京 北京 서울 Москва Αθήνα "
    "battery cable sensor housing bracket valve gasket lens panel motor"
).split()

FIRST = "Ana Bjørn Chloé Dmitri Eun-ji Farah Gonzalo Hiroshi Ingrid José Kwame Léa Mateus Nadia Oğuz Priya Quentin Rania Søren Tomás".split()
LAST = "Abara Bergström Castaño Dubois Eriksen Fujimoto García Hájek Iwu Jovanović Kowalski López Müller Nakamura O'Brien Pérez Quispe Rossi Schröder Tanaka".split()
CITIES = ["Zürich", "São Paulo", "Kraków", "Montréal", "Reykjavík", "Düsseldorf", "Málaga", "Tromsø", "Oaxaca", "Seattle", "Lagos", "Hà Nội"]
COUNTRIES = ["CH", "BR", "PL", "CA", "IS", "DE", "ES", "NO", "MX", "US", "NG", "VN"]
CURRENCIES = ["USD", "EUR", "GBP", "JPY", "CHF", "BRL"]
UNITS = ["mm", "cm", "in", "kg", "g", "lb", "W", "V", "mAh"]


class Out:
    def __init__(self, path):
        self.f = open(path, "w", encoding="utf-8", newline="\n")
        self.buf = []

    def w(self, s):
        self.buf.append(s)
        if len(self.buf) > 4096:
            self.flush()

    def flush(self):
        self.f.write("".join(self.buf))
        self.buf = []

    def close(self):
        self.flush()
        self.f.close()


def text(rng, lo, hi):
    words = [rng.choice(WORDS) for _ in range(rng.randint(lo, hi))]
    s = " ".join(words)
    # Entities and quotes appear naturally in free text.
    if rng.random() < 0.2:
        s += ' & "more" <details>'
    return escape(s, {'"': "&quot;"})


def name(rng):
    return escape(f"{rng.choice(FIRST)} {rng.choice(LAST)}", {"'": "&apos;"})


def date(rng, y0=2015, y1=2026):
    return f"{rng.randint(y0, y1):04d}-{rng.randint(1, 12):02d}-{rng.randint(1, 28):02d}"


def ts(rng):
    return f"{date(rng, 2022, 2026)}T{rng.randint(0, 23):02d}:{rng.randint(0, 59):02d}:{rng.randint(0, 59):02d}"


def money(rng, lo, hi):
    return f"{rng.uniform(lo, hi):.2f}"


def boolean(rng):
    return "true" if rng.random() < 0.5 else "false"


def gen_catalog(path, p, rng):
    o = Out(path)
    o.w('<?xml version="1.0" encoding="UTF-8"?>\n<catalog version="4.2" generated="2026-10-14T00:00:00">\n')
    for i in range(p["catalog_products"]):
        sku = f"SKU-{i:07d}"
        o.w(f'  <product sku="{sku}" status="{rng.choice(["active", "active", "active", "discontinued", "draft"])}">\n')
        o.w(f"    <name>{text(rng, 2, 6)}</name>\n")
        o.w(f"    <released>{date(rng)}</released>\n")
        o.w(f"    <in_stock>{boolean(rng)}</in_stock>\n")
        o.w(f'    <price currency="{rng.choice(CURRENCIES)}">{money(rng, 1, 4000)}</price>\n')
        o.w(f"    <description>{text(rng, *p['text_words'])}</description>\n")
        # Category path: nested 4 deep below product.
        o.w("    <taxonomy><department>")
        o.w(f"<label>{rng.choice(WORDS)}</label><category><label>{rng.choice(WORDS)}</label>")
        o.w(f'<subcategory code="{rng.randint(100, 999)}"><label>{rng.choice(WORDS)}</label>')
        if p["extra_level"]:
            o.w(f"<facet><label>{rng.choice(WORDS)}</label><weight>{rng.uniform(0, 1):.3f}</weight></facet>")
        o.w("</subcategory></category></department></taxonomy>\n")
        # specs > section > group > attribute > value: depth 6-7 under catalog.
        o.w("    <specs>\n")
        for s in range(rng.randint(1, 3)):
            o.w(f'      <section name="{rng.choice(["physical", "electrical", "compliance", "packaging"])}">\n')
            for g in range(rng.randint(1, 3)):
                o.w(f"        <group><title>{rng.choice(WORDS)}</title>\n")
                for a in range(rng.randint(1, 4)):
                    unit = rng.choice(UNITS)
                    if p["dims_as_attrs"]:
                        o.w(f'          <attribute key="{rng.choice(WORDS)}" unit="{unit}" min="{rng.uniform(0, 50):.1f}" max="{rng.uniform(50, 500):.1f}">')
                        o.w(f"<value>{rng.uniform(0, 500):.2f}</value>")
                    else:
                        o.w(f'          <attribute key="{rng.choice(WORDS)}">')
                        o.w(f'<value unit="{unit}">{rng.uniform(0, 500):.2f}</value>')
                        o.w(f"<range><min>{rng.uniform(0, 50):.1f}</min><max>{rng.uniform(50, 500):.1f}</max></range>")
                    if rng.random() < 0.2:
                        o.w(f'<note lang="{rng.choice(["en", "de", "ja", "pt"])}">{text(rng, 3, 10)}</note>')
                    if p["extra_level"] and rng.random() < 0.5:
                        o.w(f"<tolerance><abs>{rng.uniform(0, 1):.4f}</abs><source><standard>ISO {rng.randint(1000, 99999)}</standard><year>{rng.randint(1990, 2025)}</year></source></tolerance>")
                    o.w("</attribute>\n")
                o.w("        </group>\n")
            o.w("      </section>\n")
        o.w("    </specs>\n")
        # variants > variant > pricing > tier > discount: list of structs, deep.
        o.w("    <variants>\n")
        for v in range(rng.randint(*p["variants"])):
            o.w(f'      <variant id="{sku}-{v}">')
            o.w(f"<color>{rng.choice(['red', 'blue', 'graphite', 'sand', 'ivory', 'teal'])}</color>")
            if rng.random() < 0.6:
                o.w(f"<size>{rng.choice(['XS', 'S', 'M', 'L', 'XL'])}</size>")
            o.w("<pricing>")
            for t in range(rng.randint(1, 3)):
                o.w(f'<tier min_qty="{10 ** t}"><unit_price>{money(rng, 1, 900)}</unit_price>')
                if rng.random() < 0.4:
                    o.w(f"<discount><percent>{rng.randint(1, 40)}</percent><valid_until>{date(rng, 2026, 2028)}</valid_until></discount>")
                o.w("</tier>")
            o.w("</pricing>")
            o.w(f"<stock><warehouse code=\"{rng.choice(COUNTRIES)}\">{rng.randint(0, 5000)}</warehouse></stock>")
            o.w("</variant>\n")
        o.w("    </variants>\n")
        # Heterogeneous media siblings.
        o.w("    <media>")
        for m in range(rng.randint(0, 5)):
            k = rng.random()
            if k < 0.6:
                o.w(f'<image width="{rng.choice([640, 1024, 2048])}" height="{rng.choice([480, 768, 1536])}">https://cdn.example.com/{sku}/{m}.jpg</image>')
            elif k < 0.85:
                o.w(f'<video duration="{rng.randint(5, 600)}"><url>https://cdn.example.com/{sku}/{m}.mp4</url><caption>{text(rng, 2, 8)}</caption></video>')
            else:
                o.w(f'<document type="pdf"><title>{text(rng, 2, 5)}</title><pages>{rng.randint(1, 80)}</pages></document>')
        o.w("</media>\n")
        # reviews: list of structs with nested author/address.
        nrev = rng.randint(*p["reviews"])
        if nrev:
            o.w("    <reviews>\n")
            for r in range(nrev):
                o.w(f'      <review rating="{rng.randint(1, 5)}" verified="{boolean(rng)}">')
                o.w(f"<author><name>{name(rng)}</name><location><city>{rng.choice(CITIES)}</city><country>{rng.choice(COUNTRIES)}</country></location></author>")
                o.w(f"<posted>{ts(rng)}</posted><body>{text(rng, *p['text_words'])}</body>")
                if rng.random() < 0.15:
                    o.w(f"<response><from>support</from><posted>{ts(rng)}</posted><body>{text(rng, 4, 12)}</body></response>")
                o.w("</review>\n")
            o.w("    </reviews>\n")
        if rng.random() < p["p_warranty"]:
            o.w(f"    <warranty><years>{rng.randint(1, 5)}</years><provider><name>{name(rng)}</name><contact><email>support{rng.randint(1, 99)}@example.com</email><phone>+1-555-{rng.randint(1000, 9999)}</phone></contact></provider></warranty>\n")
        if rng.random() < p["p_bundle"]:
            o.w("    <bundle>")
            for b in range(rng.randint(1, 4)):
                o.w(f'<feature sku="SKU-{rng.randint(0, 9999999):07d}"><qty>{rng.randint(1, 4)}</qty></feature>')
            o.w("</bundle>\n")
        if rng.random() < p["p_recall"]:
            o.w(f"    <recall><issued>{date(rng, 2024, 2026)}</issued><severity>{rng.choice(['low', 'medium', 'high'])}</severity><notice>{text(rng, 10, 25)}</notice></recall>\n")
        o.w("  </product>\n")
    o.w("</catalog>\n")
    o.close()


def gen_member(o, rng, p, depth_pad):
    o.w(f'<member id="{rng.randint(100000, 999999)}" fte="{rng.choice(["1.0", "0.5", "0.8"])}">')
    o.w(f"<name>{name(rng)}</name><title>{rng.choice(['Engineer', 'Senior Engineer', 'Analyst', 'Manager', 'Designer', 'Técnico'])}</title>")
    o.w(f"<hired>{date(rng, 2005, 2026)}</hired><remote>{boolean(rng)}</remote>")
    if rng.random() < 0.7:
        o.w(f"<contact><email>u{rng.randint(1, 999999)}@corp.example</email>")
        if rng.random() < 0.5:
            o.w(f"<address><street>{rng.randint(1, 999)} {rng.choice(WORDS)} St</street><city>{rng.choice(CITIES)}</city>")
            if depth_pad:
                o.w(f"<geo><lat>{rng.uniform(-60, 70):.5f}</lat><lon>{rng.uniform(-180, 180):.5f}</lon></geo>")
            o.w("</address>")
        o.w("</contact>")
    if rng.random() < 0.3:
        o.w("<skills>")
        for _ in range(rng.randint(1, 5)):
            o.w(f'<skill level="{rng.randint(1, 5)}">{rng.choice(WORDS)}</skill>')
        o.w("</skills>")
    if rng.random() < 0.1:
        o.w(f"<leave><start>{date(rng, 2026, 2026)}</start><kind>{rng.choice(['parental', 'sabbatical', 'medical'])}</kind></leave>")
    o.w("</member>")


def gen_org(path, p, rng):
    o = Out(path)
    o.w('<?xml version="1.0" encoding="UTF-8"?>\n<company name="Acme Hölding AG">\n')
    dept_id = 0
    for r in range(p["org_regions"]):
        o.w(f'  <region code="R{r}"><name>{rng.choice(CITIES)}</name>\n')
        for d in range(rng.randint(3, 5)):
            o.w(f'    <division id="D{r}{d}"><name>{text(rng, 1, 3)}</name>\n')
            for _ in range(rng.randint(25, 45)):
                dept_id += 1
                o.w(f'      <department id="{dept_id}" cost_center="CC{rng.randint(1000, 9999)}">')
                o.w(f"<name>{text(rng, 1, 4)}</name><budget currency=\"{rng.choice(CURRENCIES)}\">{money(rng, 1e4, 5e6)}</budget>")
                o.w(f"<founded>{date(rng, 1990, 2025)}</founded>")
                o.w(f"<head><name>{name(rng)}</name><since>{date(rng, 2010, 2026)}</since></head>")
                o.w("<teams>")
                for t in range(rng.randint(1, 5)):
                    o.w(f'<team code="T{dept_id}-{t}"><name>{rng.choice(WORDS)}</name>')
                    if rng.random() < 0.3:
                        o.w(f"<charter>{text(rng, *p['text_words'])}</charter>")
                    o.w("<members>")
                    for _ in range(rng.randint(1, 8)):
                        gen_member(o, rng, p, p["extra_level"])
                    o.w("</members>")
                    if rng.random() < 0.25:
                        o.w("<projects>")
                        for _ in range(rng.randint(1, 3)):
                            o.w(f'<project status="{rng.choice(["planned", "active", "done"])}"><title>{text(rng, 2, 5)}</title><milestones>')
                            for _ in range(rng.randint(1, 3)):
                                o.w(f"<milestone><due>{date(rng, 2025, 2027)}</due><done>{boolean(rng)}</done></milestone>")
                            o.w("</milestones></project>")
                        o.w("</projects>")
                    o.w("</team>")
                o.w("</teams></department>\n")
            o.w("    </division>\n")
        o.w("  </region>\n")
    o.w("</company>\n")
    o.close()


def gen_claims(path, p, rng):
    o = Out(path)
    o.w('<?xml version="1.0" encoding="UTF-8"?>\n<claims_export source="clearinghouse" xmlns:x="urn:example:claims">\n')
    for i in range(p["claims"]):
        if i % 1000 == 0:
            if i:
                o.w("  </batch>\n")
            o.w(f'  <batch seq="{i // 1000}">\n')
        o.w(f'    <claim id="C{i:08d}" type="{rng.choice(["professional", "institutional", "dental"])}">')
        o.w(f"<received>{ts(rng)}</received>")
        o.w(f'<patient><member_id>M{rng.randint(10 ** 8, 10 ** 9 - 1)}</member_id><name>{name(rng)}</name><dob>{date(rng, 1935, 2024)}</dob>')
        o.w(f"<coverage><plan><code>P{rng.randint(100, 999)}</code><tier>{rng.choice(['bronze', 'silver', 'gold'])}</tier>")
        if p["extra_level"]:
            o.w(f"<network><name>{rng.choice(WORDS)}</name><region><state>{rng.choice(COUNTRIES)}</state></region></network>")
        o.w(f"</plan><active>{boolean(rng)}</active></coverage></patient>")
        o.w(f'<provider npi="{rng.randint(10 ** 9, 10 ** 10 - 1)}"><name>{name(rng)}</name><address><city>{rng.choice(CITIES)}</city><country>{rng.choice(COUNTRIES)}</country></address></provider>')
        o.w("<lines>")
        for ln in range(rng.randint(1, 6)):
            o.w(f'<line n="{ln + 1}"><procedure system="CPT">{rng.randint(10000, 99999)}</procedure>')
            o.w(f'<charge currency="USD">{money(rng, 5, 9000)}</charge><units>{rng.randint(1, 10)}</units>')
            o.w("<diagnoses>")
            for _ in range(rng.randint(1, 3)):
                o.w(f"<dx>{rng.choice('ABCDEFGHJKLMNRSTZ')}{rng.randint(0, 99):02d}.{rng.randint(0, 9)}</dx>")
            o.w("</diagnoses>")
            if rng.random() < 0.3:
                o.w("<adjustments>")
                for _ in range(rng.randint(1, 2)):
                    o.w(f'<adjustment group="{rng.choice(["CO", "PR", "OA"])}"><reason>{rng.randint(1, 250)}</reason><amount>{money(rng, 0, 500)}</amount></adjustment>')
                o.w("</adjustments>")
            o.w("</line>")
        o.w("</lines>")
        if rng.random() < 0.2:
            o.w("<attachments>")
            for _ in range(rng.randint(*p["attachments"])):
                o.w(f"<attachment kind=\"{rng.choice(['note', 'note', 'scan', 'eob'])}\">{text(rng, *p['text_words'])}</attachment>")
            o.w("</attachments>")
        if rng.random() < 0.05:
            o.w(f"<appeal><filed>{date(rng, 2025, 2026)}</filed><level>{rng.randint(1, 3)}</level><decision><outcome>{rng.choice(['upheld', 'overturned', 'pending'])}</outcome></decision></appeal>")
        o.w("</claim>\n")
    o.w("  </batch>\n</claims_export>\n")
    o.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--split", required=True, choices=sorted(PROFILES))
    args = ap.parse_args()
    p = PROFILES[args.split]
    d = os.path.join(args.out, "nested", args.split)
    os.makedirs(d, exist_ok=True)
    # One independent RNG per file, so changing one generator never shifts another file.
    for fname, fn, salt in (("catalog.xml", gen_catalog, 1), ("org.xml", gen_org, 2), ("claims.xml", gen_claims, 3)):
        path = os.path.join(d, fname)
        tmp = path + ".tmp"
        fn(tmp, p, random.Random(p["seed"] * 10 + salt))
        os.replace(tmp, path)


if __name__ == "__main__":
    main()
