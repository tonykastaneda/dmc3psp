"""Evidence handling must not turn crashed or truncated scenes into passes."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

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

    def test_encounters_keep_each_secret_room_once(self):
        header = "static const RoomOp m1_1_600_ops static const RoomOp m2_1_600_ops "
        header += "static const RoomOp m3_1_006_ops static const RoomOp m3_0_006_ops"
        self.assertEqual(audit.encounters(header), [(1, 600), (3, 6)])


if __name__ == "__main__":
    unittest.main()
