"""Regression: safety walls below a walkway must not fence off the walkway."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from prepare_stage import open_edge_walls, collision_grid_bounds, build_grid, GRID_CELL


class StageEdges(unittest.TestCase):
    def test_native_floor_outside_rendered_floor_is_indexed(self):
        rendered = (7600., 0., 4400., 7800., 0., 4400., 7600., 0., 4600.)
        native = (4000., 280., 1700., 4200., 300., 1700., 4000., 280., 1900., 0., 1., 0., -280.)
        x0, z0, w, h = collision_grid_bounds([rendered], [], [native])
        cells = build_grid([native], x0, z0, GRID_CELL, w, h, margin=1)
        cx, cz = int((4050-x0)//GRID_CELL), int((1750-z0)//GRID_CELL)
        self.assertIn(0, cells[cz*w+cx])

    def test_grid_includes_outside_wall_query_margin(self):
        floor = (0., 0., 0., 200., 0., 0., 0., 0., 200.)
        wall = (0., 0., 0., 0., 200., 0., 0., 0., 200., 1., 0., 0., 1e9)
        x0, z0, w, h = collision_grid_bounds([floor], [wall], [])
        self.assertLessEqual(x0, -60)
        self.assertLess(w, 10)  # The plane scalar is not a vertex.
        cells = build_grid([wall], x0, z0, GRID_CELL, w, h, margin=60)
        cx, cz = int((-48-x0)//GRID_CELL), int((100-z0)//GRID_CELL)
        self.assertIn(0, cells[cz*w+cx])

    def test_lower_edge_stops_below_upper_walkway(self):
        lower = (0., -150., 0., 100., -150., 0., 0., -150., 100., 0., 1., 0., 150.)
        upper = (-200., 0., -200., 200., 0., -200., 0., 0., 200., 0., 1., 0., 0.)
        guards = [t for t in open_edge_walls([lower, upper], []) if min(t[1:9:3]) < -200]
        self.assertTrue(guards)
        self.assertTrue(all(max(t[1:9:3]) <= -20 for t in guards))

    def test_exposed_edge_still_has_a_guard(self):
        floor = (0., 0., 0., 100., 0., 0., 0., 0., 100., 0., 1., 0., 0.)
        self.assertTrue(any(max(t[1:9:3]) >= 2000 for t in open_edge_walls([floor], [])))


if __name__ == '__main__':
    unittest.main()
