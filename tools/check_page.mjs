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
await page.waitForTimeout(9000); // let the tank run for a while

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
for (const p of problems) console.log("PROBLEM: " + p);
if (problems.length) process.exit(1);
console.log("the page works in a real browser");
