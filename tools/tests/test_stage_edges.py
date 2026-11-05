"""Regression: safety walls below a walkway must not fence off the walkway."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from prepare_stage import open_edge_walls


class StageEdges(unittest.TestCase):
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
