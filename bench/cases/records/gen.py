#!/usr/bin/env python3
"""Deterministic generator for the "records" benchmark family.

Large single XML files of flat-to-moderately-nested repeating records:
an order export, a product catalog, a service event log, weather-station
readings, and a large payment-transaction stream.

Usage: python3 gen.py --out <data_root> --split dev|holdout

Files land in <data_root>/records/<split>/. Every file is written to a
temporary name and renamed into place, so reruns overwrite cleanly and an
interrupted run never leaves a truncated file behind. Output is
byte-identical across runs: seeds are fixed, and the output doesn't depend on the clock or hash order.
"""

import argparse
import os
import random
from datetime import date, datetime, timedelta

FAMILY = "records"

FIRST_NAMES = [
    "Anna", "Ben", "Chloé", "Dmitri", "Élodie", "Farah", "Gustav", "Hana",
    "Ingrid", "José", "Kaito", "Lena", "Mateo", "Nadia", "Oskar", "Priya",
    "Quentin", "Rosa", "Søren", "Tomás", "Ulla", "Văn", "Wiktor", "Xiu",
    "Yusuf", "Zoë", "Łucja", "Ömer", "Ñusta", "Björn",
]
LAST_NAMES = [
    "Müller", "García", "O'Brien", "Nguyễn", "Smith", "Kowalski", "Rossi",
    "Dubois", "Johansson", "Tanaka", "Novák", "Andersen", "Silva", "Petrov",
    "Okafor", "Fernández", "Schäfer", "Lindqvist", "Yılmaz", "Chen",
]
COUNTRIES = ["DE", "FR", "ES", "IT", "NL", "SE", "PL", "PT", "IE", "AT", "DK", "CZ", "JP", "US", "GB"]
CITIES = [
    "Berlin", "München", "Paris", "Lyon", "Madrid", "Sevilla", "Roma", "Milano",
    "Amsterdam", "Göteborg", "Kraków", "Lisboa", "Dublin", "Wien", "København",
    "Praha", "東京", "Zürich", "Málaga", "Łódź",
]
STREETS = ["Hauptstraße", "Rue de la Paix", "Calle Mayor", "Via Roma", "Kerkstraat", "Storgatan", "ul. Długa", "Main St"]
WORDS = (
    "fast delivery great quality packaging damaged replacement requested please "
    "leave at door gift wrap fragile handle with care backorder partial shipment "
    "customer called twice address corrected weekend delivery invoice required "
    "café naïve résumé façade jalapeño smörgåsbord crème brûlée über"
).split()
PRODUCT_NOUNS = [
    "Kettle", "Backpack", "Headphones", "Lamp", "Chair", "Notebook", "Mug", "Jacket",
    "Blender", "Router", "Tent", "Drill", "Monitor", "Keyboard", "Teapot", "Sneakers",
]
PRODUCT_ADJ = ["Ultra", "Classic", "Pro", "Eco", "Compact", "Deluxe", "Nordic", "Urban", "Retro", "Smart"]
BRANDS = ["Acme", "Nordwind", "Kōgei", "Bellavita", "Stahlwerk", "Lumière", "Hygge&Co", "Tropicana"]
SERVICES = ["auth", "billing", "search", "checkout", "inventory", "notify", "gateway", "profile"]
LEVELS = ["DEBUG", "INFO", "INFO", "INFO", "INFO", "WARN", "ERROR"]
METHODS = ["GET", "GET", "GET", "POST", "PUT", "DELETE"]
PATHS = ["/api/v2/orders", "/api/v2/cart", "/login", "/search", "/api/v2/users/me", "/health", "/checkout/confirm"]
MESSAGES = [
    "request completed",
    "cache miss for key user:{n}",
    "slow query detected (> {n} ms)",
    "retrying upstream call, attempt {n}",
    "token refreshed for session {n}",
    "payload rejected: field \"qty\" < 0",
    "connection reset by peer & retried",
    "rate limit 80% reached for tenant {n}",
    "Ünicode path normalised: /café/{n}",
]


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def esc_attr(s):
    return esc(s).replace('"', "&quot;")


def sentence(r, lo, hi):
    return " ".join(r.choice(WORDS) for _ in range(r.randint(lo, hi)))


def person(r):
    return r.choice(FIRST_NAMES) + " " + r.choice(LAST_NAMES)


def email_for(name, n):
    local = "".join(c for c in name.lower() if c.isascii() and c.isalpha())
    return "%s.%d@example.%s" % (local or "user", n, "com" if n % 3 else "org")


class AtomicWriter:
    """Buffered text writer that renames into place on close."""

    def __init__(self, path):
        self.path = path
        self.tmp = path + ".tmp"
        self.f = open(self.tmp, "w", encoding="utf-8", newline="\n")
        self.buf = []
        self.size = 0

    def write(self, s):
        self.buf.append(s)
        self.size += len(s)
        if self.size > 1 << 20:
            self.flush()

    def flush(self):
        self.f.write("".join(self.buf))
        self.buf = []
        self.size = 0

    def close(self):
        self.flush()
        self.f.close()
        os.replace(self.tmp, self.path)


def gen_orders_dev(path, seed, n):
    """Order export: attributes on the record, nested customer/shipping, item list."""
    r = random.Random(seed)
    w = AtomicWriter(path)
    w.write('<?xml version="1.0" encoding="UTF-8"?>\n')
    w.write('<orders exported="2024-06-30T23:59:59Z" source="shop-eu">\n')
    base = date(2023, 1, 1)
    for i in range(n):
        status = r.choice(["new", "paid", "shipped", "shipped", "delivered", "cancelled"])
        cur = r.choice(["EUR", "EUR", "EUR", "SEK", "PLN", "GBP"])
        w.write('  <order id="ORD-%07d" status="%s" currency="%s">\n' % (i + 1, status, cur))
        od = base + timedelta(days=r.randint(0, 540))
        w.write("    <order_date>%s</order_date>\n" % od.isoformat())
        name = person(r)
        cid = r.randint(1000, 99999)
        w.write("    <customer>\n")
        w.write("      <customer_id>%d</customer_id>\n" % cid)
        w.write("      <name>%s</name>\n" % esc(name))
        w.write("      <email>%s</email>\n" % email_for(name, cid))
        w.write("      <country>%s</country>\n" % r.choice(COUNTRIES))
        w.write("      <vip>%s</vip>\n" % ("true" if r.random() < 0.08 else "false"))
        w.write("    </customer>\n")
        if status != "cancelled" and r.random() < 0.9:
            w.write("    <shipping>\n")
            w.write("      <method>%s</method>\n" % r.choice(["standard", "express", "pickup"]))
            w.write("      <cost>%.2f</cost>\n" % r.choice([0, 4.95, 7.5, 12.5, 19.9]))
            w.write("      <street>%s %d</street>\n" % (esc(r.choice(STREETS)), r.randint(1, 240)))
            w.write("      <city>%s</city>\n" % r.choice(CITIES))
            w.write("      <postcode>%05d</postcode>\n" % r.randint(1000, 99999))
            w.write("    </shipping>\n")
        w.write("    <items>\n")
        subtotal = 0.0
        for _ in range(r.choice([1, 1, 2, 2, 3, 4, 5])):
            qty = r.randint(1, 6)
            price = round(r.uniform(2, 400), 2)
            subtotal += qty * price
            pname = "%s %s" % (r.choice(PRODUCT_ADJ), r.choice(PRODUCT_NOUNS))
            w.write('      <item sku="SKU-%05d" qty="%d">\n' % (r.randint(1, 30000), qty))
            w.write("        <name>%s</name>\n" % esc(pname))
            w.write("        <unit_price>%.2f</unit_price>\n" % price)
            w.write("      </item>\n")
        w.write("    </items>\n")
        tax = round(subtotal * 0.19, 2)
        w.write("    <subtotal>%.2f</subtotal>\n" % subtotal)
        w.write("    <tax>%.2f</tax>\n" % tax)
        w.write("    <total>%.2f</total>\n" % (subtotal + tax))
        w.write("    <paid>%s</paid>\n" % ("false" if status in ("new", "cancelled") else "true"))
        if r.random() < 0.2:
            w.write("    <coupon>%s</coupon>\n" % r.choice(["SPRING10", "WELCOME5", "VIP-20", "BLACKFRIDAY"]))
        if r.random() < 0.3:
            w.write("    <notes>%s</notes>\n" % esc(sentence(r, 4, 30) + " & thanks"))
        w.write("  </order>\n")
    w.write("</orders>\n")
    w.close()


def gen_orders_holdout(path, seed, n):
    """Purchase-order export with a different shape: deeper nesting, more lines, no item attributes."""
    r = random.Random(seed)
    w = AtomicWriter(path)
    w.write('<?xml version="1.0" encoding="UTF-8"?>\n')
    w.write("<purchaseOrders>\n")
    base = datetime(2022, 7, 1, 8, 0, 0)
    for i in range(n):
        w.write('  <purchaseOrder number="%d" priority="%s">\n' % (500000 + i, r.choice(["low", "normal", "normal", "high"])))
        created = base + timedelta(minutes=r.randint(0, 900000))
        w.write("    <created>%s</created>\n" % created.strftime("%Y-%m-%dT%H:%M:%S"))
        if r.random() < 0.7:
            w.write("    <due>%s</due>\n" % (created.date() + timedelta(days=r.randint(3, 60))).isoformat())
        w.write("    <buyer>\n")
        w.write("      <name>%s</name>\n" % esc(person(r)))
        w.write("      <department>%s</department>\n" % r.choice(["R&amp;D", "Facilities", "IT", "Marketing", "Logistics"]))
        w.write("      <address>\n")
        w.write("        <line1>%s %d</line1>\n" % (esc(r.choice(STREETS)), r.randint(1, 300)))
        w.write("        <city>%s</city>\n" % r.choice(CITIES))
        w.write("        <country>%s</country>\n" % r.choice(COUNTRIES))
        w.write("        <geo>\n")
        w.write("          <lat>%.5f</lat>\n" % r.uniform(35, 65))
        w.write("          <lon>%.5f</lon>\n" % r.uniform(-10, 30))
        w.write("        </geo>\n")
        w.write("      </address>\n")
        w.write("    </buyer>\n")
        w.write("    <lines>\n")
        for ln in range(r.randint(1, 12)):
            w.write("      <line>\n")
            w.write("        <lineNo>%d</lineNo>\n" % (ln + 1))
            w.write("        <part>P-%06d</part>\n" % r.randint(1, 999999))
            w.write("        <description>%s</description>\n" % esc(sentence(r, 2, 12)))
            w.write("        <quantity>%d</quantity>\n" % r.randint(1, 500))
            w.write("        <price>%.3f</price>\n" % r.uniform(0.05, 900))
            if r.random() < 0.25:
                w.write("        <discountPct>%.1f</discountPct>\n" % r.choice([2.5, 5, 10, 12.5]))
            w.write("      </line>\n")
        w.write("    </lines>\n")
        w.write("    <approved>%s</approved>\n" % r.choice(["true", "false", "true"]))
        if r.random() < 0.15:
            w.write("    <comment><![CDATA[%s <urgent> & confidential]]></comment>\n" % sentence(r, 10, 60))
        w.write("  </purchaseOrder>\n")
    w.write("</purchaseOrders>\n")
    w.close()


def gen_catalog(path, seed, n_products):
    """Product catalog: products nested under vendor/category wrappers, picked out with record_element."""
    r = random.Random(seed)
    w = AtomicWriter(path)
    w.write('<?xml version="1.0" encoding="UTF-8"?>\n')
    w.write('<catalog version="7" generated="2024-05-01">\n')
    made = 0
    v = 0
    while made < n_products:
        v += 1
        w.write('  <vendor code="V%03d" name="%s">\n' % (v, esc_attr(r.choice(BRANDS) + " " + r.choice(["GmbH", "AB", "S.A.", "Ltd", "株式会社"]))))
        for c in range(r.randint(2, 6)):
            w.write('    <category path="%s/%s">\n' % (r.choice(["home", "outdoor", "tech", "kitchen"]), r.choice(["new", "sale", "core"])))
            for _ in range(r.randint(5, 40)):
                if made >= n_products:
                    break
                made += 1
                w.write('      <product sku="%s-%06d">\n' % (r.choice("ABCDEFGH"), made))
                pname = "%s %s %s" % (r.choice(BRANDS), r.choice(PRODUCT_ADJ), r.choice(PRODUCT_NOUNS))
                w.write("        <name>%s</name>\n" % esc(pname))
                w.write("        <description>%s</description>\n" % esc(sentence(r, 15, 70)))
                price = round(r.uniform(3, 1500), 2)
                w.write("        <price>%.2f</price>\n" % price)
                if r.random() < 0.35:
                    w.write("        <list_price>%.2f</list_price>\n" % (price * r.uniform(1.05, 1.6)))
                w.write("        <weight_kg>%.3f</weight_kg>\n" % r.uniform(0.05, 40))
                w.write("        <dimensions>\n")
                w.write("          <width>%d</width>\n" % r.randint(5, 200))
                w.write("          <height>%d</height>\n" % r.randint(5, 200))
                w.write("          <depth>%d</depth>\n" % r.randint(1, 120))
                w.write("        </dimensions>\n")
                w.write("        <in_stock>%s</in_stock>\n" % ("true" if r.random() < 0.8 else "false"))
                w.write("        <released>%s</released>\n" % (date(2015, 1, 1) + timedelta(days=r.randint(0, 3400))).isoformat())
                if r.random() < 0.6:
                    w.write("        <rating>%.1f</rating>\n" % r.uniform(1, 5))
                w.write("        <tags>\n")
                for _ in range(r.randint(1, 4)):
                    w.write("          <tag>%s</tag>\n" % r.choice(WORDS))
                w.write("        </tags>\n")
                w.write("      </product>\n")
            w.write("    </category>\n")
        w.write("  </vendor>\n")
    w.write("</catalog>\n")
    w.close()


def gen_events(path, seed, n, holdout=False):
    """Service event log where most fields are attributes."""
    r = random.Random(seed)
    w = AtomicWriter(path)
    w.write('<?xml version="1.0" encoding="UTF-8"?>\n')
    w.write('<eventLog cluster="%s">\n' % ("eu-west-1" if not holdout else "ap-northeast-1"))
    t = datetime(2024, 3, 1, 0, 0, 0)
    for i in range(n):
        t += timedelta(milliseconds=r.randint(1, 400))
        ts = t.strftime("%Y-%m-%dT%H:%M:%S") + ".%03dZ" % (t.microsecond // 1000)
        svc = r.choice(SERVICES)
        lvl = r.choice(LEVELS)
        msg = r.choice(MESSAGES).format(n=r.randint(1, 5000))
        if not holdout:
            attrs = 'id="%d" ts="%s" level="%s" service="%s" host="web-%02d" duration_ms="%d" status="%d" ok="%s"' % (
                i + 1, ts, lvl, svc, r.randint(1, 24), r.randint(0, 3000),
                r.choice([200, 200, 200, 201, 204, 400, 404, 500]), "false" if lvl == "ERROR" else "true")
            if r.random() < 0.6:
                attrs += ' user_id="u%06d"' % r.randint(1, 250000)
            w.write("  <event %s>\n" % attrs)
            w.write("    <message>%s</message>\n" % esc(msg))
            if r.random() < 0.4:
                w.write('    <request method="%s" bytes="%d">%s</request>\n' % (r.choice(METHODS), r.randint(0, 90000), r.choice(PATHS)))
            if lvl == "ERROR":
                w.write("    <stack>%s</stack>\n" % esc("\n".join("at %s.handler(%s.go:%d)" % (svc, svc, r.randint(10, 900)) for _ in range(r.randint(2, 6)))))
            w.write("  </event>\n")
        else:
            attrs = 'seq="%d" at="%s" severity="%d" component="%s" node="n%03d" region="%s" latency="%.3f" trace="%016x"' % (
                i + 1, ts, LEVELS.index(lvl), svc, r.randint(1, 400), r.choice(["a", "b", "c"]),
                r.uniform(0, 2.5), r.getrandbits(64))
            if r.random() < 0.3:
                attrs += ' tenant="%s"' % esc_attr(r.choice(BRANDS))
            if r.random() < 0.5:
                attrs += ' retry="%d"' % r.randint(0, 5)
            w.write("  <event %s>\n" % attrs)
            w.write('    <text lang="%s">%s</text>\n' % (r.choice(["en", "de", "ja"]), esc(msg)))
            w.write('    <context env="prod" build="%d.%d.%d">\n' % (r.randint(1, 4), r.randint(0, 20), r.randint(0, 99)))
            w.write("      <client ip=\"10.%d.%d.%d\" agent=\"%s\"/>\n" % (r.randint(0, 255), r.randint(0, 255), r.randint(1, 254), r.choice(["curl/8.4", "Mozilla/5.0", "okhttp/4.12"])))
            w.write("    </context>\n")
            w.write("  </event>\n")
    w.write("</eventLog>\n")
    w.close()


def gen_readings(path, seed, n):
    """Weather-station readings: nearly every field is numeric, a date, a timestamp or a boolean."""
    r = random.Random(seed)
    w = AtomicWriter(path)
    w.write('<?xml version="1.0" encoding="UTF-8"?>\n')
    w.write("<readings>\n")
    t0 = datetime(2023, 1, 1)
    for i in range(n):
        st = r.randint(1, 250)
        ts = t0 + timedelta(seconds=600 * (i // 250) + r.randint(0, 59))
        w.write("  <reading>\n")
        w.write("    <seq>%d</seq>\n" % (i + 1))
        w.write("    <station>%d</station>\n" % st)
        w.write("    <observed_at>%s</observed_at>\n" % ts.strftime("%Y-%m-%d %H:%M:%S"))
        w.write("    <obs_date>%s</obs_date>\n" % ts.date().isoformat())
        w.write("    <temp_c>%.2f</temp_c>\n" % r.gauss(11, 9))
        w.write("    <dewpoint_c>%.2f</dewpoint_c>\n" % r.gauss(5, 7))
        w.write("    <humidity_pct>%.1f</humidity_pct>\n" % r.uniform(15, 100))
        w.write("    <pressure_hpa>%.1f</pressure_hpa>\n" % r.gauss(1013, 9))
        w.write("    <wind_ms>%.2f</wind_ms>\n" % abs(r.gauss(4, 3)))
        w.write("    <wind_dir>%d</wind_dir>\n" % r.randint(0, 359))
        w.write("    <gust_ms>%.2f</gust_ms>\n" % abs(r.gauss(7, 4)))
        w.write("    <rain_mm>%.1f</rain_mm>\n" % (0 if r.random() < 0.8 else r.expovariate(0.5)))
        w.write("    <solar_wm2>%d</solar_wm2>\n" % r.randint(0, 1100))
        w.write("    <battery_v>%.3f</battery_v>\n" % r.uniform(3.3, 4.2))
        w.write("    <samples>%d</samples>\n" % r.randint(1, 600))
        w.write("    <counter>%d</counter>\n" % r.randint(0, 9_000_000_000))
        w.write("    <calibrated_on>%s</calibrated_on>\n" % (date(2020, 1, 1) + timedelta(days=st * 3)).isoformat())
        w.write("    <heater_on>%s</heater_on>\n" % ("true" if r.random() < 0.1 else "false"))
        w.write("    <qc_pass>%s</qc_pass>\n" % ("true" if r.random() < 0.97 else "false"))
        w.write("  </reading>\n")
    w.write("</readings>\n")
    w.close()


def gen_transactions(path, seed, n, holdout=False):
    """Large payment-transaction stream, read via SAX (file exceeds maximum_file_size)."""
    r = random.Random(seed)
    w = AtomicWriter(path)
    w.write('<?xml version="1.0" encoding="UTF-8"?>\n')
    w.write('<ledger bank="%s">\n' % ("Nordbank" if not holdout else "Banco Sol"))
    t = datetime(2024, 1, 1)
    for i in range(n):
        t += timedelta(seconds=r.randint(0, 20))
        if not holdout:
            w.write('  <txn id="T%09d" type="%s">\n' % (i + 1, r.choice(["card", "card", "sepa", "refund", "fee"])))
            w.write("    <posted>%s</posted>\n" % t.strftime("%Y-%m-%dT%H:%M:%S"))
            w.write("    <account>DE%020d</account>\n" % r.randint(0, 10**18))
            w.write("    <amount>%.2f</amount>\n" % r.uniform(-500, 5000))
            w.write("    <currency>%s</currency>\n" % r.choice(["EUR", "EUR", "EUR", "USD", "CHF"]))
            w.write("    <merchant>%s</merchant>\n" % esc(r.choice(BRANDS) + " " + r.choice(CITIES)))
            w.write("    <mcc>%d</mcc>\n" % r.choice([5411, 5812, 5999, 4111, 7011, 5732]))
            if r.random() < 0.3:
                w.write("    <reference>%s</reference>\n" % esc(sentence(r, 1, 8)))
            w.write("    <cleared>%s</cleared>\n" % ("true" if r.random() < 0.95 else "false"))
            w.write("  </txn>\n")
        else:
            w.write("  <txn>\n")
            w.write("    <txnId>%d</txnId>\n" % (900000000 + i))
            w.write("    <valueDate>%s</valueDate>\n" % t.date().isoformat())
            w.write("    <debit>\n")
            w.write("      <iban>ES%022d</iban>\n" % r.randint(0, 10**21))
            w.write("      <holder>%s</holder>\n" % esc(person(r)))
            w.write("    </debit>\n")
            w.write("    <credit>\n")
            w.write("      <iban>PT%023d</iban>\n" % r.randint(0, 10**22))
            w.write("      <holder>%s</holder>\n" % esc(person(r)))
            w.write("    </credit>\n")
            w.write("    <amount>%.4f</amount>\n" % r.uniform(0.01, 25000))
            w.write("    <fx>%.6f</fx>\n" % r.uniform(0.8, 1.3))
            w.write("    <memo>%s</memo>\n" % esc(sentence(r, 0, 14)))
            w.write("    <instant>%s</instant>\n" % ("true" if r.random() < 0.4 else "false"))
            w.write("  </txn>\n")
    w.write("</ledger>\n")
    w.close()


def gen_tickets(path, seed, n):
    """Support-ticket export: few, large records (long bodies, threaded replies) in a file
    above maximum_file_size. The record count stays below the SAX inference sample
    (sample_size defaults to 10240 records) so schema inference streams the whole file."""
    r = random.Random(seed)
    w = AtomicWriter(path)
    w.write('<?xml version="1.0" encoding="UTF-8"?>\n')
    w.write('<tickets product="Nordwind Cloud" exported="2024-09-01T00:00:00Z">\n')
    base = datetime(2023, 3, 1, 9, 0, 0)
    for i in range(n):
        opened = base + timedelta(minutes=r.randint(0, 700000))
        w.write('  <ticket id="%d" priority="%s" channel="%s">\n' % (
            70000 + i, r.choice(["low", "normal", "normal", "high", "urgent"]), r.choice(["email", "chat", "phone", "web"])))
        w.write("    <subject>%s</subject>\n" % esc(sentence(r, 3, 10).capitalize()))
        w.write("    <opened>%s</opened>\n" % opened.strftime("%Y-%m-%d %H:%M:%S"))
        name = person(r)
        cid = r.randint(10000, 999999)
        w.write("    <requester>\n")
        w.write("      <name>%s</name>\n" % esc(name))
        w.write("      <email>%s</email>\n" % email_for(name, cid))
        w.write("      <plan>%s</plan>\n" % r.choice(["free", "team", "business", "enterprise"]))
        w.write("      <seats>%d</seats>\n" % r.choice([1, 5, 10, 25, 50, 250, 1200]))
        w.write("    </requester>\n")
        paras = ["%s." % sentence(r, 25, 70).capitalize() for _ in range(r.randint(1, 4))]
        w.write("    <body>%s</body>\n" % esc("\n\n".join(paras) + " <see attached log> & regards"))
        w.write("    <replies>\n")
        t = opened
        for k in range(r.randint(0, 6)):
            t += timedelta(minutes=r.randint(3, 3000))
            agent = k % 2 == 0
            w.write('      <reply author="%s" at="%s" internal="%s">%s</reply>\n' % (
                esc_attr("agent-%02d" % r.randint(1, 40) if agent else name), t.strftime("%Y-%m-%dT%H:%M:%S"),
                "true" if agent and r.random() < 0.2 else "false", esc(sentence(r, 8, 60))))
        w.write("    </replies>\n")
        if r.random() < 0.75:
            w.write("    <resolved_at>%s</resolved_at>\n" % (t + timedelta(hours=r.randint(1, 96))).strftime("%Y-%m-%d %H:%M:%S"))
        if r.random() < 0.4:
            w.write("    <csat>%d</csat>\n" % r.randint(1, 5))
        w.write("    <sla_breached>%s</sla_breached>\n" % ("true" if r.random() < 0.12 else "false"))
        w.write("  </ticket>\n")
    w.write("</tickets>\n")
    w.close()


def build_dev(d):
    gen_orders_dev(os.path.join(d, "orders.xml"), 1101, 15000)
    gen_catalog(os.path.join(d, "catalog.xml"), 1102, 16000)
    gen_events(os.path.join(d, "events.xml"), 1103, 45000)
    gen_readings(os.path.join(d, "readings.xml"), 1104, 24500)
    gen_transactions(os.path.join(d, "transactions.xml"), 1105, 420000)
    gen_tickets(os.path.join(d, "tickets.xml"), 1106, 9000)


def build_holdout(d):
    gen_orders_holdout(os.path.join(d, "orders.xml"), 2201, 6500)
    gen_events(os.path.join(d, "events.xml"), 2203, 40000, holdout=True)
    gen_transactions(os.path.join(d, "transactions.xml"), 2205, 150000, holdout=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", required=True, help="data root")
    ap.add_argument("--split", required=True, choices=["dev", "holdout"])
    args = ap.parse_args()
    d = os.path.join(args.out, FAMILY, args.split)
    os.makedirs(d, exist_ok=True)
    (build_dev if args.split == "dev" else build_holdout)(d)


if __name__ == "__main__":
    main()
