import bisect


class SymbolMap:

    def __init__(self):
        self.map: dict[int, str] = {}
        self.map_dirty = False
        self.sorted_addrs: list[int] = []

    def __len__(self):
        return len(self.map)

    def clear(self):
        self.map.clear()
        self.sorted_addrs.clear()
        self.map_dirty = False

    def insert(self, addr: int, name: str):
        self.map[addr] = name
        self.map_dirty = True

    def load_from_file(self, path: str):
        # TODO: this is a big hack to support IDA and GCC maps
        IGNORE_PREFIXES = ['__mw_', '0', '(', 'loc_', 'locret_', 'def_', 'jpt_', 'off_', 'Abs ', 'dword_', 'word_', 'byte_', 'flt_']

        with open(path, 'r') as file:
            for line in file:
                parts = line.strip().split(maxsplit=1)
                if len(parts) != 2:
                    continue
                addr_str = parts[0]
                colon_index = addr_str.find(':')
                if colon_index != -1:
                    addr_str = addr_str[colon_index+1:]
                try:
                    addr = int(addr_str, 16)
                except ValueError:
                    continue
                name = parts[1]
                if any(name.startswith(prefix) for prefix in IGNORE_PREFIXES):
                    continue
                if ' = ' in name:
                    # rather use IDA names
                    continue
                name = name.replace('__', '::')
                name = name.replace('(void)', '()')
                self.insert(addr, name)

    def get(self, addr: int) -> str | None:
        return self.map.get(addr)
    
    def get_nearest(self, addr: int) -> tuple[int | None, str | None]:
        if self.map_dirty:
            self.sorted_addrs = sorted(self.map.keys())
            self.map_dirty = False

        idx = bisect.bisect_right(self.sorted_addrs, addr)
        if idx == 0:
            return None, None
        
        symbol_addr = self.sorted_addrs[idx - 1]

        if addr - symbol_addr > 0x10000:
            return None, None

        symbol_name = self.map[symbol_addr]
        return symbol_addr, symbol_name
