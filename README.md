# STM32F429I-DISCO projects

Programs for the STM32F429I-DISCO board (the one with the 2.4" colour touchscreen).

Nothing is installed on the computer: GitHub builds every program, and Chrome
sends it to the board.

## Programs

- **Blink** — the two lights beside the screen take turns: green, then red, half a second each.
- **HelloScreen** — lights up the screen: a greeting, three colour bars and a seconds counter.
- **Fish** — a small ecosystem. Ten sea animals, each steered by its own neural-network
  brain, live over a seafloor of sand, rocks and plants.
  - Plants grow (faster when the water is rich in nutrients) and release food. Dead
    animals enrich the water.
  - Everyone starts as a tiny Fry. Eating earns meal points; enough of them and an animal
    **evolves**: Fry, Squid, Turtle, then Shark and Orca (if it has been a hunter) or Ray
    and Whale (if it has been gentle). The more it eats, the bigger it gets; bigger
    animals bite harder and have more health.
  - Hungry animals **hunt** clearly weaker ones; enemies **fight** whatever their size.
    Every bite is also a meal.
  - They form friendships and feuds, carry nametags, and a log reports what happens.
  - Tap the water to drop food (the blue button drops some at random). The panel shows
    each animal's evolution level, health (green), hunger (orange), best friend (+) and
    worst enemy (-).
  - **Personality**: five traits in the genes (boldness, sociability, aggression,
    loyalty, curiosity) change what an animal does, and its brain can sense them.
  - **Brains** have two hidden layers (29 senses, 16 cells, 8 cells, 3 decisions).
  - **Genetics**: every animal has a genome (speed, shape, build, thrift, the five
    traits, colour, pattern, and its brain). When one dies, the best-doing animal has a baby with
    a partner; the baby gets each gene from one parent or the other, with the odd
    mutation, and joins its mum's family. Each animal belongs to the family of one of the
    ten founders. The starting brains are trained on GitHub (`tools/train_fish.c`).

Each program is a folder with a `main.c`. Code they share lives in `common/`: the startup
code, the screen driver (`lcd.c`) and the font (`font.c`, drawn as pictures you can edit).

## The website

<https://atrociouslyterrific.github.io/stm32f429-disco-projects/> is one page with everything:

- **Flash** buttons that put a program on the board from Chrome.
- Whether the latest build passed, and the recent builds.
- A **live fish tank** running in the browser: the same rules and trained brains as the
  board (compiled to WebAssembly by `tools/fish_web.c`), with nametags and a log of events.
- **Show my board's tank**: with the board plugged in, the page reads the real tank out of
  the running chip over the USB cable and shows it live. Clicking the water then drops
  food into the board's tank.
- **Genes and family**: any animal's genome (body genes against its parents', and its
  brain genes as a strip), and its family tree.
- **Families**: a small chart per founder's family showing how many are alive over time.
- **Family tree**: every animal that has lived in the tank, founders at the top and one row
  per generation, with lines to both parents. Hover to light up an animal's whole line.
- **Inside a brain**: one animal's neural network, drawn live as it thinks.
- **Training progress**: the score per generation across every training run, updating
  live while a run is going.

Before the site is published, `tools/check_site.mjs` checks it on GitHub: the scripts
parse, the files are there, and a test tank lives for five minutes without going wrong.

## Putting a program on the board

1. Plug the board in with the mini-USB cable (the socket labelled USB ST-LINK).
2. Open the website above in Chrome.
3. Click **Flash** beside the program you want, pick **STM32 STLink** in Chrome's pop-up, and click the pop-up's **Connect**.
4. Wait a few seconds for "Done". Press the black reset button on the board if nothing changes.

The page is in `site/`. It uses a copy of [webstlink](https://github.com/devanlai/webstlink)
with two small fixes: boards with old factory ST-Link firmware (such as V2J17) are accepted, and erasing works on F4 chips.

## Training

The fish brains are trained by the **Train fish brains** workflow (`.github/workflows/train.yml`),
on GitHub's computers. It starts by itself when the tank's rules change, and can be started
by hand from the Actions tab ("Run workflow", with a number of minutes). Each run:

1. downloads the population from the last run (the `fish-brains` release) and carries on;
2. publishes its progress every 15 seconds to the `status` branch, which the website shows;
3. saves the population, the ten brains the board uses, and a summary back to the release;
4. triggers a build, so the board program and the website get the new brains.
