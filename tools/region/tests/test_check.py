"""tools/region/check.py: the tree passes, and what it is there to catch is
caught.  python3 -m unittest discover -s tools/region/tests"""
import json, os, sys, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(HERE))))
from tools.region import check   # noqa: E402


class TreeTest(unittest.TestCase):
    def test_tree_in_step(self):
        problems = []
        check.check_config(problems)
        check.check_wraps(problems)
        check.check_c(problems)
        self.assertEqual(problems, [])


class CTest(unittest.TestCase):
    def run_c(self, text):
        with tempfile.TemporaryDirectory() as t:
            p = os.path.join(t, 'x.c')
            with open(p, 'w') as f:
                f.write(text)
            problems = []
            check.check_c(problems, [p])
            return problems

    def test_raw_address(self):
        self.assertEqual(len(self.run_c('#define A 0x0008731Bu\n')), 1)

    def test_raw_sub(self):
        self.assertEqual(len(self.run_c('extern void sub_001D2879(void);\n')), 1)

    def test_paired_and_comments_pass(self):
        self.assertEqual(self.run_c('/* 0x54F900 sub_00011000 */\n'
                                    '#define A BO3_VA(0x0054F900u, 0x0054FC20u, 0x0054FCC0u)  // 0x60EA00\n'
                                    'extern void BO3_SUB(sub_001D2879, sub_001D2B99, sub_001D2C39)(void);\n'
                                    'static const char *s = "0x4D5370";\n'
                                    '#define B 0x10000u\n'), [])

    def test_marked_number_passes(self):
        self.assertEqual(self.run_c('{ 0x5fe00, 128 }, /* region: not an address, an offset */\n'), [])

    def test_pairs_disagree(self):
        self.assertEqual(len(self.run_c('#define A BO3_VA(0x54F900, 0x54FC20, 0x54FCC0)\n'
                                        '#define B BO3_VA(0x54F900, 0x54FC24, 0x54FCC0)\n')), 1)

    def test_third_values_disagree(self):
        self.assertEqual(len(self.run_c('#define A BO3_VA(0x54F900, 0x54FC20, 0x54FCC0)\n'
                                        '#define B BO3_VA(0x54F900, 0x54FC20, 0x54FCC4)\n')), 1)

    def test_two_values_are_not_enough(self):
        self.assertEqual(len(self.run_c('#define A BO3_VA(0x54F900, 0x54FC20)\n')), 1)
        self.assertEqual(len(self.run_c('extern void BO3_SUB(sub_001D2879, sub_001D2B99)(void);\n')), 1)

    def test_same_value_needs_saying(self):
        self.assertEqual(len(self.run_c('#define A BO3_VA(0x54F900, 0x54F900, 0x54FCC0)\n')), 1)
        self.assertEqual(len(self.run_c('#define A BO3_VA(0x54F900, 0x54FC20, 0x54F900)\n')), 1)
        self.assertEqual(self.run_c('#define A BO3_VA(0x54F900, 0x54F900, 0x54F900) /* region: same */\n'), [])


class WrapTest(unittest.TestCase):
    def test_each_pal_file_needs_every_twin(self):
        with tempfile.TemporaryDirectory() as t:
            ntsc = os.path.join(t, 'n.c'); pal = os.path.join(t, 'p.c')
            with open(ntsc, 'w') as f:
                f.write('void sub_00011000(void) { h(sub_00011000_gen); }\n'
                        'void sub_00012000(void) { h(sub_00012000_gen); }\n')
            with open(pal, 'w') as f:
                f.write('void sub_000110A0(void) { h(sub_000110A0_gen); }   /* NTSC-U sub_00011000 */\n'
                        'void sub_00017470(void) { g(sub_00017470_gen); }   /* region: PAL-ENSV only */\n')
            orig = check.os.path.join
            def join(*a):
                if a[-1] == 'src/game/recomp/recomp_manual.c':
                    return ntsc
                if a[-1] == 'x/p.c':
                    return pal
                return orig(*a)
            check.os.path.join = join
            try:
                problems = []
                check.check_wraps_one(problems, 'x/p.c', 'PAL En/Es/Nl/Sv')
            finally:
                check.os.path.join = orig
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('sub_00012000', problems[0])


class ConfigTest(unittest.TestCase):
    def test_missing_pal_twin(self):
        real = check._load
        def fake(rel):
            d = real(rel)
            if rel == 'config/patches.json':
                d = dict(d, patches=d['patches'] + [{'va': '0x00011000', 'kind': 'mark', 'name': 'new_site'}])
            return d
        check._load = fake
        try:
            problems = []
            check.check_config(problems)
        finally:
            check._load = real
        self.assertTrue(any('port_config' in p for p in problems), problems)

    def test_pal_note_must_name_ntsc(self):
        real = check._load
        def fake(rel):
            d = real(rel)
            if rel == 'config/pal-ensv/seed_functions.json':
                d = [dict(e) for e in d]
                d[0]['note'] = 'no address'
            return d
        check._load = fake
        try:
            problems = []
            check.check_config(problems)
        finally:
            check._load = real
        self.assertEqual(len(problems), 1, problems)


if __name__ == '__main__':
    unittest.main()
