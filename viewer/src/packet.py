from dataclasses import dataclass


PACKET_MAGIC = b'NP'

@dataclass
class PacketSample:
    KIND = 1

    thread_id: int
    pc: int
    lr: int
    stack: list[int]

    @staticmethod
    def parse(data: memoryview) -> 'PacketSample':
        stack_size = int.from_bytes(data[16:20], 'little')
        stack = [int.from_bytes(data[20 + i*4:24 + i*4], 'little') for i in range(stack_size)]
        return PacketSample(
            thread_id=int.from_bytes(data[4:8], 'little'),
            pc=int.from_bytes(data[8:12], 'little'),
            lr=int.from_bytes(data[12:16], 'little'),
            stack=stack,
        )

    @property
    def size(self) -> int:
        return 5*4 + len(self.stack)

_packet_classes = [PacketSample]

_packets_by_kind = {
    c.KIND: c
    for c in _packet_classes
}

def parse_packet(data: memoryview) -> PacketSample:
    if len(data) < 4:
        raise ValueError('Data too short to contain packet kind')
    if data[0:2] != PACKET_MAGIC:
        raise ValueError('Invalid packet magic')
    kind = int.from_bytes(data[2:4], 'little')
    cls = _packets_by_kind.get(kind)
    if cls is None:
        raise ValueError(f'Unknown packet kind: {kind}')
    return cls.parse(data)
