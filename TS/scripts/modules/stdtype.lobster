// These types have special status in the language, as they can be used with
// mathematical operators, and many builtin functions.

// This file is automatically included in each Lobster program (!)

// vector types: generally for float or int elements, but useful for other types as well.

struct xy<T>:
    x:T
    y:T

struct xyz : xy
    z:T

struct xyzw : xyz
    w:T

// Specialized type names:

struct xy_f = xy<float>
struct xy_i = xy<int>

struct xyz_f = xyz<float>
struct xyz_i = xyz<int>

struct xyzw_f = xyzw<float>
struct xyzw_i = xyzw<int>

// Booleans are an enum.

enum bool:
    false
    true
