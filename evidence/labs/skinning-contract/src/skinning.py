"""Small column-vector LBS model, not an Unreal or glTF implementation.

Matrices are stored as rows; storage order does not change v' = M v.
Only finite affine 4x4 matrices and non-negative normalized weights are accepted.
The inverse uses Gauss-Jordan elimination for teaching, not production numerics.
"""

import math


def identity():
    return [[float(i == j) for j in range(4)] for i in range(4)]


def translation(x, y, z):
    result = identity()
    for i, value in enumerate((x, y, z)):
        result[i][3] = value
    return result


def rotation_z(degrees):
    angle = math.radians(degrees)
    c, s = math.cos(angle), math.sin(angle)
    return [[c, -s, 0., 0.], [s, c, 0., 0.],
            [0., 0., 1., 0.], [0., 0., 0., 1.]]


def matmul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4))
             for j in range(4)] for i in range(4)]


def transform(matrix, point):
    return [sum(matrix[i][j] * point[j] for j in range(4))
            for i in range(4)]


def validate_affine(matrix):
    if len(matrix) != 4 or any(len(row) != 4 for row in matrix):
        raise ValueError("expected 4x4 matrix")
    if not all(math.isfinite(v) for row in matrix for v in row):
        raise ValueError("non-finite matrix")
    if any(abs(matrix[3][j] - float(j == 3)) > 1e-9 for j in range(4)):
        raise ValueError("expected affine matrix")


def inverse(matrix):
    validate_affine(matrix)
    augmented = [list(row) + ident for row, ident in zip(matrix, identity())]
    for column in range(4):
        pivot = max(range(column, 4), key=lambda r: abs(augmented[r][column]))
        if abs(augmented[pivot][column]) <= 1e-12:
            raise ValueError("singular or numerically unsafe matrix")
        augmented[column], augmented[pivot] = augmented[pivot], augmented[column]
        divisor = augmented[column][column]
        augmented[column] = [v / divisor for v in augmented[column]]
        for row in range(4):
            if row != column:
                factor = augmented[row][column]
                augmented[row] = [x - factor * y for x, y in
                                  zip(augmented[row], augmented[column])]
    return [row[4:] for row in augmented]


def global_transforms(parents, local):
    """Require topological order: parent precedes child; -1 means root."""
    if len(parents) != len(local):
        raise ValueError("parent/local length mismatch")
    result = []
    for i, (parent, matrix) in enumerate(zip(parents, local)):
        validate_affine(matrix)
        if type(parent) is not int or parent < -1 or parent >= i:
            raise ValueError("parent must precede child")
        result.append(matrix if parent == -1 else matmul(result[parent], matrix))
    return result


def joint_palette(mesh_world, joints_world, inverse_bind):
    """Return mesh-local palettes: inverse(M_current) G_current IBM.

    IBM maps the stored bind-mesh vertex to bind joint-local coordinates.
    For a constructed bind pose, IBM = inverse(B_world) M_bind.
    """
    if len(joints_world) != len(inverse_bind) or not joints_world:
        raise ValueError("joint/inverse-bind count mismatch or empty skin")
    world_to_mesh = inverse(mesh_world)
    result = []
    for joint, ibm in zip(joints_world, inverse_bind):
        validate_affine(joint)
        validate_affine(ibm)
        result.append(matmul(matmul(world_to_mesh, joint), ibm))
    return result


def validate_influences(indices, weights, palette_size):
    if not indices or len(indices) != len(weights):
        raise ValueError("empty or mismatched influences")
    if any(type(i) is not int or i < 0 or i >= palette_size for i in indices):
        raise ValueError("palette index out of range")
    if any(not math.isfinite(w) or w < 0 for w in weights):
        raise ValueError("weights must be finite and non-negative")
    if not math.isclose(math.fsum(weights), 1., rel_tol=0., abs_tol=1e-9):
        raise ValueError("weights must sum to one; no implicit repair")


def skin_point(point, indices, weights, palette):
    if len(point) != 4 or not all(math.isfinite(v) for v in point) or point[3] != 1:
        raise ValueError("expected finite homogeneous position with w=1")
    validate_influences(indices, weights, len(palette))
    for matrix in palette:
        validate_affine(matrix)
    positions = [transform(palette[i], point) for i in indices]
    return [math.fsum(w * p[axis] for w, p in zip(weights, positions))
            for axis in range(4)]
