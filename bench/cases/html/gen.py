#!/usr/bin/env python3
"""Generate realistic HTML pages for the html benchmark family.

Usage: python3 gen.py --out <data_root> --split dev|holdout

Writes into <data_root>/html/<split>/:
  pages/page_NNNNN.html   many full pages (dev: news/shop shape, holdout: wiki/forum shape)
  large/report.html       (dev only) one ~20 MB data-export page with a huge table

Deterministic: fixed seeds, no wall-clock or environment input, so two runs
produce byte-identical files. Files are rewritten on every run.
"""

import argparse
import os
import random

WORDS = (
    "the of and to in is for on that with as by at from this be are or an it was not have "
    "market report data system price value city council budget energy water school project "
    "season team player result growth policy health research study review update release "
    "service network customer product order delivery quality design feature support account "
    "community event festival museum library garden river bridge station harbour mountain "
    "analysis forecast quarter revenue margin inventory supplier warehouse shipment invoice"
).split()

UNICODE_WORDS = [
    "café", "naïve", "résumé", "Zürich", "São Paulo", "München", "Kraków", "façade",
    "東京", "北京", "서울", "Αθήνα", "Москва", "smörgåsbord", "piñata", "coöperate",
    "jalapeño", "Ærø", "crème brûlée", "Đà Nẵng", "İstanbul", "Reykjavík",
]

ENTITIES = ["&amp;", "&mdash;", "&ndash;", "&eacute;", "&nbsp;", "&copy;", "&hellip;",
            "&lsquo;", "&rsquo;", "&ldquo;", "&rdquo;", "&euro;", "&#8212;", "&#x2019;", "&lt;", "&gt;"]

FIRST = ["Ana", "Ben", "Chen", "Dmitri", "Elif", "Farah", "Gustavo", "Hana", "Ivan", "Jae",
         "Kofi", "Lena", "Mateo", "Nadia", "Omar", "Priya", "Quinn", "Rosa", "Sven", "Yuki"]
LAST = ["Okafor", "Müller", "García", "Nguyen", "Kowalski", "Haddad", "Silva", "Tanaka",
        "O'Brien", "Novak", "Rossi", "Andersson", "Patel", "Kim", "Dubois", "Ivanova"]

DOMAINS = ["example.com", "news.example.org", "cdn.example.net", "shop.example.com",
           "wiki.example.org", "static.example.io", "partner.example.co.uk", "blog.example.dev"]

SECTIONS = ["World", "Business", "Technology", "Science", "Sports", "Culture", "Opinion", "Travel"]


class Shape:
    """Knobs that differ between dev and holdout pages."""

    def __init__(self, **kw):
        self.__dict__.update(kw)


DEV_SHAPE = Shape(
    seed=20241014,
    n_pages=1500,
    sections=(3, 7),          # h2 sections per article
    paras=(2, 6),             # paragraphs per section
    words=(25, 80),           # words per paragraph
    tables=(1, 3),            # tables per page
    table_rows=(8, 40),
    thead_prob=0.85,
    tfoot_prob=0.3,
    table_kind="shop",
    images=(3, 10),
    nav_links=(8, 16),
    related_links=(5, 15),
    nest_depth=(1, 2),        # wrapper divs around article content
    malformed_prob=0.25,
    script_lines=(5, 30),
    style_lines=(10, 40),
    comments=(3, 8),          # top-level comments when a thread is present
    comment_prob=0.2,
)

HOLDOUT_SHAPE = Shape(
    seed=99173,
    n_pages=700,
    sections=(6, 14),
    paras=(1, 4),
    words=(60, 160),
    tables=(2, 6),
    table_rows=(3, 15),
    thead_prob=0.4,
    tfoot_prob=0.1,
    table_kind="wiki",
    images=(0, 4),
    nav_links=(20, 40),
    related_links=(10, 30),
    nest_depth=(3, 6),
    malformed_prob=0.45,
    script_lines=(1, 8),
    style_lines=(0, 6),
    comments=(5, 25),
    comment_prob=0.8,
)


def words(rng, lo, hi):
    n = rng.randint(lo, hi)
    out = []
    for _ in range(n):
        r = rng.random()
        if r < 0.04:
            out.append(rng.choice(UNICODE_WORDS))
        elif r < 0.07:
            out.append(rng.choice(ENTITIES))
        elif r < 0.09:
            out.append(str(rng.randint(1, 99999)))
        else:
            out.append(rng.choice(WORDS))
    s = " ".join(out)
    return s[0].upper() + s[1:] if s and s[0].isalpha() else s


def url(rng, internal_prob=0.6):
    if rng.random() < internal_prob:
        path = "/" + "/".join(rng.choice(WORDS) for _ in range(rng.randint(1, 3)))
        if rng.random() < 0.3:
            path += "?id=%d&amp;ref=%s" % (rng.randint(1, 50000), rng.choice(WORDS))
        if rng.random() < 0.1:
            path += "#" + rng.choice(WORDS)
        return path
    scheme = "https" if rng.random() < 0.9 else "http"
    return "%s://%s/%s" % (scheme, rng.choice(DOMAINS), "-".join(rng.choice(WORDS) for _ in range(rng.randint(1, 4))))


def person(rng):
    return "%s %s" % (rng.choice(FIRST), rng.choice(LAST))


def date(rng):
    return "%04d-%02d-%02d" % (rng.randint(2015, 2025), rng.randint(1, 12), rng.randint(1, 28))


def inline_text(rng, shape):
    """A paragraph body with inline markup, entities, and links."""
    parts = []
    for _ in range(rng.randint(2, 6)):
        parts.append(words(rng, shape.words[0] // 4, shape.words[1] // 4))
        r = rng.random()
        if r < 0.35:
            title = ' title="%s"' % words(rng, 2, 5) if rng.random() < 0.3 else ""
            parts.append('<a href="%s"%s>%s</a>' % (url(rng), title, words(rng, 1, 4)))
        elif r < 0.5:
            parts.append("<em>%s</em>" % words(rng, 1, 3))
        elif r < 0.65:
            parts.append("<strong>%s</strong>" % words(rng, 1, 3))
        elif r < 0.72:
            parts.append("<code>%s()</code>" % rng.choice(WORDS))
        elif r < 0.78:
            parts.append("<br>")
        elif r < 0.82:
            parts.append('<span class="hl" data-k="%d">%s</span>' % (rng.randint(1, 99), words(rng, 1, 3)))
    return " ".join(parts)


def css(rng, n):
    lines = []
    for _ in range(n):
        lines.append(".%s-%s { margin: %dpx %dpx; color: #%06x; }" % (
            rng.choice(WORDS), rng.choice(WORDS), rng.randint(0, 40), rng.randint(0, 40), rng.randint(0, 0xFFFFFF)))
    return "\n".join(lines)


def js(rng, n):
    lines = ["(function(){", "  var cfg = {site: '%s', id: %d};" % (rng.choice(DOMAINS), rng.randint(1, 10 ** 6))]
    for _ in range(n):
        r = rng.random()
        if r < 0.3:
            lines.append("  if (cfg.id < %d && cfg.site !== '') { track('%s'); }" % (rng.randint(1, 1000), rng.choice(WORDS)))
        elif r < 0.6:
            lines.append("  document.querySelectorAll('.%s').forEach(function(e){ e.dataset.x = '%d'; });" % (rng.choice(WORDS), rng.randint(1, 99)))
        else:
            lines.append("  var s = '<div class=\"%s\">' + cfg.site + '</div>';" % rng.choice(WORDS))
    lines.append("})();")
    return "\n".join(lines)


def image(rng):
    src = rng.choice(["/img/", "https://cdn.example.net/media/"]) + "%s-%d.%s" % (
        rng.choice(WORDS), rng.randint(1, 99999), rng.choice(["jpg", "png", "webp", "gif"]))
    # Always emit width and height: html_extract_images leaves them uninitialized
    # when absent (see family findings), so omitting them would make the fingerprint
    # depend on garbage memory.
    w = rng.choice(["%d" % rng.randint(16, 1920)] * 6 + ["auto", "100%"])
    h = rng.choice(["%d" % rng.randint(16, 1080)] * 6 + ["auto"])
    attrs = ['src="%s"' % src, 'width="%s"' % w, 'height="%s"' % h]
    if rng.random() < 0.85:
        attrs.append('alt="%s"' % words(rng, 2, 8).replace('"', ""))
    if rng.random() < 0.25:
        attrs.append('title="%s"' % words(rng, 1, 4).replace('"', ""))
    if rng.random() < 0.4:
        attrs.append('loading="lazy"')
    rng.shuffle(attrs)
    return "<img %s>" % " ".join(attrs)


def shop_table(rng, shape, idx):
    cols = ["SKU", "Product", "Price (€)", "Qty", "In stock", "Updated"]
    out = ['<table class="data" id="t%d">' % idx]
    if rng.random() < 0.5:
        out.append("<caption>%s</caption>" % words(rng, 3, 8))
    thead = rng.random() < shape.thead_prob
    if thead:
        out.append("<thead><tr>" + "".join("<th>%s</th>" % c for c in cols) + "</tr></thead><tbody>")
    else:
        out.append("<tr>" + "".join("<th>%s</th>" % c for c in cols) + "</tr>")
    total = 0
    for _ in range(rng.randint(*shape.table_rows)):
        qty = rng.randint(0, 500)
        total += qty
        cells = [
            "SKU-%06d" % rng.randint(0, 999999),
            '<a href="%s">%s</a>' % (url(rng), words(rng, 1, 4)),
            "%d.%02d" % (rng.randint(0, 2000), rng.randint(0, 99)),
            str(qty),
            rng.choice(["yes", "no", "true", "false", "✓", ""]),
            date(rng),
        ]
        if rng.random() < 0.05:
            cells = cells[: rng.randint(2, 5)]  # short row
        row = "<tr>" + "".join("<td>%s</td>" % c for c in cells) + "</tr>"
        if rng.random() < shape.malformed_prob * 0.2:
            row = row.replace("</td>", "", 2)  # unclosed cells, browsers tolerate
        out.append(row)
    if thead:
        out.append("</tbody>")
        if rng.random() < shape.tfoot_prob:
            out.append('<tfoot><tr><td colspan="3">Total</td><td>%d</td><td></td><td></td></tr></tfoot>' % total)
    out.append("</table>")
    return "\n".join(out)


def wiki_table(rng, shape, idx):
    ncols = rng.randint(2, 7)
    heads = [rng.choice(["Year", "Population", "Area (km²)", "Name", "Notes", "Ratio", "Rank", "Founded",
                         "Elevation", "Ref.", "Δ%"]) for _ in range(ncols)]
    out = ['<table class="wikitable sortable">']
    thead = rng.random() < shape.thead_prob
    hrow = "<tr>" + "".join("<th scope=col>%s</th>" % h for h in heads) + "</tr>"
    out.append("<thead>%s</thead><tbody>" % hrow if thead else hrow)
    for r in range(rng.randint(*shape.table_rows)):
        cells = []
        for c in range(ncols):
            k = rng.random()
            if c == 0:
                cells.append("<th scope=row>%s</th>" % rng.choice(UNICODE_WORDS + SECTIONS))
            elif k < 0.3:
                cells.append("<td>%s</td>" % "{:,}".format(rng.randint(0, 10 ** 7)))
            elif k < 0.5:
                cells.append("<td>%.3f</td>" % rng.uniform(-1, 1))
            elif k < 0.6:
                cells.append('<td rowspan="1" colspan="1">%s<sup><a href="#cite-%d">[%d]</a></sup></td>' % (
                    words(rng, 1, 3), r, rng.randint(1, 40)))
            elif k < 0.7:
                cells.append("<td></td>")
            else:
                cells.append("<td>%s</td>" % words(rng, 1, 10))
        out.append("<tr>" + "".join(cells) + "</tr>")
    if thead:
        out.append("</tbody>")
    out.append("</table>")
    return "\n".join(out)


def nav(rng, shape):
    items = ['<li><a href="/">Home</a></li>']
    for _ in range(rng.randint(*shape.nav_links)):
        items.append('<li><a href="/%s/">%s</a>' % (rng.choice(WORDS), rng.choice(SECTIONS + WORDS).title()))
        if rng.random() > shape.malformed_prob:
            items[-1] += "</li>"  # sometimes leave <li> open
    return '<header class="site"><nav aria-label="primary"><ul class="menu">%s</ul></nav></header>' % "".join(items)


def malformed(rng):
    return rng.choice([
        "</div>",                                   # stray close
        "<P>Uppercase paragraph %s",               # uppercase, unclosed
        "<b><i>misnested %s</b></i>",
        "<div class=unquoted data-flag>%s</div>",  # unquoted / valueless attrs
        "<p>Bare ampersand: R&D & more %s</p>",
        "<font color=red>%s</font>",
        "<center>%s",
    ]).replace("%s", "malformed text")


def comments(rng, shape, depth=0):
    out = ['<ol class="comments">']
    for _ in range(rng.randint(*shape.comments) if depth == 0 else rng.randint(0, 3)):
        out.append('<li class="comment" id="c%d"><div class="meta"><a href="/u/%d" rel="nofollow">%s</a> '
                   '<time datetime="%sT%02d:%02d:00Z">%s</time> <span class="score">%d</span></div>'
                   '<div class="body"><p>%s</p></div>' % (
                       rng.randint(1, 10 ** 6), rng.randint(1, 99999), person(rng), date(rng),
                       rng.randint(0, 23), rng.randint(0, 59), date(rng), rng.randint(-20, 500),
                       inline_text(rng, shape)))
        if depth < 3 and rng.random() < 0.3:
            out.append(comments(rng, shape, depth + 1))
        out.append("</li>")
    out.append("</ol>")
    return "\n".join(out)


def page(rng, shape, idx):
    title = words(rng, 4, 10)
    section = rng.choice(SECTIONS)
    out = ["<!DOCTYPE html>" if rng.random() < 0.9 else "", '<html lang="en">', "<head>",
           '<meta charset="utf-8">', "<title>%s | %s</title>" % (title, section),
           '<meta name="description" content="%s">' % words(rng, 8, 20).replace('"', ""),
           '<link rel="stylesheet" href="/static/site.%d.css">' % rng.randint(1, 50)]
    n_style = rng.randint(*shape.style_lines)
    if n_style:
        out.append("<style>\n%s\n</style>" % css(rng, n_style))
    out.append('<script type="application/ld+json">{"@type": "Article", "headline": "%s", "datePublished": "%s"}</script>'
               % (title.replace('"', "").replace("&", "and"), date(rng)))
    out.append("</head>")
    out.append('<body class="%s page-%d">' % (section.lower(), idx))
    out.append(nav(rng, shape))
    depth = rng.randint(*shape.nest_depth)
    out.append("".join('<div class="wrap-%d">' % d for d in range(depth)))
    out.append('<main id="content"><article id="post-%d" data-section="%s">' % (idx, section))
    out.append("<h1>%s</h1>" % title)
    out.append('<p class="byline">By <a href="/authors/%d" rel="author">%s</a> &middot; <time datetime="%s">%s</time></p>'
               % (rng.randint(1, 500), person(rng), date(rng), date(rng)))
    n_tables = rng.randint(*shape.tables)
    n_images = rng.randint(*shape.images)
    n_sections = rng.randint(*shape.sections)
    table_at = sorted(rng.randint(0, n_sections - 1) for _ in range(n_tables))
    image_at = sorted(rng.randint(0, n_sections - 1) for _ in range(n_images))
    t_idx = 0
    for s in range(n_sections):
        out.append('<h2 id="s%d">%s</h2>' % (s, words(rng, 2, 7)))
        for _ in range(rng.randint(*shape.paras)):
            close = "</p>" if rng.random() > shape.malformed_prob * 0.5 else ""
            out.append("<p>%s%s" % (inline_text(rng, shape), close))
        while image_at and image_at[0] == s:
            image_at.pop(0)
            if rng.random() < 0.5:
                out.append("<figure>%s<figcaption>%s</figcaption></figure>" % (image(rng), words(rng, 3, 12)))
            else:
                out.append(image(rng))
        r = rng.random()
        if r < 0.2:
            tag = rng.choice(["ul", "ol"])
            items = []
            for _ in range(rng.randint(2, 8)):
                li = "<li>%s" % words(rng, 3, 15)
                if rng.random() > shape.malformed_prob:
                    li += "</li>"
                items.append(li)
            out.append("<%s>%s</%s>" % (tag, "".join(items), tag))
        elif r < 0.3:
            out.append("<blockquote><p>%s</p><cite>%s</cite></blockquote>" % (words(rng, 15, 40), person(rng)))
        elif r < 0.38:
            out.append('<pre><code class="language-sql">SELECT %s FROM %s WHERE x &lt; %d;</code></pre>'
                       % (rng.choice(WORDS), rng.choice(WORDS), rng.randint(1, 100)))
        while table_at and table_at[0] == s:
            table_at.pop(0)
            gen = shop_table if shape.table_kind == "shop" else wiki_table
            out.append(gen(rng, shape, t_idx))
            t_idx += 1
        if rng.random() < shape.malformed_prob:
            out.append(malformed(rng))
    out.append("</article>")
    if rng.random() < shape.comment_prob:
        out.append('<section id="discussion"><h2>Comments</h2>%s</section>' % comments(rng, shape))
    out.append('<aside class="related"><h3>Related</h3><ul>')
    for _ in range(rng.randint(*shape.related_links)):
        out.append('<li><a href="%s">%s</a></li>' % (url(rng, 0.5), words(rng, 3, 9)))
    out.append("</ul></aside></main>")
    out.append("".join("</div>" for _ in range(depth)))
    out.append('<footer><p>&copy; %d Example Media. <a href="/privacy">Privacy</a> &bull; <a href="/terms">Terms</a></p></footer>'
               % rng.randint(2015, 2025))
    out.append("<script>\n%s\n</script>" % js(rng, rng.randint(*shape.script_lines)))
    out.append("</body></html>\n")
    return "\n".join(x for x in out if x)


def large_report(rng, target_bytes):
    """One data-export page: summary text, a few small tables, one huge table."""
    out = ["<!DOCTYPE html>", "<html><head><meta charset='utf-8'><title>Quarterly shipment export</title>",
           "<style>table{border-collapse:collapse} td,th{padding:2px}</style></head><body>",
           "<h1>Quarterly shipment export</h1>"]
    for _ in range(40):
        out.append("<p>%s</p>" % inline_text(rng, DEV_SHAPE))
    shape = Shape(**dict(DEV_SHAPE.__dict__, table_rows=(20, 60), thead_prob=1.0, tfoot_prob=1.0, malformed_prob=0.0))
    for i in range(5):
        out.append(shop_table(rng, shape, i))
    out.append('<table id="shipments"><thead><tr><th>Shipment</th><th>Origin</th><th>Destination</th>'
               "<th>Weight (kg)</th><th>Declared value</th><th>Shipped</th><th>Delivered</th><th>Status</th>"
               "<th>Notes</th></tr></thead><tbody>")
    size = sum(len(x) for x in out)
    n = 0
    statuses = ["delivered", "in transit", "held at customs", "returned", "lost"]
    while size < target_bytes:
        n += 1
        delivered = date(rng) if rng.random() < 0.8 else ""
        # Wide rows keep the row count near 35k: html_extract_tables grows
        # quadratically in rows, and 120k narrow rows take ~20 s.
        note = words(rng, 30, 90)
        if rng.random() < 0.3:
            note += ' <a href="%s">tracking</a>' % url(rng, 0.2)
        row = ("<tr><td>SHP-%08d</td><td>%s</td><td>%s</td><td>%.1f</td><td>%d.%02d</td><td>%s</td>"
               "<td>%s</td><td>%s</td><td>%s</td></tr>\n" % (
                   n, rng.choice(UNICODE_WORDS), rng.choice(UNICODE_WORDS), rng.uniform(0.1, 9000),
                   rng.randint(0, 99999), rng.randint(0, 99), date(rng), delivered,
                   rng.choice(statuses), note))
        out.append(row)
        size += len(row)
    out.append("</tbody></table>")
    out.append("<footer><p>Generated export &mdash; %d rows</p></footer></body></html>\n" % n)
    return "\n".join(out)


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    os.replace(tmp, path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--split", required=True, choices=["dev", "holdout"])
    args = ap.parse_args()

    shape = DEV_SHAPE if args.split == "dev" else HOLDOUT_SHAPE
    base = os.path.join(args.out, "html", args.split)
    rng = random.Random(shape.seed)
    for i in range(shape.n_pages):
        write(os.path.join(base, "pages", "page_%05d.html" % i), page(rng, shape, i))
    if args.split == "dev":
        write(os.path.join(base, "large", "report.html"), large_report(random.Random(777), 20 * 1024 * 1024))


if __name__ == "__main__":
    main()
