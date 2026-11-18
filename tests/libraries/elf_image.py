"""Robot Framework library for static checks on the firmware ELF image.

Uses pyelftools, so it works the same for images built with clang or GCC.
"""

import re

from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection


class ElfImage:
    ROBOT_LIBRARY_SCOPE = "GLOBAL"

    def __init__(self, firmware="build/firmware.elf"):
        self._firmware = firmware

    def entry_point_should_be(self, address):
        """Fail unless the ELF entry point equals `address` (e.g. 0x40000000)."""
        with open(self._firmware, "rb") as f:
            entry = ELFFile(f).header.e_entry
        expected = int(address, 0)
        if entry != expected:
            raise AssertionError(f"Entry point is {entry:#x}, expected {expected:#x}")

    def get_section_size(self, name):
        """Return the size of section `name` in bytes, or 0 if it doesn't exist."""
        with open(self._firmware, "rb") as f:
            section = ELFFile(f).get_section_by_name(name)
            return 0 if section is None else section.data_size

    def section_should_not_exist(self, name):
        with open(self._firmware, "rb") as f:
            if ELFFile(f).get_section_by_name(name) is not None:
                raise AssertionError(f"Section {name} exists in the image")

    def get_symbol_address(self, name):
        """Return the address of symbol `name`. Fails if it isn't defined."""
        for symbol in self._symbols():
            if symbol.name == name:
                return symbol["st_value"]
        raise AssertionError(f"Symbol {name} not found")

    def symbols_should_not_match(self, *patterns):
        """Fail if any symbol name matches one of the given regexes."""
        regexes = [re.compile(p) for p in patterns]
        offenders = sorted(
            {s.name for s in self._symbols() if any(r.search(s.name) for r in regexes)}
        )
        if offenders:
            raise AssertionError("Unexpected symbols:\n" + "\n".join(offenders))

    def get_loaded_size(self):
        """Total size of all allocated sections that occupy file space
        (code + read-only data + initialized data)."""
        total = 0
        with open(self._firmware, "rb") as f:
            for section in ELFFile(f).iter_sections():
                flags = section["sh_flags"]
                is_alloc = flags & 0x2  # SHF_ALLOC
                if is_alloc and section["sh_type"] != "SHT_NOBITS":
                    total += section.data_size
        return total

    def _symbols(self):
        with open(self._firmware, "rb") as f:
            elf = ELFFile(f)
            symbols = []
            for section in elf.iter_sections():
                if isinstance(section, SymbolTableSection):
                    symbols.extend(section.iter_symbols())
            return symbols
