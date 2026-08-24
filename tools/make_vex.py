#!/usr/bin/env python3
import struct
import sys

def make_vex(entry, code_vaddr, code_data, data_vaddr=0, data_data=b'', bss_size=0, stack_size=4096, output='out.vex'):
    magic = 0x00000056  # 'V'，小端
    version = 1
    flags = 0

    header_size = 48
    code_offset = header_size
    data_offset = code_offset + len(code_data)

    # 对齐数据偏移到4字节
    data_offset = (data_offset + 3) & ~3

    header = struct.pack('<IIIIIIIIIIII',
        magic, version, entry,
        code_offset, len(code_data), code_vaddr,
        data_offset, len(data_data), data_vaddr,
        bss_size, stack_size, flags)

    with open(output, 'wb') as f:
        f.write(header)
        f.write(code_data)
        # 填充对齐
        f.write(b'\x00' * (data_offset - header_size - len(code_data)))
        f.write(data_data)

    print(f'Created {output} with entry=0x{entry:x} code@0x{code_vaddr:x} size={len(code_data)}')

if __name__ == '__main__':
    # 示例：加载一个简单的代码段，入口点就在代码段起始
    code = b'\xB8\x0B\x00\x00\x00'  # mov eax, 11 (exit syscall?)
    # 实际应包含完整函数，这里只是占位
    make_vex(entry=0x400000, code_vaddr=0x400000, code_data=code, output='test.vex')