from PyQt6.QtWidgets import (QMainWindow, QTableView, QAbstractItemView, 
                              QFileDialog, QMessageBox, QWidget, QVBoxLayout, QHBoxLayout, QLabel, QDoubleSpinBox, QTabWidget)
from PyQt6.QtCore import Qt, QAbstractTableModel, QModelIndex, QSortFilterProxyModel

import os

from .symbols import SymbolMap
from .profile import Profile
from .callgraph import generate_callgraph
from .callgraph_widget import CallGraphWidget


class FunctionTableModel(QAbstractTableModel):
    """Model for displaying profiling function data."""
    
    def __init__(self):
        super().__init__()
        self.funcs = []
    
    def set_data(self, funcs):
        self.beginResetModel()
        self.funcs = funcs
        self.endResetModel()
    
    def rowCount(self, parent=QModelIndex()):
        if parent.isValid():
            return 0
        return len(self.funcs)
    
    def columnCount(self, parent=QModelIndex()):
        return 4
    
    def data(self, index, role=Qt.ItemDataRole.DisplayRole):
        if not index.isValid() or index.row() >= len(self.funcs):
            return None
        
        func = self.funcs[index.row()]
        
        if role == Qt.ItemDataRole.DisplayRole:
            if index.column() == 0:
                return f'0x{func.address:08X}'
            elif index.column() == 1:
                return func.name
            elif index.column() == 2:
                return str(func.hit_count)
            elif index.column() == 3:
                return str(func.hit_count_direct)
        
        elif role == Qt.ItemDataRole.TextAlignmentRole:
            if index.column() in (0, 2, 3):
                return Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter
        
        return None
    
    def headerData(self, section, orientation, role=Qt.ItemDataRole.DisplayRole):
        if role == Qt.ItemDataRole.DisplayRole and orientation == Qt.Orientation.Horizontal:
            headers = ['Address', 'Name', 'Total Hits', 'Direct Hits']
            if section < len(headers):
                return headers[section]
        return None
    
    def sort(self, column, order):
        self.beginResetModel()
        reverse = order == Qt.SortOrder.DescendingOrder
        if column == 0:
            self.funcs.sort(key=lambda f: f.address, reverse=reverse)
        elif column == 1:
            self.funcs.sort(key=lambda f: f.name, reverse=reverse)
        elif column == 2:
            self.funcs.sort(key=lambda f: f.hit_count, reverse=reverse)
        elif column == 3:
            self.funcs.sort(key=lambda f: f.hit_count_direct, reverse=reverse)
        self.endResetModel()


class MainWindow(QMainWindow):
    def __init__(self, initial_file_path: str = None, initial_symbol_paths: list[str] = None):
        super().__init__()

        self.symbols = SymbolMap()
        if initial_symbol_paths:
            for path in initial_symbol_paths:
                self.symbols.load_from_file(path)

        self.profile = Profile(self.symbols)
        if initial_file_path:
            self.profile.load_from_file(initial_file_path)
        
        self.setup_ui()

    def setup_ui_menu_bar(self):
        menubar = self.menuBar()
        file_menu = menubar.addMenu('File')
        
        open_profile_action = file_menu.addAction('Open Profile File')
        open_profile_action.triggered.connect(self.on_open_profile_file)
        
        open_symbols_action = file_menu.addAction('Open Symbols File')
        open_symbols_action.triggered.connect(self.on_open_symbols_file)
        
        file_menu.addSeparator()
        
        exit_action = file_menu.addAction('Exit')
        exit_action.triggered.connect(self.close)

        self.setMenuBar(menubar)

    def setup_ui(self):        
        self.setWindowTitle('NextProf Viewer')
        self.setup_ui_menu_bar()

        self.model = FunctionTableModel()

        tabs = QTabWidget(self)

        # --- Call Graph tab ---
        callgraph_tab = QWidget(tabs)
        cg_layout = QVBoxLayout(callgraph_tab)

        controls = QWidget(callgraph_tab)
        controls_layout = QHBoxLayout(controls)
        controls_layout.setContentsMargins(0, 0, 0, 0)

        threshold_label = QLabel('Min %:', controls)
        controls_layout.addWidget(threshold_label)
        self.threshold_spin = QDoubleSpinBox(controls)
        self.threshold_spin.setRange(0.0, 100.0)
        self.threshold_spin.setDecimals(3)
        self.threshold_spin.setSingleStep(0.1)
        self.threshold_spin.setValue(0.5)
        self.threshold_spin.valueChanged.connect(self.on_threshold_changed)
        controls_layout.addWidget(self.threshold_spin)

        critical_label = QLabel('Critical %:', controls)
        controls_layout.addWidget(critical_label)
        self.critical_spin = QDoubleSpinBox(controls)
        self.critical_spin.setRange(0.0, 100.0)
        self.critical_spin.setDecimals(3)
        self.critical_spin.setSingleStep(0.1)
        self.critical_spin.setValue(5.0)
        self.critical_spin.valueChanged.connect(self.on_critical_changed)
        controls_layout.addWidget(self.critical_spin)

        controls_layout.addStretch(1)
        cg_layout.addWidget(controls)

        self.callgraph_widget = CallGraphWidget()
        cg_layout.addWidget(self.callgraph_widget)

        tabs.addTab(callgraph_tab, 'Call Graph')

        # --- Functions tab ---
        functions_tab = QWidget(tabs)
        fn_layout = QVBoxLayout(functions_tab)

        self.table = QTableView(functions_tab)
        self.table.setModel(self.model)
        self.table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self.table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.table.setSortingEnabled(True)
        self.table.verticalHeader().setVisible(False)
        fn_layout.addWidget(self.table)

        tabs.addTab(functions_tab, 'Functions')

        self.setCentralWidget(tabs)
        
        self.update_list()
        self.refresh_callgraph()
    
    def on_open_profile_file(self):
        file_path, _ = QFileDialog.getOpenFileName(
            self,
            'Open Profile File',
            '',
            'Binary Files (*.bin);;All Files (*)'
        )
        
        if file_path:
            self.profile.load_from_file(file_path)
            self.update_list()
    
    def on_open_symbols_file(self):
        file_path, _ = QFileDialog.getOpenFileName(
            self,
            'Open Symbols Map File',
            '',
            'Map Files (*.map);;All Files (*)'
        )
        
        if file_path:
            self.symbols.load_from_file(file_path)
    
    def refresh_callgraph(self):
        if not self.profile.funcs:
            self.callgraph_widget.scene.clear()
            return
        threshold = float(self.threshold_spin.value())
        critical = float(self.critical_spin.value())
        dot = generate_callgraph(self.profile, min_percentage=threshold, critical_percentage=critical)
        self.callgraph_widget.load_from_dot(dot)

    def update_list(self):
        self.model.set_data(self.profile.funcs)
        self.refresh_callgraph()

    def on_threshold_changed(self, _value):
        self.refresh_callgraph()

    def on_critical_changed(self, _value):
        self.refresh_callgraph()
