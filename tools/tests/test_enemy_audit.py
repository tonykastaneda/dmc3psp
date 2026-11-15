"""Evidence handling must not turn crashed or truncated scenes into passes."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
import sys

spec = importlib.util.spec_from_file_location("enemy_audit", Path(__file__).parents[1] / "enemy_audit.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class AuditEvidenceTests(unittest.TestCase):
    def parse(self, content):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "audit.csv"
            path.write_text(content)
            return audit.summarize(path)

    def test_truncated_capture_cannot_pass(self):
        with self.assertRaises(ValueError):
            self.parse("mission,room,frame,slot,kind,other,violation,depth,x,y,z\n")

    def test_counts_and_depths_preserved(self):
        result = self.parse("mission,room,frame,slot,kind,other,violation,depth,x,y,z\n"
                            "1,0,2,0,0,1,enemy,5.5,0,0,0\n"
                            "1,0,3,0,0,1,enemy,2.0,0,0,0\n"
                            "# frames=4 bodies=12 violations=2\n")
        self.assertEqual(result["counts"], {"enemy": 2})
        self.assertEqual(result["max_depth"], {"enemy": 5.5})

    def test_missing_rows_cannot_pass(self):
        with self.assertRaises(ValueError):
            self.parse("mission,room,frame,slot,kind,other,violation,depth,x,y,z\n"
                       "# frames=4 bodies=12 violations=1\n")

    def test_room_comments_do_not_hide_later_violations(self):
        result = self.parse("mission,room,frame,slot,kind,other,violation,depth,x,y,z\n"
                            "# room_summary mission=1 room=0 frames=2 bodies=6 violations=0\n"
                            "3,4,3,0,0,1,enemy,2.0,0,0,0\n"
                            "# frames=4 bodies=12 violations=1\n")
        self.assertEqual(result["counts"], {"enemy": 1})

    def test_authored_crossing_is_retained_separately(self):
        result = self.parse("mission,room,frame,slot,kind,other,violation,depth,x,y,z\n"
                            "4,111,2,0,100,-1,expected_wall,25,0,0,0\n"
                            "4,111,3,0,100,-1,wall,2,0,0,0\n"
                            "# frames=4 bodies=12 violations=1 expected=1\n")
        self.assertEqual(result["counts"], {"wall": 1})
        self.assertEqual(result["expected"], {"expected_wall": 1})

    def test_batch_retains_zero_violation_room_evidence(self):
        sys.path.insert(0, str(Path(__file__).parents[1]))
        from enemy_audit_sweep import split_rooms
        rooms = split_rooms("mission,room,frame,slot,kind,other,violation,depth,x,y,z\n"
                            "# room_summary mission=1 room=0 frames=600 bodies=1800 violations=0\n"
                            "3,4,601,0,0,1,enemy,2.0,0,0,0\n"
                            "# room_summary mission=3 room=4 frames=601 bodies=3000 violations=1\n"
                            "# frames=1201 bodies=4800 violations=1\n")
        self.assertEqual(self.parse(rooms[1, 0])["violations"], 0)
        self.assertEqual(self.parse(rooms[3, 4])["violations"], 1)

    def test_missing_authored_crossing_rows_cannot_pass(self):
        with self.assertRaises(ValueError):
            self.parse("mission,room,frame,slot,kind,other,violation,depth,x,y,z\n"
                       "# frames=4 bodies=12 violations=0 expected=1\n")

    def test_batch_preserves_authored_crossing_footer(self):
        sys.path.insert(0, str(Path(__file__).parents[1]))
        from enemy_audit_sweep import split_rooms
        rooms = split_rooms("mission,room,frame,slot,kind,other,violation,depth,x,y,z\n"
                            "4,111,2,0,100,-1,expected_wall,25,0,0,0\n"
                            "# room_summary mission=4 room=111 frames=4 bodies=12 violations=0 expected=1\n"
                            "# frames=4 bodies=12 violations=0 expected=1\n")
        self.assertEqual(self.parse(rooms[4, 111])["expected"], {"expected_wall": 1})
        with self.assertRaises(ValueError):
            self.parse(rooms[4, 111].replace("4,111,2,0,100,-1,expected_wall,25,0,0,0", ""))

    def test_encounters_keep_each_secret_room_once(self):
        header = "static const RoomOp m1_1_600_ops static const RoomOp m2_1_600_ops "
        header += "static const RoomOp m3_1_006_ops static const RoomOp m3_0_006_ops"
        self.assertEqual(audit.encounters(header), [(1, 600), (3, 6)])


if __name__ == "__main__":
    unittest.main()
