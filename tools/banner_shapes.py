#!/usr/bin/env python3
"""Solid shapes for the banner scene, made from flat outlines (the icon's letters, read from its SVG).
An outline is blown up into a soft solid, like a cushion, and comes back as plain lists (positions,
normals, texture coordinates, triangles) for tools/banner_scene.py.

Outlines are shapely polygons in scene units, x right, y up; the solid's front faces +z.
Triangles are anticlockwise seen from outside (the console draws that side only).
"""
import math, re
import shapely
from shapely.geometry import MultiPolygon, Point


class Mesh:
    """Vertices and triangles under construction. Each vertex: position, normal, texture coordinate."""
    def __init__(self):
        self.pos, self.nrm, self.uv, self.tri = [], [], [], []

    def add(self, p, n, uv):
        self.pos.append(tuple(float(v) for v in p)); self.nrm.append(tuple(float(v) for v in n)); self.uv.append(tuple(float(v) for v in uv))
        return len(self.pos) - 1

    def moved(self, dx, dy, dz):
        """A copy, shifted."""
        m = Mesh()
        m.pos = [(p[0] + dx, p[1] + dy, p[2] + dz) for p in self.pos]
        m.nrm, m.uv, m.tri = list(self.nrm), list(self.uv), list(self.tri)
        return m


# ---------------------------------------------------------------- outlines

def svg_path_polygons(d, flatten=10):
    """The closed shapes of an SVG path that uses M, m, c and z (what the brand files use), as lists of
    points; curves are cut into `flatten` straight pieces."""
    tokens = re.findall(r'[MmcCzZlL]|-?\d*\.?\d+(?:e-?\d+)?', d)
    out, cur, pos, start, i, cmd = [], [], (0.0, 0.0), (0.0, 0.0), 0, None
    def num():
        nonlocal i
        v = float(tokens[i]); i += 1
        return v
    while i < len(tokens):
        if re.match(r'[A-Za-z]', tokens[i]):
            cmd = tokens[i]; i += 1
            if cmd in 'zZ':
                if cur: out.append(cur); cur = []
                pos = start
                continue
        if cmd in 'Mm':
            x, y = num(), num()
            pos = (pos[0] + x, pos[1] + y) if cmd == 'm' else (x, y)
            start = pos; cur = [pos]
            cmd = 'l' if cmd == 'm' else 'L'
        elif cmd in 'lL':
            x, y = num(), num()
            pos = (pos[0] + x, pos[1] + y) if cmd == 'l' else (x, y)
            cur.append(pos)
        elif cmd in 'cC':
            pts = [(num(), num()) for _ in range(3)]
            if cmd == 'c': pts = [(pos[0] + x, pos[1] + y) for x, y in pts]
            p0, (p1, p2, p3) = pos, pts
            for k in range(1, flatten + 1):
                t = k / flatten; u = 1 - t
                cur.append((u ** 3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t ** 3 * p3[0],
                            u ** 3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t ** 3 * p3[1]))
            pos = p3
        else:
            raise ValueError(f'path command {cmd} is not handled')
    if cur: out.append(cur)
    return out


def quad_xy(x0, y0, x1, y1, z=0.0):
    """A rectangle facing +z, with v = 1 at its top."""
    m = Mesh()
    for p, t in zip(((x0, y0, z), (x1, y0, z), (x0, y1, z), (x1, y1, z)), ((0, 0), (1, 0), (0, 1), (1, 1))): m.add(p, (0, 0, 1), t)
    m.tri = [(0, 1, 2), (1, 3, 2)]
    return m


def quad_floor(cx, cz, rx, rz, y=0.0):
    """A rectangle lying flat, facing up."""
    m = Mesh()
    for p, t in zip(((cx - rx, y, cz + rz), (cx + rx, y, cz + rz), (cx - rx, y, cz - rz), (cx + rx, y, cz - rz)), ((0, 0), (1, 0), (0, 1), (1, 1))):
        m.add(p, (0, 1, 0), t)
    m.tri = [(0, 1, 2), (1, 3, 2)]
    return m


def simplify(poly, tolerance):
    """The outline with points removed where that moves it by less than `tolerance`."""
    return poly.simplify(tolerance, preserve_topology=True)


# ---------------------------------------------------------------- soft, inflated solids

def _resample(ring, spacing):
    """Points along a closed ring, evenly `spacing` apart (at least 8)."""
    line = shapely.LinearRing(ring)
    n = max(8, int(round(line.length / spacing)))
    return [line.interpolate(line.length * i / n).coords[0] for i in range(n)]


def inscribed_radius(poly):
    """How far the deepest point inside an outline is from its edge."""
    return shapely.maximum_inscribed_circle(poly).length


def pillow(poly, height, radius=None, spacing=0.3, roundness=2.0, levels=(0.05, 0.14, 0.28, 0.46, 0.68, 0.9), uv_fn=None, flatten=1.0):
    """The outline blown up like a cushion: the surface rises from the edge, steeply at first, to `height`
    at `radius` from the edge (default: the deepest point, so the whole top is rounded and nothing is flat).
    roundness 2 is a circular cross-section; higher is boxier (steeper sides, flatter top).
    Returns (mesh facing +z with its edge at z = 0, the edge as rings of (x, y, outward nx, ny))."""
    import numpy as np
    from scipy.spatial import Delaunay
    from shapely.prepared import prep
    polys = list(poly.geoms) if isinstance(poly, MultiPolygon) else [poly]
    mesh, edges = Mesh(), []
    for p in polys:
        p = shapely.geometry.polygon.orient(p, 1.0)
        R = radius or inscribed_radius(p)
        pts, edge_normal = [], {}
        for ring in [p.exterior, *p.interiors]:
            r = _resample(list(ring.coords)[:-1], spacing)
            n = len(r)
            rec = []
            for i, (x, y) in enumerate(r):
                (xa, ya), (xb, yb) = r[i - 1], r[(i + 1) % n]
                dx, dy = xb - xa, yb - ya
                l = math.hypot(dx, dy) or 1e-9
                nx, ny = dy / l, -dx / l                       # outward: the outside ring runs anticlockwise, holes clockwise
                edge_normal[len(pts)] = (nx, ny)
                rec.append((x, y, nx, ny, len(mesh.pos) + len(pts)))
                pts.append((x, y))
            edges.append(rec)
        for t in levels:
            inner = p.buffer(-R * t, quad_segs=6)
            if inner.is_empty: continue
            for g in (inner.geoms if hasattr(inner, 'geoms') else [inner]):
                for ring in [g.exterior, *g.interiors]:
                    if ring.length < spacing * 2: continue
                    pts += _resample(list(ring.coords)[:-1], spacing * (1 + 2.2 * t))
        c = shapely.maximum_inscribed_circle(p).coords[0]
        pts.append(c)
        arr = np.array(pts)
        tri = Delaunay(arr)
        inside = prep(p.buffer(1e-6))
        boundary = p.boundary
        base = len(mesh.pos)
        zs = []
        for i, (x, y) in enumerate(pts):
            if i in edge_normal: zs.append(0.0); continue
            d = min(1.0, boundary.distance(Point(x, y)) / R)
            zs.append(height * (1 - (1 - d) ** roundness) ** (1 / roundness) * flatten)
        normals = np.zeros((len(pts), 3))
        kept = []
        for a, b, c_ in tri.simplices:
            cx, cy = (arr[a] + arr[b] + arr[c_]) / 3
            if not inside.contains(Point(cx, cy)): continue
            pa, pb, pc = np.array([*arr[a], zs[a]]), np.array([*arr[b], zs[b]]), np.array([*arr[c_], zs[c_]])
            nrm = np.cross(pb - pa, pc - pa)
            if nrm[2] < 0: b, c_ = c_, b; nrm = -nrm
            if np.linalg.norm(nrm) < 1e-10: continue
            for v in (a, b, c_): normals[v] += nrm
            kept.append((a, b, c_))
        for i, (x, y) in enumerate(pts):
            if i in edge_normal: n = (edge_normal[i][0], edge_normal[i][1], 0.0)
            else:
                l = np.linalg.norm(normals[i]) or 1.0
                n = tuple(normals[i] / l)
            mesh.add((x, y, zs[i]), n, uv_fn(x, y, zs[i]) if uv_fn else (0.5, 0.5))
        mesh.tri += [(a + base, b + base, c_ + base) for a, b, c_ in kept]
    return mesh, edges


def puffy(poly, height, belt, back_height=None, uv_face=None, uv_back=None, uv_side=(0.5, 0.5), **kw):
    """A soft solid: a cushion on the front, a band `belt` thick round the edge, a cushion on the back.
    Returns (front, band, back) as three meshes, centred on z = 0."""
    front, edges = pillow(poly, height, uv_fn=uv_face, **kw)
    front = front.moved(0, 0, belt / 2)
    rear, _ = pillow(poly, back_height if back_height is not None else height, uv_fn=uv_back or uv_face, **kw)
    back = Mesh()
    back.pos = [(x, y, -z - belt / 2) for x, y, z in rear.pos]
    back.nrm = [(nx, ny, -nz) for nx, ny, nz in rear.nrm]
    back.uv = list(rear.uv)
    back.tri = [(a, c, b) for a, b, c in rear.tri]
    band = Mesh()
    for ring in edges:
        n = len(ring)
        ids = [(band.add((x, y, belt / 2), (nx, ny, 0), uv_side), band.add((x, y, -belt / 2), (nx, ny, 0), uv_side)) for x, y, nx, ny, _ in ring]
        for i in range(n):
            (a, c), (b, d) = ids[i], ids[(i + 1) % n]
            band.tri += [(a, c, b), (b, c, d)]
    return front, band, back
