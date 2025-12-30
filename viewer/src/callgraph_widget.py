from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QGraphicsView, QGraphicsScene)
from PyQt6.QtCore import Qt, QRectF, QEvent
from PyQt6.QtSvg import QSvgRenderer
from PyQt6.QtSvgWidgets import QGraphicsSvgItem
from PyQt6.QtGui import QPainter


class CallGraphView(QGraphicsView):
    
    def __init__(self):
        super().__init__()
        self.setDragMode(QGraphicsView.DragMode.ScrollHandDrag)
        self.setRenderHint(QPainter.RenderHint.Antialiasing)
        self.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
        self.setTransformationAnchor(QGraphicsView.ViewportAnchor.AnchorUnderMouse)
        self.setResizeAnchor(QGraphicsView.ViewportAnchor.AnchorUnderMouse)
        self._zoom_level = 1.0
        self.grabGesture(Qt.GestureType.PinchGesture)
    
    def wheelEvent(self, event):
        # Trackpads: use pixelDelta to pan; mouse wheel: angleDelta to zoom
        if not event.pixelDelta().isNull():
            super().wheelEvent(event)
            return

        delta = event.angleDelta().y()
        if delta == 0:
            super().wheelEvent(event)
            return

        zoom_factor = 1.15 if delta > 0 else 1 / 1.15
        new_zoom = self._zoom_level * zoom_factor
        if 0.1 <= new_zoom <= 10.0:
            self._zoom_level = new_zoom
            self.scale(zoom_factor, zoom_factor)
    
    def reset_zoom(self):
        self.resetTransform()
        self._zoom_level = 1.0
    
    def fit_in_view(self):
        if self.scene():
            self.fitInView(self.scene().sceneRect(), Qt.AspectRatioMode.KeepAspectRatio)
            # Update zoom level based on transform
            self._zoom_level = self.transform().m11()

    def event(self, event):
        if event.type() == QEvent.Type.Gesture:
            return self._handle_gesture(event)
        return super().event(event)

    def _handle_gesture(self, event):
        pinch = event.gesture(Qt.GestureType.PinchGesture)
        if pinch and hasattr(pinch, 'scaleFactor'):
            factor = pinch.scaleFactor()
            new_zoom = self._zoom_level * factor
            self._zoom_level = new_zoom
            self.scale(factor, factor)
            return True
        return False


class CallGraphWidget(QWidget):
    
    def __init__(self):
        super().__init__()
        self.svg_item = None
        self.setup_ui()
    
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)

        self.scene = QGraphicsScene()
        self.view = CallGraphView()
        self.view.setScene(self.scene)

        layout.addWidget(self.view)
    
    def load_from_dot(self, dot):
        svg_data = dot.pipe(format='svg')
        self.load_from_svg_data(svg_data)
    
    def load_from_svg_data(self, svg_data: bytes):
        self.scene.clear()

        renderer = QSvgRenderer(svg_data)
        if not renderer.isValid():
            return
        
        self.svg_item = QGraphicsSvgItem()
        self.svg_item.setSharedRenderer(renderer)
        
        self.scene.addItem(self.svg_item)
        self.scene.setSceneRect(self.svg_item.boundingRect())
        
        self.fit_in_view()
    
    def load_from_svg_file(self, file_path: str):
        with open(file_path, 'rb') as f:
            svg_data = f.read()
        self.load_from_svg_data(svg_data)
    
    def reset_zoom(self):
        self.view.reset_zoom()
    
    def fit_in_view(self):
        self.view.fit_in_view()
