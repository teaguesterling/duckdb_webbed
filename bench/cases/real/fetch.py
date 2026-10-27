#!/usr/bin/env python3
"""Fetch the real public datasets for the `real` bench family.

Usage: bench/cases/real/fetch.py <data_root> [name ...]

Writes <data_root>/real/<name>/. Each dataset is pinned by the sha256 of what
it writes: one file's hash, or for a directory the hash of its sorted
"path<TAB>sha256" lines. A dataset whose output already matches is skipped.
An empty pin prints the computed hash instead of checking it.

Large single-file sources stream through decompression up to a byte budget,
then stop after the next closing record tag and close the root. That leaves a
well-formed prefix of the release and downloads only what the prefix needs.

The xl datasets cut the same sources about ten times larger, plus an
OpenStreetMap extract. Fetch them by name: they are skipped by default.

Common Crawl pages may not be redistributed. Keep the data root private.
"""

import bz2
import contextlib
import gzip
import hashlib
import io
import os
import shutil
import subprocess
import sys
import zipfile
import zlib

MB = 1 << 20
CHUNK = 1 << 20


def sha_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(CHUNK), b""):
            h.update(block)
    return h.hexdigest()


def sha_output(path):
    if os.path.isfile(path):
        return sha_file(path)
    lines = []
    for dirpath, _, names in os.walk(path):
        for name in names:
            full = os.path.join(dirpath, name)
            lines.append(f"{os.path.relpath(full, path)}\t{sha_file(full)}\n")
    return hashlib.sha256("".join(sorted(lines)).encode()).hexdigest()


CURL = ["curl", "--fail", "--silent", "--show-error", "--location", "--user-agent", "duckdb_webbed-bench"]


@contextlib.contextmanager
def stream(url):
    """The body of url as a file object. Leaving early stops the transfer."""
    # Closing early makes curl report a write failure, so its stderr is dropped.
    proc = subprocess.Popen(CURL + [url], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    try:
        yield proc.stdout
    finally:
        proc.stdout.close()
        proc.terminate()
        proc.wait()


def download(url, out):
    subprocess.run(CURL + ["--retry", "3", "--output", out + ".tmp", url], check=True)
    os.replace(out + ".tmp", out)


def decompress_stream(url):
    """Yield decompressed chunks of a remote gzip or bzip2 file, across concatenated members."""
    new = bz2.BZ2Decompressor if url.endswith(".bz2") else lambda: zlib.decompressobj(wbits=31)
    with stream(url) as resp:
        d = new()
        while True:
            block = resp.read(CHUNK)
            if not block:
                return
            while block:
                yield d.decompress(block)
                if not d.eof:
                    break
                block = d.unused_data
                d = new()


def cut(url, out, record, root, budget):
    """Write a decompressed prefix of url, ending after the first </record> past budget bytes."""
    end = f"</{record}>".encode()
    written = 0
    tail = b""
    with open(out + ".tmp", "wb") as f:
        for chunk in decompress_stream(url):
            buf = tail + chunk
            if written + len(buf) >= budget:
                pos = buf.find(end, max(0, budget - written - len(end)))
                if pos >= 0:
                    f.write(buf[: pos + len(end)])
                    f.write(f"\n</{root}>\n".encode())
                    break
            keep = len(end) - 1
            if len(buf) <= keep:
                tail = buf
                continue
            f.write(buf[:-keep])
            written += len(buf) - keep
            tail = buf[-keep:]
        else:
            raise RuntimeError(f"{url}: ended before the cut")
    os.replace(out + ".tmp", out)


def fetch_clinvar(dest, budget=200 * MB):
    cut("https://ftp.ncbi.nlm.nih.gov/pub/clinvar/xml/ClinVarVCVRelease_2026-10.xml.gz",
        os.path.join(dest, "vcv.xml"), "VariationArchive", "ClinVarVariationRelease", budget)


def fetch_uniprot(dest, budget=300 * MB):
    # current_release moves on each UniProt release. A moved release changes the pin.
    cut("https://ftp.uniprot.org/pub/databases/uniprot/current_release/knowledgebase/complete/uniprot_sprot.xml.gz",
        os.path.join(dest, "sprot.xml"), "entry", "uniprot", budget)


def fetch_wiki(dest, budget=300 * MB):
    # Dated runs rotate off dumps.wikimedia.org after a few months.
    cut("https://dumps.wikimedia.org/simplewiki/20260901/simplewiki-20260901-stub-meta-history.xml.gz",
        os.path.join(dest, "stub_history.xml"), "page", "mediawiki", budget)


def fetch_osm(dest):
    # The planet directory keeps each month's first dump. The cut streams only its prefix.
    cut("https://planet.openstreetmap.org/planet/2026/planet-261005.osm.bz2",
        os.path.join(dest, "planet.osm"), "node", "osm", 3000 * MB)


def fetch_pubmed(dest):
    # Each file is kept gzipped and decompressed, so the gzip case has a plain twin to match.
    for n in ("0001", "1334"):
        gz = os.path.join(dest, f"pubmed26n{n}.xml.gz")
        download(f"https://ftp.ncbi.nlm.nih.gov/pubmed/baseline/pubmed26n{n}.xml.gz", gz)
        with gzip.open(gz) as src, open(gz[: -len(".gz")], "wb") as out:
            shutil.copyfileobj(src, out, CHUNK)


def fetch_irs990(dest):
    zpath = os.path.join(dest, "teos.zip")
    download("https://apps.irs.gov/pub/epostcard/990/xml/2024/2024_TEOS_XML_01A.zip", zpath)
    with zipfile.ZipFile(zpath) as z:
        names = sorted(n for n in z.namelist() if n.endswith(".xml"))[:5000]
        for n in names:
            with z.open(n) as src, open(os.path.join(dest, os.path.basename(n)), "wb") as out:
                shutil.copyfileobj(src, out)
    os.remove(zpath)


def fetch_cldr(dest):
    zpath = os.path.join(dest, "cldr.zip")
    download("https://unicode.org/Public/cldr/46/cldr-common-46.0.zip", zpath)
    with zipfile.ZipFile(zpath) as z:
        for n in z.namelist():
            if n.startswith("common/main/") and n.endswith(".xml"):
                with z.open(n) as src, open(os.path.join(dest, os.path.basename(n)), "wb") as out:
                    shutil.copyfileobj(src, out)
    os.remove(zpath)


def fetch_mesh(dest):
    download("https://nlmpubs.nlm.nih.gov/projects/mesh/MESH_FILES/xmlmesh/desc2026.xml",
             os.path.join(dest, "desc2026.xml"))


def fetch_cc(dest):
    """Write the first 2,000 HTML 200 responses of one WARC as pages/NNNNN.html."""
    url = ("https://data.commoncrawl.org/crawl-data/CC-MAIN-2024-51/segments/1733066035857.0/warc/"
           "CC-MAIN-20241201162023-20241201192023-00000.warc.gz")
    pages = os.path.join(dest, "pages")
    os.makedirs(pages, exist_ok=True)
    with stream(url) as resp:
        warc = gzip.GzipFile(fileobj=io.BufferedReader(resp, CHUNK))
        count = 0
        while count < 2000:
            headers = {}
            line = warc.readline()
            if not line:
                break
            if not line.startswith(b"WARC/"):
                continue
            while (line := warc.readline()) not in (b"\r\n", b""):
                key, _, value = line.decode("latin-1").partition(":")
                headers[key.strip().lower()] = value.strip()
            block = warc.read(int(headers["content-length"]))
            if headers.get("warc-type") != "response":
                continue
            head, _, body = block.partition(b"\r\n\r\n")
            status, _, rest = head.partition(b"\r\n")
            if b" 200" not in status or b"text/html" not in rest.lower():
                continue
            with open(os.path.join(pages, f"{count:05d}.html"), "wb") as f:
                f.write(body)
            count += 1


# name -> (fetch function, sha256 of the output)
DATASETS = {
    "clinvar": (fetch_clinvar, "2cad86642baaa50da97f5dc70dfa0b4740e9244ccf3b1d3af82c5bd3feb053fe"),
    "uniprot": (fetch_uniprot, "23112de491aabbf97d211cf58d3a1f2b76d18f1ae383ead951387696406c5835"),
    "wiki": (fetch_wiki, "760fbc0468f405db0d4e44526e7d5863b804c4b84fd7cd02eb51438e3bf375e4"),
    "pubmed": (fetch_pubmed, "e4698f8ec58abf14bd6b1e485f4481ee14f06c0af500df5207a0f84414b87a7d"),
    "irs990": (fetch_irs990, "bc7d630eb7613318ce667d108f44c7ad0d3d35b3749593e98fc215479df21c81"),
    "cldr": (fetch_cldr, "a7638133ac808614579a05e6c3865fe3680bdfdc8cd97b8756884b67a4165987"),
    "mesh": (fetch_mesh, "dcb6d465ed22dc28ed7d6b41be38c08184cc08750e7a3f94237ae62c0c61472c"),
    "cc": (fetch_cc, "4c8d600b503f7ccc03fae388ca09c09512414cd3edbfd3af7d33c32b8cd98a01"),
    "clinvar_xl": (lambda dest: fetch_clinvar(dest, 3000 * MB), ""),
    "uniprot_xl": (lambda dest: fetch_uniprot(dest, 3000 * MB), ""),
    "wiki_xl": (lambda dest: fetch_wiki(dest, 3000 * MB), ""),
    "osm": (fetch_osm, ""),
}
XL = {"clinvar_xl", "uniprot_xl", "wiki_xl", "osm"}


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    root = sys.argv[1]
    names = sys.argv[2:] or [n for n in DATASETS if n not in XL]
    failed = False
    for name in names:
        fetch, pin = DATASETS[name]
        dest = os.path.join(root, "real", name)
        if pin and os.path.isdir(dest) and sha_output(dest) == pin:
            print(f"{name}: up to date")
            continue
        shutil.rmtree(dest, ignore_errors=True)
        os.makedirs(dest)
        print(f"{name}: fetching", flush=True)
        fetch(dest)
        got = sha_output(dest)
        if not pin:
            print(f"{name}: unpinned, sha256 {got}")
        elif got != pin:
            print(f"{name}: checksum mismatch: {got}", file=sys.stderr)
            failed = True
        else:
            print(f"{name}: ok")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
