#!/usr/bin/env python3
"""Generate the `scalar` family: a Parquet table of XML order snippets.

Usage: python3 gen.py --out <data_root> --split dev|holdout

Writes <data_root>/scalar/<split>/orders.parquet with columns:
  id BIGINT, doc VARCHAR (one XML document per row), customer VARCHAR,
  country VARCHAR (nullable), total DOUBLE, placed DATE,
  gift BOOLEAN (nullable), tags VARCHAR (nullable, '|'-separated)

dev:     200k compact or 2-space indented snippets, flat <items>, 0.3-5 KB.
holdout: 70k larger snippets with items nested under <shipments>, per-item
         <attributes>, CDATA notes, a namespaced audit block, tab indent.

Both splits contain ~2% malformed documents. The Parquet file is written by
a small stdlib-only writer (PLAIN encoding, GZIP pages, fixed mtime) so the
output is byte-identical across runs.
"""

import argparse
import gzip
import os
import random
import struct
from datetime import date, datetime, timedelta

T_BOOLEAN, T_INT32, T_INT64, T_DOUBLE, T_BYTE_ARRAY = 0, 1, 2, 5, 6
CT_UTF8, CT_DATE = 0, 6
REQUIRED, OPTIONAL = 0, 1
CODEC_GZIP = 2
ENC_PLAIN, ENC_RLE = 0, 3

CT_I32, CT_I64, CT_BINARY, CT_LIST, CT_STRUCT = 5, 6, 8, 9, 12


def varint(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def zigzag(n):
    return (n << 1) ^ (n >> 63)


class Struct:
    """Thrift compact struct builder. Fields must be added in id order."""

    def __init__(self):
        self.buf = bytearray()
        self.last = 0

    def _hdr(self, fid, ctype):
        delta = fid - self.last
        if 0 < delta <= 15:
            self.buf.append((delta << 4) | ctype)
        else:
            self.buf.append(ctype)
            self.buf += varint(zigzag(fid))
        self.last = fid

    def i32(self, fid, v):
        self._hdr(fid, CT_I32)
        self.buf += varint(zigzag(v))
        return self

    def i64(self, fid, v):
        self._hdr(fid, CT_I64)
        self.buf += varint(zigzag(v))
        return self

    def string(self, fid, s):
        b = s.encode("utf-8")
        self._hdr(fid, CT_BINARY)
        self.buf += varint(len(b)) + b
        return self

    def struct(self, fid, st):
        self._hdr(fid, CT_STRUCT)
        self.buf += st.done()
        return self

    def lst(self, fid, etype, items):
        self._hdr(fid, CT_LIST)
        n = len(items)
        if n < 15:
            self.buf.append((n << 4) | etype)
        else:
            self.buf.append(0xF0 | etype)
            self.buf += varint(n)
        for it in items:
            if etype == CT_STRUCT:
                self.buf += it.done()
            elif etype == CT_I32:
                self.buf += varint(zigzag(it))
            elif etype == CT_BINARY:
                b = it.encode("utf-8")
                self.buf += varint(len(b)) + b
        return self

    def done(self):
        return bytes(self.buf) + b"\x00"


def encode_plain(ptype, values):
    if ptype == T_BYTE_ARRAY:
        parts = []
        for v in values:
            b = v.encode("utf-8")
            parts.append(struct.pack("<I", len(b)))
            parts.append(b)
        return b"".join(parts)
    if ptype == T_INT64:
        return struct.pack("<%dq" % len(values), *values)
    if ptype == T_INT32:
        return struct.pack("<%di" % len(values), *values)
    if ptype == T_DOUBLE:
        return struct.pack("<%dd" % len(values), *values)
    if ptype == T_BOOLEAN:
        out = bytearray((len(values) + 7) // 8)
        for i, v in enumerate(values):
            if v:
                out[i >> 3] |= 1 << (i & 7)
        return bytes(out)
    raise ValueError(ptype)


def encode_def_levels(defs):
    """RLE/bit-packed hybrid, bit width 1, as a single bit-packed run."""
    groups = (len(defs) + 7) // 8
    packed = bytearray(groups)
    for i, d in enumerate(defs):
        if d:
            packed[i >> 3] |= 1 << (i & 7)
    body = varint((groups << 1) | 1) + bytes(packed)
    return struct.pack("<I", len(body)) + body


def write_parquet(path, schema, rows, row_group_size):
    """schema: list of (name, ptype, converted_type or None, nullable)."""
    out = bytearray(b"PAR1")
    row_groups = []
    for start in range(0, len(rows), row_group_size):
        chunk = rows[start:start + row_group_size]
        cols = []
        rg_bytes = 0
        for ci, (name, ptype, _ct, nullable) in enumerate(schema):
            vals = [r[ci] for r in chunk]
            payload = b""
            if nullable:
                payload += encode_def_levels([v is not None for v in vals])
                vals = [v for v in vals if v is not None]
            payload += encode_plain(ptype, vals)
            comp = gzip.compress(payload, compresslevel=6, mtime=0)
            dph = (Struct().i32(1, len(chunk)).i32(2, ENC_PLAIN)
                   .i32(3, ENC_RLE).i32(4, ENC_RLE))
            ph = (Struct().i32(1, 0).i32(2, len(payload)).i32(3, len(comp))
                  .struct(5, dph)).done()
            offset = len(out)
            out += ph
            out += comp
            unc = len(ph) + len(payload)
            cmp_size = len(ph) + len(comp)
            rg_bytes += unc
            meta = (Struct().i32(1, ptype).lst(2, CT_I32, [ENC_PLAIN, ENC_RLE])
                    .lst(3, CT_BINARY, [name]).i32(4, CODEC_GZIP)
                    .i64(5, len(chunk)).i64(6, unc).i64(7, cmp_size)
                    .i64(9, offset))
            cols.append(Struct().i64(2, offset).struct(3, meta))
        row_groups.append(Struct().lst(1, CT_STRUCT, cols).i64(2, rg_bytes)
                          .i64(3, len(chunk)))
    schema_elems = [Struct().string(4, "schema").i32(5, len(schema))]
    for name, ptype, ct, nullable in schema:
        se = Struct().i32(1, ptype).i32(3, OPTIONAL if nullable else REQUIRED).string(4, name)
        if ct is not None:
            se.i32(6, ct)
        schema_elems.append(se)
    fmd = (Struct().i32(1, 1).lst(2, CT_STRUCT, schema_elems).i64(3, len(rows))
           .lst(4, CT_STRUCT, row_groups).string(6, "webbed-bench gen.py")).done()
    out += fmd
    out += struct.pack("<I", len(fmd))
    out += b"PAR1"
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(out)
    os.replace(tmp, path)


FIRST = ["Anna", "José", "Zoë", "Søren", "Mei", "Liam", "Øyvind", "Chloé", "Mateus", "Aiko",
         "Björn", "Fatima", "Noah", "Ines", "Kwame", "Priya", "Łukasz", "Elif", "Dmitri", "Siobhán",
         "伟", "Ngozi", "Ana-María", "Oscar", "Hana"]
LAST = ["Álvarez", "Müller", "Kierkegaard", "Chen", "O'Brien", "Nakamura", "Dubois", "Silva",
        "Kowalski", "Haddad", "Johansson", "Okafor", "Smith", "Rossi", "Yılmaz", "Novák", "李",
        "Fernández", "Ivanova", "García"]
CITIES = [("DE", "München"), ("CH", "Zürich"), ("BR", "São Paulo"), ("PL", "Kraków"),
          ("JP", "東京"), ("IS", "Reykjavík"), ("CA", "Montréal"), ("US", "Portland"),
          ("GB", "Leeds"), ("FR", "Besançon"), ("TR", "İstanbul"), ("NG", "Lagos"),
          ("IN", "Bengaluru"), ("ES", "A Coruña"), ("SE", "Göteborg")]
STREETS = ["Hauptstraße", "Rue de la Paix", "Main St", "Calle Mayor", "Ulica Długa",
           "Avenida Paulista", "Bahnhofstrasse", "Elm Road", "Kungsgatan", "Strand"]
ADJ = ["Ergonomic", "Rustic", "Compact", "Wireless", "Organic", "Stainless", "Vintage",
       "Heavy-duty", "Lightweight", "Smart", "Café-style", "Waterproof", "Handmade"]
NOUN = ["Kettle", "Desk Lamp", "Backpack", "Headphones", "Notebook", "Chair", "Teapot",
        "Bike Light", "Blender", "Thermos", "Router", "Jacket", "Pan", "Keyboard", "Umbrella"]
STATUS = ["pending", "paid", "shipped", "delivered", "cancelled", "refunded"]
CURRENCY = ["USD", "EUR", "GBP", "JPY", "CHF", "BRL"]
TIERS = ["bronze", "silver", "gold", "platinum"]
TAGS = ["priority", "fragile", "b2b", "promo", "repeat", "eco", "gift-wrap", "intl", "vip"]
CARRIERS = ["DHL", "UPS", "FedEx", "PostNord", "Correios", "Yamato", "La Poste"]
COLORS = ["red", "navy", "olive", "graphite", "crème", "sand", "teal"]
WORDS = ("please leave the parcel at the back door if nobody answers the bell we will be "
         "away until friday call before delivery the box was damaged last time so pack "
         "carefully thanks again for the quick service customer asked about warranty "
         "terms and invoice copy").split()
NOTE_SPICE = ["&amp;", "&lt;3", "&quot;asap&quot;", "&#8364;", "&#x2764;", "naïve", "→", "½",
              "R&amp;D", "&apos;s"]


def words(rng, lo, hi):
    n = rng.randint(lo, hi)
    out = []
    for _ in range(n):
        if rng.random() < 0.08:
            out.append(rng.choice(NOTE_SPICE))
        else:
            out.append(rng.choice(WORDS))
    s = " ".join(out)
    return s[0].upper() + s[1:] + "."


def raw_words(rng, lo, hi):
    n = rng.randint(lo, hi)
    s = " ".join(rng.choice(WORDS) for _ in range(n))
    return s[0].upper() + s[1:] + " <ok> & \"done\"."


def xesc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")


class Writer:
    def __init__(self, indent):
        self.indent = indent  # None for compact
        self.parts = []
        self.depth = 0

    def line(self, s):
        if self.indent is None:
            self.parts.append(s)
        else:
            self.parts.append("\n" + self.indent * self.depth + s)

    def open(self, s):
        self.line(s)
        self.depth += 1

    def close(self, s):
        self.depth -= 1
        self.line(s)

    def text(self):
        s = "".join(self.parts)
        return s.lstrip("\n")


BASE_DT = datetime(2023, 1, 1)


def gen_order(rng, oid, holdout):
    status = rng.choice(STATUS)
    cur = rng.choice(CURRENCY)
    country, city = rng.choice(CITIES)
    name = rng.choice(FIRST) + " " + rng.choice(LAST)
    placed = BASE_DT + timedelta(seconds=rng.randint(0, 2 * 365 * 86400))
    gift = None if rng.random() < 0.6 else rng.random() < 0.5
    tags = None if rng.random() < 0.35 else "|".join(sorted(rng.sample(TAGS, rng.randint(1, 4))))
    country_col = None if rng.random() < 0.07 else country

    if holdout:
        indent = "\t" if rng.random() < 0.7 else None
        n_items = max(1, int(rng.lognormvariate(1.8, 0.6)))
        n_items = min(n_items, 28)
    else:
        indent = "  " if rng.random() < 0.5 else None
        n_items = max(1, int(rng.lognormvariate(1.2, 0.8)))
        n_items = min(n_items, 40)

    w = Writer(indent)
    attrs = 'id="O-%07d" status="%s" currency="%s"' % (oid, status, cur)
    if rng.random() < 0.3:
        attrs += ' channel="%s"' % rng.choice(["web", "app", "phone", "store"])
    w.open("<order %s>" % attrs)
    if not holdout and rng.random() < 0.1:
        w.line("<!-- imported from legacy system batch %d -->" % rng.randint(1, 999))
    w.open('<customer tier="%s"%s>' % (rng.choice(TIERS),
                                       ' vip="true"' if rng.random() < 0.05 else ""))
    w.line("<name>%s</name>" % xesc(name))
    email = "%s.%d@example.%s" % (rng.choice(["anna", "jose", "mei", "liam", "kwame", "hana"]),
                                  rng.randint(1, 9999), rng.choice(["com", "org", "de", "jp"]))
    if holdout:
        w.open("<contact>")
        w.line("<email>%s</email>" % email)
        if rng.random() < 0.6:
            w.line('<phone type="%s">+%d %d</phone>' % (rng.choice(["mobile", "work"]),
                                                       rng.randint(1, 99), rng.randint(10**8, 10**9)))
        w.close("</contact>")
    elif rng.random() < 0.85:
        w.line("<email>%s</email>" % email)
    if country_col is not None or rng.random() < 0.5:
        w.open('<address country="%s">' % country)
        w.line("<street>%d %s</street>" % (rng.randint(1, 250), xesc(rng.choice(STREETS))))
        w.line("<city>%s</city>" % city)
        if rng.random() < 0.7:
            w.line("<postcode>%05d</postcode>" % rng.randint(1000, 99999))
        w.close("</address>")
    w.close("</customer>")
    w.line("<placed>%s</placed>" % placed.strftime("%Y-%m-%dT%H:%M:%SZ"))

    total = 0.0

    def item(idx):
        nonlocal total
        qty = rng.randint(1, 6) if rng.random() < 0.9 else rng.randint(7, 40)
        price = round(rng.uniform(0.5, 400.0), 2)
        total += qty * price
        sku = "%s-%05d" % (rng.choice(["KT", "LM", "BP", "HP", "NB", "CH"]), rng.randint(0, 99999))
        a = 'sku="%s" qty="%d"' % (sku, qty)
        if rng.random() < 0.1:
            a += ' backorder="true"'
        w.open("<item %s>" % a)
        w.line("<title>%s</title>" % xesc(rng.choice(ADJ) + " " + rng.choice(NOUN)))
        w.line("<price>%.2f</price>" % price)
        if rng.random() < 0.2:
            w.line('<discount kind="%s">%.2f</discount>' % (rng.choice(["coupon", "bulk", "loyalty"]),
                                                            round(price * rng.uniform(0.05, 0.3), 2)))
        if holdout:
            w.open("<attributes>")
            for _ in range(rng.randint(1, 4)):
                k = rng.choice(["color", "size", "weight_g", "material"])
                v = {"color": rng.choice(COLORS), "size": rng.choice(["S", "M", "L", "XL"]),
                     "weight_g": str(rng.randint(50, 5000)),
                     "material": rng.choice(["steel", "cotton", "bamboo", "PET"])}[k]
                w.line('<attr name="%s">%s</attr>' % (k, xesc(v)))
            w.close("</attributes>")
        if rng.random() < (0.25 if holdout else 0.12):
            w.line("<description>%s</description>" % words(rng, 8, 40 if holdout else 25))
        w.close("</item>")

    if holdout:
        w.open("<shipments>")
        remaining = n_items
        sidx = 0
        while remaining > 0:
            k = min(remaining, rng.randint(1, 8))
            remaining -= k
            sidx += 1
            w.open('<shipment id="S%d" carrier="%s">' % (sidx, rng.choice(CARRIERS)))
            w.open("<items>")
            for i in range(k):
                item(i)
            w.close("</items>")
            w.close("</shipment>")
        w.close("</shipments>")
    else:
        w.open("<items>")
        for i in range(n_items):
            item(i)
        w.close("</items>")

    w.line("<total>%.2f</total>" % total)
    if gift is not None:
        w.line("<gift>%s</gift>" % ("true" if gift else "false"))
    if tags is not None:
        w.open("<tags>")
        for t in tags.split("|"):
            w.line("<tag>%s</tag>" % t)
        w.close("</tags>")
    if rng.random() < 0.3:
        if holdout and rng.random() < 0.5:
            w.line("<notes><![CDATA[%s]]></notes>" % raw_words(rng, 10, 60))
        else:
            w.line("<notes>%s</notes>" % words(rng, 5, 50))
    if holdout and rng.random() < 0.4:
        w.open('<ext:audit xmlns:ext="urn:example:audit" ext:rev="%d">' % rng.randint(1, 30))
        for _ in range(rng.randint(1, 3)):
            w.line('<ext:event at="%s" by="%s"/>' % (
                (placed + timedelta(minutes=rng.randint(1, 5000))).strftime("%Y-%m-%dT%H:%M:%SZ"),
                rng.choice(["system", "ops", "support"])))
        w.close("</ext:audit>")
    w.close("</order>")
    doc = w.text()

    r = rng.random()
    if r < 0.006:
        doc = doc[: rng.randint(len(doc) // 3, len(doc) - 2)]          # truncated
    elif r < 0.012:
        doc = doc.replace("</customer>", "", 1)                        # missing close tag
    elif r < 0.017:
        doc = doc.replace("<total>", "<total>R&D ", 1)                  # bare ampersand
    elif r < 0.020:
        doc = doc.replace("<placed>", "<placed>&nbsp;", 1)             # undefined entity

    return (oid, doc, name, country_col, round(total, 2),
            (placed.date() - date(1970, 1, 1)).days, gift, tags)


SCHEMA = [
    ("id", T_INT64, None, False),
    ("doc", T_BYTE_ARRAY, CT_UTF8, False),
    ("customer", T_BYTE_ARRAY, CT_UTF8, False),
    ("country", T_BYTE_ARRAY, CT_UTF8, True),
    ("total", T_DOUBLE, None, False),
    ("placed", T_INT32, CT_DATE, False),
    ("gift", T_BOOLEAN, None, True),
    ("tags", T_BYTE_ARRAY, CT_UTF8, True),
]

SPLITS = {
    "dev": dict(seed=20261014, rows=200_000, holdout=False),
    "holdout": dict(seed=77_331_905, rows=70_000, holdout=True),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--split", required=True, choices=sorted(SPLITS))
    args = ap.parse_args()
    cfg = SPLITS[args.split]
    out_dir = os.path.join(args.out, "scalar", args.split)
    os.makedirs(out_dir, exist_ok=True)
    rng = random.Random(cfg["seed"])
    rows = [gen_order(rng, i, cfg["holdout"]) for i in range(cfg["rows"])]
    path = os.path.join(out_dir, "orders.parquet")
    write_parquet(path, SCHEMA, rows, row_group_size=8192)
    print("wrote %s (%d rows, %d bytes)" % (path, len(rows), os.path.getsize(path)))


if __name__ == "__main__":
    main()
