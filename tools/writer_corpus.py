"""Gathers what the Writer's language model learns from, and cleans it into text the board
can show: capitals, digits, and a little punctuation. Runs on GitHub, not on your computer.

Reads the web addresses listed in Writer/books.txt (Project Gutenberg books: their licence
text at the top and bottom is cut off) and every .txt file in Writer/books/.

Usage: python3 tools/writer_corpus.py <output file>
"""

import glob
import os
import re
import sys
import urllib.request

VOCAB = "\n !\"'(),-./0123456789:;?ABCDEFGHIJKLMNOPQRSTUVWXYZ"
SWAP = {"‘": "'", "’": "'", "“": '"', "”": '"', "—": "-", "–": "-", "_": "",
        "æ": "AE", "Æ": "AE", "&": "AND", "[": "(", "]": ")", "{": "(", "}": ")", "`": "'"}


def gutenberg_body(text):
    start = re.search(r"\*\*\* ?START OF (THE|THIS) PROJECT GUTENBERG[^\n]*\n", text)
    end = re.search(r"\*\*\* ?END OF (THE|THIS) PROJECT GUTENBERG", text)
    return text[start.end() if start else 0: end.start() if end else len(text)]


def clean(text):
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    for a, b in SWAP.items():
        text = text.replace(a, b)
    paragraphs = []
    for para in re.split(r"\n\s*\n", text):
        para = " ".join(para.split()).upper()
        para = "".join(c for c in para if c in VOCAB)
        para = re.sub(r" +", " ", para).strip()
        if len(para) >= 20:  # (skips headings, page numbers and such)
            paragraphs.append(para)
    return "\n".join(paragraphs) + "\n"


def main():
    out = sys.argv[1]
    sources = []
    with open("Writer/books.txt") as f:
        urls = [l.strip() for l in f if l.strip() and not l.startswith("#")]
    for url in urls:
        req = urllib.request.Request(url, headers={"User-Agent": "fish-writer-training"})
        raw = urllib.request.urlopen(req, timeout=60).read().decode("utf-8", errors="replace")
        sources.append((url, clean(gutenberg_body(raw))))
    for path in sorted(glob.glob("Writer/books/*.txt")):
        sources.append((path, clean(open(path, encoding="utf-8", errors="replace").read())))
    text = "".join(t for _, t in sources)
    with open(out, "w") as f:
        f.write(text)
    for name, t in sources:
        print(f"  {len(t):>9,} characters from {name}")
    print(f"wrote {len(text):,} characters, {len(text.split()):,} words, {text.count(chr(10)):,} paragraphs to {out}")
    print("  " + text[:300].replace("\n", " / "))


if __name__ == "__main__":
    main()
