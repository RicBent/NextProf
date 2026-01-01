import sys
import argparse
from PyQt6.QtWidgets import QApplication

from src.main_window import MainWindow


def main() -> int:
    parser = argparse.ArgumentParser(description='NextProf Viewer')
    parser.add_argument('-f', '--file', type=str, help='Path to the profile file to load')
    parser.add_argument('-s', '--symbols', type=str, nargs='*', help='Paths to symbol files to load')
    parser.add_argument('-c', '--code', type=str, nargs='*', help='Paths to code files to load with their base addresses in the format path:address (hex, default 0x100000)')
    args = parser.parse_args()
    
    QApplication.setStyle('windows')
    app = QApplication(sys.argv)

    initial_code_paths = []
    for code_arg in args.code or []:
        colon_indx = code_arg.rfind(':')
        if colon_indx != -1:
            path = code_arg[:colon_indx]
            addr_str = code_arg[colon_indx + 1:]
            addr = int(addr_str, 16)
        else:
            path = code_arg
            addr = 0x100000
        initial_code_paths.append((path, addr))

    window = MainWindow(
        initial_file_path=args.file,
        initial_symbol_paths=args.symbols,
        initial_code_paths=initial_code_paths,
    )
    window.show()
    return app.exec()


if __name__ == '__main__':
    sys.exit(main())
