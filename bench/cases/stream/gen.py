#!/usr/bin/env python3
"""Deterministic generator for the "stream" benchmark family.

Writes large XML files that push read_xml onto the SAX streaming path
(files larger than maximum_file_size, 16 MB by default) and namespace-heavy
documents (Atom, RSS, SOAP) for read_xml namespace modes and XPath with
prefixes.

Usage: python3 gen.py --out <data_root> --split dev|holdout

Output goes to <data_root>/stream/<split>/. Real public datasets for the
holdout split are fetched separately by fetch.sh into <data_root>/stream/real/.
"""

import argparse
import os
import random
import sys

FIRST = ["Ava", "Liam", "Sofía", "Noah", "Zoë", "Mateo", "Chloé", "Lukas", "Ingrid", "Kenji",
         "Amara", "Björn", "Priya", "Oleksandr", "Mei", "José", "Fatima", "Søren", "Hana", "Émile"]
LAST = ["García", "Smith", "Müller", "Nakamura", "O'Brien", "Kowalski", "Nguyen", "Dubois",
        "Rossi", "Andersson", "Patel", "Haddad", "Johansson", "Ivanova", "Okafor", "Lindqvist"]
CITIES = ["São Paulo", "Zürich", "Kraków", "Reykjavík", "Montréal", "Tokyo", "Lagos", "Austin",
          "Málaga", "Göteborg", "Chennai", "Oaxaca", "Wrocław", "Seattle"]
WORDS = ("the order arrived quickly and packaging was intact but one item was missing so we "
         "contacted support about a replacement shipment delivery window fragile handle with "
         "care leave at front desk call before arrival weekend preferred invoice attached "
         "café naïve résumé façade jalapeño über déjà vu").split()
TAGS = ["electronics", "garden", "books", "toys", "kitchen", "outdoor", "apparel", "office",
        "audio", "travel", "pets", "crafts"]
CURRENCIES = ["USD", "EUR", "GBP", "JPY", "SEK", "BRL"]
STATUSES = ["pending", "paid", "shipped", "delivered", "cancelled", "refunded"]
TIERS = ["bronze", "silver", "gold", "platinum"]


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def esc_attr(s):
    return esc(s).replace('"', "&quot;")


def sentence(rng, lo, hi):
    return " ".join(rng.choice(WORDS) for _ in range(rng.randint(lo, hi)))


def iso_date(rng, year_lo=2019, year_hi=2025):
    return "%04d-%02d-%02d" % (rng.randint(year_lo, year_hi), rng.randint(1, 12), rng.randint(1, 28))


def iso_ts(rng):
    return "%sT%02d:%02d:%02d" % (iso_date(rng), rng.randint(0, 23), rng.randint(0, 59), rng.randint(0, 59))


def rfc822(rng):
    days = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
    months = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]
    return "%s, %02d %s %04d %02d:%02d:%02d +0000" % (
        rng.choice(days), rng.randint(1, 28), rng.choice(months), rng.randint(2018, 2025),
        rng.randint(0, 23), rng.randint(0, 59), rng.randint(0, 59))


class Writer:
    """Buffered writer that flushes in large blocks."""

    def __init__(self, path):
        self.f = open(path, "w", encoding="utf-8", newline="\n")
        self.buf = []
        self.size = 0

    def write(self, s):
        self.buf.append(s)
        self.size += len(s)
        if len(self.buf) >= 4096:
            self.f.write("".join(self.buf))
            self.buf = []

    def close(self):
        self.f.write("".join(self.buf))
        self.f.close()


def gen_orders(path, seed, n, shape):
    """shape='dev': default namespace on root, flat-ish orders.
    shape='holdout': prefixed namespace, extension elements, deeper address
    struct, more optional children and longer notes."""
    rng = random.Random(seed)
    w = Writer(path)
    w.write('<?xml version="1.0" encoding="UTF-8"?>\n')
    if shape == "dev":
        w.write('<orders xmlns="urn:acme:orders:2" generated="2025-01-01T00:00:00">\n')
        p = ""
    else:
        w.write('<o:orders xmlns:o="urn:globex:commerce:orders:v5" '
                'xmlns:ext="urn:globex:ext" batch="H-7">\n')
        p = "o:"
    for i in range(n):
        oid = 100000 + i
        status = rng.choice(STATUSES)
        cust_first, cust_last = rng.choice(FIRST), rng.choice(LAST)
        out = ['  <%sorder id="%d" status="%s"' % (p, oid, status)]
        if rng.random() < 0.3:
            out.append(' channel="%s"' % rng.choice(["web", "mobile", "store", "phone"]))
        out.append('>\n')
        out.append('    <%scustomer tier="%s"><%sname>%s</%sname><%semail>%s.%s@example.com</%semail></%scustomer>\n' % (
            p, rng.choice(TIERS), p, esc(cust_first + " " + cust_last), p,
            p, cust_first.lower().encode("ascii", "ignore").decode(), rng.randint(1, 9999), p, p))
        out.append('    <%splaced_at>%s</%splaced_at>\n' % (p, iso_ts(rng), p))
        nitems = rng.randint(1, 6) if shape == "dev" else rng.randint(1, 3)
        total = 0.0
        items = []
        for _ in range(nitems):
            qty = rng.randint(1, 12)
            price = rng.randint(99, 99999) / 100.0
            total += qty * price
            items.append('      <%sitem sku="SKU-%05d"><%sqty>%d</%sqty><%sprice>%.2f</%sprice><%scategory>%s</%scategory></%sitem>\n' % (
                p, rng.randint(1, 50000), p, qty, p, p, price, p, p, rng.choice(TAGS), p, p))
        out.append('    <%stotal>%.2f</%stotal>\n' % (p, total, p))
        out.append('    <%scurrency>%s</%scurrency>\n' % (p, rng.choice(CURRENCIES), p))
        out.append('    <%spaid>%s</%spaid>\n' % (p, "true" if status not in ("pending", "cancelled") else "false", p))
        if shape == "holdout":
            out.append('    <%sship_to><%sstreet>%d %s St</%sstreet><%scity>%s</%scity><%spostal>%05d</%spostal><%sgeo><%slat>%.5f</%slat><%slon>%.5f</%slon></%sgeo></%sship_to>\n' % (
                p, p, rng.randint(1, 9999), esc(rng.choice(LAST)), p, p, esc(rng.choice(CITIES)), p,
                p, rng.randint(1000, 99999), p, p, p, rng.uniform(-60, 70), p, p, rng.uniform(-170, 170), p, p, p))
            if rng.random() < 0.5:
                out.append('    <ext:loyalty points="%d">%s</ext:loyalty>\n' % (rng.randint(0, 50000), rng.choice(TIERS)))
            if rng.random() < 0.2:
                out.append('    <%sdelivered_on>%s</%sdelivered_on>\n' % (p, iso_date(rng), p))
        out.append('    <%sitems>\n' % p)
        out.extend(items)
        out.append('    </%sitems>\n' % p)
        note_p = 0.3 if shape == "dev" else 0.55
        if rng.random() < note_p:
            lo, hi = (5, 30) if shape == "dev" else (20, 90)
            out.append('    <%snotes>%s &amp; %s</%snotes>\n' % (p, esc(sentence(rng, lo, hi)), esc(rng.choice(CITIES)), p))
        if rng.random() < 0.15:
            out.append('    <%scoupon code="SAVE%d">%d</%scoupon>\n' % (p, rng.randint(5, 50), rng.randint(5, 50), p))
        if rng.random() < 0.1:
            out.append('    <%sgift>true</%sgift>\n' % (p, p))
        out.append('  </%sorder>\n' % p)
        w.write("".join(out))
    w.write('</%sorders>\n' % p)
    w.close()


ATOM_NS = ('xmlns="http://www.w3.org/2005/Atom" '
           'xmlns:media="http://search.yahoo.com/mrss/" '
           'xmlns:georss="http://www.georss.org/georss" '
           'xmlns:dc="http://purl.org/dc/elements/1.1/" '
           'xmlns:thr="http://purl.org/syndication/thread/1.0"')


def gen_atom(path, seed, n, shard):
    rng = random.Random(seed)
    w = Writer(path)
    w.write('<?xml version="1.0" encoding="UTF-8"?>\n')
    w.write('<feed %s xml:lang="en">\n' % ATOM_NS)
    w.write('  <title>Field Notes Aggregator — shard %d</title>\n' % shard)
    w.write('  <id>urn:uuid:feed-%04d</id>\n  <updated>2025-06-01T00:00:00Z</updated>\n' % shard)
    for i in range(n):
        eid = shard * 10_000_000 + i
        out = ['  <entry>\n']
        out.append('    <id>tag:fieldnotes.example,2025:%d</id>\n' % eid)
        out.append('    <title type="text">%s</title>\n' % esc(sentence(rng, 3, 10).capitalize()))
        out.append('    <updated>%sZ</updated>\n' % iso_ts(rng))
        if rng.random() < 0.7:
            out.append('    <published>%sZ</published>\n' % iso_ts(rng))
        a1, a2 = rng.choice(FIRST), rng.choice(LAST)
        out.append('    <author><name>%s %s</name><uri>https://people.example/%d</uri></author>\n' % (
            esc(a1), esc(a2), rng.randint(1, 5000)))
        out.append('    <link rel="alternate" type="text/html" href="https://fieldnotes.example/p/%d"/>\n' % eid)
        for _ in range(rng.randint(1, 3)):
            out.append('    <category term="%s" scheme="https://fieldnotes.example/tags"/>\n' % rng.choice(TAGS))
        out.append('    <summary type="html">%s &lt;b&gt;%s&lt;/b&gt;</summary>\n' % (
            esc(sentence(rng, 15, 60)), esc(rng.choice(CITIES))))
        out.append('    <dc:subject>%s</dc:subject>\n' % rng.choice(TAGS))
        if rng.random() < 0.5:
            out.append('    <georss:point>%.4f %.4f</georss:point>\n' % (rng.uniform(-60, 70), rng.uniform(-170, 170)))
        if rng.random() < 0.4:
            out.append('    <media:thumbnail url="https://img.example/%d.jpg" width="%d" height="%d"/>\n' % (
                eid, rng.choice([120, 240, 480]), rng.choice([90, 180, 360])))
        out.append('    <thr:total>%d</thr:total>\n' % rng.randint(0, 400))
        out.append('  </entry>\n')
        w.write("".join(out))
    w.write('</feed>\n')
    w.close()


RSS_NS = ('xmlns:content="http://purl.org/rss/1.0/modules/content/" '
          'xmlns:dc="http://purl.org/dc/elements/1.1/" '
          'xmlns:slash="http://purl.org/rss/1.0/modules/slash/" '
          'xmlns:media="http://search.yahoo.com/mrss/" '
          'xmlns:atom="http://www.w3.org/2005/Atom"')


def gen_rss(path, seed, n, shard):
    rng = random.Random(seed)
    w = Writer(path)
    w.write('<?xml version="1.0" encoding="UTF-8"?>\n')
    w.write('<rss version="2.0" %s>\n<channel>\n' % RSS_NS)
    w.write('  <title>Harbor Gazette — archive %d</title>\n' % shard)
    w.write('  <link>https://gazette.example/</link>\n')
    w.write('  <atom:link href="https://gazette.example/rss" rel="self" type="application/rss+xml"/>\n')
    for i in range(n):
        gid = shard * 10_000_000 + i
        out = ['  <item>\n']
        out.append('    <title>%s</title>\n' % esc(sentence(rng, 4, 14).title()))
        out.append('    <link>https://gazette.example/a/%d</link>\n' % gid)
        out.append('    <guid isPermaLink="false">gz-%d</guid>\n' % gid)
        out.append('    <pubDate>%s</pubDate>\n' % rfc822(rng))
        out.append('    <dc:creator>%s %s</dc:creator>\n' % (esc(rng.choice(FIRST)), esc(rng.choice(LAST))))
        for _ in range(rng.randint(0, 4)):
            out.append('    <category>%s</category>\n' % rng.choice(TAGS))
        out.append('    <description>%s</description>\n' % esc(sentence(rng, 10, 40)))
        if rng.random() < 0.6:
            out.append('    <content:encoded><![CDATA[<p>%s</p><p>%s &amp; more</p>]]></content:encoded>\n' % (
                sentence(rng, 30, 120), rng.choice(CITIES)))
        if rng.random() < 0.35:
            out.append('    <media:content url="https://cdn.gazette.example/%d.jpg" medium="image" width="%d">'
                       '<media:credit role="photographer">%s</media:credit></media:content>\n' % (
                           gid, rng.choice([640, 1024, 2048]), esc(rng.choice(LAST))))
        out.append('    <slash:comments>%d</slash:comments>\n' % rng.randint(0, 900))
        if rng.random() < 0.2:
            out.append('    <enclosure url="https://cdn.gazette.example/%d.mp3" length="%d" type="audio/mpeg"/>\n' % (
                gid, rng.randint(100000, 90000000)))
        out.append('  </item>\n')
        w.write("".join(out))
    w.write('</channel>\n</rss>\n')
    w.close()


def soap_dev(rng, i):
    lines = rng.randint(1, 8)
    parts = ['<?xml version="1.0" encoding="UTF-8"?>\n'
             '<soap:Envelope xmlns:soap="http://schemas.xmlsoap.org/soap/envelope/" '
             'xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" '
             'xmlns:inv="urn:acme:inventory:2019">\n'
             '  <soap:Header><inv:RequestId>REQ-%08d</inv:RequestId><inv:Tenant>%s</inv:Tenant></soap:Header>\n'
             '  <soap:Body>\n'
             '    <inv:StockReport xmlns="urn:acme:inventory:2019" warehouse="WH-%02d">\n' % (
                 i, esc(rng.choice(CITIES)), rng.randint(1, 40))]
    for _ in range(lines):
        parts.append('      <inv:Line sku="SKU-%05d"><inv:OnHand>%d</inv:OnHand><inv:Reserved>%d</inv:Reserved>'
                     '<inv:Cost currency="%s">%.2f</inv:Cost><inv:Note>%s</inv:Note></inv:Line>\n' % (
                         rng.randint(1, 50000), rng.randint(0, 5000), rng.randint(0, 300),
                         rng.choice(CURRENCIES), rng.randint(50, 500000) / 100.0, esc(sentence(rng, 2, 12))))
    if rng.random() < 0.3:
        parts.append('      <inv:Alert level="%s" xsi:type="inv:LowStock">%s</inv:Alert>\n' % (
            rng.choice(["info", "warn", "critical"]), esc(sentence(rng, 3, 9))))
    parts.append('    </inv:StockReport>\n  </soap:Body>\n</soap:Envelope>\n')
    return "".join(parts)


def soap_holdout(rng, i):
    parts = ['<?xml version="1.0" encoding="UTF-8"?>\n'
             '<env:Envelope xmlns:env="http://www.w3.org/2003/05/soap-envelope" '
             'xmlns:wsa="http://www.w3.org/2005/08/addressing" '
             'xmlns:wsse="http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd" '
             'xmlns:pay="urn:initech:payments:v3">\n'
             '  <env:Header>\n'
             '    <wsa:MessageID>urn:uuid:%08x-%04x-4%03x-a%03x-%012x</wsa:MessageID>\n'
             '    <wsa:Action>urn:initech:payments:v3/Settle</wsa:Action>\n'
             '    <wsse:Security><wsse:UsernameToken><wsse:Username>svc-%s</wsse:Username></wsse:UsernameToken></wsse:Security>\n'
             '  </env:Header>\n  <env:Body>\n    <pay:SettlementBatch>\n' % (
                 i, rng.randint(0, 0xffff), rng.randint(0, 0xfff), rng.randint(0, 0xfff),
                 rng.randint(0, 0xffffffffffff), rng.choice(TAGS))]
    for _ in range(rng.randint(2, 14)):
        parts.append('      <pay:Payment><pay:Payer><pay:Name>%s %s</pay:Name><pay:Country>%s</pay:Country></pay:Payer>'
                     '<pay:Amount ccy="%s">%.2f</pay:Amount><pay:ValueDate>%s</pay:ValueDate><pay:Instant>%s</pay:Instant></pay:Payment>\n' % (
                         esc(rng.choice(FIRST)), esc(rng.choice(LAST)), rng.choice(["SE", "BR", "JP", "DE", "NG", "US"]),
                         rng.choice(CURRENCIES), rng.randint(100, 9000000) / 100.0, iso_date(rng),
                         "true" if rng.random() < 0.4 else "false"))
    parts.append('    </pay:SettlementBatch>\n  </env:Body>\n</env:Envelope>\n')
    return "".join(parts)


def gen_soap(dirpath, seed, n, shape):
    rng = random.Random(seed)
    os.makedirs(dirpath, exist_ok=True)
    fn = soap_dev if shape == "dev" else soap_holdout
    for i in range(n):
        with open(os.path.join(dirpath, "msg_%05d.xml" % i), "w", encoding="utf-8", newline="\n") as f:
            f.write(fn(rng, i))


SERVICES = ["checkout", "auth", "search", "ledger", "media", "notify", "geo"]
LEVELS = ["DEBUG", "INFO", "INFO", "INFO", "WARN", "ERROR"]


def gen_logs(path, seed, n, truncate_at=None):
    """Event log export. truncate_at cuts the file at that byte offset, leaving
    malformed XML, as a partially copied export would be."""
    rng = random.Random(seed)
    w = Writer(path)
    w.write('<?xml version="1.0" encoding="UTF-8"?>\n<events source="collector-%d">\n' % seed)
    for i in range(n):
        level = rng.choice(LEVELS)
        out = ['  <event seq="%d" level="%s">' % (i, level)]
        out.append('<ts>%s.%03dZ</ts>' % (iso_ts(rng), rng.randint(0, 999)))
        out.append('<host>node-%02d.%s</host>' % (rng.randint(1, 64), rng.choice(["us-east", "eu-west", "ap-south"])))
        out.append('<service>%s</service>' % rng.choice(SERVICES))
        out.append('<latency_ms>%.3f</latency_ms>' % rng.expovariate(1 / 40.0))
        out.append('<ok>%s</ok>' % ("false" if level == "ERROR" else "true"))
        out.append('<msg>%s</msg>' % esc(sentence(rng, 4, 20)))
        if level in ("WARN", "ERROR") or rng.random() < 0.1:
            out.append('<trace_id>%032x</trace_id>' % rng.getrandbits(128))
        if rng.random() < 0.25:
            out.append('<user id="%d">%s</user>' % (rng.randint(1, 10**6), esc(rng.choice(FIRST))))
        out.append('</event>\n')
        w.write("".join(out))
    w.write('</events>\n')
    w.close()
    if truncate_at is not None:
        with open(path, "r+b") as f:
            f.truncate(truncate_at)


def clean_dir(path):
    """Remove files previously written by this generator so reruns are clean."""
    if not os.path.isdir(path):
        return
    for name in sorted(os.listdir(path)):
        full = os.path.join(path, name)
        if os.path.isdir(full):
            clean_dir(full)
            os.rmdir(full)
        else:
            os.remove(full)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--split", required=True, choices=["dev", "holdout"])
    args = ap.parse_args()

    base = os.path.join(args.out, "stream", args.split)
    clean_dir(base)
    os.makedirs(base, exist_ok=True)

    if args.split == "dev":
        gen_orders(os.path.join(base, "orders.xml"), seed=1101, n=55_000, shape="dev")
        os.makedirs(os.path.join(base, "atom"), exist_ok=True)
        for s in range(3):
            gen_atom(os.path.join(base, "atom", "feed_%d.xml" % s), seed=1200 + s, n=18_000, shard=s)
        gen_soap(os.path.join(base, "soap"), seed=1300, n=6000, shape="dev")
        os.makedirs(os.path.join(base, "logs"), exist_ok=True)
        gen_logs(os.path.join(base, "logs", "part_0.xml"), seed=1400, n=12_000)
        gen_logs(os.path.join(base, "logs", "part_1.xml"), seed=1401, n=12_000, truncate_at=3_000_000)
    else:
        gen_orders(os.path.join(base, "orders.xml"), seed=2101, n=30_000, shape="holdout")
        os.makedirs(os.path.join(base, "rss"), exist_ok=True)
        for s in range(2):
            gen_rss(os.path.join(base, "rss", "archive_%d.xml" % s), seed=2200 + s, n=18_000, shard=s)
        gen_soap(os.path.join(base, "soap"), seed=2300, n=3000, shape="holdout")
    return 0


if __name__ == "__main__":
    sys.exit(main())
