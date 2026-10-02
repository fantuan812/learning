"""Real generated messages, two schema revisions, one pinned Python runtime."""

import json
import sys
import unittest

from google.protobuf import json_format
import old_pb2
import new_pb2


OBSERVATIONS = {}


def wire(message):
    return message.SerializeToString(deterministic=True)


def restore(message_type, data):
    message = message_type()
    message.ParseFromString(data)
    return message


def apply_patch(current_x, patch):
    """Example business contract: absent means keep, present means overwrite."""
    return patch.x if patch.HasField("x") else current_x


class EvolutionTests(unittest.TestCase):
    def test_01_zero_and_absence(self):
        implicit_zero = old_pb2.MovePatch(x=0)
        absent = new_pb2.MovePatch()
        explicit_zero = new_pb2.MovePatch(x=0)
        self.assertEqual(wire(implicit_zero), b"")
        with self.assertRaises(ValueError):
            implicit_zero.HasField("x")
        self.assertFalse(absent.HasField("x"))
        self.assertTrue(explicit_zero.HasField("x"))
        self.assertEqual(wire(explicit_zero), bytes.fromhex("0800"))
        self.assertEqual((absent.x, explicit_zero.x), (0, 0))
        OBSERVATIONS["01_zero_and_absence"] = {
            "implicit_hex": wire(implicit_zero).hex(),
            "explicit_hex": wire(explicit_zero).hex(),
            "absent_has_x": absent.HasField("x"),
            "explicit_has_x": explicit_zero.HasField("x"),
        }

    def test_02_clear_and_patch_semantics(self):
        patch = new_pb2.MovePatch(x=0)
        self.assertEqual(apply_patch(9, patch), 0)
        patch.ClearField("x")
        self.assertFalse(patch.HasField("x"))
        self.assertEqual(apply_patch(9, patch), 9)

    def test_03_merge_default_value(self):
        old_target = old_pb2.MovePatch(x=9)
        old_target.MergeFrom(old_pb2.MovePatch(x=0))
        new_target = new_pb2.MovePatch(x=9)
        new_target.MergeFrom(new_pb2.MovePatch(x=0))
        self.assertEqual(old_target.x, 9)
        self.assertEqual(new_target.x, 0)
        self.assertTrue(new_target.HasField("x"))
        OBSERVATIONS["03_merge_default"] = {
            "implicit_target_x": old_target.x,
            "explicit_target_x": new_target.x,
        }

    def test_04_old_writer_new_reader(self):
        for value in (0, 9, -9):
            with self.subTest(value=value):
                old = old_pb2.MovePatch(x=value, sequence=7)
                new = restore(new_pb2.MovePatch, wire(old))
                self.assertEqual((new.x, new.sequence, new.sprint), (value, 7, False))
                self.assertEqual(new.HasField("x"), value != 0)
        OBSERVATIONS["04_old_writer"] = {"new_sprint": False, "zero_has_x": False}

    def test_05_explicit_zero_lost_in_implicit_relay(self):
        for value in (0, 9, -9):
            with self.subTest(value=value):
                sent = new_pb2.MovePatch(x=value, sequence=7)
                old = restore(old_pb2.MovePatch, wire(sent))
                received = restore(new_pb2.MovePatch, wire(old))
                self.assertEqual(received.x, value)
                self.assertEqual(received.HasField("x"), value != 0)
                self.assertEqual(apply_patch(99, received), value if value != 0 else 99)
                if value == 0:
                    OBSERVATIONS["05_implicit_relay"] = {
                        "sent_hex": wire(sent).hex(), "relayed_hex": wire(old).hex(),
                        "received_x": received.x, "has_x": received.HasField("x"),
                        "business_target_x": apply_patch(99, received),
                    }

    def test_06_binary_unknown_preserved_while_known_field_changes(self):
        sent = new_pb2.MovePatch(x=9, sequence=7, sprint=True)
        old = restore(old_pb2.MovePatch, wire(sent))
        self.assertNotIn("sprint", old.DESCRIPTOR.fields_by_name)
        old.sequence = 8
        received = restore(new_pb2.MovePatch, wire(old))
        self.assertEqual((received.x, received.sequence, received.sprint), (9, 8, True))
        OBSERVATIONS["06_binary_relay"] = {
            "sent_hex": wire(sent).hex(), "relayed_hex": wire(old).hex(),
            "new_reader_sprint": received.sprint, "sequence": received.sequence,
        }

    def test_07_copy_preserves_rebuild_and_discard_lose_unknown(self):
        old = restore(old_pb2.MovePatch, wire(new_pb2.MovePatch(x=9, sprint=True)))
        copied = old_pb2.MovePatch()
        copied.CopyFrom(old)
        rebuilt = old_pb2.MovePatch(x=old.x, sequence=old.sequence)
        discarded = old_pb2.MovePatch()
        discarded.CopyFrom(old)
        discarded.DiscardUnknownFields()
        outcomes = {
            name: restore(new_pb2.MovePatch, wire(message)).sprint
            for name, message in (("copy", copied), ("rebuild", rebuilt), ("discard", discarded))
        }
        self.assertEqual(outcomes, {"copy": True, "rebuild": False, "discard": False})
        OBSERVATIONS["07_unknown_handling"] = outcomes

    def test_08_old_message_json_roundtrip_loses_unknown(self):
        old = restore(old_pb2.MovePatch, wire(new_pb2.MovePatch(x=9, sprint=True)))
        text = json_format.MessageToJson(old, preserving_proto_field_name=True)
        self.assertEqual(json.loads(text), {"x": 9})
        roundtrip = json_format.Parse(text, old_pb2.MovePatch())
        received = restore(new_pb2.MovePatch, wire(roundtrip))
        self.assertEqual(received.x, 9)
        self.assertFalse(received.sprint)
        OBSERVATIONS["08_old_json_relay"] = {
            "json": json.loads(text), "new_reader_sprint": received.sprint,
        }

    def test_09_new_json_rejected_or_ignored_by_old_reader(self):
        text = json_format.MessageToJson(new_pb2.MovePatch(x=9, sprint=True))
        with self.assertRaises(json_format.ParseError):
            json_format.Parse(text, old_pb2.MovePatch())
        ignored = json_format.Parse(text, old_pb2.MovePatch(), ignore_unknown_fields=True)
        received = restore(new_pb2.MovePatch, wire(ignored))
        self.assertEqual(received.x, 9)
        self.assertFalse(received.sprint)
        OBSERVATIONS["09_new_json_old_reader"] = {
            "default": "ParseError", "ignore_unknown_fields_sprint": received.sprint,
        }

    def test_10_json_zero_preserved_but_null_is_absent(self):
        text = json_format.MessageToJson(new_pb2.MovePatch(x=0))
        zero = json_format.Parse(text, new_pb2.MovePatch())
        null = json_format.Parse('{"x": null}', new_pb2.MovePatch())
        self.assertEqual(json.loads(text), {"x": 0})
        self.assertTrue(zero.HasField("x"))
        self.assertFalse(null.HasField("x"))
        self.assertEqual((apply_patch(9, zero), apply_patch(9, null)), (0, 9))
        OBSERVATIONS["10_json_presence"] = {
            "zero_json": json.loads(text), "zero_has_x": zero.HasField("x"),
            "null_has_x": null.HasField("x"),
        }

    def test_11_unknown_survival_does_not_protect_known_presence(self):
        sent = new_pb2.MovePatch(x=0, sprint=True)
        old = restore(old_pb2.MovePatch, wire(sent))
        received = restore(new_pb2.MovePatch, wire(old))
        self.assertTrue(received.sprint)
        self.assertFalse(received.HasField("x"))
        self.assertEqual(apply_patch(9, received), 9)
        OBSERVATIONS["11_mixed_relay"] = {
            "sent_hex": wire(sent).hex(), "relayed_hex": wire(old).hex(),
            "unknown_sprint_preserved": received.sprint,
            "known_x_presence_preserved": received.HasField("x"),
        }

    def test_12_parse_success_is_not_business_capability(self):
        request = new_pb2.MovePatch(x=9, sprint=True)
        old = restore(old_pb2.MovePatch, wire(request))
        self.assertEqual(old.x, 9)  # Successful parsing is established above.
        # Toy application policy only: old server always walks (1 unit), new can sprint (2).
        old_distance = 1
        new_distance = 2 if request.sprint else 1
        advertised_capabilities = frozenset()  # Explicit test input, not a real handshake.
        allowed = not request.sprint or "sprint" in advertised_capabilities
        self.assertNotEqual(old_distance, new_distance)
        self.assertFalse(allowed)
        OBSERVATIONS["12_business_capability_model"] = {
            "old_distance": old_distance, "new_distance": new_distance,
            "admission_allowed": allowed,
        }


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(EvolutionTests)
    result = unittest.TextTestRunner(stream=sys.stdout, verbosity=2).run(suite)
    print("OBSERVATIONS=" + json.dumps(OBSERVATIONS, ensure_ascii=False, sort_keys=True))
    sys.exit(0 if result.wasSuccessful() else 1)
