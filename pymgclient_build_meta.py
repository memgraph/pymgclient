# pymgclient_build_meta.py
import os
from pathlib import Path

__version__ = os.getenv("PYMGCLIENT_OVERRIDE_VERSION", "1.5.1")

_readme_text = Path("README.md").read_text(encoding="utf-8")
__readme__ = "\n".join(_readme_text.splitlines()[2:]).lstrip()
