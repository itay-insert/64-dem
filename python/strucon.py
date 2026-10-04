import sys
import tty
import termios

def make_line(src, pos):
    buff = []

    while pos < len(src) and src[pos] != '\n':
        buff.append(src[pos])
        pos += 1

    return ''.join(buff)


def parse_line(src):
    if "dw" in src:
        pos = src.find("dw") + 2
        print("   u16 " + make_line(src, pos))

    elif "db" in src:
        pos = src.find("db") + 2
        print("   u8 " + make_line(src, pos))

    elif "dd" in src:
        pos = src.find("dd") + 2
        print("   u32 " + make_line(src, pos))


lines = 0
buffer = []

fd = sys.stdin.fileno()
old_settings = termios.tcgetattr(fd)

try:
    tty.setraw(fd)

    while True:
        ch = sys.stdin.read(1)

        if ch == '\n':
            lines += 1

        if ord(ch) == 24:       # Ctrl+X
            break

        if ord(ch) < 128:
            buffer.append(ch)

finally:
    termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)

    text = ''.join(buffer)

    print("typedef struct {")

    pos = 0

    while pos < len(text) and lines > 0:
        line = make_line(text, pos)
        parse_line(line)

        pos = text.find('\n', pos)

        if pos == -1:
            break

        pos += 1
        lines -= 1

    print("} struct_name;")