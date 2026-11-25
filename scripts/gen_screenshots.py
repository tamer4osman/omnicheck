#!/usr/bin/env python3
"""OmniCheck screenshot pipeline (docs/screenshots/*.png).

Regenerates the README terminal captures from the REAL shipped binary:
every report line rendered in the PNGs is byte-exact tool output -- the
pipeline only adds terminal presentation (window chrome, token coloring),
the same claim the README captions make. Re-run after every adoption so
the captures track the live `packs:` line (the SPRINT-8 captures showed
four packs for nine sprints; this script ends that drift).

Stages:
  1. capture  -- run the oc commands, keep raw stdout bytes verbatim;
  2. render   -- token-color the lines into the dark terminal chrome
                 (HTML + the same style as the SPRINT-8 captures);
  3. shoot    -- Playwright/Chromium element screenshot, 2x scale;
  4. optimize -- adaptive-palette quantize so each PNG stays small
                 (the four SPRINT-8 captures weighed ~2.2 MB combined).

Usage: python3 scripts/gen_screenshots.py [--oc build/default/oc]
Requires: playwright (chromium) + Pillow. Writes docs/screenshots/.
"""
import os
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "docs", "screenshots")

# --------------------------------------------------------------- captures


def capture_definitions(oc):
    """(name, title, argv, env) tuples -- the six README captures."""
    cov = tempfile.mkdtemp(prefix="oc_cov_")
    for name in ("v8.json", "pycov.json"):
        with open(os.path.join(REPO, "tests", "fixtures", "coverage", name),
                  encoding="utf-8") as fh:
            text = fh.read().replace("@@OC_REPO_ROOT@@", REPO)
        with open(os.path.join(cov, name), "w", encoding="utf-8") as fh:
            fh.write(text)
    triage_env = dict(os.environ,
                      OC_OMNIDECIDE_BIN=os.path.join(REPO, "build/default/oc-omnidecide-provider"))
    return [
        ("oc-check-human",
         "oc check -- human report",
         [oc, "check", "examples", "--packs-dir", "packs", "--format", "human"],
         None),
        ("oc-check-triage",
         "oc check --triage -- human report",
         [oc, "check", "examples", "--packs-dir", "packs", "--format", "human",
          "--triage"],
         triage_env),
        ("oc-crap-leaderboard",
         "oc check --coverage -- CRAP leaderboard",
         [oc, "check", "examples", "--packs-dir", "packs", "--format", "human",
          "--coverage", os.path.join(cov, "v8.json"),
          "--coverage", os.path.join(cov, "pycov.json")],
         None),
        ("oc-supply-chain",
         "oc check --osv-snapshot -- supply-chain tier",
         [oc, "check", "tests/fixtures/supply", "--packs-dir", "packs",
          "--format", "human",
          "--osv-snapshot", "tests/fixtures/supply/osv_snapshot.json"],
         None),
        ("oc-compat-gate",
         "oc compat -- breaking-change gate",
         [oc, "compat", "--baseline", "tests/fixtures/compat/v1",
          "--current", "tests/fixtures/compat/v2", "--packs-dir", "packs"],
         None),
    ]


# ------------------------------------------------------------- colorizing

C = {
    "bg": "#262b3b", "fg": "#c8cde0", "dim": "#7f88a5", "cyan": "#56b6c2",
    "blue": "#61afef", "green": "#98c379", "yellow": "#e5c07b",
    "red": "#e06c75", "purple": "#c792ea", "orange": "#d19a66",
    "teal": "#4ec9b0", "white": "#e6e9f2",
}


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def span(color, text, bold=False):
    w = " font-weight:600" if bold else ""
    return f'<span style="color:{color}{w}">{esc(text)}</span>'


def highlight_human(line):
    """One human-report line -> colored HTML."""
    sev_color = {"error": C["red"], "warn": C["yellow"], "info": C["cyan"]}
    if line.startswith("omnicheck "):
        return span(C["teal"], line, bold=True)
    if line.startswith(("packs:", "analyzed:", "coverage:", "triage:",
                        "dup scan:", "supply:")):
        return span(C["dim"], line)
    if line.startswith("findings:"):
        return span(C["white"], line, bold=True)
    if line.startswith("CRAP leaderboard"):
        return span(C["teal"], line, bold=True)
    if line.startswith("omnicheck: error:"):
        return span(C["red"], line, bold=True)
    # rule-section header: [OC001] long-function (warn) -- 2 finding(s)
    if line.lstrip().startswith("[OC"):
        import re
        m = re.match(r"^(\s*\[OC\d+\])\s+(.*?)\s+(\((?:error|warn|info)\))"
                     r"(\s+-- .*)?$", line)
        if m:
            out = span(C["purple"], m.group(1), bold=True)
            out += " " + span(C["white"], m.group(2), bold=True)
            sev = m.group(3).strip("()")
            out += " " + span(sev_color[sev], m.group(3), bold=True)
            if m.group(4):
                out += span(C["dim"], m.group(4))
            return out
    # leaderboard row: "    128  path:line fn (comp 18, cov 66%)"
    import re
    m = re.match(r"^(\s+)(\d+)\s\s(\S+:\S+)\s+(\S+)\s+(\(.*)$", line)
    if m:
        return (m.group(1) + span(C["yellow"], m.group(2), bold=True) + "  "
                + span(C["blue"], m.group(3)) + " " + span(C["white"], m.group(4))
                + " " + span(C["dim"], m.group(5)))
    # finding row: "  path:range  Message"
    m = re.match(r"^(\s+)(\S+:\S+)(\s\s+.*)$", line)
    if m:
        return m.group(1) + span(C["blue"], m.group(2)) + span(C["fg"], m.group(3))
    # detail rows: symbol:, fp=..., triage: rank=...
    m = re.match(r"^(\s*)(symbol:|fp=|triage:)(\s*.*)$", line)
    if m:
        label, rest = m.group(2), m.group(3)
        if label == "triage:":
            m2 = re.match(r"^(\s+rank=)(\S+)(\s+confidence=)(\S+)$", rest)
            if m2:
                return (m.group(1) + span(C["dim"], label) + span(C["dim"], m2.group(1))
                        + span(C["green"], m2.group(2), bold=True)
                        + span(C["dim"], m2.group(3)) + span(C["green"], m2.group(4)))
            return m.group(1) + span(C["dim"], label + rest)
        if label == "symbol:":
            return m.group(1) + span(C["dim"], label) + span(C["purple"], rest)
        return m.group(1) + span(C["dim"], label + rest)
    return span(C["fg"], line)


def highlight_json(text):
    """Compat-report JSON -> colored HTML (one span stream)."""
    import re
    out = []
    for tok in re.split(r'("(?:[^"\\]|\\.)*"(?=\s*:)|"(?:[^"\\]|\\.)*"|\b\d+\b|[{}\[\],:])', text):
        if not tok:
            continue
        if tok.startswith('"') and tok.endswith('"'):
            # key (followed by :) vs string value -- split() keeps the
            # colon as its own token; check the NEXT non-empty token
            idx = text.index(tok) + len(tok)
            rest = text[idx:]
            is_key = rest.lstrip().startswith(":")
            color = C["blue"] if is_key else C["green"]
            out.append(span(color, tok))
        elif tok.lstrip("-").isdigit():
            out.append(span(C["orange"], tok))
        elif tok in "{}[]:,":

            out.append(esc(tok))
        else:
            out.append(esc(tok))
    return "".join(out)


def render_lines(name, text):
    lines = text.splitlines()
    if name == "oc-compat-gate":
        return [highlight_json(t) if t.strip() else "&nbsp;" for t in lines]
    return [highlight_human(t) if t.strip() else "&nbsp;" for t in lines]


# ------------------------------------------------------------------ html

PAGE = """<!doctype html><html><head><meta charset="utf-8"><style>
* {{ margin:0; padding:0; box-sizing:border-box; }}
body {{ background:transparent; font-family:'JetBrains Mono','Sarasa Mono SC',
       'DejaVu Sans Mono',monospace; }}
.window {{ width:{width}px; border-radius:10px; background:{bg};
  box-shadow:0 18px 45px rgba(8,10,18,.5); overflow:hidden; }}
.titlebar {{ display:flex; align-items:center; gap:8px; padding:11px 14px;
  border-bottom:1px solid rgba(255,255,255,.07); }}
.dot {{ width:12px; height:12px; border-radius:50%; }}
.r {{ background:#ff5f57; }} .y {{ background:#febc2e; }} .g {{ background:#28c840; }}
.title {{ margin-left:8px; font-size:13px; color:{dim}; letter-spacing:.2px; }}
.body {{ padding:14px 16px 18px; font-size:13px; line-height:1.5; }}
.out {{ white-space:pre; }}
.prompt {{ color:{fg}; margin-bottom:10px; }}
.prompt .arrow {{ color:{red}; font-weight:700; }}
.prompt .tool {{ color:{green}; font-weight:700; }}
.prompt .branch {{ color:{dim}; }}
.prompt .branch b {{ color:{purple}; font-weight:600; }}
.prompt .cmd {{ color:{fg}; }}
</style></head><body>
<div class="window" id="win">
  <div class="titlebar">
    <span class="dot r"></span><span class="dot y"></span><span class="dot g"></span>
    <span class="title">{title} &mdash; exit {exit}</span>
  </div>
  <div class="body">
    <div class="prompt"><span class="arrow">&#10095;</span> <span class="tool">omnicheck</span> <span class="branch">git:(<b>main</b>)</span> <span class="cmd">{cmd}</span></div>
{lines}
  </div>
</div>
</body></html>"""


def build_html(name, title, cmd, exit_code, text, width=1480):
    lines = render_lines(name, text)
    body = "\n".join(f'    <div class="out">{l}</div>' for l in lines)
    return PAGE.format(width=width, bg=C["bg"], dim=C["dim"], red=C["red"],
                       green=C["green"], purple=C["purple"], fg=C["fg"],
                       title=esc(title), exit=exit_code, cmd=esc(cmd),
                       lines=body)


# ------------------------------------------------- language matrix figure

# The 15-language capability matrix, rendered as a FIGURE (clearly not a
# terminal capture): same dark palette, tabular layout. One row per
# registered pack; every fact is the contracted value from
# docs/LANGUAGES.md / packs/<id>/pack.json / third_party/CMakeLists.txt.
MATRIX = [
    # language, pack, grammar (pin), scanner, resolver, coverage, supply, osv, entry
    ("JavaScript", "ts-js", "tree-sitter-javascript v0.25.0",
     "template chars", "v2/v3 + tsconfig paths", "Istanbul / V8",
     "package.json", "npm", "main.js, **/index.js globs"),
    ("TypeScript", "typescript", "tree-sitter-typescript v0.23.2 (ts)",
     "none", "v2/v3 + tsconfig paths", "Istanbul / V8",
     "package.json", "npm", "**/index.ts glob"),
    ("TSX", "tsx", "tree-sitter-typescript v0.23.2 (tsx)",
     "none", "v2/v3 + tsconfig paths", "Istanbul / V8",
     "package.json", "npm", "**/index.tsx glob"),
    ("Python", "python", "tree-sitter-python v0.25.0",
     "indent / dedent", "v3 package-root probing", "coverage.py JSON",
     "pyproject.toml, requirements.txt", "PyPI", "main.py, **/__main__.py globs"),
    ("Go", "go", "tree-sitter-go v0.25.0 (grammars org)",
     "none", "v1-go dotted -> whole package", "go coverprofile text",
     "go.mod", "Go", "main.go glob + package main fact"),
    ("Java", "java", "tree-sitter-java v0.23.5",
     "none", "v1-java class-file model", "JaCoCo XML",
     "pom.xml", "Maven", "Main.java, **/Application.java globs"),
    ("C#", "csharp", "tree-sitter-c-sharp v0.24.0",
     "interpolated strings, optional ;", "v1-csharp namespace dirs",
     "Coverlet Cobertura XML", ".csproj, packages.lock.json", "NuGet",
     "Program.cs glob + static Main fact"),
    ("Rust", "rust", "tree-sitter-rust v0.24.0",
     "raw strings, hash lifetimes", "v1-rust mod-tree + use-tree",
     "LCOV", "Cargo.toml, Cargo.lock", "crates.io",
     "lib.rs / main.rs + fn main fact"),
    ("Ruby", "ruby", "tree-sitter-ruby v0.23.1",
     "heredocs, delimited literals", "v1-ruby require_relative + load roots",
     "SimpleCov JSON", "Gemfile, Gemfile.lock", "rubygems", "bin/*, exe/* globs"),
    ("PHP", "php", "tree-sitter-php v0.24.2 (mixed php+html)",
     "template text, interpolation braces", "v1-php PSR-4/PSR-0 + require guard",
     "Clover XML", "composer.json, composer.lock", "packagist",
     "index.php, **/bin/* globs"),
    ("C", "c", "tree-sitter-c v0.23.4",
     "none", "v1-c quoted includes", "gcovr Cobertura XML",
     "none (honest empty)", "none", "main-definition parse fact"),
    ("C++", "cpp", "tree-sitter-cpp v0.23.4 (C grammar inlined)",
     "raw strings, angle brackets", "v1-cpp (shares the v1-c channel) + .h routing",
     "gcovr Cobertura XML", "none (honest empty)", "none",
     "main-definition parse fact"),
    ("Kotlin", "kotlin", "tree-sitter-kotlin 0.3.8 (fwcd)",
     "auto semicolons, interpolation", "v1-kotlin declared-fact",
     "Cobertura XML", "none (honest empty)", "none", "top-level main fact"),
    ("Swift", "swift", "tree-sitter-swift 0.7.1-with-generated-files (alex-pinkus)",
     "block comments, raw string parts", "v1-swift declared-fact + Package.swift targets",
     "Cobertura XML", "none (honest empty)", "none", "Package.swift target facts"),
    ("Scala", "scala", "tree-sitter-scala v0.23.4",
     "significant indentation", "v1-scala declared-fact + selector expansion",
     "JaCoCo / Cobertura XML (shared reader)", "none (honest empty)", "none",
     "object-main + @main facts"),
]

MATRIX_PAGE = """<!doctype html><html><head><meta charset="utf-8"><style>
* {{ margin:0; padding:0; box-sizing:border-box; }}
body {{ background:transparent; font-family:'JetBrains Mono','Sarasa Mono SC',
       'DejaVu Sans Mono',monospace; }}
.panel {{ width:1900px; border-radius:10px; background:{bg};
  box-shadow:0 18px 45px rgba(8,10,18,.5); overflow:hidden; padding:0 0 8px; }}
.panel h1 {{ font-size:14.5px; color:{teal}; padding:14px 16px 4px; }}
.panel h2 {{ font-size:11.5px; color:{dim}; font-weight:400; padding:0 16px 10px; }}
table {{ border-collapse:collapse; width:100%; }}
th {{ font-size:11px; color:{cyan}; text-align:left; padding:6px 10px;
  border-bottom:1px solid rgba(255,255,255,.14); }}
td {{ font-size:11.5px; color:{fg}; padding:5px 10px; vertical-align:top;
  border-bottom:1px solid rgba(255,255,255,.05); white-space:nowrap; }}
tr:nth-child(even) td {{ background:rgba(255,255,255,.022); }}
td.lang {{ color:{white}; font-weight:600; }}
td.pack {{ color:{cyan}; }}
td.pin {{ color:{orange}; }}
td.no {{ color:{dim}; }}
td.none {{ color:{dim}; font-style:italic; }}
td.yes {{ color:{green}; }}
.foot {{ font-size:10.5px; color:{dim}; padding:8px 16px 0; }}
</style></head><body>
<div class="panel" id="win">
  <h1>OmniCheck language support matrix &mdash; 15 registered packs, wave 3</h1>
  <h2>Every pack enables the full OC001&ndash;OC011 rule set &mdash; register, not train (SPRINT-24 tree, contracts in docs/LANGUAGES.md)</h2>
  <table>
    <tr><th>Language</th><th>Pack</th><th>Vendored grammar (pin)</th><th>External scanner</th><th>Import resolver</th><th>Coverage adapter</th><th>Supply manifests</th><th>OSV</th><th>Entry claim</th></tr>
{rows}
  </table>
  <div class="foot">Full-scope packs (OC001&ndash;OC011): java &rarr; csharp &rarr; rust &rarr; ruby &rarr; php &rarr; c &rarr; cpp &rarr; kotlin &rarr; swift &rarr; scala &mdash; the last ten adoptions; supply &ldquo;none (honest empty)&rdquo; = no canonical manifest reader is fabricated for the language.</div>
</div>
</body></html>"""


def gen_matrix(out_png):
    rows = []
    for lang, pack, pin, scanner, resolver, cov, supply, osv, entry in MATRIX:
        cells = [
            f'<td class="lang">{esc(lang)}</td>', f'<td class="pack">{esc(pack)}</td>',
            f'<td class="pin">{esc(pin)}</td>',
            f'<td class="{"yes" if scanner != "none" else "no"}">{esc(scanner)}</td>',
            f'<td>{esc(resolver)}</td>', f'<td>{esc(cov)}</td>',
            f'<td class="{"none" if supply.startswith("none") else ""}">{esc(supply)}</td>',
            f'<td class="{"none" if osv == "none" else "yes"}">{esc(osv)}</td>',
            f'<td>{esc(entry)}</td>',
        ]
        rows.append("    <tr>" + "".join(cells) + "</tr>")
    html = MATRIX_PAGE.format(bg=C["bg"], teal=C["teal"], dim=C["dim"],
                              cyan=C["cyan"], fg=C["fg"], white=C["white"],
                              orange=C["orange"], green=C["green"],
                              rows="\n".join(rows))
    shoot(html, out_png)
    return optimize(out_png)


# --------------------------------------------------------------- pipeline


def shoot(html, out_png):
    from playwright.sync_api import sync_playwright
    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page(viewport={"width": 1700, "height": 1400},
                                device_scale_factor=2)
        page.set_content(html)
        page.wait_for_timeout(120)
        page.locator("#win").screenshot(path=out_png)
        browser.close()


def optimize(path):
    """Adaptive-palette quantize: flat-color terminal art needs no Truecolor."""
    from PIL import Image
    img = Image.open(path).convert("RGB")
    pal = img.quantize(colors=256, method=Image.MEDIANCUT, dither=Image.Dither.NONE)
    pal.save(path, optimize=True)
    return os.path.getsize(path)


def main():
    oc = os.path.join(REPO, "build/default/oc")
    for i, a in enumerate(sys.argv):
        if a == "--oc" and i + 1 < len(sys.argv):
            oc = sys.argv[i + 1]
    os.makedirs(OUT, exist_ok=True)
    total = 0
    for name, title, argv, env in capture_definitions(oc):
        proc = subprocess.run(argv, cwd=REPO, env=env, capture_output=True,
                              text=True)
        text = proc.stdout
        if proc.stderr:
            text += proc.stderr
        cmd = " ".join(argv[1:])
        html = build_html(name, title, cmd, proc.returncode, text)
        raw = os.path.join(OUT, name + ".raw.png")
        final = os.path.join(OUT, name + ".png")
        shoot(html, raw)
        size = optimize(raw)
        os.replace(raw, final)
        total += size
        print(f"  {name}.png  exit={proc.returncode}  {size/1024:.0f} KB "
              f"({len(text.splitlines())} lines)")
    size = gen_matrix(os.path.join(OUT, "oc-language-matrix.png"))
    print(f"  oc-language-matrix.png  figure  {size/1024:.0f} KB ({len(MATRIX)} rows)")
    print(f"total {total/1024:.0f} KB")


if __name__ == "__main__":
    main()
