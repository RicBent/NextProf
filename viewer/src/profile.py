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
        func_addr, func_name = self.symbols.get_nearest(addr)
        if func_addr is None:
            return
        if func_addr not in self.funcs_by_addr:
            func = Function(address=func_addr, name=func_name)
            self.funcs_by_addr[func_addr] = func
            self.funcs.append(func)
        func = self.funcs_by_addr[func_addr]
        func.hit_count += 1
        if direct:
            func.hit_count_direct += 1

    def is_executable_address(self, addr: int) -> bool:
        # TODO: load this from symbol map
        if addr >= 0x00100000 and addr < 0x0056B000:
            return True
        if addr >= 0x006C4DD4 and addr < 0x0077B56C:
            return True
        return False
    
    def break_trace(self, addr: int) -> bool:
        # TODO: use thread enntry pc
        return addr == 0x100000

    def handle_sample_packet(self, packet: PacketSample):
        if self.main_thread_id is None:
            self.main_thread_id = packet.thread_id

        # TODO: right now we only track the main thread
        if packet.thread_id != self.main_thread_id:
            return

        self.track_hit(packet.pc)
        self.track_hit(packet.lr, direct=False)
        for addr in packet.stack:
            self.track_hit(addr, direct=False)

        chain = [self.symbols.get_nearest(addr)[0] for addr in [packet.pc, packet.lr] + packet.stack]
        chain = [addr for addr in chain if addr is not None and self.is_executable_address(addr)]
        for i in range(0, len(chain)):
            if self.break_trace(chain[i]):
                chain = chain[:i+1]
                break

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
