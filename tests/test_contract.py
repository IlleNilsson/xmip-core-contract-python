"""Standard-library checks, no framework: the interpreter is the only prerequisite."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "python"))

import xmip_contract  # noqa: E402


class ContractTest(unittest.TestCase):
    def test_the_identity_contract_holds_everything(self):
        self.assertEqual(xmip_contract.validate("any", b"\x00\x01\xff"), "")

    def test_implies_answers_the_bound_descriptor(self):
        self.assertEqual(xmip_contract.implies("any", "descriptor"), "any")
        self.assertIsNone(xmip_contract.implies("any", "nothing"))
        self.assertIsNone(xmip_contract.implies("", "descriptor"))


if __name__ == "__main__":
    unittest.main()
