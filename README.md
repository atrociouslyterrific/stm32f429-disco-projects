# STM32F429I-DISCO projects

Programs for the STM32F429I-DISCO board (the one with the 2.4" colour touchscreen).

Nothing is installed on the computer: GitHub builds every program, and Chrome
sends it to the board.

## Programs

- **Blink** — the two lights beside the screen take turns: green, then red, half a second each.
- **HelloScreen** — lights up the screen: a greeting, three colour bars and a seconds counter.
- **Fish** — ten fish, each one different (size, shape, colour, markings, pace) and each
  steered by its own small neural-network brain. They get hungry and lose health when
  starving; they form friendships and feuds; hungry fish can bite and eat clearly smaller
  ones that aren't their friends. Tap the water to drop food (the blue button drops some
  at random). The panel under the tank shows each fish's health (green), hunger (orange),
  best friend (+) and worst enemy (-). When a fish dies, the best-doing fish has a baby
  with a slightly changed brain, so the brains keep evolving on the board. The starting
  brains are trained on GitHub during every build (`tools/train_fish.c`).

Each program is a folder with a `main.c`. Code they share lives in `common/`: the startup
code, the screen driver (`lcd.c`) and the font (`font.c`, drawn as pictures you can edit).

## Status

<https://atrociouslyterrific.github.io/stm32f429-disco-projects/status.html> shows whether the
latest build passed, the programs it published, and the fish brain training results.

## Putting a program on the board

1. Plug the board in with the mini-USB cable (the socket labelled USB ST-LINK).
2. Open <https://atrociouslyterrific.github.io/stm32f429-disco-projects/> in Chrome.
3. Click the button for the program you want, pick **STM32 STLink** in Chrome's pop-up, and click the pop-up's **Connect**.
4. Wait a few seconds for "Done". Press the black reset button on the board if nothing changes.

The page is in `site/`. It uses a copy of [webstlink](https://github.com/devanlai/webstlink)
with two small fixes: boards with old factory ST-Link firmware (such as V2J17) are accepted, and erasing works on F4 chips.
