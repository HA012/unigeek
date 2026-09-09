Import("env")

from pathlib import Path
import re

# The pinned ST25R3916 fork assumes a GPIO IRQ and a valid CS pin. M5Stack's
# U216 exposes only SDA/SCL on Grove, so I2C mode must poll the IRQ registers.
# Keep SPI behavior unchanged when a real IRQ pin is provided.
libdeps = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV")
root = libdeps / "ST25R3916-fork" / "src"


def patch(rel, pattern, replacement, flags=0, count=1, expected=1, already=None):
    path = root / rel
    if not path.exists():
        raise RuntimeError(f"ST25R3916 patch: missing {path}")
    text = path.read_text()
    if replacement in text or (already is not None and already in text):
        return
    new, n = re.subn(pattern, replacement, text, count=count, flags=flags)
    if n != expected:
        raise RuntimeError(f"ST25R3916 patch: pattern mismatch in {path} (matched {n}, expected {expected})")
    path.write_text(new)
    print(f"ST25R3916 patch: updated {rel}")


if root.exists():
    patch(
        "rfal_rfst25r3916.cpp",
        r"pinMode\(cs_pin, OUTPUT\);\s*\n\s*digitalWrite\(cs_pin, HIGH\);",
        "if (!i2c_enabled) {\\n    pinMode(cs_pin, OUTPUT);\\n    digitalWrite(cs_pin, HIGH);\\n  }",
    )
    patch(
        "rfal_rfst25r3916.cpp",
        r"pinMode\(int_pin, INPUT\);\s*\n\s*Callback<void\(\)>::func = std::bind\(&RfalRfST25R3916Class::setISRPending, this\);\s*\n\s*irq_handler = static_cast<ST25R3916IrqHandler>\(Callback<void\(\)>::callback\);\s*\n\s*attachInterrupt\(int_pin, irq_handler, RISING\);",
        "if (int_pin >= 0) {\\n    pinMode(int_pin, INPUT);\\n    Callback<void()>::func = std::bind(&RfalRfST25R3916Class::setISRPending, this);\\n    irq_handler = static_cast<ST25R3916IrqHandler>(Callback<void()>::callback);\\n    attachInterrupt(int_pin, irq_handler, RISING);\\n  }",
    )
    patch(
        "rfal_rfst25r3916.cpp",
        r"detachInterrupt\(int_pin\);",
        "if (int_pin >= 0) { detachInterrupt(int_pin); }",
    )
    patch(
        "rfal_rfst25r3916.cpp",
        r"return \(isr_pending \|\| \(digitalRead\(int_pin\) == HIGH\)\);",
        "return (int_pin < 0) ? true : (isr_pending || (digitalRead(int_pin) == HIGH));",
    )

    # Polling makes isISRPending() true on every I2C transaction. Guard the
    # software ISR against recursively re-entering itself while it reads IRQs.
    patch(
        "rfal_rfst25r3916.h",
        r"volatile bool bus_busy;",
        "volatile bool bus_busy;\\n    volatile bool isr_active;",
    )
    patch(
        "rfal_rfst25r3916.cpp",
        r"bus_busy = false;",
        "bus_busy = false;\\n  isr_active = false;",
        count=0,
        expected=2,
    )
    patch(
        "st25r3916_interrupt.cpp",
        r"void RfalRfST25R3916Class::st25r3916Isr\(void\)\s*\{(?P<body>.*?)\n\}",
        "void RfalRfST25R3916Class::st25r3916Isr(void)\\n{\\n  if (isr_active && int_pin < 0) return;\\n  isr_active = true;\\n\\g<body>\\n  isr_active = false;\\n}",
        re.S,
        already="if (isr_active && int_pin < 0) return;",
    )

    # In polling mode the IRQ registers are read once per worker pass. With a
    # real IRQ pin, retain the original behavior of draining while it is high.
    patch(
        "st25r3916_interrupt.cpp",
        r"while \(digitalRead\(int_pin\) == HIGH\) \{(?P<body>.*?)\n\s*\}",
        r"do {\g<body>\n  } while (int_pin >= 0 && digitalRead(int_pin) == HIGH);",
        re.S,
        already="while (int_pin >= 0 && digitalRead(int_pin) == HIGH);",
    )
    # MIFARE Classic uses 9-bit ISO14443A frames (8 data bits + explicit
    # parity). When RFAL is asked to keep parity, the final partial FIFO byte
    # is intentional and must not be reported as an incomplete-byte error.
    patch(
        "rfal_rfst25r3916.cpp",
        r"(?P<comment>/\* Check if the reception ends with an incomplete byte \(residual bits\) \*/\s*)if\s*\(\s*rfalFIFOStatusIsIncompleteByte\(\)\s*\)\s*\{",
        r"\g<comment>if (rfalFIFOStatusIsIncompleteByte() && !(gRFAL.TxRx.ctx.flags & RFAL_TXRX_FLAGS_PAR_RX_KEEP)) {",
        already="rfalFIFOStatusIsIncompleteByte() && !(gRFAL.TxRx.ctx.flags & RFAL_TXRX_FLAGS_PAR_RX_KEEP)",
    )
