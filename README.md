# STM32F429I-DISCO projects

Programs for the STM32F429I-DISCO board (the one with the 2.4" colour touchscreen).

Nothing is installed on the computer: GitHub builds every program, and Chrome
sends it to the board.

## Programs

- **Blink** — the two lights beside the screen take turns: green, then red, half a second each.
- **HelloScreen** — lights up the screen: a greeting, three colour bars and a seconds counter.

Each program is a folder with a `main.c`. Code they share lives in `common/`: the startup
code, the screen driver (`lcd.c`) and the font (`font.c`, drawn as pictures you can edit).

## Putting a program on the board

1. Plug the board in with the mini-USB cable (the socket labelled USB ST-LINK).
2. Open <https://atrociouslyterrific.github.io/stm32f429-disco-projects/> in Chrome.
3. Click the button for the program you want, pick **STM32 STLink** in Chrome's pop-up, and click the pop-up's **Connect**.
4. Wait a few seconds for "Done". Press the black reset button on the board if nothing changes.

The page is in `site/`. It uses a copy of [webstlink](https://github.com/devanlai/webstlink)
with two small fixes: boards with old factory ST-Link firmware (such as V2J17) are accepted, and erasing works on F4 chips.
