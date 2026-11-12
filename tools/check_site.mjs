// Checks the finished website before it is published. Runs on GitHub during the build,
// so mistakes are caught there rather than by opening the page and finding it broken.
//
// Usage: node tools/check_site.mjs <folder holding the finished site>

import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { execFileSync } from "node:child_process";

const site = process.argv[2];
const html = fs.readFileSync(path.join(site, "index.html"), "utf8");
let problems = 0;
const problem = (text) => { console.log("PROBLEM: " + text); problems++; };

// 1. Every script on the page must at least be valid JavaScript.
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "site-check-"));
const scripts = [...html.matchAll(/<script([^>]*)>([\s\S]*?)<\/script>/g)].filter(m => m[2].trim());
scripts.forEach(([, attrs, code], n) => {
  const file = path.join(tmp, "script" + n + (/type="module"/.test(attrs) ? ".mjs" : ".js"));
  fs.writeFileSync(file, code);
  try { execFileSync(process.execPath, ["--check", file], { stdio: "pipe" }); }
  catch (e) { problem("script " + n + " does not parse:\n" + e.stderr); }
});
console.log(scripts.length + " scripts parse");

// 2. The build fills in a few placeholders; none may be left.
for (const leftover of ["__BUILD__", "__REPO__"]) if (html.includes(leftover)) problem(leftover + " was not filled in");

// 3. Files the page loads must exist.
for (const [, file] of html.matchAll(/from "\.\/([^"]+)"/g)) if (!fs.existsSync(path.join(site, file))) problem("missing file " + file);
for (const file of ["fish.wasm", "status.json", "Fish.bin", "Blink.bin", "HelloScreen.bin"]) if (!fs.existsSync(path.join(site, file))) problem("missing file " + file);

// 4. Every element the scripts look up by name must be on the page.
const ids = new Set([...html.matchAll(/\bid="([^"]+)"/g)].map(m => m[1]));
const wanted = new Set([...html.matchAll(/(?:\$|getElementById)\("([\w-]+)"\)/g)].map(m => m[1]));
for (const id of wanted) if (!ids.has(id)) problem('the scripts look for id="' + id + '" but the page has none');
console.log(wanted.size + " element lookups match the page");

// 5. The fish rules, as the browser will run them.
const sim = (await WebAssembly.instantiate(fs.readFileSync(path.join(site, "fish.wasm")), {})).instance.exports;
for (const [, name] of html.matchAll(/\bsim\.(web_\w+)/g)) if (typeof sim[name] !== "function") problem("the page calls sim." + name + ", which fish.wasm does not have");

const N = sim.web_n_fish(), PER = sim.web_per_fish(), FISH_AT = sim.web_fish_at(), VIEW = sim.web_view_size();
const KINDS = ["Fry", "Squid", "Turtle", "Shark", "Ray", "Orca", "Whale"];
const EVENTS = ["bit", "ate", "starved", "born", "likes", "hates", "fought", "evolved"];
const state = () => new Float32Array(sim.memory.buffer, sim.web_state(), VIEW);
const events = () => new Int32Array(sim.memory.buffer, sim.web_events(), 1 + sim.web_max_events() * 4);

// Let a tank live for five minutes of tank time, with someone feeding it now and then.
sim.web_init(12345);
const seenEvents = new Array(EVENTS.length).fill(0), everKinds = new Set();
const TICKS = 30 * 300;
for (let t = 0; t < TICKS; t++) {
  if (t % 120 === 0) sim.web_feed_anywhere();
  sim.web_step();
  const ev = events();
  for (let n = 0; n < ev[0]; n++) {
    const type = ev[1 + n * 4];
    seenEvents[type]++;
    if (type === 7) everKinds.add(ev[3 + n * 4]);
    if (type === 3 && (ev[3 + n * 4] === ev[2 + n * 4] || ev[4 + n * 4] === ev[2 + n * 4])) problem("a baby was listed as its own parent");
  }
}
const s = state();
if (![...s].every(Number.isFinite)) problem("the tank's numbers went bad (not finite) after " + TICKS + " ticks");
for (let i = 0; i < N; i++) {
  const x = s[FISH_AT + i * PER], y = s[FISH_AT + i * PER + 1];
  if (!(x >= 0 && x <= 240 && y >= 0 && y <= 224)) problem("animal " + i + " left the tank: " + x + ", " + y);
}
const now = new Array(KINDS.length).fill(0);
for (let i = 0; i < N; i++) now[Math.round(s[FISH_AT + i * PER + 15])]++;
console.log("after 5 minutes in one tank:");
console.log("  food eaten " + s[1] + ", animals eaten " + s[2] + ", starved " + s[3] + ", born " + s[4] + ", water richness " + s[5].toFixed(2));
console.log("  events: " + EVENTS.map((e, n) => e + " " + seenEvents[n]).join(", "));
console.log("  living now: " + KINDS.map((k, n) => now[n] + " " + k).join(", "));
console.log("  kinds evolved into at some point: " + ([...everKinds].sort().map(k => KINDS[k]).join(", ") || "none"));

// Genetics and families
const GENE_RANGES = [[21, 0.85, 1.25, "speed"], [5, 0.8, 1.25, "shape"], [22, 0.85, 1.2, "build"], [23, 0.8, 1.2, "thrift"], [24, 0, 1, "boldness"]];
const ids = new Set();
let deepest = 0;
for (let i = 0; i < N; i++) {
  const o = FISH_AT + i * PER;
  for (const [at, lo, hi, name] of GENE_RANGES) if (!(s[o + at] >= lo - 1e-4 && s[o + at] <= hi + 1e-4)) problem(name + " gene out of range: " + s[o + at]);
  for (const at of [25, 26, 27]) if (!(s[o + at] >= 70 && s[o + at] <= 255)) problem("colour gene out of range: " + s[o + at]);
  if (!(s[o + 28] >= 0 && s[o + 28] <= 2)) problem("pattern gene out of range: " + s[o + 28]);
  const id = s[o + 16], gen = s[o + 20], family = s[o + 19];
  if (ids.has(id)) problem("two animals share the number " + id);
  ids.add(id);
  if (!(family >= 0 && family < N)) problem("family out of range: " + family);
  if (gen > 0 && (!s[o + 17] || !s[o + 18])) problem("a baby without two parents");
  deepest = Math.max(deepest, gen);
}
const families = new Set([...Array(N).keys()].map(i => s[FISH_AT + i * PER + 19]));
console.log("  genetics: deepest generation alive " + deepest + ", families still going " + families.size + " of " + N);
if (seenEvents[3] > 0 && deepest === 0) problem("babies were born but nobody alive is past generation 0");

// The brain viewer reads these directly; make sure they are where the page expects.
const nIn = sim.web_n_in(), nHid = sim.web_n_hid(), nOut = sim.web_n_out();
const weights = new Float32Array(sim.memory.buffer, sim.web_brain(0), nHid * nIn + nOut * (nHid + 1));
const decided = new Float32Array(sim.memory.buffer, sim.web_decided(), N * nOut);
if (![...weights, ...decided].every(Number.isFinite) || [...decided].some(v => Math.abs(v) > 1)) problem("brain numbers look wrong");

// 6. The board's "window" for the website must be the size the page will ask for.
const status = JSON.parse(fs.readFileSync(path.join(site, "status.json"), "utf8"));
const expected = 24 + VIEW * 4 + N * 8 + 4 + 8 * 40 + 4;
if (!status.fish_mirror) problem("status.json does not say where the board's tank lives");
else if (status.fish_mirror.size !== expected) problem("the board's tank block is " + status.fish_mirror.size + " bytes but the page expects " + expected);
else console.log("board tank block: " + expected + " bytes, as the page expects");

if (problems) { console.log(problems + " problem(s) found"); process.exit(1); }
console.log("website checks passed");
