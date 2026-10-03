"""Analytic fixtures and counterexamples; unittest checks survive python -O."""

import json
import math
from pathlib import Path
import sys
import unittest

LAB = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(LAB / "src"))
from skinning import (global_transforms, identity, inverse, joint_palette,
                      matmul, rotation_z, skin_point, transform, translation)

CASE = json.loads((LAB / "data/cases.json").read_text(encoding="utf-8"))


class SkinningContractTests(unittest.TestCase):
    def setUp(self):
        self.bind = [identity(), translation(0, 1, 0)]
        self.ibm = [inverse(m) for m in self.bind]
        self.posed = [identity(), matmul(self.bind[1], rotation_z(90))]
        self.palette = joint_palette(identity(), self.posed, self.ibm)

    def assertVector(self, actual, expected):
        self.assertEqual(len(actual), len(expected))
        for a, b in zip(actual, expected):
            self.assertAlmostEqual(a, b, places=9)

    def test_bind_pose_is_identity(self):
        palette = joint_palette(identity(), self.bind, self.ibm)
        for indices, weights in [([0], [1.]), ([1], [1.]), ([0, 1], [.3, .7])]:
            self.assertVector(skin_point(CASE["vertex"], indices, weights, palette),
                              CASE["vertex"])

    def test_child_rotates_around_bind_joint_pivot(self):
        self.assertVector(skin_point(CASE["vertex"], [1], [1.], self.palette),
                          CASE["expected_child_only"])
        self.assertVector(skin_point(CASE["joint_pivot"], [1], [1.], self.palette),
                          CASE["joint_pivot"])

    def test_weighted_two_joint_fixture(self):
        self.assertVector(skin_point(CASE["vertex"], [0, 1], CASE["weights"], self.palette),
                          CASE["expected_blend"])

    def test_parent_composition_is_not_child_local(self):
        root = matmul(translation(3, 4, 0), rotation_z(90))
        actual = global_transforms([-1, 0], [root, translation(0, 1, 0)])
        self.assertVector(transform(actual[1], [0, 0, 0, 1]), [2, 4, 0, 1])
        self.assertVector(transform(translation(0, 1, 0), [0, 0, 0, 1]), [0, 1, 0, 1])

    def test_mesh_world_cancelled_exactly_once(self):
        mesh = translation(10, 0, 0)
        world_joints = [matmul(mesh, m) for m in self.posed]
        palette = joint_palette(mesh, world_joints, self.ibm)
        local = skin_point(CASE["vertex"], [0, 1], CASE["weights"], palette)
        self.assertVector(local, CASE["expected_blend"])
        self.assertVector(transform(mesh, local), CASE["expected_translated_world"])
        # Negative example: world-space palette consumed as mesh-local, then M again.
        wrong = joint_palette(identity(), world_joints, self.ibm)
        self.assertVector(transform(mesh, skin_point(CASE["vertex"], [0, 1],
                                                    CASE["weights"], wrong)),
                          CASE["expected_double_transform"])

    def test_nonidentity_bind_mesh_requires_mesh_to_joint_ibm(self):
        mesh_bind = matmul(translation(5, -2, 1), rotation_z(30))
        world_bind = [matmul(mesh_bind, b) for b in self.bind]
        ibm = [matmul(inverse(b), mesh_bind) for b in world_bind]
        palette = joint_palette(mesh_bind, world_bind, ibm)
        self.assertVector(skin_point(CASE["vertex"], [0, 1], [.5, .5], palette),
                          CASE["vertex"])
        wrong = joint_palette(mesh_bind, world_bind, [inverse(b) for b in world_bind])
        self.assertGreater(math.dist(skin_point(CASE["vertex"], [0, 1], [.5, .5], wrong),
                                     CASE["vertex"]), 1.)

    def test_reversed_product_and_missing_inverse_bind_are_wrong(self):
        reversed_order = [matmul(b, g) for b, g in zip(self.ibm, self.posed)]
        for wrong, expected in [(reversed_order, [-2, 0, 0, 1]),
                                (self.posed, [-2, 1, 0, 1])]:
            self.assertVector(skin_point(CASE["vertex"], [1], [1.], wrong), expected)
            self.assertNotEqual(expected, CASE["expected_child_only"])

    def test_palette_reorder_requires_index_remap(self):
        reordered = [self.palette[1], self.palette[0]]
        self.assertVector(skin_point(CASE["vertex"], [0], [1.], reordered),
                          CASE["expected_child_only"])
        self.assertVector(skin_point(CASE["vertex"], [1], [1.], reordered), CASE["vertex"])

    def test_more_than_four_influences_is_valid_math(self):
        palette = [translation(i, 0, 0) for i in range(12)]
        result = skin_point([0, 0, 0, 1], list(range(12)), [1. / 12] * 12, palette)
        self.assertVector(result, [5.5, 0, 0, 1])

    def test_truncation_renormalization_does_not_preserve_shape(self):
        palette = [identity(), translation(3, 0, 0), translation(0, 0, 10)]
        original = skin_point([0, 0, 0, 1], [0, 1, 2], [.6, .3, .1], palette)
        truncated = skin_point([0, 0, 0, 1], [0, 1], [2. / 3, 1. / 3], palette)
        self.assertVector(original, [.9, 0, 1, 1])
        self.assertVector(truncated, [1, 0, 0, 1])

    def test_opposing_rotations_collapse_even_with_valid_weights(self):
        palette = [rotation_z(90), rotation_z(-90)]
        self.assertVector(skin_point([1, 0, 0, 1], [0, 1], [.5, .5], palette),
                          [0, 0, 0, 1])

    def test_invalid_weights_rejected_without_implicit_repair(self):
        for weights in ([0, 0], [.4, .4], [-.1, 1.1], [math.nan, 1.], [math.inf, 0.]):
            with self.subTest(weights=weights), self.assertRaises(ValueError):
                skin_point(CASE["vertex"], [0, 1], weights, self.palette)

    def test_invalid_indices_lengths_and_points_rejected(self):
        for indices, weights in [([-1], [1.]), ([2], [1.]), ([0.0], [1.]),
                                 ([True], [1.]), ([], []), ([0], [.5, .5])]:
            with self.subTest(indices=indices), self.assertRaises(ValueError):
                skin_point(CASE["vertex"], indices, weights, self.palette)
        for point in ([0, 0, 0], [0, 0, 0, 0], [math.nan, 0, 0, 1]):
            with self.subTest(point=point), self.assertRaises(ValueError):
                skin_point(point, [0], [1.], self.palette)

    def test_bad_hierarchy_and_palette_lengths_rejected(self):
        for parents, local in [([0], [identity()]), ([1, -1], self.bind),
                               ([-2], [identity()]), ([-1], self.bind)]:
            with self.subTest(parents=parents), self.assertRaises(ValueError):
                global_transforms(parents, local)
        with self.assertRaises(ValueError):
            joint_palette(identity(), self.bind, [identity()])

    def test_singular_and_nonfinite_matrices_rejected(self):
        singular = identity()
        singular[0][0] = 0.
        nonfinite = identity()
        nonfinite[0][0] = math.inf
        for matrix in (singular, nonfinite, [[1.]], [[0.] * 4] * 4):
            with self.subTest(matrix=matrix), self.assertRaises(ValueError):
                inverse(matrix)

    def test_common_world_rigid_transform_leaves_mesh_local_result(self):
        for angle in (-137, -30, 0, 30, 90, 177):
            common = matmul(translation(7, -4, 2), rotation_z(angle))
            palette = joint_palette(common, [matmul(common, m) for m in self.posed], self.ibm)
            with self.subTest(angle=angle):
                self.assertVector(skin_point(CASE["vertex"], [0, 1], [.5, .5], palette),
                                  CASE["expected_blend"])


if __name__ == "__main__":
    print("Fixture: child_only=(-1,1,0), blend=(-0.5,1.5,0)", flush=True)
    print("Fixture: world=(9.5,1.5,0), wrong_double_transform=(19.5,1.5,0)", flush=True)
    print("Fixture: prune (0.9,0,1)->(1,0,0); opposing rotations collapse (1,0,0)->(0,0,0)",
          flush=True)
    unittest.main(verbosity=2)
