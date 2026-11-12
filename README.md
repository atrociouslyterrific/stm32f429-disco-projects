# STM32F429I-DISCO projects

Programs for the STM32F429I-DISCO board (the one with the 2.4" colour touchscreen).

Nothing is installed on the computer: GitHub builds every program, and Chrome
sends it to the board.

## Programs

- **Blink** — the two lights beside the screen take turns: green, then red, half a second each.

## Putting a program on the board

1. Plug the board in with the mini-USB cable (the socket labelled USB ST-LINK).
2. Download `Blink.bin` from the [latest release](../../releases/tag/latest).
3. Open <https://devanlai.github.io/webstlink/demo/> in Chrome.
4. Click **Connect** and pick **STM32 STLink**. If it asks for the MCU variant, pick the STM32F429 with 2 MB of flash (ZI).
5. In the flash section, type the address `0x08000000`, choose `Blink.bin`, and click **Flash**.
6. Press the black reset button on the board if the lights don't start.

The screen stays blank while Blink runs: this program only uses the lights.
