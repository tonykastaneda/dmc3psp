"""Prevent Mission 2 from ending after its first ordinary wave again."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from gen_rooms import compile_script, OP_SPAWN, OP_WAIT_ALIVE


class MissionBossGate(unittest.TestCase):
    def test_boss_waits_for_both_regular_groups(self):
        def spawn(kind):
            return [[(2,(kind,0)),(3,(3436,0,2384))]] * 5
        groups = [spawn(0), spawn(8), spawn(26), spawn(26)]
        ops = compile_script(groups,[0,1,3],0,{1:40,3:30})
        self.assertEqual([o[1] for o in ops if o[0] == OP_SPAWN], [0,8,26])
        self.assertEqual(ops[-2], (OP_WAIT_ALIVE,0,30,0,0))
        self.assertEqual(sum(o[0] == OP_WAIT_ALIVE for o in ops),2)


if __name__ == '__main__':
    unittest.main()
