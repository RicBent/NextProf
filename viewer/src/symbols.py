import bisect


class SymbolMap:

    MAX_FUNC_LEN = 0x10000

    def __init__(self):
        self.map: dict[int, str] = {}
        self.map_dirty = False
        self.sorted_addrs: list[int] = []
        self.code_data: list[tuple[str, int, bytes]] = []

    def __len__(self):
        return len(self.map)

    def clear(self):
        self.map.clear()
        self.sorted_addrs.clear()
        self.code_data.clear()
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

        if addr - symbol_addr > self.MAX_FUNC_LEN:
            return None, None

        symbol_name = self.map[symbol_addr]
        return symbol_addr, symbol_name

    def read_code_data(self, addr: int, size: int) -> bytes | None:
        for _, code_addr, code_bytes in self.code_data:
            if addr >= code_addr and addr + size <= code_addr + len(code_bytes):
                offset = addr - code_addr
                return code_bytes[offset:offset + size]
        return None
    
    def load_code_from_file(self, path: str, addr: int):
        with open(path, 'rb') as file:
            data = file.read()

        # check for overlaps
        for code_path, code_addr, code_bytes in self.code_data:
            end_addr = code_addr + len(code_bytes)
            new_end_addr = addr + len(data)
            if not (new_end_addr <= code_addr or addr >= end_addr):
                raise ValueError(f'Code data from {path} overlaps with existing code data from {code_path}')

        self.code_data.append((path, addr, data))

    def is_after_bl(self, addr: int) -> bool:
        if len(self.code_data) == 0:
            # If no code loaded, assume all addresses are after a BL
            return True

        # Thumb
        if addr & 1:        
            # TODO
            return False
        
        # ARM
        else:
            code_bytes = self.read_code_data(addr - 4, 4)
            if code_bytes is None or len(code_bytes) != 4:
                return False
            
            instr = int.from_bytes(code_bytes, 'little')
        
            # BL<cond> <immediate>
            if (instr & 0x0F000000) == 0x0B000000:
                return True
            
            # BLX <immediate>
            if (instr & 0xFE000000) == 0xFA000000:
                return True
            
            # BLX<cond> <register>
            if (instr & 0x0FFFFFF0) == 0x012FFF30:
                return True
            
            return False
    
    def is_executable(self, addr: int) -> bool:
        # TODO: load this from symbol map
        if addr >= 0x00100000 and addr < 0x0056B000:
            return True
        if addr >= 0x006C4DD4 and addr < 0x00F00000:
            return True
        return False

