import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import fast_guest_access  # noqa: E402


class Transform(unittest.TestCase):
    def test_after_each_prologue_keeping_line_endings(self):
        source = ('PPC_FUNC_IMPL(__imp__sub_1) {\r\n\tPPC_FUNC_PROLOGUE();\r\n\tsfr::enter_function(ctx, "a", 1);\r\n}\r\n'
                  'PPC_FUNC_IMPL(__imp__sub_2) {\n\tPPC_FUNC_PROLOGUE();\n}\n')
        changed, count = fast_guest_access.transform(source)
        self.assertEqual(count, 2)
        self.assertIn('\tPPC_FUNC_PROLOGUE();\r\n\tSFR_FAST_PATH();\r\n\tsfr::enter_function', changed)
        self.assertIn('\tPPC_FUNC_PROLOGUE();\n\tSFR_FAST_PATH();\n}', changed)
        self.assertEqual(changed.replace('\tSFR_FAST_PATH();\r\n', '').replace('\tSFR_FAST_PATH();\n', ''), source)

    def test_refuses_a_second_pass(self):
        changed, _ = fast_guest_access.transform('\tPPC_FUNC_PROLOGUE();\n')
        with self.assertRaises(ValueError):
            fast_guest_access.transform(changed)


if __name__ == '__main__':
    unittest.main()
