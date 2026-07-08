# Batch Shapes

The `Batch` class provides a rich set of 2D and 3D drawing primitives.
All shapes use the current color, texture, and transform state.

## 2D Shapes

### Outline Shapes (LINES)

| Method | Signature | Description |
|--------|-----------|-------------|
| `Line` | `Line(x0, y0, x1, y1)` | Single line segment |
| `Circle` | `Circle(cx, cy, r, fill, segs)` | Circle outline or filled |
| `Ellipse` | `Ellipse(cx, cy, rx, ry, fill, segs)` | Ellipse outline or filled |
| `Ring` | `Ring(cx, cy, rInner, rOuter, fill, segs)` | Ring/annulus outline or filled |
| `Arc` | `Arc(cx, cy, r, startDeg, endDeg, segs)` | Arc segment |
| `ThickLine` | `ThickLine(x0, y0, x1, y1, thickness)` | Line with thickness |
| `Polygon` | `Polygon(cx, cy, sides, r, rotDeg, fill)` | Regular polygon |
| `Polyline` | `Polyline(xyPairs, count)` | Connected line chain |
| `Grid` | `Grid(x, y, w, h, cellW, cellH)` | Rectangular grid |

### Filled Shapes (TRIANGLES)

| Method | Signature | Description |
|--------|-----------|-------------|
| `Rect` | `Rect(x, y, w, h, fill)` | Rectangle (outline or filled) |
| `Triangle` | `Triangle(x1,y1,x2,y2,x3,y3, fill)` | Triangle (outline or filled) |
| `Circle` | `Circle(cx, cy, r, true, segs)` | Filled circle (indexed fan) |
| `Ellipse` | `Ellipse(cx, cy, rx, ry, true, segs)` | Filled ellipse |
| `Ring` | `Ring(cx, cy, rInner, rOuter, true, segs)` | Filled annulus (quad strips) |
| `Polygon` | `Polygon(cx, cy, sides, r, rotDeg, true)` | Filled polygon (fan) |

### Text

| Method | Signature | Description |
|--------|-----------|-------------|
| `Text` | `Text(x, y, size, text)` | Monospace text (ASCII 32–127, `\n` supported) |
| `TextWidth` | `TextWidth(size, text)` | Measure text width (no draw) |

Uses an embedded 8×8 bitmap font (public domain). The font atlas is 128×48 pixels
with 16 columns × 6 rows of 8×8 glyphs.

### Textured Quads

| Method | Signature | Description |
|--------|-----------|-------------|
| `Quad` | `Quad(tex, x, y, w, h)` | Full texture quad |
| `Quad` | `Quad(tex, sx,sy,sw,sh, x,y,w,h)` | Sub-texture quad (sprite sheet) |

## 3D Wireframe (LINES)

Use the transform stack to position/orient wireframes in 3D space.

| Method | Signature |
|--------|-----------|
| `CubeWire` | `CubeWire(cx,cy,cz, sx,sy,sz)` |
| `SphereWire` | `SphereWire(cx,cy,cz, r, rings, slices)` |
| `CylinderWire` | `CylinderWire(cx,cy,cz, r, h, slices)` |
| `CapsuleWire` | `CapsuleWire(cx,cy,cz, r, h, slices)` |
| `Grid3D` | `Grid3D(size, step)` |
| `Axes` | `Axes(size)` |

## 3D Solid (TRIANGLES, Flat Color)

| Method | Signature |
|--------|-----------|
| `Cube` | `Cube(cx,cy,cz, sx,sy,sz)` |
| `Sphere` | `Sphere(cx,cy,cz, r, rings, slices)` |
| `Cylinder` | `Cylinder(cx,cy,cz, r, h, slices)` |
| `Capsule` | `Capsule(cx,cy,cz, r, h, rings, slices)` |

## Transform Stack

Apply transforms before drawing shapes. Transforms compose in local space:
`Translate` then `Rotate` rotates around the translated origin.

| Method | Signature | Description |
|--------|-----------|-------------|
| `PushMatrix` | `PushMatrix()` | Save current transform |
| `PopMatrix` | `PopMatrix()` | Restore last saved transform |
| `LoadIdentity` | `LoadIdentity()` | Reset to identity |
| `Translate` | `Translate(x, y, z)` | Translate |
| `Rotate` | `Rotate(deg, ax, ay, az)` | Axis-angle rotation |
| `Scale` | `Scale(x, y, z)` | Scale |
| `SetTransform` | `SetTransform(mat4)` | Direct matrix set |

Max stack depth: 32 levels.

## Vertex Format

```cpp
struct Vertex {
    float x, y, z;    // position (location 0)
    float u, v;        // texture UV (location 1)
    uint32_t rgba;     // color, normalized (location 2)
};
```

Total size: 24 bytes per vertex.

## Index Format

16-bit indices (`u16`). Maximum 65,535 vertices per batch flush.

## Auto-Flush

The batch automatically flushes when the vertex or index buffer is full.
It also flushes when switching texture or primitive mode (for command merging).
