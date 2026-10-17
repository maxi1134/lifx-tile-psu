"""Copies ESP-IDF's bootloader_support into bootloader_components/ and adds
XMC A-series flash (20 70 17 .. 20 70 20) to its XMC whitelist.

ESP-IDF compiles every folder in bootloader_components/ into the bootloader,
and a folder named like an ESP-IDF component replaces the stock one. Only the
bootloader is affected; the app keeps the stock component.

Usage (after PlatformIO has downloaded framework-espidf, e.g. via
`python -m platformio pkg install`):

    python prepare_bootloader_support.py [path/to/framework-espidf]
"""

import os
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent

# The ESP-IDF the platform in platformio.ini builds with.
IDF_VERSION = "5.5.5"

OLD = [
    "    } else if (mfid == 0x50) {",
    "        if (cpid >= 0x15 && cpid <= 0x16) {",
    "            matched =  true;",
    "        }",
    "    }",
    "",
]
NEW = [
    "    } else if (mfid == 0x50) {",
    "        if (cpid >= 0x15 && cpid <= 0x16) {",
    "            matched =  true;",
    "        }",
    "    } else if (mfid == 0x70) {",
    "        // XMC A-series (XM25QH64A = 20 70 17, XM25QH128A = 20 70 18). Not on",
    "        // Espressif's list, so the stock bootloader runs the C-series startup",
    "        // flow on them and then refuses to boot. Accept them as XMC.",
    "        if (cpid >= 0x17 && cpid <= 0x20) {",
    "            matched = true;",
    "        }",
    "    }",
    "",
]


def find_idf() -> Path:
    if len(sys.argv) > 1:
        return Path(sys.argv[1])
    core = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
    return core / "packages" / "framework-espidf"


def main() -> int:
    idf = find_idf()
    src = idf / "components" / "bootloader_support"
    if not src.is_dir():
        print(f"Not found: {src}\nBuild once or run `python -m platformio pkg install` "
              "so PlatformIO downloads framework-espidf, or pass its path.")
        return 1

    # The whitelist hasn't changed in years, so it can't tell versions apart.
    # The rest of bootloader_support does change between releases, though, and
    # it must match the ESP-IDF this project builds with.
    vf = idf / "version.txt"
    ver = vf.read_text().strip().lstrip("v") if vf.is_file() else "unknown"
    if ver != IDF_VERSION:
        print(f"{idf} is ESP-IDF {ver}, not {IDF_VERSION}. Run `python -m platformio pkg install` "
              f"first, or pass the path of the {IDF_VERSION} framework-espidf (PlatformIO may "
              "have put it in packages/framework-espidf@src-...).")
        return 1

    dst = HERE / "bootloader_components" / "bootloader_support"
    shutil.rmtree(dst, ignore_errors=True)
    shutil.copytree(src, dst)

    target = dst / "bootloader_flash" / "src" / "bootloader_flash.c"
    with open(target, encoding="utf-8", newline="") as f:
        text = f.read()
    nl = "\r\n" if "\r\n" in text else "\n"
    old, new = nl.join(OLD), nl.join(NEW)
    if text.count(old) != 1:
        print(f"The XMC whitelist in {target} doesn't look like ESP-IDF 5.5's. "
              "Not patching; check bootloader_flash_xmc_a.patch by hand.")
        return 1
    with open(target, "w", encoding="utf-8", newline="") as f:
        f.write(text.replace(old, new))

    print(f"Copied {src} (ESP-IDF {ver})\nPatched {target}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
