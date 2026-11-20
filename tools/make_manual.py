"""Builds docs/ParaEQ-Manual.html from docs/USER_MANUAL.md (needs: pip install markdown).

The HTML is self-contained (the screenshot is embedded), so it can be opened
from the installer's Start menu shortcut without anything else.
"""
import base64
import pathlib
import re
import sys

import markdown

root = pathlib.Path(__file__).resolve().parent.parent
docs = root / "docs"
md = (docs / "USER_MANUAL.md").read_text(encoding="utf-8")
body = markdown.markdown(md, extensions=["tables", "toc"])


def embed(match):
    src = match.group(1)
    path = docs / src
    if not path.exists():
        return match.group(0)
    data = base64.b64encode(path.read_bytes()).decode("ascii")
    return f'src="data:image/png;base64,{data}"'


body = re.sub(r'src="([^"]+\.png)"', embed, body)

html = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ParaEQ user manual</title>
<style>
 body {{ font-family: Segoe UI, Arial, sans-serif; max-width: 900px; margin: 0 auto; padding: 24px;
        background: #111317; color: #d9dee7; line-height: 1.55; }}
 h1, h2, h3 {{ color: #f2f4f8; }} h2 {{ border-bottom: 1px solid #2a2e37; padding-bottom: 4px; margin-top: 2em; }}
 a {{ color: #6cc4ff; }} code {{ background: #1a1d23; padding: 1px 5px; border-radius: 4px; }}
 table {{ border-collapse: collapse; margin: 12px 0; width: 100%; }}
 th, td {{ border: 1px solid #2a2e37; padding: 6px 10px; text-align: left; vertical-align: top; }}
 th {{ background: #1a1d23; }} img {{ max-width: 100%; border-radius: 8px; }} hr {{ border: 0; border-top: 1px solid #2a2e37; }}
</style></head><body>
{body}
</body></html>
"""
out = docs / "ParaEQ-Manual.html"
out.write_text(html, encoding="utf-8")
print(f"wrote {out} ({out.stat().st_size // 1024} KB)")
