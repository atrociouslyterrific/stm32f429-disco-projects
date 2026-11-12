# STM32F429I-DISCO projects

Programs for the STM32F429I-DISCO board (the one with the 2.4" colour touchscreen).

Nothing is installed on the computer: GitHub builds every program, and Chrome
sends it to the board.

## Programs

- **Blink** — the two lights beside the screen take turns: green, then red, half a second each.
- **HelloScreen** — lights up the screen: a greeting, three colour bars and a seconds counter.
- **Fish** — ten fish, each one different (size, shape, colour, markings, pace) and each
  steered by its own small neural-network brain. Every fish carries a nametag. They get
  hungry and lose health when starving; the more they eat, the bigger they grow; they form
  friendships and feuds; hungry fish can bite and eat clearly smaller ones that aren't
  their friends. Tap the water to drop food (the blue button drops some at random). The
  panel under the tank shows each fish's health (green), hunger (orange), best friend (+)
  and worst enemy (-), and a three-line log reports what happens. When a fish dies, the
  best-doing fish has a baby with a slightly changed brain, so the brains keep evolving on
  the board. The starting brains are trained on GitHub during every build
  (`tools/train_fish.c`).

Each program is a folder with a `main.c`. Code they share lives in `common/`: the startup
code, the screen driver (`lcd.c`) and the font (`font.c`, drawn as pictures you can edit).

## The website

<https://atrociouslyterrific.github.io/stm32f429-disco-projects/> is one page with everything:

- **Flash** buttons that put a program on the board from Chrome.
- Whether the latest build passed, and the recent builds.
- A **live fish tank** running in the browser: the same rules and trained brains as the
  board (compiled to WebAssembly by `tools/fish_web.c`), with nametags and a log of events.
- **Training progress**: the score per generation from this build's training run.

## Putting a program on the board

1. Plug the board in with the mini-USB cable (the socket labelled USB ST-LINK).
2. Open the website above in Chrome.
3. Click **Flash** beside the program you want, pick **STM32 STLink** in Chrome's pop-up, and click the pop-up's **Connect**.
4. Wait a few seconds for "Done". Press the black reset button on the board if nothing changes.

The page is in `site/`. It uses a copy of [webstlink](https://github.com/devanlai/webstlink)
with two small fixes: boards with old factory ST-Link firmware (such as V2J17) are accepted, and erasing works on F4 chips.
