"""Mesh reduction for rigid PSP models (drawn with one matrix, no skinning), across UV seams.

decimate.py never moves a vertex on a texture seam, so a model made of many UV islands stops shrinking
early (the Gigapede's head stalls at about 750 triangles). Here an edge collapse moves every vertex at a
position together. Each vertex (position + UV) that moves takes the UV of the vertex it meets across the
collapsed edge on the same side of the seam; where there is none, it keeps its own UV at the new position.
Open borders at the position level (holes, silhouette edges of thin parts) don't move.
"""
import heapq

from decimate import _cross, _error, _plane_quadric


def decimate_rigid(positions, normals, uvs, indices, ratio, tri_groups=None, skins=None):
    """Keep about ratio of the triangles. tri_groups: a group id per triangle (an object), kept in order.
    skins: optional (bones, weights) per vertex for a skinned model. A vertex that moves then takes the
    skinning of the point it moves to, and collapsing between points on different main bones costs more.
    Returns (positions, normals, uvs, indices, groups of the kept triangles[, skins])."""
    positions, normals, uvs = list(positions), list(normals), list(uvs)
    skins = list(skins) if skins else None
    tris = [list(indices[i:i + 3]) for i in range(0, len(indices), 3)]
    groups = list(tri_groups) if tri_groups else [0] * len(tris)
    target = int(len(tris) * ratio)
    # Position groups.
    gid_of_key, wedge_gid, gpos = {}, [], []
    for p in positions:
        key = (round(p[0], 2), round(p[1], 2), round(p[2], 2))
        if key not in gid_of_key:
            gid_of_key[key] = len(gpos)
            gpos.append(tuple(p))
        wedge_gid.append(gid_of_key[key])
    ng = len(gpos)
    rep = [0] * ng   # a vertex at each position, for its skinning
    for w, g in enumerate(wedge_gid):
        rep[g] = w
    main_bone = None
    if skins:
        main_bone = [0] * ng
        for g in range(ng):
            b, wt = skins[rep[g]]
            main_bone[g] = b[max(range(len(wt)), key=lambda k: wt[k])]
    quadric = [[0.0] * 10 for _ in range(ng)]
    g_tris = [set() for _ in range(ng)]
    for t, tri in enumerate(tris):
        gs = [wedge_gid[w] for w in tri]
        for g in gs:
            g_tris[g].add(t)
        q = _plane_quadric(*(gpos[g] for g in gs))
        if q:
            for g in gs:
                quadric[g] = [a + b for a, b in zip(quadric[g], q)]
    edge_count = {}
    for tri in tris:
        gs = [wedge_gid[w] for w in tri]
        for i in range(3):
            e = tuple(sorted((gs[i], gs[(i + 1) % 3])))
            edge_count[e] = edge_count.get(e, 0) + 1
    movable = [True] * ng
    for (a, b), n in edge_count.items():
        if n != 2:
            movable[a] = movable[b] = False
    alive = [True] * len(tris)
    alive_count = len(tris)
    dead = [False] * ng
    heap = []

    def tri_g(t):
        return [wedge_gid[w] for w in tris[t]]

    def push(u):
        if not movable[u] or dead[u]:
            return
        for t in g_tris[u]:
            for v in tri_g(t):
                if v != u:
                    q = [a + b for a, b in zip(quadric[u], quadric[v])]
                    cost = _error(q, gpos[v])
                    if main_bone and main_bone[u] != main_bone[v]:
                        cost = cost * 4.0 + 50.0
                    heapq.heappush(heap, (cost, u, v))

    for u in range(ng):
        push(u)
    while heap and alive_count > target:
        _, u, v = heapq.heappop(heap)
        if dead[u] or dead[v]:
            continue
        ts = list(g_tris[u])
        shared = [t for t in ts if v in tri_g(t)]
        if not shared:
            continue
        # The collapse must not flip a surviving triangle, nor make one that's a sliver of nothing.
        bad = False
        for t in ts:
            if t in shared:
                continue
            gs = tri_g(t)
            a, b, c = (gpos[g] for g in gs)
            na = _cross(a, b, c)
            a2, b2, c2 = (gpos[v if g == u else g] for g in gs)
            nb = _cross(a2, b2, c2)
            if na[0] * nb[0] + na[1] * nb[1] + na[2] * nb[2] <= 0.0:
                bad = True
                break
        if bad:
            continue
        # The link condition: u and v share exactly the neighbours of the faces they share (keeps it manifold).
        nu = {g for t in ts for g in tri_g(t)} - {u}
        nv = {g for t in g_tris[v] for g in tri_g(t)} - {v}
        if len(nu & nv) != len(shared):
            continue
        # Each wedge at u: the wedge at v across a shared face on its side, else a new one with its own UV.
        target_wedge = {}
        for t in shared:
            wu = next(w for w in tris[t] if wedge_gid[w] == u)
            wv = next(w for w in tris[t] if wedge_gid[w] == v)
            target_wedge.setdefault(wu, wv)
        for t in ts:
            if t in shared:
                alive[t] = False
                alive_count -= 1
                for w in tris[t]:
                    g_tris[wedge_gid[w]].discard(t)
                continue
            for i, w in enumerate(tris[t]):
                if wedge_gid[w] != u:
                    continue
                if w not in target_wedge:
                    positions.append(gpos[v])
                    normals.append(normals[w])
                    uvs.append(uvs[w])
                    if skins:
                        skins.append(skins[rep[v]])
                    wedge_gid.append(v)
                    target_wedge[w] = len(positions) - 1
                tris[t][i] = target_wedge[w]
            g_tris[v].add(t)
        dead[u] = True
        g_tris[u] = set()
        quadric[v] = [a + b for a, b in zip(quadric[u], quadric[v])]
        push(v)
        for g in nv:
            push(g)
    used = sorted({w for t, tri in enumerate(tris) if alive[t] for w in tri})
    remap = {old: new for new, old in enumerate(used)}
    order = sorted((t for t in range(len(tris)) if alive[t]), key=lambda t: (groups[t], t))
    out_idx = [remap[w] for t in order for w in tris[t]]
    out = ([positions[w] for w in used], [normals[w] for w in used], [uvs[w] for w in used], out_idx,
           [groups[t] for t in order])
    if skins:
        out += ([skins[w] for w in used],)
    return out
