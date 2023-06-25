// port of ooPo's ps2sdk math3d library

#ifndef _MATH3D_H_
#define _MATH3D_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef float VECTOR[4];
typedef float MATRIX[16];

// vector indices
#define _X 0
#define _Y 1
#define _Z 2
#define _W 3

// 4x4 matrices indices
#define _11 0
#define _12 1
#define _13 2
#define _14 3
#define _21 4
#define _22 5
#define _23 6
#define _24 7
#define _31 8
#define _32 9
#define _33 10
#define _34 11
#define _41 12
#define _42 13
#define _43 14
#define _44 15

// vector functions

// Multiply a vector by a matrix, returning a vector.
void vector_apply(VECTOR output, const VECTOR input0, const MATRIX input1);

// Clamp a vector's values by cutting them off at a minimum and maximum value.
void vector_clamp(VECTOR output, const VECTOR input0, float min, float max);

// Copy a vector.
void vector_copy(VECTOR output, const VECTOR input0);

// Calculate the inner product of two vectors. Returns a scalar value.
float vector_innerproduct(const VECTOR input0, const VECTOR input1);
float vector_dot(const VECTOR input0, const VECTOR input1);

// Multiply two vectors together.
void vector_multiply(VECTOR output, const VECTOR input0, const VECTOR input1);

// Subtract b from a.
void vector_subtract(VECTOR output, const VECTOR a, const VECTOR b);

// Add the given vectors.
void vector_add(VECTOR output, const VECTOR a, const VECTOR b);

// Normalize a vector by determining its length and dividing its values by this
// value.
void vector_normalize(VECTOR vector);

// Normalize a vector by determining its length and dividing its values by this
// value.
void vector_normalize_into(VECTOR output, const VECTOR input0);

// Calculate the outer product of two vectors.
void vector_outerproduct(VECTOR output, const VECTOR input0,
                         const VECTOR input1);
void vector_crossproduct(VECTOR output, const VECTOR input0,
                         const VECTOR input1);

// Divide by w to convert to a 3-dimensional vector.
void vector_euclidean(VECTOR output, const VECTOR input);

// matrices functions

// Copy a matrix.
void matrix_copy(MATRIX output, const MATRIX input0);

// Calculate the inverse of a homogenous transform matrix (the last column must
// be {0, 0, 0, 1}).
void matrix_inverse(MATRIX output, const MATRIX input0);

// Calculate the inverse of a generic matrix.
// Return 0 if the matrix is not invertible.
int matrix_general_inverse(MATRIX output, const MATRIX input);

// Multiply two matrices together.
void matrix_multiply(MATRIX output, const MATRIX input0, const MATRIX input1);

// Create a rotation matrix and apply it to the specified input matrix.
void matrix_rotate(MATRIX output, const MATRIX input0, const VECTOR input1);

// Create a scaling matrix and apply it to the specified input matrix.
void matrix_scale(MATRIX output, const MATRIX input0, const VECTOR input1);

// Create a translation matrix and apply it to the specified input matrix.
void matrix_translate(MATRIX output, const MATRIX input0, const VECTOR input1);

// Transpose a matrix.
void matrix_transpose(MATRIX output, const MATRIX input0);

// Create a unit matrix.
void matrix_unit(MATRIX output);

// Calculate the determinant of the given matrix.
float matrix_determinant(const MATRIX m);

// Calculate the adjoint/adjugate of the given matrix.
void matrix_adjoint(MATRIX output, const MATRIX m);

void matrix_scalar_multiply(MATRIX output, const MATRIX input, float m);

// creation functions

void create_local_world(MATRIX local_world, VECTOR translation,
                        VECTOR rotation);
// Create a local_world matrix given a translation and rotation.
// Commonly used to describe an object's position and orientation.

void create_local_light(MATRIX local_light, VECTOR rotation);
// Create a local_light matrix given a rotation.
// Commonly used to transform an object's normals for lighting calculations.

void create_world_view(MATRIX world_view, const VECTOR translation,
                       const VECTOR rotation);
// Create a world_view matrix given a translation and rotation.
// Commonly used to describe a camera's position and rotation.

void create_view_screen(MATRIX view_screen, float aspect, float left,
                        float right, float bottom, float top, float near,
                        float far);
// Create a view_screen matrix given an aspect and clipping plane values.
// Functionally similar to the opengl function: glFrustum()

void create_local_screen(MATRIX local_screen, MATRIX local_world,
                         MATRIX world_view, MATRIX view_screen);
// Create a local_screen matrix given a local_world, world_view and view_screen
// matrix. Commonly used with vector_apply() to transform kBillboardQuad for
// rendering.

void create_d3d_look_at_lh(MATRIX ret, const VECTOR eye, const VECTOR at,
                           const VECTOR up);
void create_d3d_perspective_fov_lh(MATRIX ret, float fov_y, float aspect,
                                   float z_near, float z_far);
void create_d3d_viewport(MATRIX ret, float width, float height,
                         float max_depthbuffer_value, float z_min, float z_max);
void create_d3d_standard_viewport_16(MATRIX ret, float width, float height);
void create_d3d_standard_viewport_16_float(MATRIX ret, float width,
                                           float height);
void create_d3d_standard_viewport_24(MATRIX ret, float width, float height);
void create_d3d_standard_viewport_24_float(MATRIX ret, float width,
                                           float height);

#ifdef __cplusplus
};
#endif

#endif
