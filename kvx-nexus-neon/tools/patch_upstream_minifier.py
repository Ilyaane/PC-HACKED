#!/usr/bin/env python3
"""Replace upstream's network-dependent pre-build minifier with a local fallback.

Only modifies a disposable checkout. HTML/JS/CSS are not minified, but are
still losslessly gzip-compressed and embedded. This avoids the third-party
Toptal endpoint and preserves original runtime web-interface semantics.
"""
from pathlib import Path
import re
import sys

path=Path(sys.argv[1])
source=path.read_text(encoding="utf-8")
start=source.index("def minify_css(c):")
end=source.index("# gzip web files",start)
replacement="""def minify_css(c):
    return c.read()


def minify_js(js):
    return js.read()


def minify_html(html):
    return html.read()


"""
assert "requests.post" in source[start:end], "Upstream minifier already changed; review before patching"
updated=source[:start]+replacement+source[end:]
assert updated.count("def minify_js") == 1
assert updated.count("def minify_html") == 1
assert updated.count("def minify_css") == 1
path.write_text(updated,encoding="utf-8")
print("Installed deterministic offline fallback: web assets remain gzip-compressed, unminified.")
