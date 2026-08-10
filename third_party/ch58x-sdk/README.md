# WCH CH58x SDK

The CH582M target builds against the official
[`openwch/ch583`](https://github.com/openwch/ch583) SDK, pinned by
`tools/setup_ch582m_sdk.py`. The setup step uses a sparse checkout under
`.cache/ch58x-sdk`; the SDK is deliberately not copied into this repository.

The upstream repository is Apache-2.0. Individual BLE sources and binaries
also state that they are intended for microcontrollers manufactured by
Nanjing Qinheng Microelectronics. The CH582M target satisfies that condition;
the SDK must not be reused by another target.

Pinned revision: `bd508ad7ceed48377619837051412a651952857f`
