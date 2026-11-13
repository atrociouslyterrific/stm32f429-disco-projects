// Opens the finished website in a real browser (Chromium, with no window) on GitHub's
// computer, lets it run for a few seconds, and checks that it actually works: no errors,
// and every section filled in. Takes a screenshot too. Runs during the build, before the
// site is published, so a broken page never goes live.
//
// Usage: node check_page.mjs <address of the page> <screenshot file>

import { chromium } from "playwright";

const [url, shot] = process.argv.slice(2);
const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 1000, height: 900 } });

const errors = [];
page.on("pageerror", (e) => errors.push("page error: " + e.message + "\n" + (e.stack || "")));
page.on("console", (m) => {
  if (m.type() !== "error") return;
  const text = m.text();
  // Expected here: no USB in this browser, and GitHub may refuse to answer quick repeat questions.
  if (/requestDevice|api\.github\.com|status\/training\.json|Failed to load resource/.test(text)) return;
  errors.push("console error: " + text);
});

await page.goto(url, { waitUntil: "load" });
// Let the tank run for a while, with food dropped now and then so there are babies.
for (let n = 0; n < 4; n++) { await page.waitForTimeout(2000); await page.click("#feed").catch(() => {}); }
// Then skip ahead five minutes, so there are babies and several generations to check.
for (let n = 0; n < 5; n++) await page.click("#skip");
await page.waitForTimeout(1500);
// The family tree's zoom: zooming out must make it smaller, and Fit must fit it in the box.
const treeWidth = () => page.evaluate(() => { const t = document.querySelector("#tree svg"); return t ? +t.getAttribute("width") : 0; });
const fitted = await treeWidth();
await page.click("#tree-out");
const zoomedOut = await treeWidth();
await page.click("#tree-in"); await page.click("#tree-in"); await page.click("#tree-in");
const zoomedIn = await treeWidth();
await page.click("#tree-fit");
const boxWidth = await page.evaluate(() => document.querySelector(".ft-wrap").clientWidth);
const refitted = await treeWidth();
// What is actually drawn on screen, not just the size asked for: zoom in, measure, photograph.
await page.click("#tree-in"); await page.click("#tree-in");
const drawn = await page.evaluate(() => {
  const t = document.querySelector("#tree svg"), r = t.getBoundingClientRect(), node = t.querySelector(".ft-node circle").getBoundingClientRect();
  return { asked: [+t.getAttribute("width"), +t.getAttribute("height")], drawn: [Math.round(r.width), Math.round(r.height)], dot: [Math.round(node.width), Math.round(node.height)] };
});
console.log("family tree zoomed in, asked for " + drawn.asked.join("x") + ", drawn at " + drawn.drawn.join("x") + ", one dot " + drawn.dot.join("x"));
await page.locator(".ft-wrap").screenshot({ path: process.argv[3].replace(/\.png$/, "-tree-zoomed.png") });
await page.click("#tree-fit");
await page.locator(".ft-wrap").screenshot({ path: process.argv[3].replace(/\.png$/, "-tree-fit.png") });
const drawnOk = Math.abs(drawn.drawn[0] - drawn.asked[0]) <= 2 && Math.abs(drawn.drawn[1] - drawn.asked[1]) <= 2 && Math.abs(drawn.dot[0] - drawn.dot[1]) <= 1;
const zoomOk = zoomedOut < fitted && zoomedIn > fitted && refitted <= boxWidth && refitted === fitted && drawnOk;
console.log("family tree zoom: fit " + fitted + "px, out " + zoomedOut + "px, in " + zoomedIn + "px, box " + boxWidth + "px");

const facts = await page.evaluate(() => {
  const text = (id) => (document.getElementById(id) || {}).textContent || "";
  const tank = document.getElementById("tank");
  let coloured = 0;
  if (tank) {
    const d = tank.getContext("2d").getImageData(0, 0, tank.width, tank.height).data;
    for (let i = 0; i < d.length; i += 4 * 97) if (d[i] + d[i + 1] + d[i + 2] > 60) coloured++;
  }
  return {
    build: text("build"),
    animalCards: document.querySelectorAll(".fishcard").length,
    firstCard: (document.querySelector(".fishcard") || {}).innerText || "",
    fed: text("c-fed"),
    born: text("c-born"),
    events: text("events").slice(0, 160),
    trainingChartLines: document.querySelectorAll("#chart svg path").length,
    trainingSummary: text("training-summary").slice(0, 120),
    geneRows: document.querySelectorAll("#gene-rows .gene").length,
    familyTree: text("family-tree").slice(0, 160),
    familyRows: document.querySelectorAll("#families .fam").length,
    familiesSummary: text("families-summary"),
    tankNote: text("tank-note").slice(0, 120),
    tankDrawn: coloured,
    flashButtons: document.querySelectorAll("button.go").length,
    evolutionSummary: text("evo-summary"),
    genePoolRows: document.querySelectorAll("#gene-pool .fam").length,
    newerBanner: !document.getElementById("newer").hidden,
    treeNodes: document.querySelectorAll("#tree .ft-node").length,
    treeSummary: text("tree-summary").slice(0, 90),
    treeLines: document.querySelectorAll("#tree .ft-line").length,
    treeNames: [...document.querySelectorAll("#tree .ft-node text")].map(t => t.textContent),
  };
});
await page.screenshot({ path: shot, fullPage: true });
await browser.close();

console.log(JSON.stringify(facts, null, 1));
const problems = [...errors];
if (facts.animalCards !== 10) problems.push("expected 10 animal cards, found " + facts.animalCards);
if (facts.geneRows < 10) problems.push("the genes section has " + facts.geneRows + " rows");
if (facts.familyRows !== 10) problems.push("the families section has " + facts.familyRows + " rows");
if (facts.trainingChartLines < 2) problems.push("the training chart has no lines");
if (/couldn't start/i.test(facts.tankNote)) problems.push("the live tank didn't start: " + facts.tankNote);
if (facts.tankDrawn < 50) problems.push("the tank looks blank");
if (facts.flashButtons !== 3) problems.push("expected 3 Flash buttons");
if (!/Right now:/.test(facts.evolutionSummary)) problems.push("the evolution diagram didn't fill in");
if (facts.genePoolRows < 9) problems.push("the gene pool has " + facts.genePoolRows + " rows");
if (facts.newerBanner) problems.push("the page thinks it is out of date");
if (!zoomOk) problems.push("the family tree's zoom doesn't work");
if (new Set(facts.treeNames).size !== facts.treeNames.length) problems.push("two animals in the family tree share a name");
delete facts.treeNames;
if (facts.treeNodes < 12) problems.push("the family tree has " + facts.treeNodes + " animals in it after five minutes");
if (!/over [2-9]\d* generations|over \d{2,} generations/.test(facts.treeSummary)) problems.push("the family tree has only one generation after five minutes: " + facts.treeSummary);
for (const p of problems) console.log("PROBLEM: " + p);
if (problems.length) process.exit(1);
console.log("the page works in a real browser");
