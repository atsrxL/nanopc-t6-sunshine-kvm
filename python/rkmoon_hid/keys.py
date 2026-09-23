# SPDX-License-Identifier: GPL-3.0-or-later
"""Normalized Windows VK -> kvmd Web key names. No text/clipboard injection."""
KEYS = {**{v: f"Key{chr(v)}" for v in range(0x41, 0x5B)},
        **{v: f"Digit{chr(v)}" for v in range(0x30, 0x3A)},
        **{0x70+i: f"F{i+1}" for i in range(12)},
        **{0x60+i: f"Numpad{i}" for i in range(10)},
        0x08:"Backspace",0x09:"Tab",0x0C:"Numpad5",0x0D:"Enter",0x10:"ShiftLeft",
        0x11:"ControlLeft",0x12:"AltLeft",0x13:"Pause",0x14:"CapsLock",0x1B:"Escape",
        0x20:"Space",0x21:"PageUp",0x22:"PageDown",0x23:"End",0x24:"Home",
        0x25:"ArrowLeft",0x26:"ArrowUp",0x27:"ArrowRight",0x28:"ArrowDown",
        0x2C:"PrintScreen",0x2D:"Insert",0x2E:"Delete",0x5B:"MetaLeft",0x5C:"MetaRight",0x5D:"ContextMenu",
        0x6A:"NumpadMultiply",0x6B:"NumpadAdd",0x6D:"NumpadSubtract",0x6E:"NumpadDecimal",0x6F:"NumpadDivide",
        0x90:"NumLock",0x91:"ScrollLock",0xA0:"ShiftLeft",0xA1:"ShiftRight",0xA2:"ControlLeft",0xA3:"ControlRight",0xA4:"AltLeft",0xA5:"AltRight",
        0xBA:"Semicolon",0xBB:"Equal",0xBC:"Comma",0xBD:"Minus",0xBE:"Period",0xBF:"Slash",
        0xC0:"Backquote",0xDB:"BracketLeft",0xDC:"Backslash",0xDD:"BracketRight",0xDE:"Quote",0xE2:"IntlBackslash"}
# Generic and left VKs alias the same physical HID key; Lease uses reference sets, not blind key-up.
BUTTONS = {1:"left",2:"middle",3:"right",4:"up",5:"down"}
