import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import loop_checkpoints  # noqa: E402

CHECK = '\tsfr::guest_checkpoint();\n'


class Transform(unittest.TestCase):
    def test_keeps_loops_and_drops_forward_targets(self):
        source = ('#include "x.h"\n'
                  'PPC_FUNC_IMPL(__imp__sub_1) {\n\tPPC_FUNC_PROLOGUE();\n'
                  '\tif (a) goto loc_10;\n'
                  'loc_8:\n' + CHECK +
                  '\tb();\n'
                  'loc_10:\n' + CHECK +
                  '\tif (c) goto loc_8;\n'
                  '}\n')
        changed, kept, removed = loop_checkpoints.transform(source)
        self.assertEqual((kept, removed), (1, 1))
        self.assertIn('loc_8:\n' + CHECK, changed)
        self.assertIn('loc_10:\n\tif (c)', changed)

    def test_a_jump_table_case_after_the_label_keeps_it(self):
        source = ('PPC_FUNC_IMPL(__imp__sub_1) {\n'
                  'loc_20:\n' + CHECK +
                  '\tswitch (x) {\n\tcase 0: goto loc_20;\n\tcase 1: goto loc_30;\n\t}\n'
                  'loc_30:\n' + CHECK +
                  '}\n')
        changed, kept, removed = loop_checkpoints.transform(source)
        self.assertEqual((kept, removed), (1, 1))
        self.assertIn('loc_20:\n' + CHECK, changed)
        self.assertNotIn('loc_30:\n' + CHECK, changed)

    def test_a_later_function_with_the_same_label_does_not_count(self):
        source = ('PPC_FUNC_IMPL(__imp__sub_1) {\n'
                  'loc_40:\n' + CHECK + '}\n'
                  'PPC_FUNC_IMPL(__imp__sub_2) {\n'
                  'loc_40:\n' + CHECK + '\tgoto loc_40;\n}\n')
        changed, kept, removed = loop_checkpoints.transform(source)
        self.assertEqual((kept, removed), (1, 1))
        first, second = changed.split('PPC_FUNC_IMPL(__imp__sub_2)')
        self.assertNotIn(CHECK, first)
        self.assertIn('loc_40:\n' + CHECK, second)

    def test_a_label_that_is_a_prefix_of_another_is_not_matched(self):
        source = ('PPC_FUNC_IMPL(__imp__sub_1) {\n'
                  'loc_5:\n' + CHECK +
                  'loc_50:\n' + CHECK + '\tgoto loc_50;\n}\n')
        changed, kept, removed = loop_checkpoints.transform(source)
        self.assertEqual((kept, removed), (1, 1))
        self.assertIn('loc_5:\nloc_50:\n' + CHECK, changed)

    def test_keeps_crlf_and_refuses_a_second_pass(self):
        source = 'PPC_FUNC_IMPL(__imp__sub_1) {\r\nloc_8:\r\n\tsfr::guest_checkpoint();\r\n}\r\n'
        changed, kept, removed = loop_checkpoints.transform(source)
        self.assertEqual((kept, removed), (0, 1))
        self.assertTrue(changed.startswith(loop_checkpoints.MARKER + '\r\n'))
        self.assertIn('loc_8:\r\n}', changed)
        with self.assertRaises(ValueError):
            loop_checkpoints.transform(changed)

    def test_nothing_to_remove_leaves_the_source_alone(self):
        source = 'PPC_FUNC_IMPL(__imp__sub_1) {\nloc_8:\n' + CHECK + '\tgoto loc_8;\n}\n'
        self.assertEqual(loop_checkpoints.transform(source), (source, 1, 0))


if __name__ == '__main__':
    unittest.main()
