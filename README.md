# STM32F429I-DISCO projects

Programs for the STM32F429I-DISCO board (the one with the 2.4" colour touchscreen).

Nothing is installed on the computer: GitHub builds every program, and Chrome
sends it to the board.

## Programs

- **Blink** — the two lights beside the screen take turns: green, then red, half a second each.
- **HelloScreen** — lights up the screen: a greeting, three colour bars and a seconds counter.
- **Fish** — a small ecosystem. Ten fish, each steered by its own neural-network brain,
  live over a seafloor of sand, rocks and plants.
  - Plants grow (faster when the water is rich in nutrients) and release food; the two
    smallest species also nibble the plants. Dead fish enrich the water.
  - Eating earns meal points; enough of them and a fish **evolves** into the next species
    (Fry, Darter, Striper, Hunter, Titan), each bigger and stronger with a new shape.
  - Hungry fish **hunt** clearly weaker fish; enemies **fight** whatever their size.
  - Fish form friendships and feuds, carry nametags, and a log reports what happens.
  - Tap the water to drop food (the blue button drops some at random). The panel shows
    each fish's species level, health (green), hunger (orange), best friend (+) and worst
    enemy (-).
  - When a fish dies, the best-doing fish has a baby with a slightly changed brain, so the
    brains keep evolving on the board. The starting brains are trained on GitHub during
    every build (`tools/train_fish.c`).

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
- **Training progress**: the score per generation from this build's training run.

## Putting a program on the board

1. Plug the board in with the mini-USB cable (the socket labelled USB ST-LINK).
2. Open the website above in Chrome.
3. Click **Flash** beside the program you want, pick **STM32 STLink** in Chrome's pop-up, and click the pop-up's **Connect**.
4. Wait a few seconds for "Done". Press the black reset button on the board if nothing changes.

The page is in `site/`. It uses a copy of [webstlink](https://github.com/devanlai/webstlink)
with two small fixes: boards with old factory ST-Link firmware (such as V2J17) are accepted, and erasing works on F4 chips.
