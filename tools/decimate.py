"""Mesh reduction for the PSP: quadric edge collapse that never moves a UV seam.

Works on indexed triangles with per-vertex attributes (positions, normals, uvs, bones,
weights). A vertex can go only if it's the sole vertex at its position (so not on a texture
seam or an object border), and into a neighbour on the same main bone, so skinning and
UVs stay valid. Returns kept-vertex arrays and new indices.
"""
import heapq


def _plane_quadric(a, b, c):
    ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    vx, vy, vz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
    nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
    length = (nx * nx + ny * ny + nz * nz) ** 0.5
    if length < 1e-9:
        return None
    area = length * 0.5
    nx, ny, nz = nx / length, ny / length, nz / length
    d = -(nx * a[0] + ny * a[1] + nz * a[2])
    return [area * q for q in (nx * nx, nx * ny, nx * nz, nx * d, ny * ny, ny * nz, ny * d, nz * nz, nz * d, d * d)]


def _error(q, p):
    x, y, z = p
    return (q[0] * x * x + 2 * q[1] * x * y + 2 * q[2] * x * z + 2 * q[3] * x +
            q[4] * y * y + 2 * q[5] * y * z + 2 * q[6] * y + q[7] * z * z + 2 * q[8] * z + q[9])


def weld(positions, normals, uvs, bones, weights, indices):
    """Merges vertices identical in position, UV and skinning: the MODs repeat them per strip.
    Returns (kept vertex indices, new indices)."""
    first = {}
    remap = {}
    for v, p in enumerate(positions):
        n = normals[v]
        key = (round(p[0], 2), round(p[1], 2), round(p[2], 2), round(uvs[v][0], 4), round(uvs[v][1], 4),
               round(n[0], 1), round(n[1], 1), round(n[2], 1), tuple(bones[v]), tuple(weights[v]))
        remap[v] = first.setdefault(key, v)
    used = sorted(set(remap[i] for i in indices))
    new = {old: k for k, old in enumerate(used)}
    return used, [new[remap[i]] for i in indices]


def decimate(positions, indices, bones, weights, ratio, protect=None):
    """Keep about ratio of the triangles. Returns (kept vertex indices, new indices, which triangles survive)."""
    tris = [tuple(indices[i:i + 3]) for i in range(0, len(indices), 3)]
    target = int(len(tris) * ratio)
    at_position = {}
    for v, p in enumerate(positions):
        at_position.setdefault((round(p[0], 3), round(p[1], 3), round(p[2], 3)), []).append(v)
    movable = [True] * len(positions)
    for group in at_position.values():
        if len(group) > 1:
            for v in group:
                movable[v] = False
    if protect:
        for v in protect:
            movable[v] = False
    main_bone = [b[max(range(3), key=lambda k: w[k])] for b, w in zip(bones, weights)]
    quadric = [[0.0] * 10 for _ in positions]
    vertex_tris = [set() for _ in positions]
    for t, tri in enumerate(tris):
        for v in tri:
            vertex_tris[v].add(t)
        q = _plane_quadric(*(positions[v] for v in tri))
        if q:
            for v in tri:
                quadric[v] = [a + b for a, b in zip(quadric[v], q)]
    # Open edges are borders: keep them still.
    edge_count = {}
    for tri in tris:
        for i in range(3):
            e = tuple(sorted((tri[i], tri[(i + 1) % 3])))
            edge_count[e] = edge_count.get(e, 0) + 1
    for (a, b), n in edge_count.items():
        if n != 2:
            movable[a] = movable[b] = False
    alive = [True] * len(tris)
    alive_count = len(tris)
    heap = []

    def push(u):
        if not movable[u]:
            return
        for t in vertex_tris[u]:
            for v in tris[t]:
                if v != u and main_bone[v] == main_bone[u]:
                    q = [a + b for a, b in zip(quadric[u], quadric[v])]
                    heapq.heappush(heap, (_error(q, positions[v]), u, v))

    for u in range(len(positions)):
        push(u)
    dead = [False] * len(positions)
    while heap and alive_count > target:
        _, u, v = heapq.heappop(heap)
        if dead[u] or dead[v]:
            continue
        ts = vertex_tris[u]
        if not any(v in tris[t] for t in ts):
            continue
        # A collapse must not flip a surviving triangle.
        flipped = False
        for t in ts:
            tri = tris[t]
            if v in tri:
                continue
            a, b, c = (positions[x] for x in tri)
            na = _cross(a, b, c)
            a2, b2, c2 = (positions[v if x == u else x] for x in tri)
            nb = _cross(a2, b2, c2)
            if na[0] * nb[0] + na[1] * nb[1] + na[2] * nb[2] <= 0.0:
                flipped = True
                break
        if flipped:
            continue
        for t in list(ts):
            tri = tris[t]
            if v in tri:
                alive[t] = False
                alive_count -= 1
                for x in tri:
                    vertex_tris[x].discard(t)
            else:
                tris[t] = tuple(v if x == u else x for x in tri)
                vertex_tris[v].add(t)
        dead[u] = True
        vertex_tris[u] = set()
        quadric[v] = [a + b for a, b in zip(quadric[u], quadric[v])]
        push(v)
    used = sorted({x for t, tri in enumerate(tris) if alive[t] for x in tri})
    remap = {old: new for new, old in enumerate(used)}
    new_indices = [remap[x] for t, tri in enumerate(tris) if alive[t] for x in tri]
    return used, new_indices, alive


def _cross(a, b, c):
    ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    vx, vy, vz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
    return (uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx)


def slim_mesh(mesh, ratio=1.0):
    """Welds a build_mesh() result in place and, below ratio 1, reduces it. Objects keep their order."""
    keys = ("positions", "normals", "uvs", "bones", "weights")
    used, indices = weld(mesh["positions"], mesh["normals"], mesh["uvs"], mesh["bones"], mesh["weights"], mesh["indices"])
    for k in keys:
        mesh[k] = [mesh[k][u] for u in used]
    mesh["indices"] = indices
    if ratio >= 1.0:
        return mesh
    kept, indices, alive = decimate(mesh["positions"], indices, mesh["bones"], mesh["weights"], ratio)
    for k in keys:
        mesh[k] = [mesh[k][u] for u in kept]
    mesh["indices"] = indices
    start = 0
    for obj in mesh.get("objects", []):
        first, count = obj["index_start"] // 3, obj["index_count"] // 3
        left = sum(1 for t in range(first, first + count) if alive[t])
        obj["index_start"], obj["index_count"] = start, left * 3
        start += left * 3
    return mesh
