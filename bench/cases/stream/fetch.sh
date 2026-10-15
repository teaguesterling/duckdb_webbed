#!/usr/bin/env bash
# Fetch real public XML datasets for the stream family's holdout split.
#
# Usage: bench/cases/stream/fetch.sh <data_root>
# Writes <data_root>/stream/real/{dblp.xml,nasa.xml}.
#
# Source: University of Washington XML Data Repository
# https://aiweb.cs.washington.edu/research/projects/xmltk/xmldata/www/repository.html
#
# dblp.xml declares its HTML-style character entities (&uuml; etc.) in an
# external DTD. The extension parses with XML_PARSE_NONET and doesn't load
# external DTDs. The prepared copy drops the DOCTYPE line and rewrites each named
# entity as a numeric character reference, leaving bytes and ISO-8859-1
# encoding otherwise untouched. Both the download and the prepared file are
# checksummed.
set -euo pipefail

root="${1:?usage: fetch.sh <data_root>}"
dest="$root/stream/real"
dl="$dest/download"
mkdir -p "$dl"

base="https://aiweb.cs.washington.edu/research/projects/xmltk/xmldata/data"

# name|url path|sha256 of .gz|sha256 of prepared .xml
datasets=(
  "dblp|dblp/dblp.xml.gz|6d5c493718fb90009fa4ead920c8b75aea7bd9264671438262fd69002f63e8fb|81924151a5c003b509d4ebf016c90811d14dc8e2036d3ae0343079bfe3aab811"
  "nasa|nasa/nasa.xml.gz|80b4abef946629bfcba272a9c65062a0f79b298c2268800dd907f84ea05ae064|3ce2d9b8fe7fb028929c1fde86069282162d835ef015e296b636266e62e705e9"
)

sha() { shasum -a 256 "$1" | cut -d' ' -f1; }

prepare_dblp() {
  gzip -dc "$1" | python3 -I -c '
import html.entities, re, sys
data = sys.stdin.buffer.read().decode("latin-1")
data = re.sub(r"<!DOCTYPE[^>]*>\n?", "", data, count=1)
keep = {"amp", "lt", "gt", "quot", "apos"}
def repl(m):
    name = m.group(1)
    if name in keep:
        return m.group(0)
    return "&#%d;" % html.entities.name2codepoint[name]
data = re.sub(r"&([A-Za-z][A-Za-z0-9]*);", repl, data)
sys.stdout.buffer.write(data.encode("latin-1"))
' > "$2.tmp"
  mv "$2.tmp" "$2"
}

prepare_plain() {
  gzip -dc "$1" > "$2.tmp"
  mv "$2.tmp" "$2"
}

for entry in "${datasets[@]}"; do
  IFS='|' read -r name path gz_sum xml_sum <<<"$entry"
  gz="$dl/$name.xml.gz"
  out="$dest/$name.xml"

  if [[ -f "$out" && "$(sha "$out")" == "$xml_sum" ]]; then
    echo "$name: up to date"
    continue
  fi

  if [[ ! -f "$gz" || "$(sha "$gz")" != "$gz_sum" ]]; then
    echo "$name: downloading $base/$path"
    curl --fail --silent --show-error --location --retry 3 --output "$gz.tmp" "$base/$path"
    mv "$gz.tmp" "$gz"
  fi
  got="$(sha "$gz")"
  if [[ "$got" != "$gz_sum" ]]; then
    echo "$name: checksum mismatch for download: $got" >&2
    exit 1
  fi

  if [[ "$name" == "dblp" ]]; then
    prepare_dblp "$gz" "$out"
  else
    prepare_plain "$gz" "$out"
  fi
  got="$(sha "$out")"
  if [[ "$got" != "$xml_sum" ]]; then
    echo "$name: checksum mismatch for prepared file: $got" >&2
    exit 1
  fi
  echo "$name: ok"
done

rm -rf "$dl"
