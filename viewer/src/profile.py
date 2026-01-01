from .packet import parse_packet, PacketSample
from .symbols import SymbolMap

from dataclasses import dataclass, field


@dataclass
class Function:
    address: int
    name: str
    hit_count: int = 0
    hit_count_direct: int = 0
    callees: dict[int, int] = field(default_factory=dict)   # callee_addr -> call_count
    

class Profile:

    def __init__(self, symbols: SymbolMap):
        self.main_thread_id: int | None = None
        self.symbols = symbols
        self.funcs = list[Function]()
        self.funcs_by_addr: dict[int, Function] = {}

    def track_hit(self, addr: int, direct: bool = True):
        func_name = self.symbols.get(addr)
        if func_name is None:
            return
        if addr not in self.funcs_by_addr:
            func = Function(address=addr, name=func_name)
            self.funcs_by_addr[addr] = func
            self.funcs.append(func)
        func = self.funcs_by_addr[addr]
        func.hit_count += 1
        if direct:
            func.hit_count_direct += 1
    
    def break_trace(self, addr: int) -> bool:
        # TODO: use thread enntry pc
        return addr == 0x100000
    
    def debug_address_info_str(self, addr: int) -> str:
        func_addr, func_name = self.symbols.get_nearest(addr)

        if func_name is None:
            func = 'None'
        else:
            if len(func_name) > 80:
                func_name_trunc = func_name[:77] + '...'
            else:
                func_name_trunc = func_name
            func = f'0x{func_addr:08X} : {func_name_trunc}'
        
        ex = 'X' if self.symbols.is_executable(addr) else '_'
        ab = 'B' if self.symbols.is_after_bl(addr) else '_'

        return f'0x{addr:08X} : {ex}{ab} - {func}'
    
    def debug_sample_info_print(self, packet: PacketSample):
        print(' pc   - ' + self.debug_address_info_str(packet.pc))
        print(' lr   - ' + self.debug_address_info_str(packet.lr))
        for i, addr in enumerate(packet.stack):
            s = self.debug_address_info_str(addr)
            print(f' {i*4:04X} - {s}')

    def handle_sample_packet(self, packet: PacketSample):
        if self.main_thread_id is None:
            self.main_thread_id = packet.thread_id

        # TODO: right now we only track the main thread
        if packet.thread_id != self.main_thread_id:
            return

        stack_return_addrs = [
            addr for addr in packet.stack
            if self.symbols.is_executable(addr) and self.symbols.is_after_bl(addr)
        ]

        stack_return_addrs_exec = [
            addr for addr in packet.stack
            if self.symbols.is_executable(addr)
        ]

        chain = []

        for addr in [packet.pc] + stack_return_addrs:
            nearest = self.symbols.get_nearest(addr)[0]
            if nearest is None:
                continue

            chain.append(nearest)

            if self.break_trace(nearest):
                break

        for i, addr in enumerate(chain):
            self.track_hit(addr, direct=(i == 0))

        for i in range(len(chain) - 1):
            callee_addr = chain[i]
            caller_addr = chain[i + 1]

            caller_func = self.funcs_by_addr[caller_addr]
            if callee_addr in caller_func.callees:
                caller_func.callees[callee_addr] += 1
            else:
                caller_func.callees[callee_addr] = 1

    def handle_packet(self, packet):
        if isinstance(packet, PacketSample):
            self.handle_sample_packet(packet)

    def load_from_file(self, path: str, offset: int = 0):
        with open(path, 'rb') as file:
            file.seek(offset)
            data = file.read()
        data_view = memoryview(data)
        pos = 0
        while pos < len(data):
            packet = parse_packet(data_view[pos:])
            pos += packet.size
            self.handle_packet(packet)
        return pos
