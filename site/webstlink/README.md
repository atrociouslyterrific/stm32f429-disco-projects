# webstlink (copied in)

This folder is a copy of the `src/` folder of <https://github.com/devanlai/webstlink>
(commit 4d00a1b, MIT licence — see LICENSE). It lets a web page talk to the board's
ST-Link programmer over USB.

One change from the original: in `lib/stlinkv2.js`, older ST-Link firmware (below J21)
now gives a warning instead of stopping, so boards with factory firmware can be flashed.
