import sys
import argparse
from PyQt6.QtWidgets import QApplication

from src.main_window import MainWindow


def main() -> int:
    parser = argparse.ArgumentParser(description='NextProf Viewer')
    parser.add_argument('-f', '--file', type=str, help='Path to the profile file to load')
    parser.add_argument('-s', '--symbols', type=str, nargs='*', help='Paths to symbol files to load')
    args = parser.parse_args()
    
    QApplication.setStyle('windows')
    app = QApplication(sys.argv)

    window = MainWindow(initial_file_path=args.file, initial_symbol_paths=args.symbols)
    window.show()
    return app.exec()


if __name__ == '__main__':
    sys.exit(main())
